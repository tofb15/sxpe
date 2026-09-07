#include "sxpe/games/sims3/sims3pack.hpp"

#include "sxpe/core/caps.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

namespace sxpe::games::sims3 {
namespace {

constexpr std::uint32_t kMaxXmlBytes = 16u << 20;       // 16 MiB XML
constexpr std::uint32_t kMaxEntries = 50'000;
constexpr std::uint64_t kMaxFileBytes = 4ull << 30;     // 4 GiB
constexpr char kSig[] = "TS3Pack";
constexpr std::size_t kSigLen = 7;

std::uint32_t read_u32_le(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t read_u16_le(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

std::string to_lower(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

bool ends_with_ci(std::string_view s, std::string_view suffix) {
    if (s.size() < suffix.size()) {
        return false;
    }
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        const auto a = static_cast<unsigned char>(s[s.size() - suffix.size() + i]);
        const auto b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) {
            return false;
        }
    }
    return true;
}

/// Decode a few XML entities used in Sims3Pack names.
std::string xml_decode(std::string_view in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '&') {
            auto semi = in.find(';', i);
            if (semi != std::string_view::npos && semi - i < 16) {
                const auto ent = in.substr(i + 1, semi - i - 1);
                if (ent == "amp") {
                    out.push_back('&');
                } else if (ent == "lt") {
                    out.push_back('<');
                } else if (ent == "gt") {
                    out.push_back('>');
                } else if (ent == "quot") {
                    out.push_back('"');
                } else if (ent == "apos") {
                    out.push_back('\'');
                } else {
                    out.append(in.substr(i, semi - i + 1));
                }
                i = semi;
                continue;
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

std::optional<std::string> attr_value(std::string_view tag, std::string_view attr) {
    const auto key = std::string(attr) + "=\"";
    auto pos = tag.find(key);
    if (pos == std::string_view::npos) {
        const auto key2 = std::string(attr) + "='";
        pos = tag.find(key2);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        pos += key2.size();
        auto end = tag.find('\'', pos);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        return xml_decode(tag.substr(pos, end - pos));
    }
    pos += key.size();
    auto end = tag.find('"', pos);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return xml_decode(tag.substr(pos, end - pos));
}

std::optional<std::string> child_text(std::string_view block, std::string_view tag) {
    const auto open = std::string("<") + std::string(tag) + ">";
    const auto close = std::string("</") + std::string(tag) + ">";
    auto a = block.find(open);
    if (a == std::string_view::npos) {
        // Allow attributes on the open tag: <Name foo="bar">
        const auto open2 = std::string("<") + std::string(tag);
        a = block.find(open2);
        if (a == std::string_view::npos) {
            return std::nullopt;
        }
        auto gt = block.find('>', a);
        if (gt == std::string_view::npos) {
            return std::nullopt;
        }
        if (gt > a + open2.size() && block[gt - 1] == '/') {
            return std::string{};  // empty self-close
        }
        a = gt + 1;
    } else {
        a += open.size();
    }
    auto b = block.find(close, a);
    if (b == std::string_view::npos) {
        return std::nullopt;
    }
    auto text = block.substr(a, b - a);
    // Strip CDATA wrapper if present.
    if (text.size() >= 12 && text.substr(0, 9) == "<![CDATA[") {
        auto end = text.find("]]>");
        if (end != std::string_view::npos) {
            text = text.substr(9, end - 9);
        }
    }
    // Trim whitespace.
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
            text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
            text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return xml_decode(text);
}

std::uint64_t parse_u64(std::string_view s) {
    if (s.empty()) {
        return 0;
    }
    // Allow 0x hex.
    try {
        if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            return std::stoull(std::string(s), nullptr, 16);
        }
        return std::stoull(std::string(s), nullptr, 10);
    } catch (...) {
        return 0;
    }
}

/// Sanitize a packaged filename for extraction (basename only, no path separators).
Result<std::string> safe_entry_name(std::string_view raw, std::uint32_t index) {
    std::string base;
    for (char c : raw) {
        if (c == '/' || c == '\\' || c == '\0') {
            base.clear();  // keep basename only
            continue;
        }
        base.push_back(c);
    }
    while (!base.empty() && (base == "." || base == "..")) {
        return std::unexpected(err(ErrorCode::refused, "entry name rejects . and .."));
    }
    if (base.empty()) {
        base = "entry-" + std::to_string(index) + ".bin";
    }
    // Refuse control chars and path tricks.
    for (char c : base) {
        if (static_cast<unsigned char>(c) < 0x20) {
            return std::unexpected(err(ErrorCode::refused, "entry name has control characters"));
        }
    }
    if (base == "." || base == "..") {
        return std::unexpected(err(ErrorCode::refused, "entry name rejects . and .."));
    }
    return base;
}

bool sniff_dbpf(const std::filesystem::path& path, std::uint64_t abs_off, std::uint64_t len) {
    if (len < 4) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    in.seekg(static_cast<std::streamoff>(abs_off));
    char mag[4]{};
    in.read(mag, 4);
    return in.gcount() == 4 && mag[0] == 'D' && mag[1] == 'B' && mag[2] == 'P' && mag[3] == 'F';
}

}  // namespace

Result<Sims3PackMeta> open_sims3pack(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return std::unexpected(err(ErrorCode::not_found, "sims3pack path does not exist"));
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return std::unexpected(err(ErrorCode::invalid_argument, "sims3pack path is not a file"));
    }
    const auto file_size = static_cast<std::uint64_t>(std::filesystem::file_size(path, ec));
    if (ec) {
        return std::unexpected(err(ErrorCode::io, "cannot stat sims3pack: " + ec.message()));
    }
    if (file_size < 4 + kSigLen + 2 + 4) {
        return std::unexpected(err(ErrorCode::corrupt, "sims3pack too small for TS3Pack header"));
    }
    if (file_size > kMaxFileBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "sims3pack exceeds 4 GiB open cap"));
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::unexpected(err(ErrorCode::io, "cannot open sims3pack"));
    }

    unsigned char hdr_prefix[4]{};
    in.read(reinterpret_cast<char*>(hdr_prefix), 4);
    if (in.gcount() != 4) {
        return std::unexpected(err(ErrorCode::io, "short read on sims3pack header"));
    }
    const auto sig_len = read_u32_le(hdr_prefix);
    if (sig_len != kSigLen) {
        // Honest refusal: some community notes claim XML/DBPF variants; we only
        // implement the SimsWiki TS3Pack layout.
        if (hdr_prefix[0] == 'D' && hdr_prefix[1] == 'B' && hdr_prefix[2] == 'P' &&
            hdr_prefix[3] == 'F') {
            return std::unexpected(
                err(ErrorCode::unsupported_game_or_format,
                    "file starts with DBPF, not TS3Pack — not a SimsWiki Sims3Pack"));
        }
        if (hdr_prefix[0] == 'D' && hdr_prefix[1] == 'B' && hdr_prefix[2] == 'P' &&
            hdr_prefix[3] == 'P') {
            return std::unexpected(err(ErrorCode::protected_or_encrypted,
                                       "DBPP (protected) — SXPE does not decrypt Store DRM"));
        }
        if (hdr_prefix[0] == '<' || (hdr_prefix[0] == 0xEF)) {
            return std::unexpected(
                err(ErrorCode::unsupported_game_or_format,
                    "file looks like bare XML, not TS3Pack-framed Sims3Pack"));
        }
        return std::unexpected(err(ErrorCode::unsupported_game_or_format,
                                   "not a TS3Pack Sims3Pack (signature length != 7)"));
    }

    char sig[kSigLen]{};
    in.read(sig, static_cast<std::streamsize>(kSigLen));
    if (in.gcount() != static_cast<std::streamsize>(kSigLen) ||
        std::memcmp(sig, kSig, kSigLen) != 0) {
        return std::unexpected(
            err(ErrorCode::unsupported_game_or_format, "signature is not \"TS3Pack\""));
    }

    unsigned char ver_xml[6]{};
    in.read(reinterpret_cast<char*>(ver_xml), 6);
    if (in.gcount() != 6) {
        return std::unexpected(err(ErrorCode::corrupt, "truncated TS3Pack header"));
    }
    const auto header_version = read_u16_le(ver_xml);
    const auto xml_length = read_u32_le(ver_xml + 2);
    if (xml_length == 0) {
        return std::unexpected(err(ErrorCode::corrupt, "sims3pack XML length is 0"));
    }
    if (xml_length > kMaxXmlBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "sims3pack XML exceeds 16 MiB cap"));
    }
    const std::uint64_t header_size = 4ull + kSigLen + 2ull + 4ull;
    if (header_size + xml_length > file_size) {
        return std::unexpected(err(ErrorCode::corrupt, "XML section extends past end of file"));
    }

    std::string xml(xml_length, '\0');
    in.read(xml.data(), static_cast<std::streamsize>(xml_length));
    if (in.gcount() != static_cast<std::streamsize>(xml_length)) {
        return std::unexpected(err(ErrorCode::io, "short read on Sims3Pack XML section"));
    }

    Sims3PackMeta meta;
    meta.path = path.string();
    meta.header_version = header_version;
    meta.xml_length = xml_length;
    meta.archive_offset = header_size + xml_length;
    meta.archive_size = file_size - meta.archive_offset;
    meta.file_size = file_size;

    // Root attributes.
    {
        auto root = xml.find("<Sims3Package");
        if (root != std::string::npos) {
            auto gt = xml.find('>', root);
            if (gt != std::string::npos) {
                const auto tag = std::string_view(xml).substr(root, gt - root + 1);
                if (auto t = attr_value(tag, "Type")) {
                    meta.package_type = *t;
                }
                if (auto t = attr_value(tag, "SubType")) {
                    meta.package_subtype = *t;
                }
            }
        }
    }
    if (auto v = child_text(xml, "ArchiveVersion")) {
        meta.archive_version = *v;
    }
    if (auto v = child_text(xml, "DisplayName")) {
        meta.display_name = *v;
    }
    if (auto v = child_text(xml, "Description")) {
        meta.description = *v;
    }
    if (auto v = child_text(xml, "PackageId")) {
        meta.package_id = *v;
    }

    // Best-effort PackagedFile scrape (not a full XML DOM — no pugixml in SXPE).
    std::size_t search = 0;
    while (meta.entries.size() < kMaxEntries) {
        auto start = xml.find("<PackagedFile", search);
        if (start == std::string::npos) {
            break;
        }
        auto end = xml.find("</PackagedFile>", start);
        if (end == std::string::npos) {
            // Self-closing or truncated — stop honestly.
            break;
        }
        end += std::strlen("</PackagedFile>");
        const auto block = std::string_view(xml).substr(start, end - start);
        search = end;

        Sims3PackEntry e;
        e.index = static_cast<std::uint32_t>(meta.entries.size());
        if (auto n = child_text(block, "Name")) {
            e.name = *n;
        }
        if (auto n = child_text(block, "Length")) {
            e.length = parse_u64(*n);
        }
        if (auto n = child_text(block, "Offset")) {
            e.offset = parse_u64(*n);
        }
        if (auto n = child_text(block, "Crc")) {
            e.crc = *n;
        }
        if (auto n = child_text(block, "Guid")) {
            e.guid = *n;
        }
        if (auto n = child_text(block, "ContentType")) {
            e.content_type = *n;
        }

        // Bounds check against archive section.
        if (e.offset > meta.archive_size ||
            e.length > meta.archive_size - e.offset) {
            return std::unexpected(err(ErrorCode::corrupt,
                                       "PackagedFile offset/length outside archive section: " +
                                           (e.name.empty() ? ("#" + std::to_string(e.index))
                                                           : e.name)));
        }

        e.looks_like_package = ends_with_ci(e.name, ".package") ||
                               to_lower(e.content_type).find("package") != std::string::npos ||
                               sniff_dbpf(path, meta.archive_offset + e.offset, e.length);
        meta.entries.push_back(std::move(e));
    }

    if (meta.entries.empty()) {
        // Still a valid inspect if XML parsed — report empty archive honestly.
        // Some packs may use unexpected element names; document the limit.
    }

    return meta;
}

Result<std::filesystem::path> extract_sims3pack_entry(const Sims3PackMeta& meta,
                                                      std::uint32_t index,
                                                      const std::filesystem::path& out_dir,
                                                      bool force) {
    if (index >= meta.entries.size()) {
        return std::unexpected(err(ErrorCode::not_found, "entry index out of range"));
    }
    const auto& e = meta.entries[index];
    if (e.length > sxpe::core::caps::kMaxResourceBytes) {
        // Reuse resource cap as per-entry extract cap (256 MiB).
        return std::unexpected(err(ErrorCode::cap_exceeded, "entry exceeds 256 MiB extract cap"));
    }

    auto name = safe_entry_name(e.name, index);
    if (!name) {
        return std::unexpected(name.error());
    }

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    if (ec) {
        return std::unexpected(err(ErrorCode::io, "cannot create outDir: " + ec.message()));
    }
    // Refuse .. in out_dir parts (caller should have check_path'd already).
    for (const auto& part : out_dir.lexically_normal()) {
        if (part == "..") {
            return std::unexpected(err(ErrorCode::refused, "outDir contains .."));
        }
    }

    const auto dest = out_dir / *name;
    for (const auto& part : dest.lexically_normal()) {
        if (part == "..") {
            return std::unexpected(err(ErrorCode::refused, "extract path contains .."));
        }
    }
    if (std::filesystem::exists(dest, ec) && !ec) {
        if (!force) {
            return std::unexpected(
                err(ErrorCode::refused, "destination exists (pass force to overwrite)"));
        }
    }

    std::ifstream in(meta.path, std::ios::binary);
    if (!in) {
        return std::unexpected(err(ErrorCode::io, "cannot reopen sims3pack for extract"));
    }
    const auto abs = entry_abs_offset(meta, e);
    in.seekg(static_cast<std::streamoff>(abs));
    if (!in) {
        return std::unexpected(err(ErrorCode::io, "seek to packaged payload failed"));
    }

    std::ofstream out(dest, std::ios::binary | std::ios::trunc);
    if (!out) {
        return std::unexpected(err(ErrorCode::io, "cannot write extract destination"));
    }

    constexpr std::size_t kBuf = 1u << 20;
    std::vector<char> buf(kBuf);
    std::uint64_t left = e.length;
    while (left > 0) {
        const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(left, kBuf));
        in.read(buf.data(), static_cast<std::streamsize>(chunk));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got == 0) {
            return std::unexpected(err(ErrorCode::corrupt, "short read extracting packaged file"));
        }
        out.write(buf.data(), static_cast<std::streamsize>(got));
        if (!out) {
            return std::unexpected(err(ErrorCode::io, "write failed during extract"));
        }
        left -= got;
    }
    return dest;
}

}  // namespace sxpe::games::sims3
