#include "sxpe/resources/key_table.hpp"

#include "sxpe/core/caps.hpp"

#include <cstring>

namespace sxpe::resources {
namespace {

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

}  // namespace

Result<std::vector<sxpe::games::sims3::Tgi>> parse_key_table_tgis(std::span<const std::byte> bytes,
                                                                  bool count_is_dword) {
    if (bytes.empty()) {
        return std::vector<sxpe::games::sims3::Tgi>{};
    }
    std::size_t p = 0;
    std::uint32_t count = 0;
    if (count_is_dword) {
        if (p + 4 > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "key table count"));
        }
        count = ru32(bytes, p);
        p += 4;
    } else {
        count = static_cast<std::uint8_t>(bytes[p]);
        ++p;
    }
    if (count > sxpe::core::caps::kMaxTableEntries) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "key table count"));
    }
    const std::size_t need = static_cast<std::size_t>(count) * 16;
    if (p + need > bytes.size()) {
        return std::unexpected(err(ErrorCode::corrupt, "key table truncated"));
    }
    std::vector<sxpe::games::sims3::Tgi> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        sxpe::games::sims3::Tgi t;
        t.type = ru32(bytes, p);
        t.group = ru32(bytes, p + 4);
        t.instance = ru64(bytes, p + 8);
        p += 16;
        out.push_back(t);
    }
    return out;
}

}  // namespace sxpe::resources
