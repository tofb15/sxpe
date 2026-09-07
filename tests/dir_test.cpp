#include "check.hpp"
#include "sxpe/resources/dir.hpp"

#include <span>
#include <vector>

int main() {
    using namespace sxpe::resources;
    using sxpe::games::sims3::Tgi;

    DirEntry e{{0x00B2D882u, 0, 0x100}, 4096};
    auto raw = write_dir(std::span<const DirEntry>(&e, 1));
    CHECK(raw.has_value());
    CHECK(raw->size() == 20);
    auto parsed = parse_dir(*raw);
    CHECK(parsed.has_value());
    CHECK(parsed->size() == 1);
    CHECK((*parsed)[0].tgi.type == 0x00B2D882u);
    CHECK((*parsed)[0].tgi.instance == 0x100);
    CHECK((*parsed)[0].mem_size == 4096);

    std::vector<std::byte> legacy(16, std::byte{0});
    legacy[0] = std::byte{1};
    legacy[12] = std::byte{8};
    auto old = parse_dir(legacy);
    CHECK(old.has_value());
    CHECK((*old)[0].tgi.type == 1);
    CHECK((*old)[0].mem_size == 8);

    CHECK(!parse_dir(std::vector<std::byte>(3, std::byte{0})).has_value());

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
