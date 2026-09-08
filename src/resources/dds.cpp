#include "sxpe/resources/dds.hpp"

#include "sxpe/core/caps.hpp"

#include <cstring>
#include <string>

namespace sxpe::resources {
namespace {

constexpr std::uint32_t kDdsMagic = 0x20534444;
constexpr std::uint32_t kDxt1 = 0x31545844;
constexpr std::uint32_t kDxt2 = 0x32545844;
constexpr std::uint32_t kDxt3 = 0x33545844;
constexpr std::uint32_t kDxt4 = 0x34545844;
constexpr std::uint32_t kDxt5 = 0x35545844;

constexpr std::uint32_t kDdpfFourcc = 0x4;
constexpr std::uint32_t kDdpfRgb = 0x40;
constexpr std::uint32_t kDdpfAlphaPixels = 0x1;
constexpr std::uint32_t kDdpfLuminance = 0x20000;
constexpr std::uint32_t kDdpfAlpha = 0x2;

constexpr std::uint32_t kDdscaps2Cubemap = 0x200;
constexpr std::uint32_t kDdscaps2Volume = 0x200000;

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

int mask_shift(std::uint32_t mask) {
    if (mask == 0) {
        return 0;
    }
    int s = 0;
    while ((mask & 1u) == 0) {
        mask >>= 1;
        ++s;
    }
    return s;
}

int mask_bits(std::uint32_t mask) {
    int n = 0;
    while (mask) {
        n += static_cast<int>(mask & 1u);
        mask >>= 1;
    }
    return n;
}

std::uint8_t extract_chan(std::uint32_t pixel, std::uint32_t mask) {
    if (mask == 0) {
        return 0;
    }
    const int shift = mask_shift(mask);
    const int bits = mask_bits(mask);
    const auto v = (pixel & mask) >> shift;
    if (bits >= 8) {
        return static_cast<std::uint8_t>(v & 0xFFu);
    }
    if (bits == 0) {
        return 0;
    }
    return static_cast<std::uint8_t>((v * 255u) / ((1u << bits) - 1u));
}

std::string fourcc_name(std::uint32_t fourcc) {
    char fcc[5]{};
    std::memcpy(fcc, &fourcc, 4);
    for (int i = 0; i < 4; ++i) {
        const auto c = static_cast<unsigned char>(fcc[i]);
        if (c < 32 || c > 126) {
            fcc[i] = '?';
        }
    }
    return std::string(fcc);
}

std::string rgb_layout_name(std::uint32_t bitcount, std::uint32_t r, std::uint32_t g, std::uint32_t b,
                            std::uint32_t a, std::uint32_t flags) {
    if (bitcount == 32 && r == 0 && g == 0 && b == 0 && a == 0) {
        return "A8R8G8B8";  // legacy omitted masks
    }
    if (bitcount == 32 && r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF) {
        return (flags & kDdpfAlphaPixels) && a == 0xFF000000 ? "A8R8G8B8" : "X8R8G8B8";
    }
    if (bitcount == 32 && r == 0x000000FF && g == 0x0000FF00 && b == 0x00FF0000) {
        return (flags & kDdpfAlphaPixels) && a == 0xFF000000 ? "A8B8G8R8" : "X8B8G8R8";
    }
    if (bitcount == 32 && r == 0xFF000000 && g == 0x00FF0000 && b == 0x0000FF00 && a == 0x000000FF) {
        return "R8G8B8A8";
    }
    if (bitcount == 32 && r == 0x0000FF00 && g == 0x00FF0000 && b == 0xFF000000 && a == 0x000000FF) {
        return "B8G8R8A8";
    }
    if (bitcount == 24 && r == 0x00FF0000 && g == 0x0000FF00 && b == 0x000000FF) {
        return "R8G8B8";
    }
    if (bitcount == 24 && r == 0x000000FF && g == 0x0000FF00 && b == 0x00FF0000) {
        return "B8G8R8";
    }
    if (bitcount == 16 && r == 0xF800 && g == 0x07E0 && b == 0x001F) {
        return "R5G6B5";
    }
    if (bitcount == 16 && r == 0x7C00 && g == 0x03E0 && b == 0x001F) {
        return (a == 0x8000) ? "A1R5G5B5" : "X1R5G5B5";
    }
    if (bitcount == 8 && (flags & kDdpfLuminance)) {
        return "L8";
    }
    if (bitcount == 8 && (flags & kDdpfAlpha)) {
        return "A8";
    }
    return "RGB" + std::to_string(bitcount);
}

bool rgb_layout_supported(std::uint32_t bitcount, std::uint32_t /*flags*/, std::uint32_t r,
                          std::uint32_t g, std::uint32_t b, std::uint32_t a) {
    if (bitcount == 32) {
        // Writers sometimes omit masks; treat as BGRA (A8R8G8B8 memory order).
        if (r == 0 && g == 0 && b == 0 && a == 0) {
            return true;
        }
        // Any 8-bit-per-channel mask layout we can extract.
        return mask_bits(r) == 8 && mask_bits(g) == 8 && mask_bits(b) == 8 &&
               (a == 0 || mask_bits(a) == 8);
    }
    if (bitcount == 24) {
        return mask_bits(r) == 8 && mask_bits(g) == 8 && mask_bits(b) == 8;
    }
    if (bitcount == 16) {
        // R5G6B5 or A1R5G5B5 / X1R5G5B5
        if (r == 0xF800 && g == 0x07E0 && b == 0x001F) {
            return true;
        }
        if (r == 0x7C00 && g == 0x03E0 && b == 0x001F) {
            return true;
        }
    }
    return false;
}

bool fourcc_decode_supported(std::uint32_t fourcc) {
    return fourcc == kDxt1 || fourcc == kDxt3 || fourcc == kDxt5;
}

void decode_bc1_block(const std::byte* blk, std::byte* rgba, std::uint32_t w, std::uint32_t x,
                      std::uint32_t y, std::uint32_t h, bool write_alpha) {
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
            if (write_alpha) {
                p[3] = std::byte{a[idx]};
            }
        }
    }
}

void apply_dxt3_alpha(const std::byte* alpha_blk, std::byte* rgba, std::uint32_t w, std::uint32_t x,
                      std::uint32_t y, std::uint32_t h) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, alpha_blk, 8);
    for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
            const auto xx = x + static_cast<std::uint32_t>(px);
            const auto yy = y + static_cast<std::uint32_t>(py);
            const auto nibble = static_cast<std::uint8_t>(bits & 0xF);
            bits >>= 4;
            if (xx >= w || yy >= h) {
                continue;
            }
            auto* p = rgba + (static_cast<std::size_t>(yy) * w + xx) * 4;
            p[3] = std::byte{static_cast<std::uint8_t>(nibble * 17)};
        }
    }
}

void apply_dxt5_alpha(const std::byte* alpha_blk, std::byte* rgba, std::uint32_t w, std::uint32_t x,
                      std::uint32_t y, std::uint32_t h) {
    const auto a0 = static_cast<std::uint8_t>(alpha_blk[0]);
    const auto a1 = static_cast<std::uint8_t>(alpha_blk[1]);
    std::uint8_t a[8];
    a[0] = a0;
    a[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            a[i + 1] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            a[i + 1] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
        }
        a[6] = 0;
        a[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(alpha_blk[2 + i]))
                << (8 * i);
    }
    for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
            const auto xx = x + static_cast<std::uint32_t>(px);
            const auto yy = y + static_cast<std::uint32_t>(py);
            const int idx = static_cast<int>(bits & 7);
            bits >>= 3;
            if (xx >= w || yy >= h) {
                continue;
            }
            auto* p = rgba + (static_cast<std::size_t>(yy) * w + xx) * 4;
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
        return std::unexpected(err(ErrorCode::cap_exceeded, "dds dimensions exceed kMaxDdsEdge"));
    }
    inf.pitch_or_linear = ru32(bytes, 20);
    inf.mipmap_count = ru32(bytes, 28);
    if (inf.mipmap_count == 0) {
        inf.mipmap_count = 1;
    }
    const auto flags = ru32(bytes, 80);
    const auto fourcc = ru32(bytes, 84);
    const auto bitcount = ru32(bytes, 88);
    inf.bit_count = bitcount;
    inf.r_mask = ru32(bytes, 92);
    inf.g_mask = ru32(bytes, 96);
    inf.b_mask = ru32(bytes, 100);
    inf.a_mask = ru32(bytes, 104);
    const auto caps2 = ru32(bytes, 112);
    inf.cubemap = (caps2 & kDdscaps2Cubemap) != 0;
    inf.volume = (caps2 & kDdscaps2Volume) != 0;
    if (flags & kDdpfFourcc) {
        inf.compressed = true;
        inf.format = fourcc_name(fourcc);
        inf.decode_supported = fourcc_decode_supported(fourcc) && !inf.cubemap && !inf.volume;
    } else {
        inf.compressed = false;
        inf.format = rgb_layout_name(bitcount, inf.r_mask, inf.g_mask, inf.b_mask, inf.a_mask, flags);
        inf.decode_supported =
            rgb_layout_supported(bitcount, flags, inf.r_mask, inf.g_mask, inf.b_mask, inf.a_mask) &&
            !inf.cubemap && !inf.volume;
    }
    return inf;
}

Result<std::vector<std::byte>> decode_dds_rgba(std::span<const std::byte> bytes) {
    auto inf = parse_dds(bytes);
    if (!inf) {
        return std::unexpected(inf.error());
    }
    if (inf->cubemap) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                   "DDS cubemap refused (SXPE decodes 2D textures only; "
                                   "supported: DXT1/DXT3/DXT5, A8R8G8B8 and RGB mask variants)"));
    }
    if (inf->volume) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                   "DDS volume/3D texture refused (SXPE decodes 2D textures only)"));
    }
    const auto pixels = static_cast<std::uint64_t>(inf->width) * inf->height * 4ull;
    if (inf->width > sxpe::core::caps::kMaxDdsEdge || inf->height > sxpe::core::caps::kMaxDdsEdge ||
        pixels > sxpe::core::caps::kMaxResourceBytes ||
        pixels / 4 != static_cast<std::uint64_t>(inf->width) * inf->height) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "dds pixels"));
    }
    std::vector<std::byte> out(static_cast<std::size_t>(pixels));
    const auto data = bytes.subspan(128);
    const auto pf_flags = ru32(bytes, 80);
    if (!inf->compressed) {
        const auto bitcount = inf->bit_count;
        const auto rmask = inf->r_mask;
        const auto gmask = inf->g_mask;
        const auto bmask = inf->b_mask;
        const auto amask = inf->a_mask;
        if (bitcount == 32) {
            if (!rgb_layout_supported(bitcount, pf_flags, rmask, gmask, bmask, amask)) {
                return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                           "unsupported DDS RGB32 layout (need 8-bit R/G/B masks; "
                                           "e.g. A8R8G8B8 / A8B8G8R8 / R8G8B8A8)"));
            }
            const auto need = static_cast<std::size_t>(inf->width) * inf->height * 4;
            if (data.size() < need) {
                return std::unexpected(err(ErrorCode::corrupt, "dds rgb32 truncated"));
            }
            // Default legacy path when masks are zero (some writers omit them): treat as BGRA.
            const bool use_masks = rmask != 0 || gmask != 0 || bmask != 0;
            for (std::size_t i = 0; i + 3 < need; i += 4) {
                std::uint32_t pix = 0;
                std::memcpy(&pix, data.data() + i, 4);
                if (!use_masks) {
                    out[i + 0] = data[i + 2];
                    out[i + 1] = data[i + 1];
                    out[i + 2] = data[i + 0];
                    out[i + 3] = data[i + 3];
                } else {
                    out[i + 0] = std::byte{extract_chan(pix, rmask)};
                    out[i + 1] = std::byte{extract_chan(pix, gmask)};
                    out[i + 2] = std::byte{extract_chan(pix, bmask)};
                    out[i + 3] = amask == 0 ? std::byte{255} : std::byte{extract_chan(pix, amask)};
                }
            }
            return out;
        }
        if (bitcount == 24) {
            std::uint32_t r = rmask, g = gmask, b = bmask;
            if (r == 0 && g == 0 && b == 0) {
                // Legacy: BGR packed.
                r = 0x000000FF;
                g = 0x0000FF00;
                b = 0x00FF0000;
            }
            if (!rgb_layout_supported(24, pf_flags, r, g, b, 0)) {
                return std::unexpected(
                    err(ErrorCode::unsupported_game_or_format, "unsupported DDS RGB24 layout"));
            }
            const auto need = static_cast<std::size_t>(inf->width) * inf->height * 3;
            if (data.size() < need) {
                return std::unexpected(err(ErrorCode::corrupt, "dds rgb24 truncated"));
            }
            std::size_t s = 0;
            for (std::size_t i = 0; i < out.size(); i += 4) {
                std::uint32_t pix = 0;
                pix |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[s++]));
                pix |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[s++])) << 8;
                pix |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(data[s++])) << 16;
                out[i + 0] = std::byte{extract_chan(pix, r)};
                out[i + 1] = std::byte{extract_chan(pix, g)};
                out[i + 2] = std::byte{extract_chan(pix, b)};
                out[i + 3] = std::byte{255};
            }
            return out;
        }
        if (bitcount == 16) {
            if (!rgb_layout_supported(16, pf_flags, rmask, gmask, bmask, amask)) {
                return std::unexpected(
                    err(ErrorCode::unsupported_game_or_format,
                        "unsupported DDS RGB16 layout (supported: R5G6B5, A1R5G5B5/X1R5G5B5)"));
            }
            const auto need = static_cast<std::size_t>(inf->width) * inf->height * 2;
            if (data.size() < need) {
                return std::unexpected(err(ErrorCode::corrupt, "dds rgb16 truncated"));
            }
            std::size_t s = 0;
            for (std::size_t i = 0; i < out.size(); i += 4) {
                std::uint16_t pix16 = 0;
                std::memcpy(&pix16, data.data() + s, 2);
                s += 2;
                const auto pix = static_cast<std::uint32_t>(pix16);
                out[i + 0] = std::byte{extract_chan(pix, rmask)};
                out[i + 1] = std::byte{extract_chan(pix, gmask)};
                out[i + 2] = std::byte{extract_chan(pix, bmask)};
                out[i + 3] = amask == 0 ? std::byte{255} : std::byte{extract_chan(pix, amask)};
            }
            return out;
        }
        return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                   "unsupported DDS RGB layout (supported: 16/24/32-bit mask "
                                   "variants; see docs/spec/dds.md)"));
    }
    const auto fourcc = ru32(bytes, 84);
    if (fourcc == kDxt2 || fourcc == kDxt4) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                   std::string("DDS ") + fourcc_name(fourcc) +
                                       " (premultiplied) refused; use DXT3/DXT5"));
    }
    if (!fourcc_decode_supported(fourcc)) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                   "unsupported DDS FourCC '" + fourcc_name(fourcc) +
                                       "' (supported: DXT1, DXT3, DXT5; no BC7/DX10/DirectXTex)"));
    }
    std::size_t src = 0;
    for (std::uint32_t y = 0; y < inf->height; y += 4) {
        for (std::uint32_t x = 0; x < inf->width; x += 4) {
            if (fourcc == kDxt1) {
                if (src + 8 > data.size()) {
                    return std::unexpected(err(ErrorCode::corrupt, "dxt1 truncated"));
                }
                decode_bc1_block(data.data() + src, out.data(), inf->width, x, y, inf->height, true);
                src += 8;
            } else if (fourcc == kDxt3) {
                if (src + 16 > data.size()) {
                    return std::unexpected(err(ErrorCode::corrupt, "dxt3 truncated"));
                }
                decode_bc1_block(data.data() + src + 8, out.data(), inf->width, x, y, inf->height,
                                 false);
                apply_dxt3_alpha(data.data() + src, out.data(), inf->width, x, y, inf->height);
                src += 16;
            } else {  // DXT5
                if (src + 16 > data.size()) {
                    return std::unexpected(err(ErrorCode::corrupt, "dxt5 truncated"));
                }
                decode_bc1_block(data.data() + src + 8, out.data(), inf->width, x, y, inf->height,
                                 false);
                apply_dxt5_alpha(data.data() + src, out.data(), inf->width, x, y, inf->height);
                src += 16;
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
    if (width == 0 || height == 0 || width > sxpe::core::caps::kMaxDdsEdge ||
        height > sxpe::core::caps::kMaxDdsEdge) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "dds dimensions exceed kMaxDdsEdge"));
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

Result<DdsInfo> validate_dds_replace(std::span<const std::byte> bytes) {
    auto inf = parse_dds(bytes);
    if (!inf) {
        return std::unexpected(inf.error());
    }
    if (inf->cubemap) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                   "Replace DDS refused cubemap (2D textures only; "
                                   "supported: DXT1/DXT3/DXT5, A8R8G8B8 and RGB mask variants)"));
    }
    if (inf->volume) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                   "Replace DDS refused volume/3D texture (2D textures only)"));
    }
    if (!inf->decode_supported) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                   "Replace DDS unsupported format '" + inf->format +
                                       "' (supported: DXT1/DXT3/DXT5, A8R8G8B8 and RGB mask "
                                       "variants; no BC7/DX10 — see docs/spec/dds.md)"));
    }
    // Ensure payload actually decodes (truncated / corrupt caught here).
    auto pix = decode_dds_rgba(bytes);
    if (!pix) {
        return std::unexpected(pix.error());
    }
    return inf;
}

}  // namespace sxpe::resources
