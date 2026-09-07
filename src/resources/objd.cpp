#include "sxpe/resources/objd.hpp"

#include "sxpe/resources/binary.hpp"

#include <algorithm>

namespace sxpe::resources {
namespace {

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

bool parse_common(std::span<const std::byte> bytes, std::size_t& p, Objd& o) {
    using namespace bin;
    if (!take_u32(bytes, p, o.common_version)) {
        return false;
    }
    if (!take_u64(bytes, p, o.name_guid) || !take_u64(bytes, p, o.desc_guid)) {
        return false;
    }
    if (!take_7bit_ascii(bytes, p, o.internal_name) || !take_7bit_ascii(bytes, p, o.internal_desc)) {
        return false;
    }
    float niceness = 0;
    float crap = 0;
    if (!take_f32(bytes, p, o.price) || !take_f32(bytes, p, niceness) || !take_f32(bytes, p, crap)) {
        return false;
    }
    std::uint8_t status = 0;
    if (!take_u8(bytes, p, status)) {
        return false;
    }
    if (!take_u64(bytes, p, o.thumb_iid)) {
        return false;
    }
    return true;
}

}  // namespace

Result<Objd> parse_objd(std::span<const std::byte> bytes) {
    using namespace bin;
    if (bytes.size() < 16) {
        return std::unexpected(err(ErrorCode::corrupt, "objd too short"));
    }
    Objd o;
    std::size_t p = 0;
    if (!take_u32(bytes, p, o.version)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd version"));
    }
    std::uint32_t tgi_off = 0;
    std::uint32_t tgi_size = 0;
    if (!take_u32(bytes, p, tgi_off) || !take_u32(bytes, p, tgi_size)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd tgi"));
    }
    std::size_t stop = bytes.size();
    if (tgi_off <= bytes.size() - 12) {
        stop = std::min(stop, 12 + static_cast<std::size_t>(tgi_off));
    } else if (tgi_off < bytes.size()) {
        stop = std::min(stop, static_cast<std::size_t>(tgi_off));
    }
    if (!skip_material_list(bytes, p, stop)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd materials"));
    }
    o.materials_skipped = true;
    if (o.version >= 0x16) {
        if (!take_7bit_ascii(bytes, p, o.instance_name)) {
            return std::unexpected(err(ErrorCode::corrupt, "objd instance name"));
        }
    }
    if (!parse_common(bytes, p, o)) {
        return std::unexpected(err(ErrorCode::corrupt, "objd common"));
    }
    return o;
}

}  // namespace sxpe::resources
