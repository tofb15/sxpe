#include "check.hpp"
#include "sxpe/core/update_check.hpp"

#include <string>
#include <vector>

int main() {
    using namespace sxpe::core;

    CHECK(normalize_version("v0.7.0") == "0.7.0");
    CHECK(normalize_version("0.7.0") == "0.7.0");
    CHECK(normalize_version("  V0.7.0+dirty ") == "0.7.0");
    CHECK(normalize_version("0.7.0-dev") == "0.7.0");

    CHECK(compare_versions("0.7.0", "v0.7.0") == 0);
    CHECK(compare_versions("0.6.0", "v0.7.0") < 0);
    CHECK(compare_versions("0.8.0", "v0.7.0") > 0);
    CHECK(compare_versions("1.0", "1.0.0") == 0);
    CHECK(compare_versions("0.7.0+local", "0.7.0") == 0);

    auto up = evaluate_update("0.7.0", "v0.7.0",
                              "https://github.com/tofb15/sxpe/releases/tag/v0.7.0");
    CHECK(up.status == UpdateStatus::up_to_date);
    CHECK(up.downloads == false);
    CHECK(up.latest == "0.7.0");
    CHECK(std::string(update_status_id(up.status)) == "upToDate");
    CHECK(!up.summary.empty());

    auto newer = evaluate_update("0.6.0", "v0.7.0",
                                 "https://github.com/tofb15/sxpe/releases/tag/v0.7.0");
    CHECK(newer.status == UpdateStatus::newer_available);
    CHECK(std::string(update_status_id(newer.status)) == "newerAvailable");

    auto local = evaluate_update("0.8.0", "v0.7.0", "");
    CHECK(local.status == UpdateStatus::local_newer);
    CHECK(local.html_url == kGithubReleasesPage);

    auto missing = evaluate_update("0.7.0", "", "");
    CHECK(missing.status == UpdateStatus::not_found);

    auto nf = update_not_found("0.7.0", 404);
    CHECK(nf.status == UpdateStatus::not_found);
    CHECK(nf.http_status == 404);
    CHECK(nf.downloads == false);
    CHECK(nf.message.find("non-draft") != std::string::npos || nf.message.find("Release") != std::string::npos);


    {
        using Info = GithubReleaseInfo;
        std::vector<Info> list{
            {"v0.7.0", "https://example/tag/v0.7.0", false, true},
            {"v0.6.0", "https://example/tag/v0.6.0", false, false},
        };
        auto picked = pick_newest_published_release(list);
        CHECK(picked.tag_name == "v0.7.0");
        CHECK(picked.prerelease == true);

        std::vector<Info> drafts_first{
            {"v9.9.9", "https://example/tag/v9.9.9", true, false},
            {"v0.7.0", "https://example/tag/v0.7.0", false, true},
        };
        auto skip_draft = pick_newest_published_release(drafts_first);
        CHECK(skip_draft.tag_name == "v0.7.0");

        std::vector<Info> only_drafts{{"v9.9.9", "", true, false}};
        CHECK(pick_newest_published_release(only_drafts).tag_name.empty());

        // Version compare, not GitHub list order: older stable first, newer Beta later.
        std::vector<Info> older_first{
            {"v0.6.0", "https://example/tag/v0.6.0", false, false},
            {"v0.7.0", "https://example/tag/v0.7.0", false, true},
        };
        auto by_version = pick_newest_published_release(older_first);
        CHECK(by_version.tag_name == "v0.7.0");
        CHECK(by_version.prerelease == true);
    }

    CHECK(github_token_from_env().find('\n') == std::string::npos);

    if (g_failed) {
        std::cerr << g_failed << " failed\n";
        return 1;
    }
    return 0;
}
