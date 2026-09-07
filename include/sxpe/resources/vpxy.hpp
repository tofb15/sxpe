#pragma once

#include "sxpe/error.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace sxpe::resources {

struct VpxyEntry {
    std::uint8_t type{0};
    std::uint32_t id{0};
    std::vector<std::uint32_t> indices;
};

struct Vpxy {
    std::uint32_t version{0};
    std::vector<VpxyEntry> entries;
    std::array<float, 6> bbox{};
    bool has_bbox{false};
    bool modular{false};
    std::uint32_t ftpt_index{0};
    std::uint32_t tgi_count{0};
};

/// Minimal VPXY (0x736884F1) reader: magic, version, entry types, bounding box.
Result<Vpxy> parse_vpxy(std::span<const std::byte> bytes);

}  // namespace sxpe::resources
