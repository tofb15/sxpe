#include "check.hpp"
#include "sxpe/error.hpp"
#include "sxpe/resources/dds.hpp"

#include <cstring>
#include <vector>

namespace {

using sxpe::resources::decode_dds_rgba;
using sxpe::resources::encode_dds_bgra;
using sxpe::resources::parse_dds;
using sxpe::resources::validate_dds_replace;

void wu32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}

std::vector<std::byte> make_header(std::uint32_t height, std::uint32_t width, std::uint32_t pf_flags,
                                   std::uint32_t fourcc, std::uint32_t bitcount, std::uint32_t r,
                                   std::uint32_t g, std::uint32_t b, std::uint32_t a,
                                   std::uint32_t caps2, std::uint32_t pitch) {
    std::vector<std::byte> o;
    o.reserve(128);
    wu32(o, 0x20534444);
    wu32(o, 124);
    wu32(o, 0x1007);
    wu32(o, height);
    wu32(o, width);
    wu32(o, pitch);
    wu32(o, 0);
    wu32(o, 1);
    o.insert(o.end(), 44, std::byte{0});
    wu32(o, 32);
    wu32(o, pf_flags);
    wu32(o, fourcc);
    wu32(o, bitcount);
    wu32(o, r);
    wu32(o, g);
    wu32(o, b);
    wu32(o, a);
    wu32(o, 0x1000);
    wu32(o, caps2);
    wu32(o, 0);
    wu32(o, 0);
    wu32(o, 0);
    CHECK(o.size() == 128);
    return o;
}

std::vector<std::byte> solid_dxt1_block(std::uint16_t c0, std::uint16_t c1) {
    std::vector<std::byte> blk(8, std::byte{0});
    std::memcpy(blk.data(), &c0, 2);
    std::memcpy(blk.data() + 2, &c1, 2);
    // bits all 0 → colour 0
    return blk;
}

}  // namespace

int main() {
    // A8R8G8B8 round-trip via encode helper
    {
        std::vector<std::byte> px = {std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40}};
        auto dds = encode_dds_bgra(1, 1, px);
        CHECK(dds.has_value());
        auto inf = parse_dds(*dds);
        CHECK(inf && inf->format == "A8R8G8B8" && inf->decode_supported && !inf->cubemap);
        auto rgba = decode_dds_rgba(*dds);
        CHECK(rgba && rgba->size() == 4);
        CHECK((*rgba)[0] == std::byte{30} && (*rgba)[1] == std::byte{20} &&
              (*rgba)[2] == std::byte{10} && (*rgba)[3] == std::byte{40});
        CHECK(validate_dds_replace(*dds).has_value());
    }

    // A8B8G8R8 synthetic 2×1
    {
        auto hdr = make_header(1, 2, 0x41, 0, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000, 0,
                               8);
        // pixel0: R=1 G=2 B=3 A=4  in A8B8G8R8 little-endian packing
        // memory: R, G, B, A bytes when masks are A8B8G8R8 → dword bits
        std::uint32_t p0 = 0x04030201;  // A=4 B=3 G=2 R=1 with A8B8G8R8 masks
        std::uint32_t p1 = 0xFF0000FF;  // R=255 opaque
        for (int i = 0; i < 4; ++i) {
            hdr.push_back(std::byte{static_cast<unsigned char>((p0 >> (8 * i)) & 0xFF)});
        }
        for (int i = 0; i < 4; ++i) {
            hdr.push_back(std::byte{static_cast<unsigned char>((p1 >> (8 * i)) & 0xFF)});
        }
        auto inf = parse_dds(hdr);
        CHECK(inf && inf->format == "A8B8G8R8" && inf->decode_supported);
        auto rgba = decode_dds_rgba(hdr);
        CHECK(rgba && rgba->size() == 8);
        CHECK((*rgba)[0] == std::byte{1} && (*rgba)[1] == std::byte{2} &&
              (*rgba)[2] == std::byte{3} && (*rgba)[3] == std::byte{4});
    }

    // DXT1 4×4 solid
    {
        auto hdr = make_header(4, 4, 0x4, 0x31545844, 0, 0, 0, 0, 0, 0, 8);
        auto blk = solid_dxt1_block(0xF800, 0x07E0);  // red / green, c0>c1
        hdr.insert(hdr.end(), blk.begin(), blk.end());
        auto inf = parse_dds(hdr);
        CHECK(inf && inf->format == "DXT1" && inf->decode_supported);
        auto rgba = decode_dds_rgba(hdr);
        CHECK(rgba && rgba->size() == 64);
        CHECK((*rgba)[0] == std::byte{255});  // red channel of c0
        CHECK(validate_dds_replace(hdr).has_value());
    }

    // DXT3 4×4: opaque alpha nibbles 0xF + red colour
    {
        auto hdr = make_header(4, 4, 0x4, 0x33545844, 0, 0, 0, 0, 0, 0, 16);
        std::vector<std::byte> alpha(8, std::byte{0xFF});  // all nibbles 0xF → alpha 255
        auto color = solid_dxt1_block(0xF800, 0x07E0);
        hdr.insert(hdr.end(), alpha.begin(), alpha.end());
        hdr.insert(hdr.end(), color.begin(), color.end());
        auto inf = parse_dds(hdr);
        CHECK(inf && inf->format == "DXT3" && inf->decode_supported);
        auto rgba = decode_dds_rgba(hdr);
        CHECK(rgba && rgba->size() == 64);
        CHECK((*rgba)[3] == std::byte{255});
        CHECK(validate_dds_replace(hdr).has_value());
    }

    // DXT5 4×4: a0=255 a1=0 with a0>a1 → opaque endpoints; bits 0 → a[0]=255
    {
        auto hdr = make_header(4, 4, 0x4, 0x35545844, 0, 0, 0, 0, 0, 0, 16);
        std::vector<std::byte> alpha(8, std::byte{0});
        alpha[0] = std::byte{255};
        alpha[1] = std::byte{0};
        auto color = solid_dxt1_block(0xF800, 0x07E0);
        hdr.insert(hdr.end(), alpha.begin(), alpha.end());
        hdr.insert(hdr.end(), color.begin(), color.end());
        auto inf = parse_dds(hdr);
        CHECK(inf && inf->format == "DXT5" && inf->decode_supported);
        auto rgba = decode_dds_rgba(hdr);
        CHECK(rgba && (*rgba)[3] == std::byte{255});
        CHECK(validate_dds_replace(hdr).has_value());
    }

    // Cubemap refused
    {
        auto hdr = make_header(4, 4, 0x4, 0x31545844, 0, 0, 0, 0, 0, 0x200, 8);
        auto blk = solid_dxt1_block(0xF800, 0x001F);
        hdr.insert(hdr.end(), blk.begin(), blk.end());
        auto inf = parse_dds(hdr);
        CHECK(inf && inf->cubemap && !inf->decode_supported);
        auto rgba = decode_dds_rgba(hdr);
        CHECK(!rgba);
        if (!rgba) {
            CHECK(rgba.error().code == sxpe::ErrorCode::unsupported_game_or_format);
            CHECK(rgba.error().message.find("cubemap") != std::string::npos);
        }
        auto rep = validate_dds_replace(hdr);
        CHECK(!rep);
        if (!rep) {
            CHECK(rep.error().message.find("cubemap") != std::string::npos);
        }
    }

    // BC7 FourCC refused
    {
        // 'DXT1' swapped to fake 'BC7 ' is awkward; use DX10 fourcc
        constexpr std::uint32_t kDx10 = 0x30315844;  // 'DX10'
        auto hdr = make_header(4, 4, 0x4, kDx10, 0, 0, 0, 0, 0, 0, 0);
        auto inf = parse_dds(hdr);
        CHECK(inf && !inf->decode_supported);
        auto rgba = decode_dds_rgba(hdr);
        CHECK(!rgba);
        if (!rgba) {
            CHECK(rgba.error().message.find("DX10") != std::string::npos ||
                  rgba.error().message.find("FourCC") != std::string::npos);
        }
        CHECK(!validate_dds_replace(hdr).has_value());
    }

    // R5G6B5 1×1
    {
        auto hdr = make_header(1, 1, 0x40, 0, 16, 0xF800, 0x07E0, 0x001F, 0, 0, 2);
        std::uint16_t red = 0xF800;
        hdr.push_back(std::byte{static_cast<unsigned char>(red & 0xFF)});
        hdr.push_back(std::byte{static_cast<unsigned char>((red >> 8) & 0xFF)});
        auto inf = parse_dds(hdr);
        CHECK(inf && inf->format == "R5G6B5");
        auto rgba = decode_dds_rgba(hdr);
        CHECK(rgba && (*rgba)[0] == std::byte{255} && (*rgba)[3] == std::byte{255});
    }

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
