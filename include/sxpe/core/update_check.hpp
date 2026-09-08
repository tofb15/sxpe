#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace sxpe::core {

inline constexpr char kGithubLatestUrl[] =
    "https://api.github.com/repos/tofb15/sxpe/releases/latest";
inline constexpr char kGithubReleasesListUrl[] =
    "https://api.github.com/repos/tofb15/sxpe/releases?per_page=10";
inline constexpr char kGithubApiHost[] = "api.github.com";
inline constexpr char kGithubLatestPath[] = "/repos/tofb15/sxpe/releases/latest";
inline constexpr char kGithubReleasesListPath[] = "/repos/tofb15/sxpe/releases?per_page=10";
inline constexpr char kGithubReleasesPage[] = "https://github.com/tofb15/sxpe/releases";

/// Strip a leading `v`/`V`, then `+` build metadata and `-` pre-release suffix.
[[nodiscard]] std::string normalize_version(std::string_view v);

/// Compare dotted numeric versions after normalize_version. -1 if a<b, 0 if equal, 1 if a>b.
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b);

struct HttpGetResult {
    int status{0};
    std::string body;
    std::string error;
};

/// HTTPS GET. `auth_bearer` is sent as `Authorization: Bearer …` when non-empty.
[[nodiscard]] HttpGetResult https_get(std::string_view url, std::string_view user_agent,
                                      std::string_view auth_bearer, int timeout_ms);

/// SXPE_GITHUB_TOKEN, then GITHUB_TOKEN, then GH_TOKEN. Empty if unset.
[[nodiscard]] std::string github_token_from_env();

/// Env first; if empty, `gh auth token` on PATH (best-effort, never logs the value).
[[nodiscard]] std::string github_token();

enum class UpdateStatus {
    up_to_date,
    newer_available,
    local_newer,
    not_found,
    error,
};

struct UpdateCheck {
    UpdateStatus status{UpdateStatus::error};
    std::string current;
    std::string latest;
    std::string tag_name;
    std::string html_url;
    std::string message;
    std::vector<std::string> summary;
    std::vector<std::string> assets;
    int http_status{0};
    bool downloads{false};
};


struct GithubReleaseInfo {
    std::string tag_name;
    std::string html_url;
    bool draft{false};
    bool prerelease{false};
};

/// Newest non-draft release by version compare (prereleases / Beta included).
/// Returns empty `tag_name` when none qualify.
[[nodiscard]] GithubReleaseInfo pick_newest_published_release(
    const std::vector<GithubReleaseInfo>& releases);

[[nodiscard]] const char* update_status_id(UpdateStatus s);

/// Compare running version to a GitHub `tag_name` (e.g. v0.7.0).
[[nodiscard]] UpdateCheck evaluate_update(std::string_view current, std::string_view tag_name,
                                          std::string_view html_url);

[[nodiscard]] UpdateCheck update_not_found(std::string_view current, int http_status);

}  // namespace sxpe::core
