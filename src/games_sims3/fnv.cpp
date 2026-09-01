#include "sxpe/games/sims3/fnv.hpp"

#include "sxpe/games/sims3/tgi.hpp"

#include <cctype>
#include <format>

namespace sxpe::games::sims3 {
namespace {

char lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

}  // namespace

std::uint32_t fnv1_32(std::string_view s, bool lowercase) {
    std::uint32_t h = 0x811C9DC5u;
    for (char c : s) {
        const auto b = static_cast<std::uint8_t>(lowercase ? lower_ascii(c) : c);
        h *= 0x01000193u;
        h ^= b;
    }
    return h;
}

std::uint64_t fnv1_64(std::string_view s, bool lowercase) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (char c : s) {
        const auto b = static_cast<std::uint8_t>(lowercase ? lower_ascii(c) : c);
        h *= 0x00000100000001B3ull;
        h ^= b;
    }
    return h;
}

std::uint64_t fnv64_clip(std::string_view clip_name) { return fnv1_64(clip_name, true); }

std::string community_filename(Tgi tgi, std::string_view name, std::string_view ext) {
    std::string safe;
    safe.reserve(name.size());
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-') {
            safe.push_back(c);
        }
    }
    if (safe.empty()) {
        safe = "resource";
    }
    std::string e(ext);
    if (!e.empty() && e.front() == '.') {
        e.erase(e.begin());
    }
    return std::format("S3_{:08X}_{:08X}_{:016X}_{}%%+{}", tgi.type, tgi.group, tgi.instance, safe,
                       e);
}

}  // namespace sxpe::games::sims3
