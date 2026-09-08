#include "check.hpp"
#include "sxpe/core/update_check.hpp"

#include <string>

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
    CHECK(nf.message.find("404") != std::string::npos);

    CHECK(github_token_from_env().find('\n') == std::string::npos);

    if (g_failed) {
        std::cerr << g_failed << " failed\n";
        return 1;
    }
    return 0;
}
