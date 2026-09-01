#include "check.hpp"
#include "sxpe/error.hpp"
#include "sxpe/resources/dds.hpp"
#include "sxpe/resources/nmap.hpp"
#include "sxpe/resources/stbl.hpp"

#include <string>
#include <vector>

int main() {
    using namespace sxpe::resources;
    Stbl t;
    t.entries.push_back({1, "Hello"});
    t.entries.push_back({2, "Café"});
    auto bytes = write_stbl(t);
    CHECK(bytes.has_value());
    if (bytes) {
        auto back = parse_stbl(*bytes);
        CHECK(back.has_value());
        if (back) {
            CHECK(back->entries.size() == 2);
            CHECK(back->entries[0].id == 1);
            CHECK(back->entries[0].text == "Hello");
            CHECK(back->entries[1].text == "Café");
        }
    }

    Nmap n;
    n.entries.push_back({0xABC, "MeshLOD"});
    auto nb = write_nmap(n);
    CHECK(nb.has_value());
    if (nb) {
        auto n2 = parse_nmap(*nb);
        CHECK(n2.has_value() && lookup_name(*n2, 0xABC) == "MeshLOD");
    }

    std::vector<std::byte> px(4);
    px[0] = std::byte{0};
    px[1] = std::byte{0};
    px[2] = std::byte{255};
    px[3] = std::byte{255};
    auto dds = encode_dds_bgra(1, 1, px);
    CHECK(dds.has_value());
    if (dds) {
        auto inf = parse_dds(*dds);
        CHECK(inf.has_value());
        if (inf) {
            CHECK(inf->width == 1 && inf->height == 1);
        }
        auto rgba = decode_dds_rgba(*dds);
        CHECK(rgba.has_value());
        if (rgba) {
            CHECK(rgba->size() == 4);
            CHECK((*rgba)[0] == std::byte{255});
        }
    }

    // Fixture-style bogus NMAP: ASCII "NMAP\0fak..." is not a count. Must not reserve billions.
    const char fake[] = "NMAP\0fake-name-map";
    std::vector<std::byte> bomb(sizeof(fake) - 1);
    for (std::size_t i = 0; i < bomb.size(); ++i) {
        bomb[i] = static_cast<std::byte>(static_cast<unsigned char>(fake[i]));
    }
    auto boom = parse_nmap(bomb);
    CHECK(!boom);
    if (!boom) {
        CHECK(boom.error().code == sxpe::ErrorCode::cap_exceeded);
    }

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
