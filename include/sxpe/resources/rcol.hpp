#pragma once

#include "sxpe/error.hpp"
#include "sxpe/games/sims3/tgi.hpp"
#include "sxpe/resources/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sxpe::resources {

struct RcolTextureRef {
    std::uint32_t param_hash{0};
    std::string param_name;  // when known (DiffuseMap, …)
    sxpe::games::sims3::Tgi tgi{};
    bool resolved{false};
    /// Raw RCOL reference dword when the param used an index (type-code 4 / size 4).
    std::uint32_t rcol_ref{0};
};

struct RcolChunk {
    std::uint32_t type{0};
    std::string tag;
    std::uint32_t size{0};
    std::uint32_t vertex_count{0};
    std::uint32_t face_count{0};
    std::uint32_t group_count{0};
    bool has_mesh_counts{false};
    /// MATD fields when parseable.
    bool has_matd{false};
    std::uint32_t matd_version{0};
    std::uint32_t material_name_hash{0};
    std::uint32_t shader_hash{0};
    std::string shader_name;  // when known from community shader list
    std::vector<RcolTextureRef> textures;
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
    /// External TGI table (often texture keys: _IMG / TXTC / ANIM).
    std::vector<sxpe::games::sims3::Tgi> external_tgis;
    /// Internal chunk TGIs (same order as chunks[]).
    std::vector<sxpe::games::sims3::Tgi> internal_tgis;
    /// Flattened MATD texture refs across chunks (best-effort).
    std::vector<RcolTextureRef> textures;
};

/// RCOL-style summary for MODL / MLOD / GEOM / MATD: chunk tags, mesh counts,
/// MATD shader name + texture TGIs when parseable. Not a mesh viewer.
Result<RcolSummary> parse_rcol_summary(std::span<const std::byte> bytes);

/// Replace one internal chunk payload by 0-based index. Rebuilds the location
/// table; preserves version, public count, and TGI tables. Bare GEOM (no RCOL
/// wrapper) only accepts index 0 (whole body).
Result<std::vector<std::byte>> extract_rcol_chunk(std::span<const std::byte> bytes,
                                                  std::uint32_t chunk_index);

Result<std::vector<std::byte>> replace_rcol_chunk(std::span<const std::byte> bytes,
                                                  std::uint32_t chunk_index,
                                                  std::span<const std::byte> new_payload);

/// Known community shader name for an FNV-1 32 hash, or empty.
std::string_view rcol_shader_name(std::uint32_t hash);

/// Known MTNF/MTRL texture param name for an FNV-1 32 hash, or empty.
std::string_view rcol_param_name(std::uint32_t hash);

}  // namespace sxpe::resources
