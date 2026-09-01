#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace sxpe::resources {

struct PngIhdr {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::uint8_t bit_depth{0};
    std::uint8_t color_type{0};
};

inline std::uint32_t png_be32(std::span<const std::byte> b, std::size_t off) {
    return (static_cast<std::uint32_t>(b[off]) << 24) |
           (static_cast<std::uint32_t>(b[off + 1]) << 16) |
           (static_cast<std::uint32_t>(b[off + 2]) << 8) | static_cast<std::uint32_t>(b[off + 3]);
}

inline std::optional<PngIhdr> parse_png_ihdr(std::span<const std::byte> b) {
    if (b.size() < 26) {
        return std::nullopt;
    }
    const unsigned char m0 = static_cast<unsigned char>(b[0]);
    if (m0 != 0x89 || b[1] != std::byte{'P'} || b[2] != std::byte{'N'} || b[3] != std::byte{'G'}) {
        return std::nullopt;
    }
    if (png_be32(b, 12) != 0x49484452u) {
        return std::nullopt;
    }
    PngIhdr i;
    i.width = png_be32(b, 16);
    i.height = png_be32(b, 20);
    i.bit_depth = static_cast<std::uint8_t>(b[24]);
    i.color_type = static_cast<std::uint8_t>(b[25]);
    if (i.width == 0 || i.height == 0) {
        return std::nullopt;
    }
    return i;
}

/// True when the blob is a PNG the game SNAP decoder accepts: signature,
/// IHDR, one or more IDAT, IEND, and optional trailing zeros. Ancillary
/// chunks (iCCP, pHYs, tEXt, zTXt, iTXt, …) make TS3 refuse the save.
inline bool png_is_game_snap(std::span<const std::byte> b) {
    if (!parse_png_ihdr(b)) {
        return false;
    }
    constexpr std::uint32_t kIhdr = 0x49484452u;
    constexpr std::uint32_t kIdat = 0x49444154u;
    constexpr std::uint32_t kIend = 0x49454E44u;
    std::size_t off = 8;
    bool saw_ihdr = false;
    bool saw_idat = false;
    while (off + 12 <= b.size()) {
        const std::uint32_t len = png_be32(b, off);
        if (len > b.size() - (off + 12)) {
            return false;
        }
        const std::uint32_t type = png_be32(b, off + 4);
        if (type == kIhdr) {
            if (saw_ihdr || saw_idat || len != 13) {
                return false;
            }
            saw_ihdr = true;
        } else if (type == kIdat) {
            if (!saw_ihdr) {
                return false;
            }
            saw_idat = true;
        } else if (type == kIend) {
            if (!saw_ihdr || !saw_idat || len != 0) {
                return false;
            }
            off += 12;
            for (; off < b.size(); ++off) {
                if (b[off] != std::byte{0}) {
                    return false;
                }
            }
            return true;
        } else {
            return false;
        }
        off += 12 + static_cast<std::size_t>(len);
    }
    return false;
}

}  // namespace sxpe::resources
