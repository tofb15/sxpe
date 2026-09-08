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
    bool cubemap{false};
    bool volume{false};
    std::uint32_t mipmap_count{1};
    std::uint32_t bit_count{0};
    std::uint32_t r_mask{0};
    std::uint32_t g_mask{0};
    std::uint32_t b_mask{0};
    std::uint32_t a_mask{0};
    /// True when decode_dds_rgba can produce pixels (2D, supported FourCC/RGB layout).
    bool decode_supported{false};
};

Result<DdsInfo> parse_dds(std::span<const std::byte> bytes);
/// RGBA8 pixels, row-major. Caps large images. Refuses cubemaps/volumes with a clear error.
Result<std::vector<std::byte>> decode_dds_rgba(std::span<const std::byte> bytes);
/// Encode uncompressed A8R8G8B8 (BGRA bytes in memory).
Result<std::vector<std::byte>> encode_dds_bgra(std::uint32_t width, std::uint32_t height,
                                               std::span<const std::byte> bgra);
/// Validate a DDS payload for Replace DDS (2D, within caps, decode-supported). Clear errors.
Result<DdsInfo> validate_dds_replace(std::span<const std::byte> bytes);

}  // namespace sxpe::resources
