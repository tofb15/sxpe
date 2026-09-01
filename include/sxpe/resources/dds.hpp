#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources {

struct DdsInfo {
    std::uint32_t width{0};
    std::uint32_t height{0};
    std::string format;
    std::uint32_t pitch_or_linear{0};
    bool compressed{false};
};

Result<DdsInfo> parse_dds(std::span<const std::byte> bytes);
/// RGBA8 pixels, row-major. Caps large images.
Result<std::vector<std::byte>> decode_dds_rgba(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> encode_dds_bgra(std::uint32_t width, std::uint32_t height,
                                               std::span<const std::byte> bgra);

}  // namespace sxpe::resources
