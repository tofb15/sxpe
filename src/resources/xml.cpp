#include "sxpe/resources/xml.hpp"

#include <algorithm>
#include <cstring>

namespace sxpe::resources {
namespace {

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
            n = 1;
        }
        i += n;
        if (cp < 0x10000) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

std::string_view strip_utf8_bom(std::string_view s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF) {
        return s.substr(3);
    }
    return s;
}

bool text_looks_xml(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) {
        ++i;
    }
    return i < s.size() && s[i] == '<';
}

}  // namespace

bool looks_like_xml(std::span<const std::byte> bytes) {
    if (bytes.empty()) {
        return false;
    }
    auto decoded = decode_xml_text(bytes);
    if (!decoded) {
        if (bytes.size() >= 1) {
            const auto* p = reinterpret_cast<const char*>(bytes.data());
            return text_looks_xml(std::string_view(p, std::min<std::size_t>(bytes.size(), 64)));
        }
        return false;
    }
    return text_looks_xml(*decoded);
}

XmlEncoding sniff_xml_encoding(std::span<const std::byte> bytes) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const auto n = bytes.size();
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) {
        return XmlEncoding::Utf16Le;
    }
    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) {
        return XmlEncoding::Utf16Be;
    }
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
        return XmlEncoding::Utf8Bom;
    }
    if (n >= 8) {
        std::size_t zeros = 0;
        for (std::size_t i = 1; i < std::min(n, std::size_t{64}); i += 2) {
            if (p[i] == 0) {
                ++zeros;
            }
        }
        if (zeros >= 8 && p[0] == '<' && p[1] == 0) {
            return XmlEncoding::Utf16Le;
        }
    }
    return XmlEncoding::Utf8;
}

Result<std::string> decode_xml_text(std::span<const std::byte> bytes) {
    const auto enc = sniff_xml_encoding(bytes);
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const auto n = bytes.size();
    switch (enc) {
        case XmlEncoding::Utf16Le: {
            std::size_t off = (n >= 2 && p[0] == 0xFF && p[1] == 0xFE) ? 2 : 0;
            if ((n - off) % 2 != 0) {
                return std::unexpected(err(ErrorCode::corrupt, "UTF-16LE XML length odd"));
            }
            const auto* u = reinterpret_cast<const char16_t*>(p + off);
            return utf16le_to_utf8(std::span<const char16_t>(u, (n - off) / 2));
        }
        case XmlEncoding::Utf16Be: {
            std::size_t off = (n >= 2 && p[0] == 0xFE && p[1] == 0xFF) ? 2 : 0;
            if ((n - off) % 2 != 0) {
                return std::unexpected(err(ErrorCode::corrupt, "UTF-16BE XML length odd"));
            }
            std::u16string tmp((n - off) / 2, 0);
            for (std::size_t i = 0; i < tmp.size(); ++i) {
                tmp[i] = static_cast<char16_t>((p[off + 2 * i] << 8) | p[off + 2 * i + 1]);
            }
            return utf16le_to_utf8(std::span<const char16_t>(tmp.data(), tmp.size()));
        }
        case XmlEncoding::Utf8Bom:
        case XmlEncoding::Utf8:
        default: {
            std::string_view s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            s = strip_utf8_bom(s);
            return std::string(s);
        }
    }
}

Result<std::vector<std::byte>> encode_xml_text(std::string_view utf8, XmlEncoding encoding) {
    utf8 = strip_utf8_bom(utf8);
    std::vector<std::byte> out;
    switch (encoding) {
        case XmlEncoding::Utf8Bom:
            out.push_back(std::byte{0xEF});
            out.push_back(std::byte{0xBB});
            out.push_back(std::byte{0xBF});
            [[fallthrough]];
        case XmlEncoding::Utf8:
            out.reserve(out.size() + utf8.size());
            for (unsigned char c : utf8) {
                out.push_back(static_cast<std::byte>(c));
            }
            return out;
        case XmlEncoding::Utf16Le: {
            const auto u = utf8_to_utf16(utf8);
            out.reserve(2 + u.size() * 2);
            out.push_back(std::byte{0xFF});
            out.push_back(std::byte{0xFE});
            for (char16_t c : u) {
                out.push_back(static_cast<std::byte>(c & 0xFF));
                out.push_back(static_cast<std::byte>((c >> 8) & 0xFF));
            }
            return out;
        }
        case XmlEncoding::Utf16Be: {
            const auto u = utf8_to_utf16(utf8);
            out.reserve(2 + u.size() * 2);
            out.push_back(std::byte{0xFE});
            out.push_back(std::byte{0xFF});
            for (char16_t c : u) {
                out.push_back(static_cast<std::byte>((c >> 8) & 0xFF));
                out.push_back(static_cast<std::byte>(c & 0xFF));
            }
            return out;
        }
    }
    return std::unexpected(err(ErrorCode::invalid_argument, "unknown XML encoding"));
}

}  // namespace sxpe::resources
