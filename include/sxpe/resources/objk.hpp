#pragma once

#include "sxpe/error.hpp"
#include "sxpe/games/sims3/tgi.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources {

struct ObjkData {
    std::string key;
    std::uint8_t type{0};
    std::string text;
    std::uint32_t number{0};
};

struct Objk {
    std::uint32_t version{0};
    std::vector<std::uint32_t> components;
    std::vector<ObjkData> data;
    std::uint8_t visibility{0};
    std::uint32_t tgi_count{0};
    std::vector<sxpe::games::sims3::Tgi> tgis;
};

/// Minimal OBJK (0x02DC343F) reader: version, component IDs, data keys, visibility.
Result<Objk> parse_objk(std::span<const std::byte> bytes);

}  // namespace sxpe::resources
