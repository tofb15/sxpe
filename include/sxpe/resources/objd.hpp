#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace sxpe::resources {

/// Public catalog Common fields used by OBJD Preview (SimsWiki Catalog Resource).
struct Objd {
    std::uint32_t version{0};
    std::uint32_t common_version{0};
    std::uint64_t name_guid{0};
    std::uint64_t desc_guid{0};
    std::string internal_name;
    std::string internal_desc;
    float price{0};
    std::uint64_t thumb_iid{0};
    std::string instance_name;
    bool materials_skipped{false};
    bool partial{false};
};

/// OBJD (0x319E4F1D) catalog header: Common name/desc GUIDs, price, thumb IID.
/// Skips Material List via each preset's size offset (community layout).
Result<Objd> parse_objd(std::span<const std::byte> bytes);

}  // namespace sxpe::resources
