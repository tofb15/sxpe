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

}  // namespace sxpe::resources
