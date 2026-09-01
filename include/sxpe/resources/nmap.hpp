#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources {

struct NmapEntry {
    std::uint64_t instance{0};
    std::string name;
};

struct Nmap {
    std::uint32_t version{1};
    std::vector<NmapEntry> entries;
};

Result<Nmap> parse_nmap(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> write_nmap(const Nmap& n);
std::string lookup_name(const Nmap& n, std::uint64_t instance);

}  // namespace sxpe::resources
