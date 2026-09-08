#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
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

/// Optional field overrides for `clip.set`. Omitted keys leave on-disk values unchanged.
/// Safe fields only — see docs/spec/clip.md (no frame data / playback).
struct ClipPatch {
    std::optional<std::string> anim_name;
    std::optional<std::string> source_file;
    std::optional<std::string> actor_name;
    /// Partial track-hash updates: (0-based index, new FNV32/bone hash).
    std::vector<std::pair<std::uint32_t, std::uint32_t>> track_hashes;
};

/// CLIP (0x6B20C4F3) summary: duration + names/hashes (no playback).
Result<ClipInfo> parse_clip(std::span<const std::byte> bytes);

/// Patch safe metadata strings + optional track hashes.
/// Preserves frame data, events, slot tables; may append strings and adjust offsets.
Result<std::vector<std::byte>> apply_clip(std::span<const std::byte> bytes, const ClipPatch& patch);

}  // namespace sxpe::resources
