#include "sxpe/resources/dir.hpp"

#include "sxpe/core/caps.hpp"

#include <cstring>

namespace sxpe::resources {
namespace {

std::uint32_t ru32(std::span<const std::byte> s, std::size_t o) {
    std::uint32_t v = 0;
    std::memcpy(&v, s.data() + o, 4);
    return v;
}
void wu32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}

}  // namespace

Result<std::vector<DirEntry>> parse_dir(std::span<const std::byte> bytes) {
    if (bytes.size() > sxpe::core::caps::kMaxResourceBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "dir size"));
    }
    std::size_t rec = 0;
    if (bytes.size() % 20 == 0) {
        rec = 20;
    } else if (bytes.size() % 16 == 0) {
        rec = 16;
    } else {
        return std::unexpected(err(ErrorCode::corrupt, "dir record size"));
    }
    const auto n = bytes.size() / rec;
    if (n > sxpe::core::caps::kMaxTableEntries) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "dir count"));
    }
    std::vector<DirEntry> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const auto o = i * rec;
        DirEntry e;
        e.tgi.type = ru32(bytes, o);
        e.tgi.group = ru32(bytes, o + 4);
        if (rec == 20) {
            e.tgi.instance = (static_cast<std::uint64_t>(ru32(bytes, o + 8)) << 32) | ru32(bytes, o + 12);
            e.mem_size = ru32(bytes, o + 16);
        } else {
            e.tgi.instance = ru32(bytes, o + 8);
            e.mem_size = ru32(bytes, o + 12);
        }
        out.push_back(e);
    }
    return out;
}

Result<std::vector<std::byte>> write_dir(std::span<const DirEntry> entries) {
    if (entries.size() > sxpe::core::caps::kMaxTableEntries) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "dir count"));
    }
    std::vector<std::byte> o;
    o.reserve(entries.size() * 20);
    for (const auto& e : entries) {
        wu32(o, e.tgi.type);
        wu32(o, e.tgi.group);
        wu32(o, static_cast<std::uint32_t>(e.tgi.instance >> 32));
        wu32(o, static_cast<std::uint32_t>(e.tgi.instance));
        wu32(o, e.mem_size);
    }
    return o;
}

}  // namespace sxpe::resources
