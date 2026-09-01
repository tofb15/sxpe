#include "sxpe/resources/dds.hpp"

#include "sxpe/core/caps.hpp"

#include <cstring>
#include <string>

namespace sxpe::resources {
namespace {

constexpr std::uint32_t kDdsMagic = 0x20534444;
constexpr std::uint32_t kDxt1 = 0x31545844;
constexpr std::uint32_t kDxt5 = 0x35545844;

std::uint32_t ru32(std::span<const std::byte> s, std::size_t o) {
    std::uint32_t v = 0;
    std::memcpy(&v, s.data() + o, 4);
    return v;
}

void wu32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}

std::uint8_t lerp(std::uint8_t a, std::uint8_t b, int num, int den) {
    return static_cast<std::uint8_t>((a * (den - num) + b * num) / den);
}

void rgb565(std::uint16_t c, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
    r = static_cast<std::uint8_t>(((c >> 11) & 31) * 255 / 31);
    g = static_cast<std::uint8_t>(((c >> 5) & 63) * 255 / 63);
    b = static_cast<std::uint8_t>((c & 31) * 255 / 31);
}

void decode_bc1_block(const std::byte* blk, std::byte* rgba, std::uint32_t w, std::uint32_t x,
                      std::uint32_t y, std::uint32_t h) {
    std::uint16_t c0 = 0, c1 = 0;
    std::memcpy(&c0, blk, 2);
    std::memcpy(&c1, blk + 2, 2);
    std::uint32_t bits = 0;
    std::memcpy(&bits, blk + 4, 4);
    std::uint8_t r[4], g[4], b[4], a[4];
    rgb565(c0, r[0], g[0], b[0]);
    rgb565(c1, r[1], g[1], b[1]);
    a[0] = a[1] = 255;
    if (c0 > c1) {
        r[2] = lerp(r[0], r[1], 1, 3);
        g[2] = lerp(g[0], g[1], 1, 3);
        b[2] = lerp(b[0], b[1], 1, 3);
        a[2] = 255;
        r[3] = lerp(r[0], r[1], 2, 3);
        g[3] = lerp(g[0], g[1], 2, 3);
        b[3] = lerp(b[0], b[1], 2, 3);
        a[3] = 255;
    } else {
        r[2] = lerp(r[0], r[1], 1, 2);
        g[2] = lerp(g[0], g[1], 1, 2);
        b[2] = lerp(b[0], b[1], 1, 2);
        a[2] = 255;
        r[3] = g[3] = b[3] = a[3] = 0;
    }
    for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
            const auto xx = x + static_cast<std::uint32_t>(px);
            const auto yy = y + static_cast<std::uint32_t>(py);
            if (xx >= w || yy >= h) {
                bits >>= 2;
                continue;
            }
            const int idx = static_cast<int>(bits & 3);
            bits >>= 2;
            auto* p = rgba + (static_cast<std::size_t>(yy) * w + xx) * 4;
            p[0] = std::byte{r[idx]};
            p[1] = std::byte{g[idx]};
            p[2] = std::byte{b[idx]};
            p[3] = std::byte{a[idx]};
        }
    }
}

}  // namespace

Result<DdsInfo> parse_dds(std::span<const std::byte> bytes) {
    if (bytes.size() < 128 || ru32(bytes, 0) != kDdsMagic) {
        return std::unexpected(err(ErrorCode::corrupt, "not DDS"));
    }
    DdsInfo inf;
    inf.height = ru32(bytes, 12);
    inf.width = ru32(bytes, 16);
    if (inf.width == 0 || inf.height == 0 || inf.width > sxpe::core::caps::kMaxDdsEdge ||
        inf.height > sxpe::core::caps::kMaxDdsEdge) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "dds dimensions"));
    }
    inf.pitch_or_linear = ru32(bytes, 20);
    const auto flags = ru32(bytes, 80);
    const auto fourcc = ru32(bytes, 84);
    const auto bitcount = ru32(bytes, 88);
    if (flags & 0x4) {
        inf.compressed = true;
        char fcc[5]{};
        std::memcpy(fcc, &fourcc, 4);
        inf.format = fcc;
    } else {
        inf.format = "RGB" + std::to_string(bitcount);
    }
    return inf;
}

Result<std::vector<std::byte>> decode_dds_rgba(std::span<const std::byte> bytes) {
    auto inf = parse_dds(bytes);
    if (!inf) {
        return std::unexpected(inf.error());
    }
    const auto pixels = static_cast<std::uint64_t>(inf->width) * inf->height * 4ull;
    if (inf->width > sxpe::core::caps::kMaxDdsEdge || inf->height > sxpe::core::caps::kMaxDdsEdge ||
        pixels > sxpe::core::caps::kMaxResourceBytes || pixels / 4 != static_cast<std::uint64_t>(inf->width) * inf->height) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "dds pixels"));
    }
    std::vector<std::byte> out(static_cast<std::size_t>(pixels));
    const auto data = bytes.subspan(128);
    if (!inf->compressed) {
        const auto bitcount = ru32(bytes, 88);
        if (bitcount == 32 && data.size() >= out.size()) {
            for (std::size_t i = 0; i + 3 < out.size(); i += 4) {
                out[i + 0] = data[i + 2];
                out[i + 1] = data[i + 1];
                out[i + 2] = data[i + 0];
                out[i + 3] = data[i + 3];
            }
            return out;
        }
        if (bitcount == 24 && data.size() >= static_cast<std::size_t>(inf->width) * inf->height * 3) {
            std::size_t s = 0;
            for (std::size_t i = 0; i < out.size(); i += 4) {
                out[i + 2] = data[s++];
                out[i + 1] = data[s++];
                out[i + 0] = data[s++];
                out[i + 3] = std::byte{255};
            }
            return out;
        }
        return std::unexpected(err(ErrorCode::unsupported_game_or_format, "dds rgb layout"));
    }
    const auto fourcc = ru32(bytes, 84);
    std::size_t src = 0;
    for (std::uint32_t y = 0; y < inf->height; y += 4) {
        for (std::uint32_t x = 0; x < inf->width; x += 4) {
            if (fourcc == kDxt1) {
                if (src + 8 > data.size()) {
                    return std::unexpected(err(ErrorCode::corrupt, "dxt1 truncated"));
                }
                decode_bc1_block(data.data() + src, out.data(), inf->width, x, y, inf->height);
                src += 8;
            } else if (fourcc == kDxt5) {
                if (src + 16 > data.size()) {
                    return std::unexpected(err(ErrorCode::corrupt, "dxt5 truncated"));
                }
                decode_bc1_block(data.data() + src + 8, out.data(), inf->width, x, y, inf->height);
                src += 16;
            } else {
                return std::unexpected(err(ErrorCode::unsupported_game_or_format, "dds fourcc"));
            }
        }
    }
    return out;
}

Result<std::vector<std::byte>> encode_dds_bgra(std::uint32_t width, std::uint32_t height,
                                               std::span<const std::byte> bgra) {
    if (static_cast<std::uint64_t>(width) * height * 4 != bgra.size()) {
        return std::unexpected(err(ErrorCode::invalid_argument, "bgra size"));
    }
    std::vector<std::byte> o;
    o.reserve(128 + bgra.size());
    wu32(o, kDdsMagic);
    wu32(o, 124);
    wu32(o, 0x1007);  // caps | height | width | pixelformat
    wu32(o, height);
    wu32(o, width);
    wu32(o, width * 4);
    wu32(o, 0);  // depth
    wu32(o, 1);  // mipmap
    o.insert(o.end(), 44, std::byte{0});
    wu32(o, 32);     // pf size
    wu32(o, 0x41);   // rgb | alpha
    wu32(o, 0);
    wu32(o, 32);
    wu32(o, 0x00FF0000);
    wu32(o, 0x0000FF00);
    wu32(o, 0x000000FF);
    wu32(o, 0xFF000000);
    wu32(o, 0x1000);  // texture
    o.insert(o.end(), 16, std::byte{0});
    o.insert(o.end(), bgra.begin(), bgra.end());
    return o;
}

}  // namespace sxpe::resources
