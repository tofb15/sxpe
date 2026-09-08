#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources {

/// Public catalog Common fields used by OBJD Preview / editors (SimsWiki Catalog Resource).
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

/// Optional field overrides for `objd.set`. Omitted keys leave on-disk values unchanged.
struct ObjdPatch {
    std::optional<std::uint64_t> name_guid;
    std::optional<std::uint64_t> desc_guid;
    std::optional<std::string> internal_name;
    std::optional<std::string> internal_desc;
    std::optional<float> price;
    std::optional<std::uint64_t> thumb_iid;
    std::optional<std::string> instance_name;
};

/// OBJD (0x319E4F1D) catalog header: Common name/desc GUIDs, price, thumb IID.
/// Skips Material List via each preset's size offset (community layout).
Result<Objd> parse_objd(std::span<const std::byte> bytes);

/// Patch editable Common (+ optional InstanceName) fields.
/// Preserves materials, unknown bytes after Common, and the TGI block; rewrites
/// `tgi_off` when variable-length strings change size. See docs/spec/objd.md.
Result<std::vector<std::byte>> apply_objd(std::span<const std::byte> bytes, const ObjdPatch& patch);

}  // namespace sxpe::resources
