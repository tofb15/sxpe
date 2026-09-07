#pragma once

#include "sxpe/error.hpp"
#include "sxpe/resources/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sxpe::resources {

/// On-disk encoding for `_XML` / `ITUN` (and XML-like payloads).
/// Preserved across xml.get → edit → xml.set so UTF-16LE mods round-trip.
enum class XmlEncoding : std::uint8_t {
    Utf8 = 0,
    Utf8Bom = 1,
    Utf16Le = 2,
    Utf16Be = 3,
};

inline constexpr std::string_view xml_encoding_name(XmlEncoding e) {
    switch (e) {
        case XmlEncoding::Utf8Bom:
            return "utf-8-bom";
        case XmlEncoding::Utf16Le:
            return "utf-16le";
        case XmlEncoding::Utf16Be:
            return "utf-16be";
        case XmlEncoding::Utf8:
        default:
            return "utf-8";
    }
}

inline std::optional<XmlEncoding> xml_encoding_from_name(std::string_view s) {
    if (s == "utf-8" || s == "utf8" || s == "UTF-8") {
        return XmlEncoding::Utf8;
    }
    if (s == "utf-8-bom" || s == "utf8-bom" || s == "UTF-8-BOM") {
        return XmlEncoding::Utf8Bom;
    }
    if (s == "utf-16le" || s == "utf-16-le" || s == "UTF-16LE" || s == "UTF-16") {
        return XmlEncoding::Utf16Le;
    }
    if (s == "utf-16be" || s == "utf-16-be" || s == "UTF-16BE") {
        return XmlEncoding::Utf16Be;
    }
    return std::nullopt;
}

inline bool is_xml_editor_type(std::uint32_t type) { return type == kXml || type == kItun; }

/// Sniff BOM / leading `<?xml` / `<`. Does not validate the full document.
bool looks_like_xml(std::span<const std::byte> bytes);

XmlEncoding sniff_xml_encoding(std::span<const std::byte> bytes);

/// Decode payload to UTF-8 text (no BOM in the returned string).
Result<std::string> decode_xml_text(std::span<const std::byte> bytes);

/// Encode UTF-8 text (BOM optional in input; stripped then re-applied per encoding).
Result<std::vector<std::byte>> encode_xml_text(std::string_view utf8, XmlEncoding encoding);

}  // namespace sxpe::resources
