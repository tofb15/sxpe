#include "check.hpp"
#include "sxpe/resources/key_table.hpp"
#include "sxpe/resources/refs.hpp"

#include <cstring>
#include <vector>

namespace {

void wu8(std::vector<std::byte>& o, std::uint8_t v) { o.push_back(std::byte{v}); }
void wu16(std::vector<std::byte>& o, std::uint16_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 2);
}
void wu32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}
void wu64(std::vector<std::byte>& o, std::uint64_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 8);
}
void wtgi(std::vector<std::byte>& o, std::uint32_t type, std::uint32_t group, std::uint64_t inst) {
    wu32(o, type);
    wu32(o, group);
    wu64(o, inst);
}

}  // namespace

int main() {
    {
        std::vector<std::byte> bytes;
        wu16(bytes, 1);  // version
        wu32(bytes, 2);  // count1
        wtgi(bytes, 0x0333406Cu, 0, 0x11);
        wu16(bytes, 0);
        wtgi(bytes, 0x00B2D882u, 1, 0x22);
        wu16(bytes, 1);
        wu32(bytes, 1);  // count2
        wu16(bytes, 7);

        auto r = sxpe::resources::parse_refs(bytes);
        CHECK(r.has_value());
        CHECK(r->version == 1);
        CHECK(!r->has_thingy);
        CHECK(!r->aux_is_dword);
        CHECK(r->entries.size() == 2);
        CHECK(r->entries[0].tgi.type == 0x0333406Cu);
        CHECK(r->entries[0].tgi.instance == 0x11);
        CHECK(r->entries[1].tgi.group == 1);
        CHECK(r->entries[1].tgi.instance == 0x22);
        CHECK(r->indices.size() == 1);
        CHECK(r->indices[0] == 7);
        CHECK(!r->partial);
    }

    {
        auto bad = sxpe::resources::parse_refs(std::span<const std::byte>{});
        CHECK(!bad.has_value());
    }

    {
        std::vector<std::byte> kt;
        wu8(kt, 1);
        wtgi(kt, 0x736884F1u, 0, 1);
        auto tgis = sxpe::resources::parse_key_table_tgis(kt);
        CHECK(tgis.has_value());
        CHECK(tgis->size() == 1);
        CHECK((*tgis)[0].type == 0x736884F1u);
        CHECK((*tgis)[0].instance == 1);
    }

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
