#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources {

struct ClipInfo {
    std::uint32_t version{0};
    float frame_duration{0};
    std::uint16_t frame_count{0};
    float duration_seconds{0};
    std::string anim_name;
    std::string source_file;
    std::string actor_name;
    std::uint32_t track_count{0};
    std::vector<std::uint32_t> track_hashes;
    bool partial{false};
};

/// CLIP (0x6B20C4F3) summary: duration + names/hashes (no playback).
Result<ClipInfo> parse_clip(std::span<const std::byte> bytes);

}  // namespace sxpe::resources
