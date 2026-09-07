#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources {

struct RcolChunk {
    std::uint32_t type{0};
    std::string tag;
    std::uint32_t size{0};
    std::uint32_t vertex_count{0};
    std::uint32_t face_count{0};
    std::uint32_t group_count{0};
    bool has_mesh_counts{false};
};

struct RcolSummary {
    std::uint32_t version{0};
    std::uint32_t internal_count{0};
    std::uint32_t external_count{0};
    std::uint32_t public_chunks{0};
    std::vector<RcolChunk> chunks;
    std::uint32_t total_vertices{0};
    std::uint32_t total_faces{0};
    std::uint32_t lod_groups{0};
    bool partial{false};
};

/// RCOL-style summary for MODL / MLOD / GEOM: chunk tags + mesh counts when known.
Result<RcolSummary> parse_rcol_summary(std::span<const std::byte> bytes);

}  // namespace sxpe::resources
