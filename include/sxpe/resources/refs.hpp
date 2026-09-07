#pragma once

#include "sxpe/error.hpp"
#include "sxpe/games/sims3/tgi.hpp"

#include <cstdint>
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

/// REFS (0x05ED1226) best-effort: version, TGI list + aux, trailing WORD indices.
Result<Refs> parse_refs(std::span<const std::byte> bytes);

}  // namespace sxpe::resources
