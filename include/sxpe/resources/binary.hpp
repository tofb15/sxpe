#pragma once

#include "sxpe/core/caps.hpp"
#include "sxpe/error.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
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


inline void put_u8(std::vector<std::byte>& o, std::uint8_t v) { o.push_back(std::byte{v}); }

inline void put_u32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}

inline void put_u64(std::vector<std::byte>& o, std::uint64_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 8);
}

inline void put_f32(std::vector<std::byte>& o, float v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}

/// .NET-style 7-bit encoded length (matches take_7bit_len).
inline void put_7bit_len(std::vector<std::byte>& o, std::uint32_t len) {
    do {
        std::uint8_t b = static_cast<std::uint8_t>(len & 0x7F);
        len >>= 7;
        if (len != 0) {
            b |= 0x80;
        }
        put_u8(o, b);
    } while (len != 0);
}

inline void put_7bit_ascii(std::vector<std::byte>& o, std::string_view s) {
    put_7bit_len(o, static_cast<std::uint32_t>(s.size()));
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    o.insert(o.end(), p, p + s.size());
}

/// Encode UTF-8 (BMP) as CASP 7STRING Unicode BE.
inline bool put_7bit_utf16be(std::vector<std::byte>& o, std::string_view utf8) {
    std::u16string units;
    units.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        std::uint32_t cp = 0;
        std::size_t n = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i + 1 < utf8.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            n = 2;
        } else if ((c >> 4) == 0xE && i + 2 < utf8.size()) {
            cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(utf8[i + 2]) & 0x3F);
            n = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < utf8.size()) {
            // Supplementary planes need surrogates; refuse for editor simplicity.
            return false;
        } else {
            return false;
        }
        i += n;
        if (cp > 0xFFFF) {
            return false;
        }
        units.push_back(static_cast<char16_t>(cp));
    }
    if (units.size() > sxpe::core::caps::kMaxNameBytes / 2) {
        return false;
    }
    put_7bit_len(o, static_cast<std::uint32_t>(units.size()));
    for (char16_t u : units) {
        put_u8(o, static_cast<std::uint8_t>((u >> 8) & 0xFF));
        put_u8(o, static_cast<std::uint8_t>(u & 0xFF));
    }
    return true;
}

}  // namespace sxpe::resources::bin
