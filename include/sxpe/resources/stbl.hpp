#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources {

struct StblEntry {
    std::uint64_t id{0};
    std::string text;
};

struct Stbl {
    std::uint8_t version{2};
    std::vector<StblEntry> entries;
};

Result<Stbl> parse_stbl(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> write_stbl(const Stbl& t);

}  // namespace sxpe::resources
