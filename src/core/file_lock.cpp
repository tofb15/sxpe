#include "sxpe/core/file_lock.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace sxpe::core {
namespace {

std::string lower_ascii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

}  // namespace

std::string file_locked_message() {
    return "file is locked — close the game or copy the file first";
}

std::string map_open_failure_message(bool writable) {
#ifdef _WIN32
    const DWORD e = GetLastError();
    switch (e) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return "file not found";
        case ERROR_ACCESS_DENIED:
            return writable ? "access denied — close the game or check file permissions"
                            : "access denied";
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return file_locked_message();
        default:
            return "open failed (Windows error " + std::to_string(e) + ")";
    }
#else
    switch (errno) {
        case ENOENT:
            return "file not found";
        case EACCES:
        case EPERM:
            return writable ? "access denied — close the game or check file permissions"
                            : "access denied";
        case EBUSY:
        case ETXTBSY:
        case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            return file_locked_message();
        default:
            return std::string("open failed: ") + std::strerror(errno);
    }
#endif
}

std::string map_save_failure_message() {
#ifdef _WIN32
    const DWORD e = GetLastError();
    switch (e) {
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            return file_locked_message();
        case ERROR_ACCESS_DENIED:
            return "access denied — close the game or check file permissions";
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return "file not found";
        default:
            return "save failed (Windows error " + std::to_string(e) + ")";
    }
#else
    switch (errno) {
        case EACCES:
        case EPERM:
            return "access denied — close the game or check file permissions";
        case EBUSY:
        case ETXTBSY:
        case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            return file_locked_message();
        case ENOENT:
            return "file not found";
        default:
            if (errno == 0) {
                return file_locked_message();
            }
            return std::string("save failed: ") + std::strerror(errno);
    }
#endif
}

bool looks_like_ea_mods_path(const std::filesystem::path& path) {
    // Match .../Electronic Arts/<anything>/Mods... (Documents\Electronic Arts\The Sims 3\Mods).
    bool saw_ea = false;
    for (const auto& part : path) {
        auto s = lower_ascii(part.string());
        if (s == "electronic arts" || s == "electronicarts") {
            saw_ea = true;
            continue;
        }
        if (saw_ea && (s == "mods" || s.starts_with("mods"))) {
            return true;
        }
    }
    // Also accept a path segment that is literally Mods under a Sims folder name.
    std::string joined;
    for (const auto& part : path) {
        if (!joined.empty()) {
            joined.push_back('/');
        }
        joined += lower_ascii(part.string());
    }
    return joined.find("/electronic arts/") != std::string::npos &&
           joined.find("/mods") != std::string::npos;
}

std::string mods_path_lock_warning() {
    return "path is under Documents/Electronic Arts/.../Mods and SXPE could not take an exclusive "
           "lock — close the game or copy the file first before editing";
}

LockProbeResult probe_exclusive_write(const std::filesystem::path& path) {
    LockProbeResult out;
#ifdef _WIN32
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        if (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION) {
            out.status = LockProbeStatus::locked;
            out.message = file_locked_message();
            return out;
        }
        if (e == ERROR_ACCESS_DENIED) {
            out.status = LockProbeStatus::access_denied;
            out.message = map_open_failure_message(true);
            return out;
        }
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) {
            out.status = LockProbeStatus::not_found;
            out.message = "file not found";
            return out;
        }
        out.status = LockProbeStatus::other;
        out.message = map_open_failure_message(true);
        return out;
    }
    CloseHandle(h);
    out.status = LockProbeStatus::ok;
    return out;
#else
    const int fd = ::open(path.c_str(), O_RDWR);
    if (fd < 0) {
        if (errno == ENOENT) {
            out.status = LockProbeStatus::not_found;
            out.message = "file not found";
            return out;
        }
        if (errno == EACCES || errno == EPERM) {
            out.status = LockProbeStatus::access_denied;
            out.message = map_open_failure_message(true);
            return out;
        }
        if (errno == EBUSY || errno == ETXTBSY) {
            out.status = LockProbeStatus::locked;
            out.message = file_locked_message();
            return out;
        }
        out.status = LockProbeStatus::other;
        out.message = map_open_failure_message(true);
        return out;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int saved = errno;
        ::close(fd);
        if (saved == EWOULDBLOCK || saved == EAGAIN || saved == EBUSY) {
            out.status = LockProbeStatus::locked;
            out.message = file_locked_message();
            return out;
        }
        errno = saved;
        out.status = LockProbeStatus::other;
        out.message = map_open_failure_message(true);
        return out;
    }
    flock(fd, LOCK_UN);
    ::close(fd);
    out.status = LockProbeStatus::ok;
    return out;
#endif
}

}  // namespace sxpe::core
