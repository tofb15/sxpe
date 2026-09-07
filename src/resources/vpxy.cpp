#include "sxpe/resources/vpxy.hpp"

#include <algorithm>
#include <cstring>

namespace sxpe::resources {
namespace {

std::uint32_t ru32(std::span<const std::byte> s, std::size_t o) {
    std::uint32_t v = 0;
    std::memcpy(&v, s.data() + o, 4);
    return v;
}

bool take_u32(std::span<const std::byte> s, std::size_t& o, std::uint32_t& v) {
    if (o + 4 > s.size()) {
        return false;
    }
    v = ru32(s, o);
    o += 4;
    return true;
}

bool take_u8(std::span<const std::byte> s, std::size_t& o, std::uint8_t& v) {
    if (o >= s.size()) {
        return false;
    }
    v = static_cast<std::uint8_t>(s[o]);
    ++o;
    return true;
}

bool take_f32(std::span<const std::byte> s, std::size_t& o, float& v) {
    if (o + 4 > s.size()) {
        return false;
    }
    std::memcpy(&v, s.data() + o, 4);
    o += 4;
    return true;
}

}  // namespace

Result<Vpxy> parse_vpxy(std::span<const std::byte> bytes) {
    if (bytes.size() < 16) {
        return std::unexpected(err(ErrorCode::corrupt, "vpxy too short"));
    }
    if (bytes[0] != std::byte{'V'} || bytes[1] != std::byte{'P'} || bytes[2] != std::byte{'X'} ||
        bytes[3] != std::byte{'Y'}) {
        return std::unexpected(err(ErrorCode::corrupt, "vpxy magic"));
    }
    Vpxy v;
    v.version = ru32(bytes, 4);
    const auto tgi_off = ru32(bytes, 8);
    const auto tgi_size = ru32(bytes, 12);
    std::size_t tgi_at = 16;
    if (tgi_off <= bytes.size() - 16) {
        tgi_at = 16 + tgi_off;
    } else if (tgi_off < bytes.size()) {
        tgi_at = tgi_off;
    }
    std::size_t p = 16;
    const std::size_t stop = std::min(tgi_at, bytes.size());
    std::uint8_t nent = 0;
    if (!take_u8(bytes, p, nent)) {
        return std::unexpected(err(ErrorCode::corrupt, "vpxy entries"));
    }
    v.entries.reserve(nent);
    for (std::uint8_t i = 0; i < nent && p < stop; ++i) {
        VpxyEntry e;
        if (!take_u8(bytes, p, e.type)) {
            return std::unexpected(err(ErrorCode::corrupt, "vpxy entry type"));
        }
        if (e.type == 0) {
            std::uint8_t ms = 0;
            std::uint8_t n = 0;
            if (!take_u8(bytes, p, ms) || !take_u8(bytes, p, n)) {
                return std::unexpected(err(ErrorCode::corrupt, "vpxy entry0"));
            }
            e.id = ms;
            e.indices.reserve(n);
            for (std::uint8_t k = 0; k < n; ++k) {
                std::uint32_t ix = 0;
                if (!take_u32(bytes, p, ix)) {
                    return std::unexpected(err(ErrorCode::corrupt, "vpxy index"));
                }
                e.indices.push_back(ix);
            }
        } else if (e.type == 1) {
            if (!take_u32(bytes, p, e.id)) {
                return std::unexpected(err(ErrorCode::corrupt, "vpxy entry1"));
            }
        } else {
            return std::unexpected(err(ErrorCode::corrupt, "vpxy unknown entry"));
        }
        v.entries.push_back(std::move(e));
    }
    std::uint8_t bbox_tag = 0;
    if (p < stop && take_u8(bytes, p, bbox_tag) && bbox_tag == 2) {
        v.has_bbox = true;
        for (float& f : v.bbox) {
            if (!take_f32(bytes, p, f)) {
                return std::unexpected(err(ErrorCode::corrupt, "vpxy bbox"));
            }
        }
        if (p + 4 <= bytes.size()) {
            p += 4;
        }
        std::uint8_t flag = 0;
        if (take_u8(bytes, p, flag) && flag == 1) {
            v.modular = true;
            take_u32(bytes, p, v.ftpt_index);
        }
    }
    if (tgi_at < bytes.size() && tgi_size > 0) {
        v.tgi_count = static_cast<std::uint8_t>(bytes[tgi_at]);
    }
    return v;
}

}  // namespace sxpe::resources
