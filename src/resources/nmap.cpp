#include "sxpe/resources/nmap.hpp"

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
void wu32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}
void wu64(std::vector<std::byte>& o, std::uint64_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 8);
}

}  // namespace

Result<Nmap> parse_nmap(std::span<const std::byte> bytes) {
    if (bytes.size() < 8) {
        return std::unexpected(err(ErrorCode::corrupt, "nmap too short"));
    }
    Nmap n;
    n.version = ru32(bytes, 0);
    const auto count = ru32(bytes, 4);
    std::size_t o = 8;
    n.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (o + 12 > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "nmap truncated"));
        }
        NmapEntry e;
        e.instance = ru64(bytes, o);
        const auto len = ru32(bytes, o + 8);
        o += 12;
        if (o + len > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "nmap name"));
        }
        e.name.assign(reinterpret_cast<const char*>(bytes.data() + o), len);
        o += len;
        n.entries.push_back(std::move(e));
    }
    return n;
}

Result<std::vector<std::byte>> write_nmap(const Nmap& n) {
    std::vector<std::byte> o;
    wu32(o, n.version);
    wu32(o, static_cast<std::uint32_t>(n.entries.size()));
    for (const auto& e : n.entries) {
        wu64(o, e.instance);
        wu32(o, static_cast<std::uint32_t>(e.name.size()));
        const auto* p = reinterpret_cast<const std::byte*>(e.name.data());
        o.insert(o.end(), p, p + e.name.size());
    }
    return o;
}

std::string lookup_name(const Nmap& n, std::uint64_t instance) {
    for (const auto& e : n.entries) {
        if (e.instance == instance) {
            return e.name;
        }
    }
    return {};
}

}  // namespace sxpe::resources
