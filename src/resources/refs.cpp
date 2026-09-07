#include "sxpe/resources/refs.hpp"
#include <vector>

#include "sxpe/core/caps.hpp"

#include <cstring>

namespace sxpe::resources {
namespace {

std::uint16_t ru16(std::span<const std::byte> s, std::size_t o) {
    std::uint16_t v = 0;
    std::memcpy(&v, s.data() + o, 2);
    return v;
}

std::uint32_t ru32(std::span<const std::byte> s, std::size_t o) {
    std::uint32_t v = 0;
    std::memcpy(&v, s.data() + o, 4);
    return v;
}

std::uint64_t ru64(std::span<const std::byte> s, std::size_t o) {
    std::uint64_t v = 0;
    std::memcpy(&v, s.data() + o, 8);
    return v;
}

bool take_u16(std::span<const std::byte> s, std::size_t& o, std::uint16_t& v) {
    if (o + 2 > s.size()) {
        return false;
    }
    v = ru16(s, o);
    o += 2;
    return true;
}

bool take_u32(std::span<const std::byte> s, std::size_t& o, std::uint32_t& v) {
    if (o + 4 > s.size()) {
        return false;
    }
    v = ru32(s, o);
    o += 4;
    return true;
}

bool try_parse_body(std::span<const std::byte> bytes, std::size_t p, bool aux_dword, Refs& r) {
    std::uint32_t count1 = 0;
    if (!take_u32(bytes, p, count1) || count1 > sxpe::core::caps::kMaxTableEntries) {
        return false;
    }
    const std::size_t aux_sz = aux_dword ? 4u : 2u;
    const std::size_t need = static_cast<std::size_t>(count1) * (16 + aux_sz) + 4;
    if (p + need > bytes.size()) {
        return false;
    }
    r.entries.clear();
    r.entries.reserve(count1);
    r.aux_is_dword = aux_dword;
    for (std::uint32_t i = 0; i < count1; ++i) {
        RefsEntry e;
        e.tgi.type = ru32(bytes, p);
        e.tgi.group = ru32(bytes, p + 4);
        e.tgi.instance = ru64(bytes, p + 8);
        p += 16;
        if (aux_dword) {
            e.aux = ru32(bytes, p);
            p += 4;
        } else {
            e.aux = ru16(bytes, p);
            p += 2;
        }
        r.entries.push_back(e);
    }
    std::uint32_t count2 = 0;
    if (!take_u32(bytes, p, count2) || count2 > sxpe::core::caps::kMaxTableEntries) {
        return false;
    }
    if (p + static_cast<std::size_t>(count2) * 2 > bytes.size()) {
        return false;
    }
    r.indices.clear();
    r.indices.reserve(count2);
    for (std::uint32_t i = 0; i < count2; ++i) {
        std::uint16_t ix = 0;
        if (!take_u16(bytes, p, ix)) {
            return false;
        }
        r.indices.push_back(ix);
    }
    return true;
}

}  // namespace

Result<Refs> parse_refs(std::span<const std::byte> bytes) {
    if (bytes.size() < 6) {
        return std::unexpected(err(ErrorCode::corrupt, "refs too short"));
    }
    if (bytes.size() > sxpe::core::caps::kMaxResourceBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "refs size"));
    }
    Refs r;
    r.version = ru16(bytes, 0);

    // Prefer: version [+ thingy if v>=3] + WORD-aux body; fall back to DWORD-aux.
    struct Cand {
        std::size_t p;
        bool thingy;
        std::uint8_t thingy_val;
    };
    std::vector<Cand> cands;
    cands.push_back({2, false, 0});
    if (r.version >= 3 && bytes.size() > 3) {
        cands.insert(cands.begin(),
                     Cand{3, true, static_cast<std::uint8_t>(bytes[2])});
    }

    for (const auto& c : cands) {
        for (bool dword_aux : {false, true}) {
            Refs trial = r;
            if (try_parse_body(bytes, c.p, dword_aux, trial)) {
                trial.has_thingy = c.thingy;
                trial.thingy = c.thingy_val;
                trial.partial = false;
                return trial;
            }
        }
    }

    // Best-effort partial: version + count1 + as many TGI+WORD as fit.
    std::size_t p = 2;
    std::uint32_t count1 = 0;
    if (!take_u32(bytes, p, count1)) {
        return std::unexpected(err(ErrorCode::corrupt, "refs count1"));
    }
    if (count1 > sxpe::core::caps::kMaxTableEntries) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "refs count1"));
    }
    r.partial = true;
    r.entries.reserve(std::min<std::uint32_t>(count1, 1024));
    for (std::uint32_t i = 0; i < count1 && p + 18 <= bytes.size(); ++i) {
        RefsEntry e;
        e.tgi.type = ru32(bytes, p);
        e.tgi.group = ru32(bytes, p + 4);
        e.tgi.instance = ru64(bytes, p + 8);
        e.aux = ru16(bytes, p + 16);
        p += 18;
        r.entries.push_back(e);
    }
    return r;
}

}  // namespace sxpe::resources
