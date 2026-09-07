#include "sxpe/resources/objk.hpp"

#include "sxpe/core/caps.hpp"

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

bool take_str(std::span<const std::byte> s, std::size_t& o, std::uint32_t n, std::string& out) {
    if (n > sxpe::core::caps::kMaxNameBytes || o + n > s.size()) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(s.data() + o), n);
    o += n;
    return true;
}

}  // namespace

Result<Objk> parse_objk(std::span<const std::byte> bytes) {
    if (bytes.size() < 12) {
        return std::unexpected(err(ErrorCode::corrupt, "objk too short"));
    }
    Objk o;
    o.version = ru32(bytes, 0);
    const auto tgi_off = ru32(bytes, 4);
    const auto tgi_size = ru32(bytes, 8);
    std::size_t tgi_at = 12;
    if (tgi_off <= bytes.size() - 12) {
        tgi_at = 12 + tgi_off;
    } else if (tgi_off < bytes.size()) {
        tgi_at = tgi_off;
    }
    std::size_t p = 12;
    const std::size_t stop = std::min(tgi_at, bytes.size());
    std::uint8_t ncomp = 0;
    if (!take_u8(bytes, p, ncomp) || p > stop) {
        return std::unexpected(err(ErrorCode::corrupt, "objk components"));
    }
    o.components.reserve(ncomp);
    for (std::uint8_t i = 0; i < ncomp; ++i) {
        std::uint32_t id = 0;
        if (!take_u32(bytes, p, id) || p > stop) {
            return std::unexpected(err(ErrorCode::corrupt, "objk component id"));
        }
        o.components.push_back(id);
    }
    std::uint8_t ndata = 0;
    if (!take_u8(bytes, p, ndata) || p > stop) {
        return std::unexpected(err(ErrorCode::corrupt, "objk data count"));
    }
    o.data.reserve(ndata);
    for (std::uint8_t i = 0; i < ndata; ++i) {
        ObjkData d;
        std::uint32_t klen = 0;
        if (!take_u32(bytes, p, klen) || !take_str(bytes, p, klen, d.key)) {
            return std::unexpected(err(ErrorCode::corrupt, "objk data key"));
        }
        if (!take_u8(bytes, p, d.type)) {
            return std::unexpected(err(ErrorCode::corrupt, "objk data type"));
        }
        if (d.type == 0 || d.type == 3) {
            std::uint32_t slen = 0;
            if (!take_u32(bytes, p, slen) || !take_str(bytes, p, slen, d.text)) {
                return std::unexpected(err(ErrorCode::corrupt, "objk data string"));
            }
        } else if (d.type == 4) {
            if (!take_u32(bytes, p, d.number)) {
                return std::unexpected(err(ErrorCode::corrupt, "objk data u32"));
            }
        } else {
            if (!take_u32(bytes, p, d.number)) {
                return std::unexpected(err(ErrorCode::corrupt, "objk data index"));
            }
        }
        o.data.push_back(std::move(d));
    }
    if (p < stop) {
        o.visibility = static_cast<std::uint8_t>(bytes[p]);
    }
    if (tgi_at < bytes.size() && tgi_size > 0) {
        o.tgi_count = static_cast<std::uint8_t>(bytes[tgi_at]);
    }
    return o;
}

}  // namespace sxpe::resources
