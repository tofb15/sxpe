#include "sxpe/resources/stbl.hpp"

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
void wu32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}
void wu64(std::vector<std::byte>& o, std::uint64_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 8);
}

std::string utf16le_to_utf8(std::span<const char16_t> in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        auto c = static_cast<std::uint32_t>(in[i]);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < in.size()) {
            const auto d = static_cast<std::uint32_t>(in[i + 1]);
            if (d >= 0xDC00 && d <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                ++i;
            }
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::u16string utf8_to_utf16(std::string_view in) {
    std::u16string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        const auto c = static_cast<unsigned char>(in[i]);
        std::uint32_t cp = 0;
        std::size_t n = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 0x6 && i + 1 < in.size()) {
            cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(in[i + 1]) & 0x3F);
            n = 2;
        } else if ((c >> 4) == 0xE && i + 2 < in.size()) {
            cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(in[i + 2]) & 0x3F);
            n = 3;
        } else if ((c >> 3) == 0x1E && i + 3 < in.size()) {
            cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(in[i + 1]) & 0x3F) << 12) |
                 ((static_cast<unsigned char>(in[i + 2]) & 0x3F) << 6) |
                 (static_cast<unsigned char>(in[i + 3]) & 0x3F);
            n = 4;
        } else {
            cp = 0xFFFD;
        }
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(static_cast<char16_t>(cp));
        }
    }
    return out;
}

}  // namespace

Result<Stbl> parse_stbl(std::span<const std::byte> bytes) {
    if (bytes.size() < 17) {
        return std::unexpected(err(ErrorCode::corrupt, "stbl too short"));
    }
    if (ru32(bytes, 0) != 0x4C425453) {
        return std::unexpected(err(ErrorCode::corrupt, "stbl magic"));
    }
    Stbl t;
    t.version = static_cast<std::uint8_t>(bytes[4]);
    const auto count = ru32(bytes, 7);
    const auto remain = bytes.size() - 17;
    const auto max_by_size = remain / 12;
    if (count > sxpe::core::caps::kMaxTableEntries || count > max_by_size) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "stbl count"));
    }
    std::size_t o = 17;
    t.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (o + 12 > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "stbl truncated"));
        }
        StblEntry e;
        e.id = ru64(bytes, o);
        const auto n = ru32(bytes, o + 8);
        o += 12;
        if (n > sxpe::core::caps::kMaxNameBytes ||
            static_cast<std::uint64_t>(n) * 2 > bytes.size() - o) {
            return std::unexpected(err(ErrorCode::cap_exceeded, "stbl text"));
        }
        std::vector<char16_t> u(n);
        std::memcpy(u.data(), bytes.data() + o, n * 2);
        e.text = utf16le_to_utf8(u);
        o += n * 2;
        t.entries.push_back(std::move(e));
    }
    return t;
}

Result<std::vector<std::byte>> write_stbl(const Stbl& t) {
    std::vector<std::byte> o;
    wu32(o, 0x4C425453);
    o.push_back(std::byte{t.version});
    o.push_back(std::byte{0});
    o.push_back(std::byte{0});
    wu32(o, static_cast<std::uint32_t>(t.entries.size()));
    o.insert(o.end(), 6, std::byte{0});
    for (const auto& e : t.entries) {
        const auto u = utf8_to_utf16(e.text);
        wu64(o, e.id);
        wu32(o, static_cast<std::uint32_t>(u.size()));
        const auto* p = reinterpret_cast<const std::byte*>(u.data());
        o.insert(o.end(), p, p + u.size() * 2);
    }
    return o;
}

}  // namespace sxpe::resources
