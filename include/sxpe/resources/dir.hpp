#pragma once

#include "sxpe/error.hpp"
#include "sxpe/games/sims3/tgi.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sxpe::resources {

struct DirEntry {
    sxpe::games::sims3::Tgi tgi{};
    std::uint32_t mem_size{0};
};

/// TS3 DIR `0xE86B1EEF`: 20-byte records (type, group, instance_hi, instance_lo, mem_size).
/// 16-byte legacy (32-bit instance) is accepted on read.
Result<std::vector<DirEntry>> parse_dir(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> write_dir(std::span<const DirEntry> entries);

}  // namespace sxpe::resources
