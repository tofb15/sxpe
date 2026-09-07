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

/// Duplicate instance ids kept after merge concat; Name column uses last-wins.
struct NmapDuplicate {
    std::uint64_t instance{0};
    std::uint32_t count{0};
    std::string effective_name;
};

Result<Nmap> parse_nmap(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> write_nmap(const Nmap& n);
/// Last matching row wins (same policy as resource.list Name column / name_index).
std::string lookup_name(const Nmap& n, std::uint64_t instance);
std::vector<NmapDuplicate> nmap_duplicates(const Nmap& n);

}  // namespace sxpe::resources
