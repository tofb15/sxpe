#include "check.hpp"
#include "sxpe/resources/objk.hpp"
#include "sxpe/resources/vpxy.hpp"

#include <cstring>
#include <string_view>
#include <vector>

namespace {

void wu32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}
void wu8(std::vector<std::byte>& o, std::uint8_t v) { o.push_back(std::byte{v}); }
void wstr(std::vector<std::byte>& o, std::string_view s) {
    wu32(o, static_cast<std::uint32_t>(s.size()));
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    o.insert(o.end(), p, p + s.size());
}
void wf32(std::vector<std::byte>& o, float v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}

}  // namespace

int main() {
    {
        std::vector<std::byte> body;
        wu8(body, 1);
        wu32(body, 0x23177498u);
        wu8(body, 1);
        wstr(body, "scriptClass");
        wu8(body, 0);
        wstr(body, "My.Class");
        wu8(body, 1);
        const auto tgi_off = static_cast<std::uint32_t>(body.size());
        wu8(body, 1);
        wu32(body, 0x736884F1u);
        wu32(body, 0);
        wu32(body, 1);
        wu32(body, 0);

        std::vector<std::byte> bytes;
        wu32(bytes, 7);
        wu32(bytes, tgi_off);
        wu32(bytes, 17);
        bytes.insert(bytes.end(), body.begin(), body.end());

        auto p = sxpe::resources::parse_objk(bytes);
        CHECK(p.has_value());
        CHECK(p->version == 7);
        CHECK(p->components.size() == 1);
        CHECK(p->components[0] == 0x23177498u);
        CHECK(p->data.size() == 1);
        CHECK(p->data[0].key == "scriptClass");
        CHECK(p->data[0].text == "My.Class");
        CHECK(p->visibility == 1);
        CHECK(p->tgi_count == 1);
        CHECK(p->tgis.size() == 1);
        CHECK(p->tgis[0].type == 0x736884F1u);
        CHECK(p->tgis[0].group == 0);
        CHECK(p->tgis[0].instance == 1);
    }

    {
        auto bad = sxpe::resources::parse_objk(std::span<const std::byte>{});
        CHECK(!bad.has_value());
    }

    {
        std::vector<std::byte> body;
        wu8(body, 1);
        wu8(body, 1);
        wu32(body, 0);
        wu8(body, 2);
        for (int i = 0; i < 6; ++i) {
            wf32(body, static_cast<float>(i));
        }
        wu32(body, 0);
        wu8(body, 0);
        const auto tgi_off = static_cast<std::uint32_t>(body.size());
        wu8(body, 0);

        std::vector<std::byte> bytes;
        bytes.push_back(std::byte{'V'});
        bytes.push_back(std::byte{'P'});
        bytes.push_back(std::byte{'X'});
        bytes.push_back(std::byte{'Y'});
        wu32(bytes, 4);
        wu32(bytes, tgi_off);
        wu32(bytes, 1);
        bytes.insert(bytes.end(), body.begin(), body.end());

        auto p = sxpe::resources::parse_vpxy(bytes);
        CHECK(p.has_value());
        CHECK(p->version == 4);
        CHECK(p->entries.size() == 1);
        CHECK(p->entries[0].type == 1);
        CHECK(p->has_bbox);
        CHECK(p->bbox[3] == 3.0f);
        CHECK(!p->modular);
        CHECK(p->tgi_count == 0);
    }

    {
        std::vector<std::byte> no_magic(32, std::byte{0});
        auto bad = sxpe::resources::parse_vpxy(no_magic);
        CHECK(!bad.has_value());
    }

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
