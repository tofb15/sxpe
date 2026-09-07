#include "sxpe/games/sims3/fnv.hpp"

#include "sxpe/games/sims3/tgi.hpp"

#include <cctype>
#include <format>
#include <string>

namespace sxpe::games::sims3 {
namespace {

char lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::uint8_t clip_age_mask(char c) {
    switch (c) {
        case 'b':
            return 0x01;
        case 'p':
            return 0x02;
        case 'c':
            return 0x03;
        case 't':
            return 0x04;
        case 'h':
            return 0x05;
        case 'e':
            return 0x06;
        default:
            return 0;
    }
}

bool clip_default_age(char c) { return c == 'a' || c == 'o'; }

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

std::uint64_t fnv64_clip(std::string_view clip_name) {
    std::string s;
    s.reserve(clip_name.size());
    for (char c : clip_name) {
        s.push_back(lower_ascii(c));
    }
    const auto apply = [](std::string hash_name, bool age, char primary, char target) {
        auto h = fnv1_64(hash_name, false);
        if (!age) {
            return h & ~(1ull << 63);
        }
        h |= 1ull << 63;
        auto hi = static_cast<std::uint8_t>(h >> 56);
        auto mid = static_cast<std::uint8_t>(h >> 48);
        hi = static_cast<std::uint8_t>(hi ^ clip_age_mask(primary));
        mid = static_cast<std::uint8_t>(mid ^ clip_age_mask(target));
        return (h & 0x0000FFFFFFFFFFFFull) | (static_cast<std::uint64_t>(hi) << 56) |
               (static_cast<std::uint64_t>(mid) << 48);
    };
    if (s.size() >= 4 && s[1] == '2' && s[3] == '_' &&
        std::isalpha(static_cast<unsigned char>(s[0])) &&
        std::isalpha(static_cast<unsigned char>(s[2]))) {
        const char x = s[0];
        const char y = s[2];
        const bool xdef = clip_default_age(x);
        const bool ydef = clip_default_age(y);
        std::string hashed = s;
        if (!xdef) {
            hashed[0] = 'a';
        }
        if (!ydef) {
            hashed[2] = 'a';
        }
        return apply(std::move(hashed), !(xdef && ydef), xdef ? '\0' : x, ydef ? '\0' : y);
    }
    if (s.size() >= 2 && s[1] == '_' && std::isalpha(static_cast<unsigned char>(s[0]))) {
        const char x = s[0];
        const bool xdef = clip_default_age(x);
        std::string hashed = s;
        if (!xdef) {
            hashed[0] = 'a';
        }
        return apply(std::move(hashed), !xdef, xdef ? '\0' : x, '\0');
    }
    return fnv1_64(s, false);
}

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
