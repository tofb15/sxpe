#include "sxpe/resources/objd.hpp"

#include "sxpe/resources/binary.hpp"

#include <algorithm>
#include <cstring>

namespace sxpe::resources {
namespace {

struct ObjdLayout {
    Objd value;
    std::uint32_t tgi_off{0};
    std::uint32_t tgi_size{0};
    std::size_t stop{0};
    std::size_t after_materials{0};
    std::size_t instance_name_at{0};  // npos if none
    std::size_t common_at{0};
    std::size_t name_guid_at{0};
    std::size_t desc_guid_at{0};
    std::size_t internal_name_at{0};
    std::size_t internal_desc_at{0};
    std::size_t price_at{0};
    /// Bytes after price through thumb (niceness, crap, status) — preserved as-is.
    std::size_t mid_common_at{0};
    std::size_t thumb_iid_at{0};
    std::size_t after_common{0};
    std::size_t tgi_at{0};
};

bool skip_material_list(std::span<const std::byte> bytes, std::size_t& p, std::size_t stop) {
    using namespace bin;
    std::uint32_t count = 0;
    if (!take_u32(bytes, p, count)) {
        return false;
    }
    constexpr std::uint32_t kMaxMaterials = 4096;
    if (count > kMaxMaterials) {
        return false;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (p >= stop) {
            return false;
        }
        std::uint8_t type = 0;
        if (!take_u8(bytes, p, type)) {
            return false;
        }
        if (type != 1) {
            std::uint32_t unused = 0;
            if (!take_u32(bytes, p, unused)) {
                return false;
            }
        }
        if (p + 4 > bytes.size()) {
            return false;
        }
        const std::size_t off_at = p;
        const auto offset = ru32(bytes, p);
        p += 4;
        if (offset < 4 || off_at + offset > bytes.size() || off_at + offset > stop) {
            return false;
        }
        p = off_at + offset;
        std::uint32_t id = 0;
        if (!take_u32(bytes, p, id)) {
            return false;
        }
    }
    return true;
}

Result<ObjdLayout> parse_objd_layout(std::span<const std::byte> bytes) {
    using namespace bin;
    if (bytes.size() < 16) {
        return std::unexpected(err(ErrorCode::corrupt, "objd too short"));
    }
    ObjdLayout L;
    Objd& o = L.value;
    std::size_t p = 0;
    if (!take_u32(bytes, p, o.version)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd version"));
    }
    if (!take_u32(bytes, p, L.tgi_off) || !take_u32(bytes, p, L.tgi_size)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd tgi"));
    }
    L.stop = bytes.size();
    if (L.tgi_off <= bytes.size() - 12) {
        L.stop = std::min(L.stop, 12 + static_cast<std::size_t>(L.tgi_off));
    } else if (L.tgi_off < bytes.size()) {
        L.stop = std::min(L.stop, static_cast<std::size_t>(L.tgi_off));
    }
    if (L.tgi_off <= bytes.size() - 12) {
        L.tgi_at = 12 + static_cast<std::size_t>(L.tgi_off);
    } else {
        L.tgi_at = std::min(bytes.size(), static_cast<std::size_t>(L.tgi_off));
    }
    if (!skip_material_list(bytes, p, L.stop)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd materials"));
    }
    o.materials_skipped = true;
    L.after_materials = p;
    L.instance_name_at = static_cast<std::size_t>(-1);
    if (o.version >= 0x16) {
        L.instance_name_at = p;
        if (!take_7bit_ascii(bytes, p, o.instance_name)) {
            return std::unexpected(err(ErrorCode::corrupt, "objd instance name"));
        }
    }
    L.common_at = p;
    if (!take_u32(bytes, p, o.common_version)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd common"));
    }
    L.name_guid_at = p;
    if (!take_u64(bytes, p, o.name_guid)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd common"));
    }
    L.desc_guid_at = p;
    if (!take_u64(bytes, p, o.desc_guid)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd common"));
    }
    L.internal_name_at = p;
    if (!take_7bit_ascii(bytes, p, o.internal_name)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd common"));
    }
    L.internal_desc_at = p;
    if (!take_7bit_ascii(bytes, p, o.internal_desc)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd common"));
    }
    L.price_at = p;
    if (!take_f32(bytes, p, o.price)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd common"));
    }
    L.mid_common_at = p;
    float niceness = 0;
    float crap = 0;
    std::uint8_t status = 0;
    if (!take_f32(bytes, p, niceness) || !take_f32(bytes, p, crap) || !take_u8(bytes, p, status)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd common"));
    }
    L.thumb_iid_at = p;
    if (!take_u64(bytes, p, o.thumb_iid)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd common"));
    }
    L.after_common = p;
    return L;
}

void patch_u64(std::vector<std::byte>& out, std::size_t at, std::uint64_t v) {
    std::memcpy(out.data() + at, &v, 8);
}
void patch_f32(std::vector<std::byte>& out, std::size_t at, float v) {
    std::memcpy(out.data() + at, &v, 4);
}
void patch_u32(std::vector<std::byte>& out, std::size_t at, std::uint32_t v) {
    std::memcpy(out.data() + at, &v, 4);
}

}  // namespace

Result<Objd> parse_objd(std::span<const std::byte> bytes) {
    auto L = parse_objd_layout(bytes);
    if (!L) {
        return std::unexpected(L.error());
    }
    return L->value;
}

Result<std::vector<std::byte>> apply_objd(std::span<const std::byte> bytes, const ObjdPatch& patch) {
    using namespace bin;
    auto Lr = parse_objd_layout(bytes);
    if (!Lr) {
        return std::unexpected(Lr.error());
    }
    const ObjdLayout& L = *Lr;
    Objd o = L.value;

    if (patch.name_guid) {
        o.name_guid = *patch.name_guid;
    }
    if (patch.desc_guid) {
        o.desc_guid = *patch.desc_guid;
    }
    if (patch.internal_name) {
        o.internal_name = *patch.internal_name;
    }
    if (patch.internal_desc) {
        o.internal_desc = *patch.internal_desc;
    }
    if (patch.price) {
        o.price = *patch.price;
    }
    if (patch.thumb_iid) {
        o.thumb_iid = *patch.thumb_iid;
    }
    if (patch.instance_name) {
        if (o.version < 0x16) {
            return std::unexpected(
                err(ErrorCode::invalid_argument, "objd instanceName requires version >= 0x16"));
        }
        o.instance_name = *patch.instance_name;
    }

    // Rebuild from after_materials through after_common; keep prefix + mid-trailing + TGI.
    std::vector<std::byte> mid;
    if (o.version >= 0x16) {
        put_7bit_ascii(mid, o.instance_name);
    }
    put_u32(mid, o.common_version);
    put_u64(mid, o.name_guid);
    put_u64(mid, o.desc_guid);
    put_7bit_ascii(mid, o.internal_name);
    put_7bit_ascii(mid, o.internal_desc);
    put_f32(mid, o.price);
    // Preserve niceness / crap / status bytes from original mid_common.
    mid.insert(mid.end(), bytes.begin() + static_cast<std::ptrdiff_t>(L.mid_common_at),
               bytes.begin() + static_cast<std::ptrdiff_t>(L.thumb_iid_at));
    put_u64(mid, o.thumb_iid);

    const std::ptrdiff_t old_mid_len =
        static_cast<std::ptrdiff_t>(L.after_common) - static_cast<std::ptrdiff_t>(L.after_materials);
    const std::ptrdiff_t delta = static_cast<std::ptrdiff_t>(mid.size()) - old_mid_len;

    std::vector<std::byte> out;
    out.reserve(bytes.size() + static_cast<std::size_t>(std::max<std::ptrdiff_t>(delta, 0)));
    out.insert(out.end(), bytes.begin(),
               bytes.begin() + static_cast<std::ptrdiff_t>(L.after_materials));
    out.insert(out.end(), mid.begin(), mid.end());
    // Unknown bytes between Common and TGI table (object-specific fields).
    if (L.after_common < L.tgi_at) {
        out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(L.after_common),
                   bytes.begin() + static_cast<std::ptrdiff_t>(L.tgi_at));
    }
    // TGI block and anything after.
    if (L.tgi_at <= bytes.size()) {
        out.insert(out.end(), bytes.begin() + static_cast<std::ptrdiff_t>(L.tgi_at), bytes.end());
    }

    if (delta != 0) {
        // tgi_off is relative to byte 12 (end of version+offset+size header).
        const auto new_off = static_cast<std::uint32_t>(static_cast<std::int64_t>(L.tgi_off) + delta);
        patch_u32(out, 4, new_off);
    }
    return out;
}

}  // namespace sxpe::resources
