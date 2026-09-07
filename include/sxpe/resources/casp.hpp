#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
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
    bool partial{false};
};

/// CASP (0x034AEECB) best-effort: clothing type + age/gender/species flags.
Result<Casp> parse_casp(std::span<const std::byte> bytes);

const char* casp_clothing_type_name(std::uint32_t id);
std::vector<std::string> casp_age_names(std::uint8_t age_flags);
std::vector<std::string> casp_gender_names(std::uint8_t gender_flags);
const char* casp_species_name(std::uint8_t species);

}  // namespace sxpe::resources
