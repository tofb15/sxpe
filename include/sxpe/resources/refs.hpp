#pragma once

#include "sxpe/error.hpp"
#include "sxpe/games/sims3/tgi.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sxpe::resources {

struct RefsEntry {
    sxpe::games::sims3::Tgi tgi{};
    /// Extra field after each TGI (WORD or DWORD depending on version/thingy).
    std::uint32_t aux{0};
};

struct Refs {
    std::uint16_t version{0};
    std::uint8_t thingy{0};
    bool has_thingy{false};
    bool aux_is_dword{false};
    std::vector<RefsEntry> entries;
    std::vector<std::uint16_t> indices;
    bool partial{false};
};

/// Optional overrides for `refs.set`. Omitted keys leave on-disk values unchanged.
struct RefsPatch {
    std::optional<std::vector<RefsEntry>> entries;
    std::optional<std::vector<std::uint16_t>> indices;
};

/// REFS (0x05ED1226) best-effort: version, TGI list + aux, trailing WORD indices.
Result<Refs> parse_refs(std::span<const std::byte> bytes);

/// Serialize a fully-parsed REFS (not partial) to on-disk bytes.
Result<std::vector<std::byte>> serialize_refs(const Refs& r);

/// Replace entries and/or indices; preserves version / thingy / aux width.
/// Refuses partial parses. See docs/spec/refs.md.
Result<std::vector<std::byte>> apply_refs(std::span<const std::byte> bytes, const RefsPatch& patch);

}  // namespace sxpe::resources
