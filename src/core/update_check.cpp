#include "sxpe/core/update_check.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace sxpe::core {
namespace {

std::string trim_copy(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
        s.erase(s.begin());
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    return s;
}

std::string env_nonempty(const char* key) {
    const char* v = std::getenv(key);
    if (!v || !*v) {
        return {};
    }
    return trim_copy(v);
}

#ifdef _WIN32
std::wstring widen_utf8(std::string_view s) {
    if (s.empty()) {
        return L"";
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) {
        return L"";
    }
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

bool parse_https_url(std::string_view url, std::wstring& host, std::wstring& path) {
    const std::string_view prefix = "https://";
    if (url.size() < prefix.size() || url.substr(0, prefix.size()) != prefix) {
        return false;
    }
    const auto rest = url.substr(prefix.size());
    const auto slash = rest.find('/');
    if (slash == std::string_view::npos) {
        host = widen_utf8(rest);
        path = L"/";
        return !host.empty();
    }
    host = widen_utf8(rest.substr(0, slash));
    path = widen_utf8(rest.substr(slash));
    return !host.empty() && !path.empty();
}

std::string last_win_error(const char* what) {
    const DWORD e = GetLastError();
    return std::string(what) + " (Win32 " + std::to_string(static_cast<unsigned long long>(e)) + ")";
}

HttpGetResult winhttp_get(std::string_view url, std::string_view user_agent,
                          std::string_view auth_bearer, int timeout_ms) {
    HttpGetResult out;
    std::wstring host;
    std::wstring path;
    if (!parse_https_url(url, host, path)) {
        out.error = "only https URLs are supported";
        return out;
    }
    const auto ua = widen_utf8(user_agent);
    HINTERNET session = WinHttpOpen(ua.empty() ? L"SXPE" : ua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        out.error = last_win_error("WinHttpOpen");
        return out;
    }
    const int t = timeout_ms > 0 ? timeout_ms : 15000;
    WinHttpSetTimeouts(session, t, t, t, t);

    HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        out.error = last_win_error("WinHttpConnect");
        WinHttpCloseHandle(session);
        return out;
    }
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        out.error = last_win_error("WinHttpOpenRequest");
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }

    std::wstring headers = L"Accept: application/vnd.github+json\r\n";
    if (!auth_bearer.empty()) {
        headers += L"Authorization: Bearer ";
        headers += widen_utf8(auth_bearer);
        headers += L"\r\n";
    }
    if (!WinHttpAddRequestHeaders(request, headers.c_str(), static_cast<DWORD>(-1),
                                  WINHTTP_ADDREQ_FLAG_ADD)) {
        out.error = last_win_error("WinHttpAddRequestHeaders");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                            0)) {
        out.error = last_win_error("WinHttpSendRequest");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
    if (!WinHttpReceiveResponse(request, nullptr)) {
        out.error = last_win_error("WinHttpReceiveResponse");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                             WINHTTP_NO_HEADER_INDEX)) {
        out.error = last_win_error("WinHttpQueryHeaders");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
    out.status = static_cast<int>(status);

    std::string body;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request, &avail)) {
            out.error = last_win_error("WinHttpQueryDataAvailable");
            break;
        }
        if (avail == 0) {
            break;
        }
        if (body.size() + avail > 2 * 1024 * 1024) {
            out.error = "response too large";
            break;
        }
        const auto off = body.size();
        body.resize(off + avail);
        DWORD got = 0;
        if (!WinHttpReadData(request, body.data() + off, avail, &got)) {
            out.error = last_win_error("WinHttpReadData");
            body.resize(off);
            break;
        }
        body.resize(off + got);
        if (got == 0) {
            break;
        }
    }
    out.body = std::move(body);
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return out;
}

std::string run_capture(const std::wstring& exe, const std::wstring& args, int timeout_ms) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr;
    HANDLE wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        return {};
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                        nullptr, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        return {};
    }
    CloseHandle(wr);
    const DWORD wait = timeout_ms > 0 ? static_cast<DWORD>(timeout_ms) : 3000;
    if (WaitForSingleObject(pi.hProcess, wait) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(rd);
        return {};
    }
    std::string out;
    char tmp[512];
    DWORD n = 0;
    while (ReadFile(rd, tmp, sizeof(tmp), &n, nullptr) && n > 0) {
        out.append(tmp, tmp + n);
        if (out.size() > 8192) {
            break;
        }
    }
    CloseHandle(rd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return out;
}

std::string token_from_gh_cli() {
    wchar_t exe[MAX_PATH]{};
    const DWORD n = SearchPathW(nullptr, L"gh.exe", nullptr, MAX_PATH, exe, nullptr);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    auto got = trim_copy(run_capture(exe, L"auth token", 4000));
    // Strip CR/LF leftover if the pipe mixed streams.
    const auto nl = got.find_first_of("\r\n");
    if (nl != std::string::npos) {
        got = got.substr(0, nl);
    }
    if (got.size() < 8) {
        return {};
    }
    return got;
}

#else

HttpGetResult curl_get(std::string_view url, std::string_view user_agent,
                       std::string_view auth_bearer, int timeout_ms) {
    HttpGetResult out;
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        out.error = "pipe failed";
        return out;
    }
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        out.error = "fork failed";
        return out;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        const std::string url_s(url);
        const std::string ua_s(user_agent.empty() ? "SXPE" : std::string(user_agent));
        const int secs = timeout_ms > 0 ? (timeout_ms + 999) / 1000 : 15;
        const std::string secs_s = std::to_string(secs);
        std::string auth;
        std::vector<const char*> argv;
        argv.push_back("curl");
        argv.push_back("-sS");
        argv.push_back("-L");
        argv.push_back("--max-time");
        argv.push_back(secs_s.c_str());
        argv.push_back("-A");
        argv.push_back(ua_s.c_str());
        argv.push_back("-H");
        argv.push_back("Accept: application/vnd.github+json");
        if (!auth_bearer.empty()) {
            auth = "Authorization: Bearer " + std::string(auth_bearer);
            argv.push_back("-H");
            argv.push_back(auth.c_str());
        }
        argv.push_back("-w");
        argv.push_back("\n__SXPE_HTTP_%{http_code}__");
        argv.push_back(url_s.c_str());
        argv.push_back(nullptr);
        execvp("curl", const_cast<char**>(argv.data()));
        _exit(127);
    }
    close(pipefd[1]);
    std::string body;
    char buf[4096];
    ssize_t n = 0;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        body.append(buf, buf + n);
        if (body.size() > 2 * 1024 * 1024) {
            break;
        }
    }
    close(pipefd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 127) {
        out.error = "curl not found on PATH";
        return out;
    }
    const auto marker = body.rfind("__SXPE_HTTP_");
    if (marker == std::string::npos) {
        if (WIFEXITED(st) && WEXITSTATUS(st) != 0) {
            out.error = "curl failed";
            return out;
        }
        out.error = "curl returned no HTTP status";
        out.body = std::move(body);
        return out;
    }
    auto code_s = body.substr(marker + std::strlen("__SXPE_HTTP_"));
    if (!code_s.empty() && code_s.back() == '_') {
        code_s.pop_back();
    }
    code_s = trim_copy(std::move(code_s));
    try {
        out.status = std::stoi(code_s);
    } catch (...) {
        out.status = 0;
    }
    out.body = body.substr(0, marker);
    if (!out.body.empty() && out.body.back() == '\n') {
        out.body.pop_back();
    }
    if (WIFEXITED(st) && WEXITSTATUS(st) != 0 && out.status == 0) {
        out.error = "curl failed";
    }
    return out;
}

std::string token_from_gh_cli() {
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        return {};
    }
    const pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {};
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);
        execlp("gh", "gh", "auth", "token", static_cast<char*>(nullptr));
        _exit(127);
    }
    close(pipefd[1]);
    std::string got;
    char buf[512];
    ssize_t n = 0;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
        got.append(buf, buf + n);
        if (got.size() > 4096) {
            break;
        }
    }
    close(pipefd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        return {};
    }
    got = trim_copy(std::move(got));
    const auto nl = got.find_first_of("\r\n");
    if (nl != std::string::npos) {
        got = got.substr(0, nl);
    }
    if (got.size() < 8) {
        return {};
    }
    return got;
}

#endif

}  // namespace

std::string normalize_version(std::string_view v) {
    std::string s = trim_copy(std::string(v));
    if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) {
        s.erase(s.begin());
    }
    const auto plus = s.find('+');
    if (plus != std::string::npos) {
        s = s.substr(0, plus);
    }
    const auto dash = s.find('-');
    if (dash != std::string::npos) {
        s = s.substr(0, dash);
    }
    return s;
}

int compare_versions(std::string_view a, std::string_view b) {
    auto parts = [](std::string_view v) {
        std::vector<int> out;
        std::string cur;
        const auto n = normalize_version(v);
        for (std::size_t i = 0; i <= n.size(); ++i) {
            if (i == n.size() || n[i] == '.') {
                int x = 0;
                try {
                    if (!cur.empty()) {
                        x = std::stoi(cur);
                    }
                } catch (...) {
                    x = 0;
                }
                out.push_back(x);
                cur.clear();
            } else {
                cur.push_back(n[i]);
            }
        }
        while (out.size() < 3) {
            out.push_back(0);
        }
        return out;
    };
    const auto pa = parts(a);
    const auto pb = parts(b);
    const auto n = std::max(pa.size(), pb.size());
    for (std::size_t i = 0; i < n; ++i) {
        const int x = i < pa.size() ? pa[i] : 0;
        const int y = i < pb.size() ? pb[i] : 0;
        if (x < y) {
            return -1;
        }
        if (x > y) {
            return 1;
        }
    }
    return 0;
}

HttpGetResult https_get(std::string_view url, std::string_view user_agent,
                        std::string_view auth_bearer, int timeout_ms) {
#ifdef _WIN32
    return winhttp_get(url, user_agent, auth_bearer, timeout_ms);
#else
    return curl_get(url, user_agent, auth_bearer, timeout_ms);
#endif
}

std::string github_token_from_env() {
    for (const char* k : {"SXPE_GITHUB_TOKEN", "GITHUB_TOKEN", "GH_TOKEN"}) {
        auto v = env_nonempty(k);
        if (!v.empty()) {
            return v;
        }
    }
    return {};
}

std::string github_token() {
    auto t = github_token_from_env();
    if (!t.empty()) {
        return t;
    }
    if (env_nonempty("SXPE_NO_GH_TOKEN") == "1") {
        return {};
    }
    return token_from_gh_cli();
}


GithubReleaseInfo pick_newest_published_release(const std::vector<GithubReleaseInfo>& releases) {
    GithubReleaseInfo best;
    for (const auto& r : releases) {
        if (r.draft || r.tag_name.empty()) {
            continue;
        }
        if (best.tag_name.empty() || compare_versions(r.tag_name, best.tag_name) > 0) {
            best = r;
        }
    }
    return best;
}

const char* update_status_id(UpdateStatus s) {
    switch (s) {
        case UpdateStatus::up_to_date:
            return "upToDate";
        case UpdateStatus::newer_available:
            return "newerAvailable";
        case UpdateStatus::local_newer:
            return "localNewer";
        case UpdateStatus::not_found:
            return "notFound";
        case UpdateStatus::error:
            return "error";
    }
    return "error";
}

UpdateCheck evaluate_update(std::string_view current, std::string_view tag_name,
                            std::string_view html_url) {
    UpdateCheck r;
    r.current = trim_copy(std::string(current));
    r.tag_name = trim_copy(std::string(tag_name));
    r.html_url = trim_copy(std::string(html_url));
    if (r.html_url.empty()) {
        r.html_url = kGithubReleasesPage;
    }
    r.latest = normalize_version(r.tag_name);
    r.downloads = false;
    if (r.tag_name.empty()) {
        r.status = UpdateStatus::not_found;
        r.message = "No release tag found yet.";
        r.summary = {"No release tag found yet.", "You are running SXPE " + r.current + ".",
                     std::string("Releases: ") + r.html_url};
        return r;
    }
    const int cmp = compare_versions(r.current, r.tag_name);
    if (cmp < 0) {
        r.status = UpdateStatus::newer_available;
        r.message = "A newer release is available.";
        r.summary = {r.message,
                     "You have SXPE " + r.current + "; latest is " + r.tag_name + ".",
                     "SXPE does not download updates automatically — open the release page and "
                     "install when you choose.",
                     r.html_url};
    } else if (cmp == 0) {
        r.status = UpdateStatus::up_to_date;
        r.message = "You are up to date.";
        r.summary = {r.message,
                     "Running SXPE " + r.current + " (matches latest release " + r.tag_name + ").",
                     std::string("Releases: ") + r.html_url};
    } else {
        r.status = UpdateStatus::local_newer;
        r.message = "You appear newer than the latest GitHub Release (dev or local build).";
        r.summary = {r.message, "Running SXPE " + r.current + "; latest published is " + r.tag_name + ".",
                     std::string("Releases: ") + r.html_url};
    }
    return r;
}

UpdateCheck update_not_found(std::string_view current, int http_status) {
    UpdateCheck r;
    r.status = UpdateStatus::not_found;
    r.current = trim_copy(std::string(current));
    r.html_url = kGithubReleasesPage;
    r.http_status = http_status;
    r.downloads = false;
    r.message =
        "No public non-draft GitHub Release is visible (the repository may be private, "
        "or no releases exist yet).";
    r.summary = {r.message,
                 "You are running SXPE " + r.current + ".",
                 "Set SXPE_GITHUB_TOKEN or GITHUB_TOKEN (repo scope), or open the releases page.",
                 r.html_url};
    return r;
}

}  // namespace sxpe::core
