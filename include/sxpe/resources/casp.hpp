#pragma once

#include "sxpe/error.hpp"
#include "sxpe/games/sims3/tgi.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources {

struct Casp {
    std::uint32_t version{0};
    std::string name;
    float sort_priority{0};
    std::uint32_t clothing_type{0};
    std::uint32_t type_flags{0};
    std::uint32_t age_gender{0};
    std::uint8_t age_flags{0};
    std::uint8_t species{0};
    std::uint8_t gender_flags{0};
    std::uint16_t handedness{0};
    std::uint32_t clothing_category{0};
    /// I64GT key table at header offset+8 (SimsWiki CASP). Empty if absent/unreadable.
    std::vector<sxpe::games::sims3::Tgi> tgis;
    bool partial{false};
};

/// Optional overrides for `casp.set`. Age/species/gender/handedness pack into age_gender
/// unless age_gender itself is provided. `tgis` replaces the entire I64GT key table.
struct CaspPatch {
    std::optional<std::string> name;
    std::optional<float> sort_priority;
    std::optional<std::uint32_t> clothing_type;
    std::optional<std::uint32_t> type_flags;
    std::optional<std::uint32_t> age_gender;
    std::optional<std::uint8_t> age_flags;
    std::optional<std::uint8_t> species;
    std::optional<std::uint8_t> gender_flags;
    std::optional<std::uint16_t> handedness;
    std::optional<std::uint32_t> clothing_category;
    std::optional<std::vector<sxpe::games::sims3::Tgi>> tgis;
};

/// CASP (0x034AEECB) best-effort: clothing type + age/gender/species flags + TGI refs.
Result<Casp> parse_casp(std::span<const std::byte> bytes);

/// Patch editable CASP fields; preserves presets and unknown mid/trailing bytes where possible.
/// See docs/spec/casp.md.
Result<std::vector<std::byte>> apply_casp(std::span<const std::byte> bytes, const CaspPatch& patch);

const char* casp_clothing_type_name(std::uint32_t id);
std::vector<std::string> casp_age_names(std::uint8_t age_flags);
std::vector<std::string> casp_gender_names(std::uint8_t gender_flags);
const char* casp_species_name(std::uint8_t species);

inline std::uint32_t pack_casp_age_gender(std::uint8_t age_flags, std::uint8_t species,
                                          std::uint8_t gender_flags, std::uint16_t handedness) {
    const auto sg = static_cast<std::uint8_t>((species & 0x0F) | ((gender_flags & 0x0F) << 4));
    return static_cast<std::uint32_t>(age_flags) | (static_cast<std::uint32_t>(sg) << 8) |
           (static_cast<std::uint32_t>(handedness) << 16);
}

}  // namespace sxpe::resources
