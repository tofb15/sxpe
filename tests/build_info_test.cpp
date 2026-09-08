#include "check.hpp"
#include "sxpe/build_info.hpp"

#include <regex>
#include <string>

int main() {
    const std::string utc{SXPE_BUILD_UTC};
    CHECK(std::regex_match(utc, std::regex(R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2} UTC)")));

    const std::string lineage{SXPE_GIT_LINEAGE};
    CHECK(lineage == "official" || lineage == "fork" || lineage == "unknown");

    const std::string canonical{SXPE_GIT_CANONICAL_URL};
    CHECK(canonical == "https://github.com/tofb15/sxpe");

    const std::string commit{SXPE_GIT_COMMIT};
    CHECK(!commit.empty());

    const std::string branch{SXPE_GIT_BRANCH};
    CHECK(!branch.empty());

    if (lineage == "official") {
        CHECK(std::string{SXPE_GIT_SOURCE_URL} == canonical);
    }
    if (lineage == "fork") {
        const std::string src{SXPE_GIT_SOURCE_URL};
        CHECK(!src.empty());
        CHECK(src != canonical);
        CHECK(src.find("https://github.com/") == 0);
    }

    CHECK(SXPE_GIT_DIRTY == 0 || SXPE_GIT_DIRTY == 1);
    return 0;
}
