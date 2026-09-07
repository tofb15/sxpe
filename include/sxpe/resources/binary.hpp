#pragma once

#include "sxpe/core/caps.hpp"
#include "sxpe/error.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources::bin {

inline std::uint32_t ru32(std::span<const std::byte> s, std::size_t o) {
    std::uint32_t v = 0;
    std::memcpy(&v, s.data() + o, 4);
    return v;
}

inline std::uint64_t ru64(std::span<const std::byte> s, std::size_t o) {
    std::uint64_t v = 0;
    std::memcpy(&v, s.data() + o, 8);
    return v;
}

inline std::uint16_t ru16(std::span<const std::byte> s, std::size_t o) {
    std::uint16_t v = 0;
    std::memcpy(&v, s.data() + o, 2);
    return v;
}

inline float rf32(std::span<const std::byte> s, std::size_t o) {
    float v = 0;
    std::memcpy(&v, s.data() + o, 4);
    return v;
}

inline bool take_u8(std::span<const std::byte> s, std::size_t& o, std::uint8_t& v) {
    if (o >= s.size()) {
        return false;
    }
    v = static_cast<std::uint8_t>(s[o]);
    ++o;
    return true;
}

inline bool take_u16(std::span<const std::byte> s, std::size_t& o, std::uint16_t& v) {
    if (o + 2 > s.size()) {
        return false;
    }
    v = ru16(s, o);
    o += 2;
    return true;
}

inline bool take_u32(std::span<const std::byte> s, std::size_t& o, std::uint32_t& v) {
    if (o + 4 > s.size()) {
        return false;
    }
    v = ru32(s, o);
    o += 4;
    return true;
}

inline bool take_u64(std::span<const std::byte> s, std::size_t& o, std::uint64_t& v) {
    if (o + 8 > s.size()) {
        return false;
    }
    v = ru64(s, o);
    o += 8;
    return true;
}

inline bool take_f32(std::span<const std::byte> s, std::size_t& o, float& v) {
    if (o + 4 > s.size()) {
        return false;
    }
    v = rf32(s, o);
    o += 4;
    return true;
}

/// .NET-style 7-bit encoded length (SimsWiki 7BITSTR / s3pi ReadString).
inline bool take_7bit_len(std::span<const std::byte> s, std::size_t& o, std::uint32_t& len) {
    len = 0;
    std::uint32_t shift = 0;
    for (int i = 0; i < 5; ++i) {
        std::uint8_t b = 0;
        if (!take_u8(s, o, b)) {
            return false;
        }
        len |= static_cast<std::uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) {
            return true;
        }
        shift += 7;
    }
    return false;
}

inline bool take_7bit_ascii(std::span<const std::byte> s, std::size_t& o, std::string& out) {
    std::uint32_t n = 0;
    if (!take_7bit_len(s, o, n) || n > sxpe::core::caps::kMaxNameBytes || o + n > s.size()) {
        return false;
    }
    out.assign(reinterpret_cast<const char*>(s.data() + o), n);
    o += n;
    return true;
}

/// CASP-style 7STRING Unicode BE: 7-bit char count, then UTF-16BE code units.
inline bool take_7bit_utf16be(std::span<const std::byte> s, std::size_t& o, std::string& out) {
    std::uint32_t n = 0;
    if (!take_7bit_len(s, o, n) || n > sxpe::core::caps::kMaxNameBytes / 2) {
        return false;
    }
    const std::size_t bytes = static_cast<std::size_t>(n) * 2;
    if (o + bytes > s.size()) {
        return false;
    }
    out.clear();
    out.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        const auto hi = static_cast<unsigned char>(s[o + 2 * i]);
        const auto lo = static_cast<unsigned char>(s[o + 2 * i + 1]);
        const char16_t c = static_cast<char16_t>((hi << 8) | lo);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    o += bytes;
    return true;
}

inline bool take_cstr(std::span<const std::byte> s, std::size_t& o, std::string& out,
                      std::size_t max_len = 512) {
    out.clear();
    while (o < s.size() && out.size() < max_len) {
        const char c = static_cast<char>(s[o]);
        ++o;
        if (c == '\0') {
            return true;
        }
        out.push_back(c);
    }
    return !out.empty() || (o > 0 && o <= s.size());
}

}  // namespace sxpe::resources::bin
