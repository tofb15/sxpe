#include "sxpe/commands/bus.hpp"
#include "sxpe/commands/validate_report.hpp"
#include "sxpe/commands/package_diff_report.hpp"
#include "sxpe/commands/find_refs_report.hpp"
#include "sxpe/commands/folder_scan_report.hpp"
#include "sxpe/commands/sims3pack_report.hpp"
#include "sxpe/core/sha256.hpp"

#include "sxpe/core/caps.hpp"
#include "sxpe/core/file_lock.hpp"
#include "sxpe/games/sims3/fnv.hpp"
#include "sxpe/games/sims3/package.hpp"
#include "sxpe/games/sims3/sims3pack.hpp"
#include "sxpe/games/sims3/tgi.hpp"
#include "sxpe/resources/dds.hpp"
#include "sxpe/resources/dir.hpp"
#include "sxpe/resources/nmap.hpp"
#include "sxpe/resources/casp.hpp"
#include "sxpe/resources/clip.hpp"
#include "sxpe/resources/objd.hpp"
#include "sxpe/resources/objk.hpp"
#include "sxpe/resources/rcol.hpp"
#include "sxpe/resources/s3sa.hpp"
#include "sxpe/resources/png.hpp"
#include "sxpe/resources/stbl.hpp"
#include "sxpe/resources/xml.hpp"
#include "sxpe/resources/types.hpp"
#include "sxpe/resources/merge_hygiene.hpp"
#include "sxpe/resources/vpxy.hpp"
#include "sxpe/resources/refs.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <unordered_set>
#include <utility>

namespace sxpe::commands {
namespace {

using nlohmann::json;
using sxpe::games::sims3::Package;
using sxpe::games::sims3::Sims3PackMeta;
using sxpe::games::sims3::Tgi;
using sxpe::resources::kNmap;
using sxpe::resources::kS3sa;
using sxpe::resources::kStbl;

constexpr int kMaxSessions = 64;
constexpr int kUndoCap = 50;
constexpr std::uint32_t kListDefault = 100;
constexpr std::uint32_t kListMax = 500;
constexpr std::uint32_t kPayloadCap = 1u << 20;

json obj_schema(json props, json required) {
    return {{"type", "object"},
            {"additionalProperties", false},
            {"properties", std::move(props)},
            {"required", std::move(required)}};
}

json rid_schema() {
    return obj_schema({{"type", {{"type", "integer"}}},
                       {"group", {{"type", "integer"}}},
                       {"instance", {{"type", "integer"}}},
                       {"ordinal", {{"type", "integer"}, {"default", 0}}}},
                      json::array({"type", "group", "instance"}));
}

json sess_prop() { return {{"type", "string"}}; }

bool neighborhood_file(const std::filesystem::path& p) {
    auto e = p.extension().string();
    for (char& c : e) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return e == ".nhd" || e == ".world" || e == ".dbc";
}

json dry_prop() { return {{"type", "boolean"}, {"default", false}}; }
json force_prop() { return {{"type", "boolean"}, {"default", false}}; }

std::string b64_encode(std::span<const std::byte> in) {
    static constexpr char k[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < in.size()) {
        const auto n = (static_cast<unsigned>(in[i]) << 16) |
                       (static_cast<unsigned>(in[i + 1]) << 8) | static_cast<unsigned>(in[i + 2]);
        o.push_back(k[(n >> 18) & 63]);
        o.push_back(k[(n >> 12) & 63]);
        o.push_back(k[(n >> 6) & 63]);
        o.push_back(k[n & 63]);
        i += 3;
    }
    if (i < in.size()) {
        unsigned n = static_cast<unsigned>(in[i]) << 16;
        o.push_back(k[(n >> 18) & 63]);
        if (i + 1 < in.size()) {
            n |= static_cast<unsigned>(in[i + 1]) << 8;
            o.push_back(k[(n >> 12) & 63]);
            o.push_back(k[(n >> 6) & 63]);
            o.push_back('=');
        } else {
            o.push_back(k[(n >> 12) & 63]);
            o.append("==");
        }
    }
    return o;
}

int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

Result<std::vector<std::byte>> b64_decode(std::string_view s) {
    std::vector<std::byte> o;
    int val = 0, bits = 0;
    for (char c : s) {
        if (c == '=' || std::isspace(static_cast<unsigned char>(c))) {
            continue;
        }
        const int d = b64_val(c);
        if (d < 0) {
            return std::unexpected(err(ErrorCode::invalid_argument, "base64"));
        }
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            o.push_back(std::byte{static_cast<unsigned char>((val >> bits) & 0xFF)});
        }
    }
    return o;
}

std::uint64_t as_u64(const json& j) {
    if (j.is_number_unsigned()) {
        return j.get<std::uint64_t>();
    }
    if (j.is_number_integer()) {
        return static_cast<std::uint64_t>(j.get<std::int64_t>());
    }
    auto s = j.get<std::string>();
    int base = 10;
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s = s.substr(2);
    }
    return std::stoull(s, nullptr, base);
}

std::string hex32(std::uint32_t v) {
    char b[11];
    std::snprintf(b, sizeof(b), "0x%08X", v);
    return b;
}

std::string hex64(std::uint64_t v) {
    char b[19];
    std::snprintf(b, sizeof(b), "0x%016llX", static_cast<unsigned long long>(v));
    return b;
}

json u64_field(const json& j, const char* num, const char* hex) {
    if (j.contains(num)) {
        return j.at(num);
    }
    return j.at(hex);
}

Tgi tgi_from(const json& j) {
    Tgi t;
    t.type = static_cast<std::uint32_t>(as_u64(u64_field(j, "type", "typeHex")));
    t.group = static_cast<std::uint32_t>(as_u64(u64_field(j, "group", "groupHex")));
    t.instance = as_u64(u64_field(j, "instance", "instanceHex"));
    return t;
}

json tgi_json(Tgi t) {
    return {{"type", t.type},
            {"group", t.group},
            {"instance", t.instance},
            {"typeHex", hex32(t.type)},
            {"groupHex", hex32(t.group)},
            {"instanceHex", hex64(t.instance)}};
}

json rid_json(Tgi t, std::uint32_t ord) {
    auto j = tgi_json(t);
    j["ordinal"] = ord;
    return j;
}


json sims3pack_entry_json(const sxpe::games::sims3::Sims3PackEntry& e) {
    return json{{"index", e.index},
                {"name", e.name},
                {"length", e.length},
                {"offset", e.offset},
                {"crc", e.crc},
                {"guid", e.guid},
                {"contentType", e.content_type},
                {"looksLikePackage", e.looks_like_package}};
}

json sims3pack_meta_json(const Sims3PackMeta& m, bool include_entries) {
    json limitations = json::array({
        "TS3Pack-framed SimsWiki layout only (not bare XML+DBPF, not DBPP/DRM)",
        "PackagedFile XML scrape is best-effort (no full DOM); CRC not verified",
        "No Store download",
    });
    json out{{"path", m.path},
             {"headerVersion", m.header_version},
             {"xmlLength", m.xml_length},
             {"archiveOffset", m.archive_offset},
             {"archiveSize", m.archive_size},
             {"fileSize", m.file_size},
             {"packageType", m.package_type},
             {"packageSubType", m.package_subtype},
             {"archiveVersion", m.archive_version},
             {"displayName", m.display_name},
             {"description", m.description},
             {"packageId", m.package_id},
             {"entryCount", static_cast<std::uint32_t>(m.entries.size())},
             {"readOnly", true},
             {"limitations", limitations}};
    if (include_entries) {
        json arr = json::array();
        for (const auto& e : m.entries) {
            arr.push_back(sims3pack_entry_json(e));
        }
        out["entries"] = std::move(arr);
    }
    out["summary"] = sims3pack_summary_json(out);
    return out;
}

Result<std::filesystem::path> check_path(std::string_view raw) {
    if (raw.empty()) {
        return std::unexpected(err(ErrorCode::invalid_argument, "empty path"));
    }
    std::filesystem::path p{std::string(raw)};
    const auto n = p.lexically_normal();
    for (const auto& part : n) {
        if (part == "..") {
            return std::unexpected(err(ErrorCode::refused, "path contains .."));
        }
    }
    if (const char* allow = std::getenv("SXPE_ALLOW_PATHS")) {
        std::string list(allow);
        bool ok = false;
        std::string cur;
        for (std::size_t i = 0; i <= list.size(); ++i) {
            if (i == list.size() || list[i] == ';' || list[i] == '|') {
                if (!cur.empty()) {
                    std::filesystem::path root(cur);
                    auto rel = n.lexically_relative(root.lexically_normal());
                    const auto rels = rel.generic_string();
                    if (!rels.empty() && rels.find("..") == std::string::npos) {
                        ok = true;
                    }
                    if (n == root.lexically_normal()) {
                        ok = true;
                    }
                }
                cur.clear();
            } else {
                cur.push_back(list[i]);
            }
        }
        if (!ok) {
            return std::unexpected(err(ErrorCode::refused, "path not in SXPE_ALLOW_PATHS"));
        }
    }
    return n;
}

bool dry(const json& a) { return a.value("dryRun", false); }
bool force(const json& a) { return a.value("force", false); }

bool spawn_viewer_detached(const std::string& command) {
    if (command.empty()) {
        return false;
    }
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
    if (wlen <= 0) {
        return false;
    }
    std::wstring wcmd(static_cast<std::size_t>(wlen), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wcmd.data(), wlen) <= 0) {
        return false;
    }
    // cmd.exe resolves PATH for tools like ILSpy; DETACHED so we do not wait.
    std::wstring full = L"cmd.exe /c " + wcmd;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, full.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS, nullptr, nullptr, &si,
                        &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
#else
    const pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    return true;
#endif
}

std::filesystem::path make_temp_s3sa_dll_path() {
    const auto dir = std::filesystem::temp_directory_path();
    const auto stamp =
        std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return dir / ("sxpe-s3sa-" + std::to_string(stamp) + ".dll");
}


struct UndoItem {
    std::string kind;
    std::uint32_t index{0};
    Tgi tgi{};
    std::uint16_t compressed{0};
    bool deleted{false};
    std::vector<std::byte> payload;
};

struct Session {
    explicit Session(Package p) : pkg(std::move(p)) {}
    std::string id;
    Package pkg;
    std::vector<UndoItem> undo;
    std::vector<UndoItem> redo;
    json clipboard = json::array();
    /// Cached NMAP instance→name map for snappy resource.list / ui_index pagination.
    std::unordered_map<std::uint64_t, std::string> names;
    bool names_ready{false};
    void invalidate_names() {
        names_ready = false;
        names.clear();
    }
};

sxpe::resources::Nmap load_nmap(Package& pkg) {
    sxpe::resources::Nmap merged;
    merged.version = 1;
    for (std::uint32_t i = 0; i < pkg.count(); ++i) {
        const auto& e = pkg.entry(i);
        if (e.tgi.type != kNmap) {
            continue;
        }
        // Never decode a huge NMAP just to label the grid — keeps list O(index).
        if (e.mem_size > sxpe::core::caps::kMaxNmapIndexBytes) {
            continue;
        }
        auto body = pkg.uncompressed(i);
        if (!body) {
            continue;
        }
        auto n = sxpe::resources::parse_nmap(*body);
        if (!n) {
            continue;
        }
        for (auto& ne : n->entries) {
            merged.entries.push_back(std::move(ne));
        }
    }
    return merged;
}

std::unordered_map<std::uint64_t, std::string> build_name_index(Package& pkg) {
    auto names = load_nmap(pkg);
    std::unordered_map<std::uint64_t, std::string> m;
    m.reserve(names.entries.size() * 2 + 1);
    for (auto& e : names.entries) {
        m.insert_or_assign(e.instance, std::move(e.name));
    }
    return m;
}

const std::unordered_map<std::uint64_t, std::string>& name_index(Session& s) {
    if (!s.names_ready) {
        s.names = build_name_index(s.pkg);
        s.names_ready = true;
    }
    return s.names;
}

json item_meta(Package& pkg, std::uint32_t i,
               const std::unordered_map<std::uint64_t, std::string>& names) {
    const auto& e = pkg.entry(i);
    json j = rid_json(e.tgi, e.ordinal);
    j["tag"] = sxpe::resources::tag_for(e.tgi.type);
    auto it = names.find(e.tgi.instance);
    j["name"] = it == names.end() ? "" : it->second;
    j["chunkOffset"] = e.chunk_offset;
    j["fileSize"] = e.file_size;
    j["memSize"] = e.mem_size;
    j["compressed"] = e.compressed == 0xFFFF;
    j["deleted"] = pkg.deleted(i);
    return j;
}

VoidResult write_file(const std::filesystem::path& p, std::span<const std::byte> b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        return std::unexpected(err(ErrorCode::io, "open out"));
    }
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
    f.close();
    if (!f) {
        return std::unexpected(err(ErrorCode::io, "write out"));
    }
    return ok();
}

Result<std::vector<std::byte>> read_file(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        return std::unexpected(err(ErrorCode::io, "open in"));
    }
    std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::byte> o(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        o[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
    }
    return o;
}


Result<std::string> sanitize_unmerge_basename(std::string fname, int nsrc) {
    if (fname.empty()) {
        return "source-" + std::to_string(nsrc) + ".package";
    }
    if (fname.find('/') != std::string::npos || fname.find('\\') != std::string::npos) {
        return std::unexpected(err(ErrorCode::refused, "originalFileName must be basename-only"));
    }
    if (fname == "." || fname == "..") {
        return std::unexpected(err(ErrorCode::refused, "originalFileName rejects . and .."));
    }
    // Reject absolute Windows drive paths and Unix absolute (leading separator already caught).
    if (fname.size() >= 2 && std::isalpha(static_cast<unsigned char>(fname[0])) && fname[1] == ':') {
        return std::unexpected(err(ErrorCode::refused, "originalFileName rejects absolute paths"));
    }
    if (fname.find("..") != std::string::npos) {
        return std::unexpected(err(ErrorCode::refused, "originalFileName rejects .."));
    }
    const auto as_path = std::filesystem::path(fname);
    if (as_path.has_parent_path() || as_path.is_absolute() || as_path.filename() != as_path) {
        return std::unexpected(err(ErrorCode::refused, "originalFileName must be basename-only"));
    }
    return fname;
}

Result<std::uint32_t> copy_resource_through(Package& dest, const Package& src, std::uint32_t src_i) {
    auto disk = src.raw(src_i);
    if (!disk) {
        return std::unexpected(disk.error());
    }
    const auto& e = src.entry(src_i);
    return dest.add_raw(e.tgi, *disk, e.mem_size, e.compressed, e.unknown2, e.file_size_high_bit);
}

VoidResult replace_resource_through(Package& dest, std::uint32_t dest_i, const Package& src,
                                    std::uint32_t src_i) {
    auto disk = src.raw(src_i);
    if (!disk) {
        return std::unexpected(disk.error());
    }
    const auto& e = src.entry(src_i);
    return dest.set_raw(dest_i, *disk, e.mem_size, e.compressed, e.unknown2, e.file_size_high_bit);
}

// s3pe concatenates NameMap records on merge. Duplicate TGI 0166038C:0:0 is a table
// union, not last-wins payload replace — otherwise later packages wipe earlier names.
VoidResult merge_nmap_from(Package& dest, std::uint32_t dest_i, const Package& src,
                           std::uint32_t src_i) {
    auto a = dest.uncompressed(dest_i);
    if (!a) {
        return std::unexpected(a.error());
    }
    auto b = src.uncompressed(src_i);
    if (!b) {
        return std::unexpected(b.error());
    }
    auto na = sxpe::resources::parse_nmap(*a);
    if (!na) {
        return std::unexpected(na.error());
    }
    auto nb = sxpe::resources::parse_nmap(*b);
    if (!nb) {
        return std::unexpected(nb.error());
    }
    if (na->entries.size() + nb->entries.size() > sxpe::core::caps::kMaxTableEntries) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "nmap merge count"));
    }
    na->entries.insert(na->entries.end(), nb->entries.begin(), nb->entries.end());
    if (na->version == 0) {
        na->version = nb->version != 0 ? nb->version : 1;
    }
    auto out = sxpe::resources::write_nmap(*na);
    if (!out) {
        return std::unexpected(out.error());
    }
    return dest.set_uncompressed(dest_i, *out, false);
}

VoidResult pin_nmap_front(Package& pkg) {
    if (pkg.layout_locked()) {
        return ok();
    }
    for (std::uint32_t i = 0; i < pkg.count(); ++i) {
        if (pkg.entry(i).tgi.type == kNmap) {
            if (i == 0) {
                return ok();
            }
            return pkg.move(i, 0);
        }
    }
    return ok();
}

json snapshot_source_nmap(const Package& pkg) {
    for (std::uint32_t i = 0; i < pkg.count(); ++i) {
        if (pkg.entry(i).tgi.type != kNmap) {
            continue;
        }
        auto body = pkg.uncompressed(i);
        if (!body) {
            continue;
        }
        auto n = sxpe::resources::parse_nmap(*body);
        if (!n) {
            continue;
        }
        json ents = json::array();
        for (const auto& e : n->entries) {
            ents.push_back({{"instance", e.instance}, {"name", e.name}});
        }
        auto j = rid_json(pkg.entry(i).tgi, pkg.entry(i).ordinal);
        j["version"] = n->version;
        j["entries"] = std::move(ents);
        return j;
    }
    return json();
}

Result<sxpe::resources::Nmap> nmap_from_manifest(const json& nm) {
    sxpe::resources::Nmap n;
    n.version = nm.value("version", 1);
    if (!nm.contains("entries") || !nm["entries"].is_array()) {
        return std::unexpected(err(ErrorCode::corrupt, "nameMap entries"));
    }
    if (nm["entries"].size() > sxpe::core::caps::kMaxTableEntries) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "nameMap count"));
    }
    n.entries.reserve(nm["entries"].size());
    for (const auto& e : nm["entries"]) {
        if (!e.is_object() || !e.contains("instance") || !e.contains("name")) {
            return std::unexpected(err(ErrorCode::corrupt, "nameMap row"));
        }
        const auto name = e["name"].get<std::string>();
        if (name.size() > sxpe::core::caps::kMaxNameBytes) {
            return std::unexpected(err(ErrorCode::cap_exceeded, "nameMap name"));
        }
        n.entries.push_back({as_u64(e["instance"]), name});
    }
    return n;
}

Result<std::uint32_t> add_restored_nmap(Package& dest, const json& nm) {
    auto n = nmap_from_manifest(nm);
    if (!n) {
        return std::unexpected(n.error());
    }
    auto body = sxpe::resources::write_nmap(*n);
    if (!body) {
        return std::unexpected(body.error());
    }
    Tgi t{};
    t.type = kNmap;
    if (nm.contains("type") || nm.contains("typeHex")) {
        t = tgi_from(nm);
    }
    return dest.add(t, *body, false);
}

Result<sxpe::resources::Nmap> slice_nmap_for_listed(const Package& merged, std::uint32_t nmap_i,
                                                   const json& resources) {
    auto body = merged.uncompressed(nmap_i);
    if (!body) {
        return std::unexpected(body.error());
    }
    auto n = sxpe::resources::parse_nmap(*body);
    if (!n) {
        return std::unexpected(n.error());
    }
    std::unordered_set<std::uint64_t> insts;
    if (resources.is_array()) {
        for (const auto& r : resources) {
            Tgi t = tgi_from(r);
            if (t.type == kNmap) {
                continue;
            }
            insts.insert(t.instance);
        }
    }
    sxpe::resources::Nmap out;
    out.version = n->version != 0 ? n->version : 1;
    for (auto& e : n->entries) {
        if (insts.count(e.instance) != 0) {
            out.entries.push_back(std::move(e));
        }
    }
    return out;
}

Result<std::vector<std::byte>> payload_from_args(const json& args) {
    if (args.contains("payloadB64") && args["payloadB64"].is_string()) {
        const auto s = args["payloadB64"].get<std::string>();
        if (!s.empty()) {
            return b64_decode(s);
        }
    }
    if (args.contains("path") && args["path"].is_string()) {
        auto p = check_path(args["path"].get<std::string>());
        if (!p) {
            return std::unexpected(p.error());
        }
        return read_file(*p);
    }
    return std::unexpected(
        err(ErrorCode::invalid_argument, "need payloadB64 or path (bytes to insert)"));
}

Result<std::uint32_t> ensure_nmap_index(Package& pkg) {
    for (std::uint32_t i = 0; i < pkg.count(); ++i) {
        if (pkg.entry(i).tgi.type == kNmap) {
            return i;
        }
    }
    if (pkg.layout_locked()) {
        return std::unexpected(err(ErrorCode::refused,
                                   "neighborhood / world layout lock: this file has no name map and cannot gain one"));
    }
    sxpe::resources::Nmap blank;
    blank.version = 1;
    auto body = sxpe::resources::write_nmap(blank);
    if (!body) {
        return std::unexpected(body.error());
    }
    Tgi t{};
    t.type = kNmap;
    auto idx = pkg.add(t, *body, false);
    if (!idx) {
        return std::unexpected(idx.error());
    }
    return *idx;
}

bool parse_community_name(std::string_view fn, Tgi& t, std::string& name) {
    // S3_TTTTTTTT_GGGGGGGG_IIIIIIIIIIIIIIII_name%%+ext
    if (fn.size() < 40 || fn.substr(0, 3) != "S3_") {
        return false;
    }
    auto rest = fn.substr(3);
    auto p1 = rest.find('_');
    if (p1 == std::string_view::npos) {
        return false;
    }
    auto p2 = rest.find('_', p1 + 1);
    if (p2 == std::string_view::npos) {
        return false;
    }
    auto p3 = rest.find('_', p2 + 1);
    if (p3 == std::string_view::npos) {
        return false;
    }
    try {
        t.type = static_cast<std::uint32_t>(std::stoul(std::string(rest.substr(0, p1)), nullptr, 16));
        t.group = static_cast<std::uint32_t>(
            std::stoul(std::string(rest.substr(p1 + 1, p2 - p1 - 1)), nullptr, 16));
        t.instance = std::stoull(std::string(rest.substr(p2 + 1, p3 - p2 - 1)), nullptr, 16);
    } catch (...) {
        return false;
    }
    auto tail = rest.substr(p3 + 1);
    auto pct = tail.find("%%+");
    name = std::string(pct == std::string_view::npos ? tail : tail.substr(0, pct));
    return true;
}

std::vector<Tool> make_catalog() {
    std::vector<Tool> t;
    auto add = [&](Tool x) { t.push_back(std::move(x)); };
    const json env_out = {{"type", "object"}};
    add({"package.new",
         "New package",
         "Create an empty TS3 package session. When not to use: opening an existing file (use package.open).",
         obj_schema({}, json::array()),
         env_out,
         false,
         false,
         false,
         false});
    add({"package.open",
         "Open package",
         "Open a DBPF file via mmap after sniffing Sims 3 (index-only; payloads stay lazy). "
         "writable defaults false. If writable true and on-disk size >= kOpenReadOnlyBytes (256 MiB), "
         "opens read-only unless forceWritable true (openedReadOnlyDueToSize). "
         "Sharing violations / exclusive locks return a clear io error (close the game or copy the file first). "
         "Paths under Documents/Electronic Arts/.../Mods may add warnings[] when exclusive lock is unavailable. "
         "Example: {\"path\":\"mod.package\"}. Do not use for Sims 4.",
         obj_schema({{"path", {{"type", "string"}}},
                     {"writable", {{"type", "boolean"}, {"default", false}}},
                     {"forceWritable", {{"type", "boolean"}, {"default", false}}},
                     {"game", {{"type", "string"}}}},
                    json::array({"path"})),
         env_out,
         true,
         false,
         true,
         true});
    add({"package.close", "Close", "Drop a session without saving.",
         obj_schema({{"sessionId", sess_prop()}}, json::array({"sessionId"})), env_out, false, false,
         true, false});
    add({"package.save", "Save",
         "Write this session to its current path (in place). Neighborhood .nhd/.world/.dbc "
         "are layout-locked: only in-place payload replace within hole capacity; "
         "add/delete/reorder/compact are refused. Does not save other open packages.",
         obj_schema({{"sessionId", sess_prop()}, {"dryRun", dry_prop()}}, json::array({"sessionId"})),
         env_out, false, true, false, true});
    add({"package.saveAs", "Save As",
         "Write to a new path and switch this session to it (later saves use that path).",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"dryRun", dry_prop()},
                     {"force", force_prop()}},
                    json::array({"sessionId", "path"})),
         env_out, false, true, false, true});
    add({"package.saveCopyAs", "Save Copy As",
         "Write a copy to a new path; this session keeps its current path.",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"dryRun", dry_prop()},
                     {"force", force_prop()}},
                    json::array({"sessionId", "path"})),
         env_out, false, false, false, true});
    add({"package.info", "Package info",
         "Header summary for a session. Includes layoutLocked and pathKind "
         "(nhd|world|dbc|package). Neighborhood .nhd/.world/.dbc are layout-locked.",
         obj_schema({{"sessionId", sess_prop()}}, json::array({"sessionId"})), env_out, true, false,
         true, false});
    add({"package.validate", "Validate",
         "Sniff + DIR cross-checks on an open session. Returns ok, issues[], indexCount, dir{}, "
         "conflictHotspots[] (leftover Sims3Pack manifests / duplicate TGIs), layoutLocked, "
         "pathKind, and summary[] lines for CLI --format text / GUI "
         "(summary names neighborhood / world layout lock and conflict hotspots).",
         obj_schema({{"sessionId", sess_prop()}}, json::array({"sessionId"})), env_out, true, false,
         true, false});
    add({"package.diff", "Compare packages",
         "Diff two TS3 packages by TGI key (type, group, instance, ordinal). "
         "Returns onlyInA[], onlyInB[], different[] (same key, different SHA-256 of uncompressed "
         "payload), sameCount, and summary[] for CLI --format text / GUI. "
         "Example: {\"pathA\":\"a.package\",\"pathB\":\"b.package\"}.",
         obj_schema({{"pathA", {{"type", "string"}}},
                     {"pathB", {{"type", "string"}}}},
                    json::array({"pathA", "pathB"})),
         env_out, true, false, true, true});
    add({"folder.scan", "Scan folder",
         "Read-only recursive scan of *.package under path for empty/zero-byte, unreadable or "
         "corrupt DBPF, wrong-game sniff (non-TS3), and duplicate TGI (type+group+instance) across "
         "files. Never deletes or moves. Caps: maxFiles (default 5000), maxTotalBytes (default 8 GiB), "
         "maxDuplicateSamples (default 100), maxPathsPerDuplicate (default 8). Path refuses '..' "
         "(and SXPE_ALLOW_PATHS when set). Returns files[], issues[], duplicates[] (sample), "
         "summary[] for CLI --format text / GUI. Example: {\"path\":\"Mods\"}.",
         obj_schema({{"path", {{"type", "string"}}},
                     {"maxFiles", {{"type", "integer"}}},
                     {"maxTotalBytes", {{"type", "integer"}}},
                     {"maxDuplicateSamples", {{"type", "integer"}}},
                     {"maxPathsPerDuplicate", {{"type", "integer"}}}},
                    json::array({"path"})),
         env_out, true, false, true, true});

    add({"sims3pack.info", "Sims3Pack info",
         "Read-only inspect of a .sims3pack (SimsWiki TS3Pack header + XML metadata + entry "
         "count). No Store download / DRM. Example: {\"path\":\"mod.sims3pack\"}.",
         obj_schema({{"path", {{"type", "string"}}}}, json::array({"path"})),
         env_out, true, false, true, true});
    add({"sims3pack.list", "Sims3Pack list",
         "List <PackagedFile> entries (name, length, offset, guid, contentType, looksLikePackage). "
         "Read-only. Example: {\"path\":\"mod.sims3pack\"}.",
         obj_schema({{"path", {{"type", "string"}}}}, json::array({"path"})),
         env_out, true, false, true, true});
    add({"sims3pack.extract", "Sims3Pack extract",
         "Extract one packaged payload by index into outDir (basename only). Pass force to "
         "overwrite. Read-only of the sims3pack; writes extracted files (openWorld). No DRM. "
         "Example: {\"path\":\"mod.sims3pack\",\"outDir\":\"/tmp/out\",\"index\":0,\"force\":true}.",
         obj_schema({{"path", {{"type", "string"}}},
                     {"outDir", {{"type", "string"}}},
                     {"index", {{"type", "integer"}}},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"path", "outDir", "index"})),
         env_out, true, false, false, true});

    add({"sims3pack.pack", "Sims3Pack pack",
         "Limited TS3Pack authoring: pack non-recursive *.package files from sourceDir into path "
         "(.sims3pack). Optional metaXml subset and/or displayName/description/packageId/"
         "packageType/packageSubType/archiveVersion. CRC left as zeros (unknown algorithm). "
         "No Store upload / DRM / DBPP. Pass force to overwrite. openWorld write. "
         "Example: {\"path\":\"out.sims3pack\",\"sourceDir\":\"/tmp/pkgs\",\"displayName\":\"My Mod\","
         "\"force\":true}.",
         obj_schema({{"path", {{"type", "string"}}},
                     {"sourceDir", {{"type", "string"}}},
                     {"metaXml", {{"type", "string"}}},
                     {"displayName", {{"type", "string"}}},
                     {"description", {{"type", "string"}}},
                     {"packageId", {{"type", "string"}}},
                     {"packageType", {{"type", "string"}}},
                     {"packageSubType", {{"type", "string"}}},
                     {"archiveVersion", {{"type", "string"}}},
                     {"name", {{"type", "string"}}},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"path", "sourceDir"})),
         env_out, false, false, false, true});


    add({"package.compact", "Compact", "Save dropping session-deleted resources.",
         obj_schema({{"sessionId", sess_prop()}, {"dryRun", dry_prop()}}, json::array({"sessionId"})),
         env_out, false, true, false, true});
    add({"resource.list", "List resources",
         "Metadata only; paginate with limit/cursor (default 100, max 500). Never dumps payloads. Filter: type, group, tag, nameContains, compressed.",
         obj_schema({{"sessionId", sess_prop()},
                     {"limit", {{"type", "integer"}, {"default", 100}}},
                     {"cursor", {{"type", "string"}}},
                     {"filter", {{"type", "object"}}}},
                    json::array({"sessionId"})),
         env_out, true, false, true, false});
    add({"resource.read", "Read resource",
         "Metadata by default. Set includePayload true and maxBytes to embed base64 (cap 1 MiB). Prefer resource.export for files.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"includePayload", {{"type", "boolean"}, {"default", false}}},
                     {"maxBytes", {{"type", "integer"}, {"default", 0}}}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"resource.findRefs", "Find references",
         "Scan REFS + OBJK/VPXY/CASP TGI lists for resources that point at a target TGI (inbound). "
         "Optional byteScan:true does a capped uncompressed payload scan (slow). "
         "Outbound: resource.listRefs. CLI: sxpe resource find-refs / list-refs.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"limit", {{"type", "integer"}, {"default", 200}}},
                     {"byteScan", {{"type", "boolean"}, {"default", false}}},
                     {"byteScanMaxBytes", {{"type", "integer"}, {"default", 1048576}}},
                     {"byteScanMaxResources", {{"type", "integer"}, {"default", 500}}}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"resource.export", "Export to file", "Write uncompressed (or raw) bytes to a path.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"raw", {{"type", "boolean"}, {"default", false}}},
                     {"force", force_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, true, false, true, true});
    add({"resource.extract", "Extract", "Alias of resource.export.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"raw", {{"type", "boolean"}, {"default", false}}},
                     {"force", force_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, true, false, true, true});
    add({"resource.add", "Add resource",
         "Insert bytes with a TGI. Pass payloadB64 or path to a file (not both required).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"payloadB64", {{"type", "string"}}},
                     {"path", {{"type", "string"}}},
                     {"compress", {{"type", "boolean"}, {"default", false}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, false, true, false, false});
    add({"resource.delete", "Delete resource", "Remove from the index (undoable).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, false, true, false, false});
    add({"resource.duplicate", "Duplicate", "Copy payload; duplicate TGI uses the next ordinal.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, false, true, false, false});
    add({"resource.replace", "Replace payload",
         "Keep TGI, replace uncompressed body. Pass payloadB64 or path.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"payloadB64", {{"type", "string"}}},
                     {"path", {{"type", "string"}}},
                     {"compress", {{"type", "boolean"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, false, true, false, false});
    add({"resource.replaceInPlace", "Replace in place",
         "Patch one resource into its existing on-disk hole. Does not rewrite the package. "
         "For SNAP/PNG: keep width/height and 8-bit RGBA; new bytes must fit the hole. "
         "Do not File-Save an .nhd after this.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, false, true, false, true});
    add({"resource.importFiles", "Import files",
         "Import one filesystem path. Community S3_TYPE_GROUP_INSTANCE_name%%+ext names set TGI. Requires --force to overwrite.",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"resourceId", rid_schema()},
                     {"compress", {{"type", "boolean"}, {"default", false}}},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "path"})),
         env_out, false, true, false, true});
    add({"resource.importPackage", "Import package",
         "Copy resources from one or more TS3 packages. Pass path or paths[]. "
         "Duplicate NMAP TGIs concatenate name records and the name map is moved to index 0 "
         "(s3pe merge). writeMergeManifest records SXMM so package.unmerge can reverse an SXPE merge. "
         "dirPolicy: strip (default with writeMergeManifest), copy-through, or rebuild (not yet; refused). "
         "Without writeMergeManifest, default dirPolicy is copy-through. Never invents DIR on empty packages. "
         "leftoverManifestPolicy: strip (default; auto-drop documented allowlist TGIs such as "
         "Sims3Pack leftover 0x73E93EEB instance 0), keep, or warn (copy but list). "
         "duplicateTgiPolicy: force | skip | fail (default force when --force, else fail). "
         "Reports strippedLeftovers[] and duplicates[] warning lists. "
         "Caps (defaults in core/caps): maxPackages, maxTotalBytes, maxResources — refuse with cap_exceeded "
         "before OOM; split the job. reportProgress (default true) emits bus progress events and returns "
         "progress[] in data. Optional checkpointPath + checkpointBetweenPackages saves after each source "
         "(explicit; not autosave) and remaps so RAM stays bounded. Cooperative cancel via bus "
         "request_cancel / cancel_check rolls the session package back to its pre-import state.",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"paths", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                     {"writeMergeManifest", {{"type", "boolean"}, {"default", false}}},
                     {"dirPolicy",
                      {{"type", "string"},
                       {"description", "strip | copy-through | rebuild (rebuild refused for now)"}}},
                     {"leftoverManifestPolicy",
                      {{"type", "string"},
                       {"description", "strip (default) | keep | warn — documented leftover allowlist"}}},
                     {"duplicateTgiPolicy",
                      {{"type", "string"},
                       {"description", "force | skip | fail (default: force if force=true else fail)"}}},
                     {"maxPackages", {{"type", "integer"}, {"default", 500}}},
                     {"maxTotalBytes", {{"type", "integer"}, {"default", 2147483648}}},
                     {"maxResources", {{"type", "integer"}, {"default", 200000}}},
                     {"reportProgress", {{"type", "boolean"}, {"default", true}}},
                     {"checkpointPath",
                      {{"type", "string"},
                       {"description", "Explicit save path when checkpointBetweenPackages is true"}}},
                     {"checkpointBetweenPackages",
                      {{"type", "boolean"},
                       {"default", false},
                       {"description", "Save to checkpointPath after each successful source package"}}},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId"})),
         env_out, false, true, false, true});
    add({"package.unmerge", "Un-merge package",
         "Recreate source packages from an SXPE merge manifest (SXMM). Refuses packages without a valid manifest.",
         obj_schema({{"path", {{"type", "string"}}},
                     {"outDir", {{"type", "string"}}},
                     {"force", force_prop()}},
                    json::array({"path", "outDir"})),
         env_out, false, true, false, true});
    add({"resource.importDbc", "Import DBC",
         "Treat .dbc/DBPF files as packages and copy resources (DBC-equivalent of importPackage). "
         "Pass path or paths[]. Same leftoverManifestPolicy / duplicateTgiPolicy / caps / "
         "reportProgress / checkpoint* as resource.importPackage.",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"paths", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                     {"leftoverManifestPolicy",
                      {{"type", "string"},
                       {"description", "strip (default) | keep | warn"}}},
                     {"duplicateTgiPolicy",
                      {{"type", "string"},
                       {"description", "force | skip | fail"}}},
                     {"maxPackages", {{"type", "integer"}}},
                     {"maxTotalBytes", {{"type", "integer"}}},
                     {"maxResources", {{"type", "integer"}}},
                     {"reportProgress", {{"type", "boolean"}, {"default", true}}},
                     {"checkpointPath", {{"type", "string"}}},
                     {"checkpointBetweenPackages", {{"type", "boolean"}, {"default", false}}},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId"})),
         env_out, false, true, false, true});
    add({"resource.exportToPackage", "Export to package", "Copy one resource into a dest package (created if missing).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, false, true, false, true});
    add({"resource.setFlags", "Set flags",
         "compressed WORD and/or session deleted (deleted is RAM-only; save omits the row).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"compressed", {{"type", "boolean"}}},
                     {"deleted", {{"type", "boolean"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, false, true, false, false});
    add({"resource.rekey", "Rekey TGI", "Change type/group/instance.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"newId", rid_schema()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "newId"})),
         env_out, false, true, false, false});
    add({"resource.copy", "Copy",
         "Clipboard JSON of the resource (cap 32 MiB). append keeps prior copies in the session clipboard.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"append", {{"type", "boolean"}, {"default", false}}}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, false, false});
    add({"resource.paste", "Paste", "Insert clipboard items.",
         obj_schema({{"sessionId", sess_prop()}, {"dryRun", dry_prop()}}, json::array({"sessionId"})),
         env_out, false, true, false, false});
    add({"stbl.get", "STBL get", "List {id,text} for a string table.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"stbl.set", "STBL set", "Set or add a string by id. dryRun available.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"id", {{"type", "integer"}}},
                     {"text", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "id", "text"})),
         env_out, false, true, false, false});
    add({"stbl.delete", "STBL delete string", "Remove a string id from the table.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"id", {{"type", "integer"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "id"})),
         env_out, false, true, false, false});
    add({"nmap.get", "NMAP get",
         "Name-map rows (instance↔name). Optional resourceId selects one NMAP; otherwise "
         "all NMAP resources are concatenated. duplicates[] lists instance ids that appear "
         "more than once; effectiveName is last-wins (same as the Name column).",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId"})),
         env_out, true, false, true, false});
    add({"nmap.list", "NMAP list",
         "Alias of nmap.get for CLI (sxpe nmap list).",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId"})),
         env_out, true, false, true, false});
    add({"nmap.set", "NMAP set",
         "Set the display name for an instance (the Name column). Creates a name map "
         "if the package has none. Prefer resource.rename when you have a resourceId. "
         "Updates the last matching row when duplicates exist (last-wins).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"instance", {{"type", "integer"}}},
                     {"name", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "instance", "name"})),
         env_out, false, true, false, false});
    add({"nmap.delete", "NMAP delete",
         "Remove all name-map rows for an instance id. dryRun available.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"instance", {{"type", "integer"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "instance"})),
         env_out, false, true, false, false});
    add({"nmap.replace", "NMAP replace",
         "Replace the entire name map in one write (one undo). Pass entries as "
         "[{instance,name},…]. Creates a name map if needed. Prefer this for batch edits.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"entries",
                      {{"type", "array"},
                       {"items",
                        {{"type", "object"},
                         {"properties",
                          {{"instance", {{"type", "integer"}}},
                           {"name", {{"type", "string"}}}}},
                         {"required", json::array({"instance", "name"})}}}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "entries"})),
         env_out, false, true, false, false});
    add({"resource.rename", "Rename resource",
         "Set the NMAP display name for a resource (creates a name map if needed). "
         "This is the Name the game and the resource list show.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"name", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "name"})),
         env_out, false, true, false, false});
    add({"dds.info", "DDS info",
         "Width/height/format/cubemap/volume/decodeSupported from a DDS resource. "
         "See docs/spec/dds.md for the format matrix.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"dds.decode", "DDS decode",
         "Decode DXT1/DXT3/DXT5 or 16/24/32-bit RGB(A) mask layouts to RGBA byte count. "
         "Cubemaps, volumes, BC7/DX10, and other FourCCs return unsupported with a clear "
         "message. Does not embed pixels in MCP (use dds.export for the file).",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"dds.export", "DDS export",
         "Write the on-disk DDS bytes to a .dds path (no recompress; works without DirectXTex).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"force", force_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, true, false, true, true});
    add({"dds.replace", "Replace DDS",
         "Validate a filesystem DDS (2D, within kMaxDdsEdge, decode-supported formats only) "
         "and replace the resource payload. Refuses cubemaps/volumes/BC7/DX10 with a clear "
         "error. Pass path; optional compress/dryRun.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"compress", {{"type", "boolean"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, false, true, false, true});
    add({"objk.get", "OBJK get",
         "Parse OBJK version, component IDs, and data keys (wiki 0x02DC343F). Not a full object editor.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"vpxy.get", "VPXY get",
         "Parse VPXY version, entry types, and bounding box (wiki 0x736884F1). Not a mesh viewer.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"objd.get", "OBJD get",
         "Catalog Common header: name/desc GUIDs, price, thumb IID (wiki 0x319E4F1D / Catalog Resource).",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"casp.get", "CASP get",
         "CAS part clothing type and age/gender flags (wiki 0x034AEECB; best-effort).",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"objd.set", "OBJD set",
         "Patch catalog Common fields (name/desc GUIDs, names, price, thumb IID, instanceName). "
         "Preserves materials, unknown trailing bytes, and TGI off. dryRun + undo.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"nameGuid", {{"type", "integer"}}},
                     {"descGuid", {{"type", "integer"}}},
                     {"internalName", {{"type", "string"}}},
                     {"internalDesc", {{"type", "string"}}},
                     {"price", {{"type", "number"}}},
                     {"thumbIid", {{"type", "integer"}}},
                     {"instanceName", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, false, true, false, false});
    add({"casp.set", "CASP set",
         "Patch CAS part name, sortPriority, clothing type/flags, age/gender/species, category, "
         "and optional tgis[] key table. Preserves presets + unknown mid bytes. dryRun + undo.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"name", {{"type", "string"}}},
                     {"sortPriority", {{"type", "number"}}},
                     {"clothingType", {{"type", "integer"}}},
                     {"typeFlags", {{"type", "integer"}}},
                     {"ageGender", {{"type", "integer"}}},
                     {"ageFlags", {{"type", "integer"}}},
                     {"species", {{"type", "integer"}}},
                     {"genderFlags", {{"type", "integer"}}},
                     {"handedness", {{"type", "integer"}}},
                     {"clothingCategory", {{"type", "integer"}}},
                     {"tgis", {{"type", "array"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, false, true, false, false});
    add({"refs.get", "REFS get",
         "Parse REFS (0x05ED1226) TGI+aux table and trailing WORD indices.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"refs.set", "REFS set",
         "Replace REFS entries[] and/or indices[]; preserves version/thingy/aux width. dryRun + undo.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"entries", {{"type", "array"}}},
                     {"indices", {{"type", "array"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, false, true, false, false});
    add({"resource.listRefs", "List outbound references",
         "List TGIs this resource points at (REFS entries, OBJK/VPXY/CASP key tables). "
         "Companion to resource.findRefs (inbound).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"limit", {{"type", "integer"}, {"default", 500}}}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"clip.info", "CLIP info",
         "CLIP duration, anim/source/actor names, track hashes (wiki 0x6B20C4F3). No playback.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"rcol.summary", "RCOL summary",
         "MODL/MLOD/GEOM/MATD chunk tags, mesh counts, MATD shader name + texture TGIs when "
         "parseable (RCOL scan; no mesh view).",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"rcol.replaceChunk", "RCOL replace chunk",
         "Replace one internal RCOL chunk payload by 0-based index. Preserves TGI tables. "
         "Session undo; optional backupPath writes the previous chunk bytes. dryRun.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"chunkIndex", {{"type", "integer"}}},
                     {"payloadB64", {{"type", "string"}}},
                     {"path", {{"type", "string"}}},
                     {"backupPath", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "chunkIndex"})),
         env_out, false, true, false, true});
    add({"clip.exportAs", "CLIP export as new name",
         "Copy CLIP with instance = fnv64_clip (age-letter masks, SimsWiki 0x6B20C4F3).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"name", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "name"})),
         env_out, false, true, false, false});
    add({"clip.exportAsBatch", "CLIP batch exportAs",
         "Export many CLIPs as new names in one call. items: [{resourceId,name},…]. Cap 256. dryRun.",
         obj_schema({{"sessionId", sess_prop()},
                     {"items", {{"type", "array"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "items"})),
         env_out, false, true, false, false});
    add({"clip.set", "CLIP set metadata",
         "Patch safe CLIP fields: animName, sourceFile, actorName, trackHashes[{index,hash}]. "
         "No frame data / playback. dryRun + undo. See docs/spec/clip.md.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"animName", {{"type", "string"}}},
                     {"sourceFile", {{"type", "string"}}},
                     {"actorName", {{"type", "string"}}},
                     {"trackHashes", {{"type", "array"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, false, true, false, false});
    add({"s3sa.info", "S3SA info",
         "Wrapper fields and decrypted PE offset. Never LoadLibrary.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"s3sa.exportDll", "Export DLL",
         "Decrypt the S3SA and write the PE. Never LoadLibrary.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"force", force_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, true, false, true, true});
    add({"s3sa.view", "View S3SA",
         "Export decrypted PE for an external viewer (ILSpy/dnSpy). Never LoadLibrary. "
         "Pass path and/or viewer. Without viewer, path is required (or keepTemp:true for GUI "
         "temp hand-off). With viewer, optional path uses a temp; keepTemp (default true when "
         "viewer set) leaves the file for the viewer — GUI deletes after exit.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"viewer", {{"type", "string"}}},
                     {"keepTemp", {{"type", "boolean"}}},
                     {"force", force_prop()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, true});
    add({"s3sa.importDll", "Import DLL",
         "Wrap a PE as community S3SA v1 (replace resourceId or add). Never LoadLibrary.",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"resourceId", rid_schema()},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "path"})),
         env_out, false, true, false, true});
    add({"s3sa.wrap", "Wrap DLL",
         "Stateless wrap of a PE file as community S3SA v1 bytes. Never LoadLibrary.",
         obj_schema({{"path", {{"type", "string"}}},
                     {"out", {{"type", "string"}}},
                     {"force", force_prop()}},
                    json::array({"path"})),
         env_out, true, false, true, true});
    add({"vid.export", "VID export", "Write VP6/VID payload bytes to a path.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"force", force_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, true, false, true, true});
    add({"hex.get", "Hex preview", "First maxBytes of uncompressed payload as hex.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"maxBytes", {{"type", "integer"}, {"default", 256}}}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"xml.get", "XML get",
         "Decode `_XML`/`ITUN` (or XML-like) payload to UTF-8 text. Preserves encoding sniff "
         "(utf-8 / utf-8-bom / utf-16le / utf-16be). Cap: kMaxXmlEditorBytes (4 MiB).",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"xml.set", "XML set",
         "Replace `_XML`/`ITUN` (or XML-like) payload from UTF-8 text. encoding optional "
         "(default: sniff existing). dryRun + undo. Cap: kMaxXmlEditorBytes (4 MiB).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"text", {{"type", "string"}}},
                     {"encoding", {{"type", "string"},
                                   {"enum", json::array({"utf-8", "utf-8-bom", "utf-16le",
                                                         "utf-16le-bom", "utf-16be",
                                                         "utf-16be-bom"})}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "text"})),
         env_out, false, true, false, false});
    add({"text.get", "Text preview", "Uncompressed payload as UTF-8 if valid.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"maxBytes", {{"type", "integer"}, {"default", 4096}}}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"graph.get", "Resource graph", "Inspector tree. Unknown types are a single bytes node.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"graph.set", "Set graph node", "Currently STBL text nodes only (id=stbl/<hex id>).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"nodeId", {{"type", "string"}}},
                     {"value", {}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "nodeId"})),
         env_out, false, true, false, false});
    add({"hash.fnv", "FNV-1",
         "FNV-1 32/64 or CLIP (fnv64_clip age-letter masks). Example: {\"text\":\"a\",\"width\":32}.",
         obj_schema({{"text", {{"type", "string"}}},
                     {"width", {{"type", "integer"}, {"enum", json::array({32, 64})}}},
                     {"clip", {{"type", "boolean"}, {"default", false}}}},
                    json::array({"text"})),
         env_out, true, false, true, false});
    add({"search.bytes", "Search bytes", "Scan uncompressed resources (skip oversize) for a hex or utf8 needle.",
         obj_schema({{"sessionId", sess_prop()},
                     {"hex", {{"type", "string"}}},
                     {"text", {{"type", "string"}}},
                     {"limit", {{"type", "integer"}, {"default", 100}}}},
                    json::array({"sessionId"})),
         env_out, true, false, true, false});
    add({"handler.list", "List handlers", "Compiled first-party type handlers.",
         obj_schema({}, json::array()), env_out, true, false, true, false});
    add({"editor.list", "List editors", "Headless editors (STBL, NMAP, DDS, S3SA, CLIP, XML/ITUN).",
         obj_schema({}, json::array()), env_out, true, false, true, false});
    add({"settings.get", "Settings", "Feature flags (no personal paths).",
         obj_schema({}, json::array()), env_out, true, false, true, false});
    add({"undo", "Undo", "Undo last session mutation (stack 50).",
         obj_schema({{"sessionId", sess_prop()}}, json::array({"sessionId"})), env_out, false, true,
         false, false});
    add({"redo", "Redo", "Redo last undone mutation (stack 50).",
         obj_schema({{"sessionId", sess_prop()}}, json::array({"sessionId"})), env_out, false, true,
         false, false});
    add({"plan", "Plan", "dry-run alias: returns {dryRun:true} without writing.",
         obj_schema({{"sessionId", sess_prop()}, {"command", {{"type", "string"}}},
                     {"args", {{"type", "object"}}}},
                    json::array({"command", "args"})),
         env_out, true, false, true, false});
    add({"session.start", "Start session", "Open or create and return sessionId.",
         obj_schema({{"path", {{"type", "string"}}}, {"writable", {{"type", "boolean"}}}},
                    json::array()),
         env_out, false, false, false, true});
    add({"session.stop", "Stop session", "Close without save.",
         obj_schema({{"sessionId", sess_prop()}}, json::array({"sessionId"})), env_out, false, false,
         true, false});
    add({"manifest", "Manifest", "Full tool catalog with schemas and hints. Same as MCP tools/list.",
         obj_schema({}, json::array()), env_out, true, false, true, false});
    std::sort(t.begin(), t.end(), [](const Tool& a, const Tool& b) { return a.id < b.id; });
    return t;
}

}  // namespace

nlohmann::json envelope_ok(nlohmann::json data) {
    return {{"schemaVersion", 1}, {"ok", true}, {"data", std::move(data)}};
}

nlohmann::json envelope_err(const Error& e, bool retryable, std::string_view side_effects) {
    return {{"schemaVersion", 1},
            {"ok", false},
            {"error",
             {{"code", error_name(e.code)},
              {"message", e.message},
              {"retryable", retryable},
              {"side_effects", std::string(side_effects)}}}};
}

std::string error_name(ErrorCode c) {
    switch (c) {
        case ErrorCode::io:
            return "io";
        case ErrorCode::unsupported_game_or_format:
            return "unsupported_game_or_format";
        case ErrorCode::protected_or_encrypted:
            return "protected_or_encrypted";
        case ErrorCode::cap_exceeded:
            return "cap_exceeded";
        case ErrorCode::corrupt:
            return "corrupt";
        case ErrorCode::refpack:
            return "refpack";
        case ErrorCode::not_found:
            return "not_found";
        case ErrorCode::refused:
            return "refused";
        case ErrorCode::invalid_argument:
            return "invalid_argument";
    }
    return "unexpected";
}

int exit_code_for(ErrorCode c) {
    switch (c) {
        case ErrorCode::invalid_argument:
            return 2;
        case ErrorCode::not_found:
            return 3;
        case ErrorCode::refused:
            return 4;
        case ErrorCode::unsupported_game_or_format:
        case ErrorCode::protected_or_encrypted:
            return 5;
        case ErrorCode::io:
        case ErrorCode::cap_exceeded:
            return 6;
        case ErrorCode::corrupt:
        case ErrorCode::refpack:
            return 7;
    }
    return 1;
}

struct Bus::Impl {
    std::recursive_mutex mu;
    std::vector<Tool> catalog = make_catalog();
    std::vector<std::unique_ptr<Session>> sessions;
    std::uint32_t next_id{1};
    std::map<std::string, json> idem;
    ProgressHandler progress;
    CancelCheck cancel_check;
    std::atomic<bool> cancel_flag{false};

    void emit_progress(json ev) {
        if (progress) {
            progress(ev);
        }
    }

    [[nodiscard]] bool cancelled() const {
        if (cancel_flag.load(std::memory_order_relaxed)) {
            return true;
        }
        return cancel_check && cancel_check();
    }

    void clear_cancel_state() {
        cancel_flag.store(false, std::memory_order_relaxed);
    }

    /// Snapshot a pre-existing resource before in-place merge mutation (TGI+ordinal keyed).
    struct MutSnap {
        Tgi tgi{};
        std::uint32_t ordinal{0};
        std::uint16_t compressed{0};
        bool deleted{false};
        std::vector<std::byte> payload;
    };

    static VoidResult capture_mut(Session& s, std::uint32_t i, std::vector<MutSnap>& snaps,
                                  std::unordered_set<std::uint64_t>& seen_keys) {
        const auto& e = s.pkg.entry(i);
        // Pack type|group|ordinal low bits + instance into a stable key for de-dupe.
        const std::uint64_t key =
            (static_cast<std::uint64_t>(e.tgi.type) << 32) ^
            (static_cast<std::uint64_t>(e.tgi.group) << 16) ^ e.ordinal ^
            (e.tgi.instance * 0x9e3779b97f4a7c15ull);
        if (!seen_keys.insert(key).second) {
            return ok();
        }
        auto body = s.pkg.uncompressed(i);
        if (!body) {
            return std::unexpected(body.error());
        }
        MutSnap snap;
        snap.tgi = e.tgi;
        snap.ordinal = e.ordinal;
        snap.compressed = e.compressed;
        snap.deleted = s.pkg.deleted(i);
        snap.payload = std::move(*body);
        snaps.push_back(std::move(snap));
        return ok();
    }

    static VoidResult rollback_import(Session& s, std::uint32_t baseline_count, bool baseline_dirty,
                                      std::vector<MutSnap>& snaps) {
        while (s.pkg.count() > baseline_count) {
            if (auto r = s.pkg.remove(s.pkg.count() - 1); !r) {
                return r;
            }
        }
        for (auto& snap : snaps) {
            auto idx = s.pkg.find(snap.tgi, snap.ordinal);
            if (!idx) {
                return std::unexpected(err(ErrorCode::corrupt,
                                           "cancel rollback: mutated resource missing"));
            }
            if (auto r = s.pkg.set_uncompressed(*idx, snap.payload, snap.compressed == 0xFFFF);
                !r) {
                return r;
            }
            if (auto r = s.pkg.set_deleted(*idx, snap.deleted); !r) {
                return r;
            }
        }
        s.pkg.set_dirty(baseline_dirty);
        s.invalidate_names();
        return ok();
    }

    Session* find(const std::string& id) {
        for (auto& s : sessions) {
            if (s->id == id) {
                return s.get();
            }
        }
        return nullptr;
    }

    Result<Session*> require(const json& args) {
        if (!args.contains("sessionId") || !args["sessionId"].is_string()) {
            return std::unexpected(err(ErrorCode::invalid_argument, "sessionId required"));
        }
        auto* s = find(args["sessionId"].get<std::string>());
        if (!s) {
            return std::unexpected(err(ErrorCode::not_found, "session"));
        }
        return s;
    }

    Result<std::uint32_t> idx(Session& s, const json& args) {
        if (!args.contains("resourceId")) {
            return std::unexpected(err(ErrorCode::invalid_argument, "resourceId required"));
        }
        Tgi t = tgi_from(args["resourceId"]);
        std::uint32_t ord = 0;
        if (args["resourceId"].contains("ordinal")) {
            ord = static_cast<std::uint32_t>(as_u64(args["resourceId"]["ordinal"]));
        }
        auto i = s.pkg.find(t, ord);
        if (!i) {
            return std::unexpected(err(ErrorCode::not_found, "resource"));
        }
        return *i;
    }

    static bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
        if (a.empty() || b.empty()) {
            return false;
        }
        std::error_code ec;
        if (std::filesystem::exists(a, ec) && std::filesystem::exists(b, ec)) {
            if (std::filesystem::equivalent(a, b, ec) && !ec) {
                return true;
            }
        }
        return false;
    }

    Session* find_by_path(const std::filesystem::path& p) {
        for (auto& s : sessions) {
            if (same_file(s->pkg.path(), p)) {
                return s.get();
            }
        }
        return nullptr;
    }

    Result<std::string> add_session(Package p) {
        if (static_cast<int>(sessions.size()) >= kMaxSessions) {
            return std::unexpected(err(ErrorCode::refused, "too many packages open (64)"));
        }
        auto s = std::make_unique<Session>(std::move(p));
        s->id = "s-" + std::to_string(next_id++);
        auto* ptr = s.get();
        sessions.push_back(std::move(s));
        return ptr->id;
    }

    void push_undo(Session& s, UndoItem u) {
        s.undo.push_back(std::move(u));
        if (s.undo.size() > kUndoCap) {
            s.undo.erase(s.undo.begin());
        }
        s.redo.clear();
    }

    VoidResult snapshot(Session& s, std::uint32_t i) {
        UndoItem u;
        u.kind = "restore";
        u.index = i;
        u.tgi = s.pkg.entry(i).tgi;
        u.compressed = s.pkg.entry(i).compressed;
        u.deleted = s.pkg.deleted(i);
        auto body = s.pkg.uncompressed(i);
        if (!body) {
            return std::unexpected(body.error());
        }
        u.payload = std::move(*body);
        push_undo(s, std::move(u));
        return ok();
    }

    Result<UndoItem> capture_item(Session& s, std::uint32_t i, std::string kind) {
        UndoItem u;
        u.kind = std::move(kind);
        u.index = i;
        u.tgi = s.pkg.entry(i).tgi;
        u.compressed = s.pkg.entry(i).compressed;
        u.deleted = s.pkg.deleted(i);
        auto body = s.pkg.uncompressed(i);
        if (!body) {
            return std::unexpected(body.error());
        }
        u.payload = std::move(*body);
        return u;
    }

    VoidResult apply_item(Session& s, UndoItem& u) {
        if (u.kind == "remove") {
            return s.pkg.remove(u.index);
        }
        if (u.kind == "inplace") {
            return s.pkg.patch_in_place(u.index, u.payload, u.compressed == 0xFFFF);
        }
        if (u.kind == "insert") {
            auto r = s.pkg.add(u.tgi, u.payload, u.compressed == 0xFFFF);
            if (!r) {
                return std::unexpected(r.error());
            }
            u.index = *r;
            if (u.deleted) {
                return s.pkg.set_deleted(*r, true);
            }
            return ok();
        }
        if (auto r = s.pkg.rekey(u.index, u.tgi); !r) {
            return r;
        }
        if (auto r = s.pkg.set_uncompressed(u.index, u.payload, u.compressed == 0xFFFF); !r) {
            return r;
        }
        return s.pkg.set_deleted(u.index, u.deleted);
    }

    static void push_stack(std::vector<UndoItem>& st, UndoItem u) {
        st.push_back(std::move(u));
        if (static_cast<int>(st.size()) > kUndoCap) {
            st.erase(st.begin());
        }
    }

    json info(Session& s) {
        const bool locked = s.pkg.layout_locked();
        return {{"sessionId", s.id},
                {"path", s.pkg.path().string()},
                {"readWrite", s.pkg.writable()},
                {"dirty", s.pkg.dirty()},
                {"magic", "DBPF"},
                {"major", s.pkg.major()},
                {"minor", s.pkg.minor()},
                {"indexVersion", s.pkg.index_version()},
                {"indexCount", s.pkg.count()},
                {"compressedCount", s.pkg.compressed_count()},
                {"deletedCount", s.pkg.deleted_count()},
                {"dirPresent", s.pkg.dir_present()},
                {"mappedBytes", s.pkg.mapped_bytes()},
                {"layoutLocked", locked},
                {"pathKind", s.pkg.path_kind()},
                {"holdsExclusiveLock", s.pkg.holds_exclusive_lock()},
                {"game", "sims3"}};
    }

    json exec(std::string_view id, json args);
};

Bus::Bus() : impl_(std::make_unique<Impl>()) {}
Bus::~Bus() = default;
Bus::Bus(Bus&&) noexcept = default;
Bus& Bus::operator=(Bus&&) noexcept = default;

void Bus::set_progress_handler(ProgressHandler handler) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mu);
    impl_->progress = std::move(handler);
}

void Bus::clear_progress_handler() {
    std::lock_guard<std::recursive_mutex> lock(impl_->mu);
    impl_->progress = nullptr;
}

void Bus::set_cancel_check(CancelCheck check) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mu);
    impl_->cancel_check = std::move(check);
}

void Bus::clear_cancel_check() {
    std::lock_guard<std::recursive_mutex> lock(impl_->mu);
    impl_->cancel_check = nullptr;
}

void Bus::request_cancel() {
    // Async-signal-safe: only touch the atomic.
    impl_->cancel_flag.store(true, std::memory_order_relaxed);
}

void Bus::clear_cancel() {
    impl_->clear_cancel_state();
}

bool Bus::cancel_requested() const {
    return impl_->cancelled();
}

std::vector<Tool> Bus::tools() const { return impl_->catalog; }

nlohmann::json Bus::manifest() const {
    json tools = json::array();
    for (const auto& t : impl_->catalog) {
        tools.push_back({{"name", t.id},
                         {"mcpName", [&] {
                              std::string m = t.id;
                              std::replace(m.begin(), m.end(), '.', '_');
                              return m;
                          }()},
                         {"title", t.title},
                         {"description", t.description},
                         {"inputSchema", t.input_schema},
                         {"outputSchema", t.output_schema},
                         {"annotations",
                          {{"readOnlyHint", t.read_only},
                           {"destructiveHint", t.destructive},
                           {"idempotentHint", t.idempotent},
                           {"openWorldHint", t.open_world}}}});
    }
    return envelope_ok({{"tools", tools}});
}

Result<std::vector<UiRow>> Bus::ui_index(std::string_view session_id) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mu);
    auto* s = impl_->find(std::string(session_id));
    if (!s) {
        return std::unexpected(err(ErrorCode::not_found, "session"));
    }
    const auto& names = name_index(*s);
    std::vector<UiRow> rows;
    rows.reserve(s->pkg.count());
    for (std::uint32_t i = 0; i < s->pkg.count(); ++i) {
        const auto& e = s->pkg.entry(i);
        UiRow r;
        r.index = i;
        r.type = e.tgi.type;
        r.group = e.tgi.group;
        r.instance = e.tgi.instance;
        r.ordinal = e.ordinal;
        r.chunk_offset = e.chunk_offset;
        r.file_size = e.file_size;
        r.mem_size = e.mem_size;
        r.tag = std::string(sxpe::resources::tag_for(e.tgi.type));
        if (auto it = names.find(e.tgi.instance); it != names.end()) {
            r.name = it->second;
        }
        r.compressed = e.compressed == 0xFFFF;
        r.deleted = s->pkg.deleted(i);
        rows.push_back(std::move(r));
    }
    return rows;
}

nlohmann::json Bus::execute(std::string_view id, const nlohmann::json& args) {
    std::lock_guard<std::recursive_mutex> lock(impl_->mu);
    try {
        if (args.contains("game") && args["game"].is_string()) {
            const auto g = args["game"].get<std::string>();
            if (g != "sims3" && !g.empty()) {
                return envelope_err(err(ErrorCode::unsupported_game_or_format,
                                        "not implemented in this version"),
                                    false);
            }
        }
        if (args.contains("idempotencyKey") && args["idempotencyKey"].is_string()) {
            const auto k = std::string(id) + ":" + args["idempotencyKey"].get<std::string>();
            if (auto it = impl_->idem.find(k); it != impl_->idem.end()) {
                return it->second;
            }
            auto r = impl_->exec(id, args);
            impl_->idem[k] = r;
            if (impl_->idem.size() > 64) {
                impl_->idem.erase(impl_->idem.begin());
            }
            return r;
        }
        return impl_->exec(id, args);
    } catch (const json::exception& e) {
        return envelope_err(err(ErrorCode::invalid_argument, e.what()), false);
    } catch (const std::exception& e) {
        return envelope_err(err(ErrorCode::invalid_argument, e.what()), false);
    }
}

json Bus::Impl::exec(std::string_view id, json args) {
    const std::string cmd(id);
    if (cmd == "manifest") {
        json tools = json::array();
        for (const auto& t : catalog) {
            std::string mcp = t.id;
            std::replace(mcp.begin(), mcp.end(), '.', '_');
            tools.push_back({{"name", t.id},
                             {"mcpName", mcp},
                             {"title", t.title},
                             {"description", t.description},
                             {"inputSchema", t.input_schema},
                             {"outputSchema", t.output_schema},
                             {"annotations",
                              {{"readOnlyHint", t.read_only},
                               {"destructiveHint", t.destructive},
                               {"idempotentHint", t.idempotent},
                               {"openWorldHint", t.open_world}}}});
        }
        return envelope_ok({{"tools", tools}});
    }
    if (cmd == "package.diff") {
        auto path_a = check_path(args.at("pathA").get<std::string>());
        if (!path_a) {
            return envelope_err(path_a.error());
        }
        auto path_b = check_path(args.at("pathB").get<std::string>());
        if (!path_b) {
            return envelope_err(path_b.error());
        }
        auto open_a = Package::open(*path_a, false);
        if (!open_a) {
            return envelope_err(open_a.error(), open_a.error().code == ErrorCode::io, "none");
        }
        auto open_b = Package::open(*path_b, false);
        if (!open_b) {
            return envelope_err(open_b.error(), open_b.error().code == ErrorCode::io, "none");
        }
        struct DiffKey {
            std::uint32_t type{0};
            std::uint32_t group{0};
            std::uint64_t instance{0};
            std::uint32_t ordinal{0};
            bool operator==(const DiffKey&) const = default;
            bool operator<(const DiffKey& o) const {
                if (type != o.type) {
                    return type < o.type;
                }
                if (group != o.group) {
                    return group < o.group;
                }
                if (instance != o.instance) {
                    return instance < o.instance;
                }
                return ordinal < o.ordinal;
            }
        };
        struct DiffHash {
            std::size_t operator()(const DiffKey& k) const noexcept {
                std::size_t h = k.type;
                h ^= static_cast<std::size_t>(k.group) + 0x9e3779b9u + (h << 6) + (h >> 2);
                h ^= static_cast<std::size_t>(k.ordinal) + 0x9e3779b9u + (h << 6) + (h >> 2);
                h ^= static_cast<std::size_t>(k.instance) + 0x9e3779b9u + (h << 6) + (h >> 2);
                h ^= static_cast<std::size_t>(k.instance >> 32) + 0x9e3779b9u + (h << 6) + (h >> 2);
                return h;
            }
        };
        struct SideInfo {
            std::uint32_t index{0};
            std::uint32_t mem_size{0};
            std::uint16_t compressed{0};
            std::string hash;
            bool hash_ok{false};
            std::string error;
        };
        auto index_side = [](Package& pkg) -> Result<std::unordered_map<DiffKey, SideInfo, DiffHash>> {
            std::unordered_map<DiffKey, SideInfo, DiffHash> m;
            m.reserve(pkg.count() * 2 + 1);
            for (std::uint32_t i = 0; i < pkg.count(); ++i) {
                const auto& e = pkg.entry(i);
                DiffKey key{e.tgi.type, e.tgi.group, e.tgi.instance, e.ordinal};
                SideInfo info;
                info.index = i;
                info.mem_size = e.mem_size;
                info.compressed = e.compressed;
                auto body = pkg.uncompressed(i);
                if (!body) {
                    info.hash_ok = false;
                    info.error = body.error().message;
                } else {
                    info.hash = sxpe::core::sha256_hex(*body);
                    info.hash_ok = true;
                }
                m[key] = std::move(info);
            }
            return m;
        };
        auto map_a = index_side(*open_a);
        if (!map_a) {
            return envelope_err(map_a.error());
        }
        auto map_b = index_side(*open_b);
        if (!map_b) {
            return envelope_err(map_b.error());
        }
        json only_a = json::array();
        json only_b = json::array();
        json different = json::array();
        std::uint32_t same = 0;
        std::vector<DiffKey> keys;
        keys.reserve(map_a->size() + map_b->size());
        for (const auto& [k, _] : *map_a) {
            keys.push_back(k);
        }
        for (const auto& [k, _] : *map_b) {
            if (!map_a->count(k)) {
                keys.push_back(k);
            }
        }
        std::sort(keys.begin(), keys.end());
        for (const auto& k : keys) {
            const auto ia = map_a->find(k);
            const auto ib = map_b->find(k);
            const bool in_a = ia != map_a->end();
            const bool in_b = ib != map_b->end();
            Tgi tgi{k.type, k.group, k.instance};
            if (in_a && !in_b) {
                auto row = rid_json(tgi, k.ordinal);
                row["memSize"] = ia->second.mem_size;
                row["compressed"] = ia->second.compressed != 0;
                if (ia->second.hash_ok) {
                    row["hash"] = ia->second.hash;
                } else {
                    row["error"] = ia->second.error;
                }
                only_a.push_back(std::move(row));
                continue;
            }
            if (!in_a && in_b) {
                auto row = rid_json(tgi, k.ordinal);
                row["memSize"] = ib->second.mem_size;
                row["compressed"] = ib->second.compressed != 0;
                if (ib->second.hash_ok) {
                    row["hash"] = ib->second.hash;
                } else {
                    row["error"] = ib->second.error;
                }
                only_b.push_back(std::move(row));
                continue;
            }
            const auto& a = ia->second;
            const auto& b = ib->second;
            const bool both_ok = a.hash_ok && b.hash_ok;
            if (both_ok && a.hash == b.hash) {
                ++same;
                continue;
            }
            auto row = rid_json(tgi, k.ordinal);
            row["memSizeA"] = a.mem_size;
            row["memSizeB"] = b.mem_size;
            row["compressedA"] = a.compressed != 0;
            row["compressedB"] = b.compressed != 0;
            if (a.hash_ok) {
                row["hashA"] = a.hash;
            } else {
                row["errorA"] = a.error;
            }
            if (b.hash_ok) {
                row["hashB"] = b.hash;
            } else {
                row["errorB"] = b.error;
            }
            different.push_back(std::move(row));
        }
        json data{{"pathA", path_a->string()},
                  {"pathB", path_b->string()},
                  {"hashAlgorithm", "sha256-uncompressed"},
                  {"countA", open_a->count()},
                  {"countB", open_b->count()},
                  {"sameCount", same},
                  {"onlyInA", only_a},
                  {"onlyInB", only_b},
                  {"different", different}};
        data["summary"] = package_diff_summary_json(data);
        return envelope_ok(std::move(data));
    }

    if (cmd == "sims3pack.info" || cmd == "sims3pack.list" || cmd == "sims3pack.extract") {
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        auto opened = sxpe::games::sims3::open_sims3pack(*path);
        if (!opened) {
            return envelope_err(opened.error());
        }
        if (cmd == "sims3pack.info") {
            return envelope_ok(sims3pack_meta_json(*opened, false));
        }
        if (cmd == "sims3pack.list") {
            return envelope_ok(sims3pack_meta_json(*opened, true));
        }
        // extract
        auto out_dir = check_path(args.at("outDir").get<std::string>());
        if (!out_dir) {
            return envelope_err(out_dir.error());
        }
        const auto index = static_cast<std::uint32_t>(as_u64(args.at("index")));
        if (dry(args)) {
            json data = sims3pack_meta_json(*opened, false);
            if (index >= opened->entries.size()) {
                return envelope_err(err(ErrorCode::not_found, "entry index out of range"));
            }
            const auto& e = opened->entries[index];
            data["dryRun"] = true;
            data["index"] = index;
            data["name"] = e.name;
            data["outDir"] = out_dir->string();
            data["wouldWrite"] = (*out_dir / (e.name.empty() ? ("entry-" + std::to_string(index) + ".bin")
                                                             : std::filesystem::path(e.name).filename().string()))
                                     .string();
            data["summary"] = sims3pack_summary_json(data);
            return envelope_ok(std::move(data));
        }
        auto written = sxpe::games::sims3::extract_sims3pack_entry(*opened, index, *out_dir, force(args));
        if (!written) {
            return envelope_err(written.error());
        }
        json data = sims3pack_meta_json(*opened, false);
        data["index"] = index;
        data["name"] = opened->entries[index].name;
        data["outDir"] = out_dir->string();
        data["writtenPath"] = written->string();
        data["written"] = json::array({written->string()});
        data["summary"] = sims3pack_summary_json(data);
        return envelope_ok(std::move(data));
    }


    if (cmd == "sims3pack.pack") {
        auto out_path = check_path(args.at("path").get<std::string>());
        if (!out_path) {
            return envelope_err(out_path.error());
        }
        auto source_dir = check_path(args.at("sourceDir").get<std::string>());
        if (!source_dir) {
            return envelope_err(source_dir.error());
        }
        sxpe::games::sims3::Sims3PackCreateOptions opts;
        if (args.contains("metaXml") && args["metaXml"].is_string() &&
            !args["metaXml"].get<std::string>().empty()) {
            auto meta_path = check_path(args["metaXml"].get<std::string>());
            if (!meta_path) {
                return envelope_err(meta_path.error());
            }
            auto loaded = sxpe::games::sims3::load_sims3pack_meta_subset(*meta_path);
            if (!loaded) {
                return envelope_err(loaded.error());
            }
            opts = *loaded;
        }
        auto override_str = [&](const char* key, std::string& dest) {
            if (args.contains(key) && args[key].is_string()) {
                auto s = args[key].get<std::string>();
                if (!s.empty()) {
                    dest = std::move(s);
                }
            }
        };
        override_str("displayName", opts.display_name);
        override_str("description", opts.description);
        override_str("packageId", opts.package_id);
        override_str("packageType", opts.package_type);
        override_str("packageSubType", opts.package_subtype);
        override_str("archiveVersion", opts.archive_version);
        // Convenience: CLI --name maps to top-level name
        if (opts.display_name.empty() && args.contains("name") && args["name"].is_string()) {
            opts.display_name = args["name"].get<std::string>();
        }

        auto items = sxpe::games::sims3::collect_sims3pack_packages(*source_dir);
        if (!items) {
            return envelope_err(items.error());
        }
        if (dry(args)) {
            json data{{"path", out_path->string()},
                      {"sourceDir", source_dir->string()},
                      {"dryRun", true},
                      {"authored", true},
                      {"entryCount", static_cast<std::uint32_t>(items->size())},
                      {"displayName", opts.display_name},
                      {"description", opts.description},
                      {"packageId", opts.package_id},
                      {"packageType", opts.package_type},
                      {"packageSubType", opts.package_subtype},
                      {"archiveVersion", opts.archive_version},
                      {"wouldWrite", out_path->string()},
                      {"limitations",
                       json::array({"Limited TS3Pack authoring only",
                                    "Non-recursive *.package from sourceDir",
                                    "CRC placeholder zeros (algorithm unknown)",
                                    "No Store upload / DRM / DBPP"})}};
            json names = json::array();
            for (const auto& it : *items) {
                names.push_back(it.name.empty() ? it.source_path.filename().string() : it.name);
            }
            data["packageNames"] = std::move(names);
            data["summary"] = sims3pack_summary_json(data);
            return envelope_ok(std::move(data));
        }
        auto packed =
            sxpe::games::sims3::pack_sims3pack(*out_path, *items, opts, force(args));
        if (!packed) {
            return envelope_err(packed.error());
        }
        json data = sims3pack_meta_json(*packed, true);
        data["authored"] = true;
        data["readOnly"] = false;
        data["sourceDir"] = source_dir->string();
        data["writtenPath"] = out_path->string();
        data["limitations"] = json::array(
            {"Limited TS3Pack authoring only", "Non-recursive *.package from sourceDir",
             "CRC placeholder zeros (algorithm unknown)", "No Store upload / DRM / DBPP",
             "PackagedFile XML scrape on re-open is best-effort"});
        data["summary"] = sims3pack_summary_json(data);
        return envelope_ok(std::move(data));
    }

    if (cmd == "folder.scan") {
        auto root = check_path(args.at("path").get<std::string>());
        if (!root) {
            return envelope_err(root.error());
        }
        std::error_code ec;
        if (!std::filesystem::exists(*root, ec) || ec) {
            return envelope_err(err(ErrorCode::not_found, "path does not exist"));
        }
        if (!std::filesystem::is_directory(*root, ec) || ec) {
            return envelope_err(err(ErrorCode::invalid_argument, "path is not a directory"));
        }

        auto as_u32 = [&](const char* key, std::uint32_t def) -> std::uint32_t {
            if (!args.contains(key)) {
                return def;
            }
            const auto v = as_u64(args.at(key));
            if (v == 0 || v > 0xffffffffu) {
                return def;
            }
            return static_cast<std::uint32_t>(v);
        };
        auto as_u64_opt = [&](const char* key, std::uint64_t def) -> std::uint64_t {
            if (!args.contains(key)) {
                return def;
            }
            const auto v = as_u64(args.at(key));
            return v == 0 ? def : v;
        };

        const auto max_files = as_u32("maxFiles", sxpe::core::caps::kFolderScanMaxFiles);
        const auto max_bytes =
            as_u64_opt("maxTotalBytes", sxpe::core::caps::kFolderScanMaxTotalBytes);
        const auto max_dup_samples =
            as_u32("maxDuplicateSamples", sxpe::core::caps::kFolderScanMaxDuplicateSamples);
        const auto max_paths_per_dup =
            as_u32("maxPathsPerDuplicate", sxpe::core::caps::kFolderScanMaxPathsPerDuplicate);

        auto is_package_ext = [](const std::filesystem::path& p) {
            auto e = p.extension().string();
            for (char& c : e) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return e == ".package";
        };

        auto sniff_wrong_game = [](const std::filesystem::path& p) -> std::string {
            std::ifstream in(p, std::ios::binary);
            if (!in) {
                return {};
            }
            unsigned char hdr[8]{};
            in.read(reinterpret_cast<char*>(hdr), 8);
            if (in.gcount() < 4) {
                return {};
            }
            if (!(hdr[0] == 'D' && hdr[1] == 'B' && hdr[2] == 'P' && hdr[3] == 'F')) {
                return "not DBPF";
            }
            if (in.gcount() < 8) {
                return "DBPF truncated header";
            }
            const std::uint32_t major = static_cast<std::uint32_t>(hdr[4]) |
                                       (static_cast<std::uint32_t>(hdr[5]) << 8) |
                                       (static_cast<std::uint32_t>(hdr[6]) << 16) |
                                       (static_cast<std::uint32_t>(hdr[7]) << 24);
            if (major == 1) {
                return "DBPF major 1 (likely Sims 2 / older)";
            }
            if (major != 2) {
                return "DBPF major " + std::to_string(major) + " (not Sims 3)";
            }
            return "DBPF major 2 but not a Sims 3 package SXPE can open";
        };

        struct DupKey {
            std::uint32_t type{0};
            std::uint32_t group{0};
            std::uint64_t instance{0};
            bool operator==(const DupKey&) const = default;
        };
        struct DupHash {
            std::size_t operator()(const DupKey& k) const noexcept {
                std::size_t h = k.type;
                h ^= static_cast<std::size_t>(k.group) + 0x9e3779b9u + (h << 6) + (h >> 2);
                h ^= static_cast<std::size_t>(k.instance) + 0x9e3779b9u + (h << 6) + (h >> 2);
                h ^= static_cast<std::size_t>(k.instance >> 32) + 0x9e3779b9u + (h << 6) + (h >> 2);
                return h;
            }
        };

        json files_arr = json::array();
        json issues_arr = json::array();
        std::unordered_map<DupKey, std::vector<std::string>, DupHash> tgi_files;
        std::uint32_t files_scanned = 0;
        std::uint64_t bytes_scanned = 0;
        std::uint32_t ok_count = 0;
        bool capped = false;
        std::string cap_reason;
        bool scan_cancelled = false;
        const bool report_progress = args.value("reportProgress", true);
        clear_cancel_state();

        const auto opts = std::filesystem::directory_options::skip_permission_denied;
        std::filesystem::recursive_directory_iterator it(*root, opts, ec);
        std::filesystem::recursive_directory_iterator end;
        if (ec) {
            return envelope_err(err(ErrorCode::io, "cannot iterate directory: " + ec.message()), true);
        }
        if (report_progress) {
            emit_progress({{"command", cmd},
                           {"phase", "start"},
                           {"filesScanned", 0},
                           {"path", root->string()}});
        }
        for (; it != end; it.increment(ec)) {
            if (cancelled()) {
                scan_cancelled = true;
                break;
            }
            if (ec) {
                ec.clear();
                continue;
            }
            const auto& entry = *it;
            std::error_code fec;
            if (!entry.is_regular_file(fec) || fec) {
                continue;
            }
            if (!is_package_ext(entry.path())) {
                continue;
            }

            // Refuse escaped relative display paths (defense in depth; check_path already
            // refused ".." on the root).
            const auto abs = entry.path().lexically_normal();
            for (const auto& part : abs) {
                if (part == "..") {
                    return envelope_err(err(ErrorCode::refused, "path contains .."));
                }
            }

            if (files_scanned >= max_files) {
                capped = true;
                cap_reason = "maxFiles";
                break;
            }

            std::error_code sz_ec;
            const auto sz = std::filesystem::file_size(entry.path(), sz_ec);
            const std::uint64_t file_bytes = sz_ec ? 0ull : static_cast<std::uint64_t>(sz);
            if (bytes_scanned + file_bytes > max_bytes) {
                capped = true;
                cap_reason = "maxTotalBytes";
                break;
            }

            ++files_scanned;
            bytes_scanned += file_bytes;
            const auto path_s = entry.path().string();

            json file_row{{"path", path_s}, {"bytes", file_bytes}};

            if (file_bytes == 0) {
                file_row["status"] = "empty";
                file_row["message"] = "zero-byte package";
                files_arr.push_back(file_row);
                issues_arr.push_back({{"path", path_s},
                                      {"kind", "empty"},
                                      {"message", "zero-byte package"},
                                      {"bytes", file_bytes}});
                continue;
            }

            auto opened = Package::open(entry.path(), false);
            if (!opened) {
                std::string kind = "unreadable";
                std::string message = opened.error().message;
                if (opened.error().code == ErrorCode::corrupt ||
                    opened.error().code == ErrorCode::refpack) {
                    kind = "corrupt";
                } else if (opened.error().code == ErrorCode::unsupported_game_or_format ||
                           opened.error().code == ErrorCode::protected_or_encrypted) {
                    kind = "wrong_game";
                    auto hint = sniff_wrong_game(entry.path());
                    if (!hint.empty()) {
                        message = hint;
                        file_row["gameHint"] = hint;
                    }
                } else if (opened.error().code == ErrorCode::io) {
                    kind = "unreadable";
                } else {
                    kind = "unreadable";
                }
                file_row["status"] = kind;
                file_row["message"] = message;
                files_arr.push_back(file_row);
                json issue{{"path", path_s},
                           {"kind", kind},
                           {"message", message},
                           {"bytes", file_bytes}};
                if (file_row.contains("gameHint")) {
                    issue["gameHint"] = file_row["gameHint"];
                }
                issues_arr.push_back(std::move(issue));
                continue;
            }

            ++ok_count;
            file_row["status"] = "ok";
            file_row["resourceCount"] = opened->count();
            files_arr.push_back(file_row);

            std::unordered_set<DupKey, DupHash> seen_in_file;
            for (std::uint32_t i = 0; i < opened->count(); ++i) {
                const auto& e = opened->entry(i);
                DupKey key{e.tgi.type, e.tgi.group, e.tgi.instance};
                if (!seen_in_file.insert(key).second) {
                    continue;  // same TGI twice in one file is legal (ordinals)
                }
                tgi_files[key].push_back(path_s);
            }
        }

        std::vector<std::pair<DupKey, std::vector<std::string>>> dup_list;
        for (auto& [key, paths] : tgi_files) {
            if (paths.size() >= 2) {
                dup_list.emplace_back(key, std::move(paths));
            }
        }
        std::sort(dup_list.begin(), dup_list.end(),
                  [](const auto& a, const auto& b) {
                      if (a.first.type != b.first.type) {
                          return a.first.type < b.first.type;
                      }
                      if (a.first.group != b.first.group) {
                          return a.first.group < b.first.group;
                      }
                      return a.first.instance < b.first.instance;
                  });

        const std::uint32_t dup_total = static_cast<std::uint32_t>(dup_list.size());
        const bool dups_truncated = dup_total > max_dup_samples;
        json duplicates = json::array();
        const std::size_t sample_n =
            std::min<std::size_t>(dup_list.size(), static_cast<std::size_t>(max_dup_samples));
        for (std::size_t i = 0; i < sample_n; ++i) {
            const auto& key = dup_list[i].first;
            const auto& paths = dup_list[i].second;
            Tgi tgi{key.type, key.group, key.instance};
            auto row = tgi_json(tgi);
            row["fileCount"] = paths.size();
            json path_arr = json::array();
            const bool paths_trunc = paths.size() > max_paths_per_dup;
            const std::size_t path_n =
                std::min(paths.size(), static_cast<std::size_t>(max_paths_per_dup));
            for (std::size_t p = 0; p < path_n; ++p) {
                path_arr.push_back(paths[p]);
            }
            row["paths"] = std::move(path_arr);
            row["pathsTruncated"] = paths_trunc;
            duplicates.push_back(std::move(row));
        }

        json data{{"path", root->string()},
                  {"filesScanned", files_scanned},
                  {"bytesScanned", bytes_scanned},
                  {"okCount", ok_count},
                  {"issueCount", issues_arr.size()},
                  {"capped", capped},
                  {"cancelled", scan_cancelled},
                  {"files", files_arr},
                  {"issues", issues_arr},
                  {"duplicates", duplicates},
                  {"duplicateTgiCount", dup_total},
                  {"duplicatesTruncated", dups_truncated},
                  {"limits",
                   {{"maxFiles", max_files},
                    {"maxTotalBytes", max_bytes},
                    {"maxDuplicateSamples", max_dup_samples},
                    {"maxPathsPerDuplicate", max_paths_per_dup}}},
                  {"readOnly", true}};
        if (capped) {
            data["capReason"] = cap_reason;
        }
        if (report_progress) {
            emit_progress({{"command", cmd},
                           {"phase", scan_cancelled ? "cancelled" : "done"},
                           {"filesScanned", files_scanned},
                           {"cancelled", scan_cancelled}});
        }
        clear_cancel_state();
        data["summary"] = folder_scan_summary_json(data);
        if (scan_cancelled) {
            auto env = envelope_err(err(ErrorCode::refused, "cancelled"), false, "none");
            env["data"] = std::move(data);
            return env;
        }
        return envelope_ok(std::move(data));
    }
    if (cmd == "package.unmerge") {
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        auto outd = check_path(args.at("outDir").get<std::string>());
        if (!outd) {
            return envelope_err(outd.error());
        }
        auto src = Package::open(*path, false);
        if (!src) {
            return envelope_err(src.error());
        }
        std::optional<std::uint32_t> mi;
        for (std::uint32_t i = 0; i < src->count(); ++i) {
            if (src->entry(i).tgi.type == sxpe::resources::kSxmm) {
                mi = i;
                break;
            }
        }
        if (!mi) {
            return envelope_err(err(ErrorCode::refused,
                                    "not an SXPE merged package (no SXMM manifest)"));
        }
        auto body = src->uncompressed(*mi);
        if (!body) {
            return envelope_err(body.error());
        }
        json man;
        try {
            man = json::parse(std::string(reinterpret_cast<const char*>(body->data()), body->size()));
        } catch (const json::exception& e) {
            return envelope_err(err(ErrorCode::corrupt, std::string("manifest JSON: ") + e.what()));
        }
        if (man.value("format", "") != "sxpe.mergeManifest" || man.value("version", 0) < 1 ||
            !man.contains("sources") || !man["sources"].is_array()) {
            return envelope_err(err(ErrorCode::refused, "invalid SXPE merge manifest"));
        }
        std::error_code ec;
        std::filesystem::create_directories(*outd, ec);
        if (ec) {
            return envelope_err(err(ErrorCode::io, ec.message()));
        }
        json written = json::array();
        json warnings = json::array();
        json orphans = json::array();
        struct ListedKey {
            std::uint32_t type{0};
            std::uint32_t group{0};
            std::uint64_t instance{0};
            std::uint32_t ordinal{0};
            bool operator==(const ListedKey&) const = default;
        };
        struct ListedHash {
            std::size_t operator()(const ListedKey& k) const noexcept {
                std::size_t h = k.type;
                h ^= static_cast<std::size_t>(k.group) + 0x9e3779b9u + (h << 6) + (h >> 2);
                h ^= static_cast<std::size_t>(k.ordinal) + 0x9e3779b9u + (h << 6) + (h >> 2);
                h ^= static_cast<std::size_t>(k.instance) + 0x9e3779b9u + (h << 6) + (h >> 2);
                h ^= static_cast<std::size_t>(k.instance >> 32) + 0x9e3779b9u + (h << 6) + (h >> 2);
                return h;
            }
        };
        std::unordered_set<ListedKey, ListedHash> listed;
        auto pack_key = [](Tgi t, std::uint32_t ord) {
            return ListedKey{t.type, t.group, t.instance, ord};
        };
        int nsrc = 0;
        for (const auto& srcj : man["sources"]) {
            ++nsrc;
            auto raw_name = srcj.value("originalFileName", "source-" + std::to_string(nsrc) + ".package");
            auto fname_r = sanitize_unmerge_basename(std::move(raw_name), nsrc);
            if (!fname_r) {
                return envelope_err(fname_r.error());
            }
            const auto fname = *fname_r;
            auto dest = *outd / fname;
            if (std::filesystem::exists(dest) && !force(args)) {
                dest = *outd / (dest.stem().string() + "-" + std::to_string(nsrc) + dest.extension().string());
            }
            Package child = Package::create_new();
            int copied = 0;
            int skipped = 0;
            if (srcj.contains("resources") && srcj["resources"].is_array()) {
                for (const auto& r : srcj["resources"]) {
                    Tgi t = tgi_from(r);
                    std::uint32_t ord = r.contains("ordinal") ? static_cast<std::uint32_t>(as_u64(r["ordinal"])) : 0;
                    listed.insert(pack_key(t, ord));
                    auto idx = src->find(t, ord);
                    if (!idx) {
                        ++skipped;
                        warnings.push_back({{"file", fname},
                                            {"message", "listed TGI missing from merged package"},
                                            {"type", t.type},
                                            {"group", t.group},
                                            {"instance", t.instance},
                                            {"ordinal", ord}});
                        continue;
                    }
                    const auto typ = src->entry(*idx).tgi.type;
                    if (typ == sxpe::resources::kSxmm || typ == sxpe::resources::kDir) {
                        ++skipped;
                        continue;
                    }
                    if (typ == kNmap) {
                        Result<std::uint32_t> add = std::unexpected(err(ErrorCode::corrupt, "nmap"));
                        if (srcj.contains("nameMap") && srcj["nameMap"].is_object()) {
                            add = add_restored_nmap(child, srcj["nameMap"]);
                        } else {
                            auto sliced = slice_nmap_for_listed(*src, *idx, srcj.value("resources", json::array()));
                            if (!sliced) {
                                ++skipped;
                                warnings.push_back({{"file", fname}, {"message", sliced.error().message}});
                                continue;
                            }
                            auto body = sxpe::resources::write_nmap(*sliced);
                            if (!body) {
                                ++skipped;
                                warnings.push_back({{"file", fname}, {"message", body.error().message}});
                                continue;
                            }
                            add = child.add(src->entry(*idx).tgi, *body, false);
                        }
                        if (!add) {
                            ++skipped;
                            warnings.push_back({{"file", fname}, {"message", add.error().message}});
                            continue;
                        }
                        (void)*add;
                        ++copied;
                        continue;
                    }
                    auto add = copy_resource_through(child, *src, *idx);
                    if (!add) {
                        ++skipped;
                        warnings.push_back({{"file", fname}, {"message", add.error().message}});
                        continue;
                    }
                    (void)*add;
                    ++copied;
                }
            }
            auto sv = child.save_as(dest);
            if (!sv) {
                return envelope_err(sv.error());
            }
            written.push_back({{"path", dest.string()}, {"copied", copied}, {"skipped", skipped}});
        }
        for (std::uint32_t i = 0; i < src->count(); ++i) {
            const auto& e = src->entry(i);
            if (e.tgi.type == sxpe::resources::kSxmm || e.tgi.type == sxpe::resources::kDir) {
                continue;
            }
            if (!listed.count(pack_key(e.tgi, e.ordinal))) {
                orphans.push_back(rid_json(e.tgi, e.ordinal));
            }
        }
        if (!orphans.empty()) {
            warnings.push_back({{"message", "orphan resources not listed in SXMM"},
                                {"count", orphans.size()},
                                {"resources", orphans}});
        }
        return envelope_ok({{"packagesWritten", written.size()},
                            {"packages", written},
                            {"warnings", warnings},
                            {"orphans", orphans}});
    }
    if (cmd == "s3sa.wrap") {
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        auto pe = read_file(*path);
        if (!pe) {
            return envelope_err(pe.error());
        }
        auto wrapped = sxpe::resources::wrap_s3sa_v1(*pe);
        if (!wrapped) {
            return envelope_err(wrapped.error());
        }
        if (args.contains("out") || args.contains("pathOut")) {
            const auto dests = args.contains("out") ? args.at("out").get<std::string>()
                                                    : args.at("pathOut").get<std::string>();
            auto dest = check_path(dests);
            if (!dest) {
                return envelope_err(dest.error());
            }
            if (std::filesystem::exists(*dest) && !force(args)) {
                return envelope_err(err(ErrorCode::refused, "exists; pass force"));
            }
            if (auto w = write_file(*dest, *wrapped); !w) {
                return envelope_err(w.error());
            }
            return envelope_ok({{"path", dest->string()}, {"bytes", wrapped->size()}});
        }
        return envelope_ok({{"bytes", wrapped->size()}, {"payloadB64", b64_encode(*wrapped)}});
    }
    if (cmd == "hash.fnv") {
        const auto text = args.at("text").get<std::string>();
        const bool clip = args.value("clip", false);
        const int width = args.value("width", clip ? 64 : 32);
        json data{{"text", text}, {"width", width}};
        if (clip) {
            data["value"] = sxpe::games::sims3::fnv64_clip(text);
            data["kind"] = "clip";
        } else if (width == 64) {
            data["value"] = sxpe::games::sims3::fnv1_64(text, true);
        } else {
            data["value"] = sxpe::games::sims3::fnv1_32(text, true);
        }
        return envelope_ok(data);
    }
    if (cmd == "handler.list" || cmd == "editor.list") {
        json arr = json::array();
        for (const auto& t : sxpe::resources::kTypes) {
            arr.push_back({{"id", t.id}, {"tag", t.tag}, {"name", t.name}, {"headless", true}});
        }
        return envelope_ok({{"handlers", arr}});
    }
    if (cmd == "settings.get") {
        return envelope_ok({{"schemaVersion", 1},
                            {"features",
                             {{"simCity5Create", false},
                              {"directXTex", false},
                              {"ddsDecode", true},
                              {"ddsExport", true},
                              {"ddsReplace", true},
                              {"mcpHttp", false}}},
                            {"mruMax", 12}});
    }
    if (cmd == "package.new" || (cmd == "session.start" && !args.contains("path"))) {
        auto idr = add_session(Package::create_new());
        if (!idr) {
            return envelope_err(idr.error());
        }
        return envelope_ok({{"sessionId", *idr}, {"game", "sims3"}, {"indexCount", 0}});
    }
    if (cmd == "package.open" || cmd == "session.start") {
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        bool wr = args.value("writable", false);
        const bool force_wr = args.value("forceWritable", false);
        std::error_code fec;
        const auto file_bytes = std::filesystem::file_size(*path, fec);
        bool demoted = false;
        if (wr && !force_wr && !fec && file_bytes >= sxpe::core::caps::kOpenReadOnlyBytes) {
            wr = false;
            demoted = true;
        }
        if (auto* existing = find_by_path(*path)) {
            auto out = info(*existing);
            out["alreadyOpen"] = true;
            return envelope_ok(out);
        }
        const auto t0 = std::chrono::steady_clock::now();
        auto p = Package::open(*path, wr);
        if (!p) {
            return envelope_err(p.error(), p.error().code == ErrorCode::io, "none");
        }
        const auto open_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - t0)
                .count());
        auto idr = add_session(std::move(*p));
        if (!idr) {
            return envelope_err(idr.error());
        }
        auto* s = find(*idr);
        auto out = info(*s);
        out["openMs"] = open_ms;
        out["openedReadOnlyDueToSize"] = demoted;
        out["readOnlyThresholdBytes"] = sxpe::core::caps::kOpenReadOnlyBytes;
        out["holdsExclusiveLock"] = s->pkg.holds_exclusive_lock();
        if (!fec) {
            out["fileBytes"] = file_bytes;
        }
        json warnings = json::array();
        // Optional #68: Mods tree + cannot take exclusive lock → actionable warning (still open).
        if (sxpe::core::looks_like_ea_mods_path(*path) && !s->pkg.holds_exclusive_lock()) {
            auto probe = sxpe::core::probe_exclusive_write(*path);
            if (probe.status == sxpe::core::LockProbeStatus::locked ||
                probe.status == sxpe::core::LockProbeStatus::access_denied) {
                warnings.push_back(sxpe::core::mods_path_lock_warning());
            }
        }
        out["warnings"] = warnings;
        out["suggestedCommands"] = json::array(
            {"resource.list --session " + *idr + " --limit 100"});
        return envelope_ok(out);
    }
    if (cmd == "package.close" || cmd == "session.stop") {
        auto s = require(args);
        if (!s) {
            return envelope_err(s.error());
        }
        const auto sid = (*s)->id;
        sessions.erase(std::remove_if(sessions.begin(), sessions.end(),
                                      [&](const auto& x) { return x->id == sid; }),
                       sessions.end());
        return envelope_ok({{"closed", sid}});
    }

    bool known = false;
    for (const auto& t : catalog) {
        if (t.id == cmd) {
            known = true;
            break;
        }
    }
    if (!known) {
        return envelope_err(err(ErrorCode::invalid_argument, "unknown command: '" + cmd + "'"));
    }

    auto sr = require(args);
    if (!sr) {
        return envelope_err(sr.error());
    }
    Session& s = **sr;
    for (const auto& t : catalog) {
        if (t.id == cmd) {
            if (!t.read_only) {
                s.invalidate_names();
            }
            break;
        }
    }

    if (cmd == "package.info") {
        return envelope_ok(info(s));
    }
    if (cmd == "package.validate") {
        json issues = json::array();
        if (s.pkg.major() != 2) {
            issues.push_back("major");
        }
        json dir = json::object();
        dir["present"] = false;
        json conflict_hotspots = json::array();
        std::map<std::tuple<std::uint32_t, std::uint32_t, std::uint64_t>, std::uint32_t> tgi_counts;
        bool saw_leftover = false;
        bool saw_dup = false;
        for (std::uint32_t i = 0; i < s.pkg.count(); ++i) {
            const auto& e = s.pkg.entry(i);
            const auto key = std::make_tuple(e.tgi.type, e.tgi.group, e.tgi.instance);
            ++tgi_counts[key];
            if (sxpe::resources::is_leftover_manifest_tgi(e.tgi.type, e.tgi.group, e.tgi.instance)) {
                saw_leftover = true;
                conflict_hotspots.push_back(
                    {{"kind", "leftover_manifest"},
                     {"type", e.tgi.type},
                     {"group", e.tgi.group},
                     {"instance", e.tgi.instance},
                     {"ordinal", e.ordinal},
                     {"reason", std::string(sxpe::resources::leftover_manifest_reason(
                                    e.tgi.type, e.tgi.instance))}});
            }
            if (e.tgi.type != sxpe::resources::kDir) {
                continue;
            }
            if (dir.value("present", false)) {
                continue;
            }
            dir["present"] = true;
            auto body = s.pkg.uncompressed(i);
            if (!body) {
                issues.push_back("dir_unreadable");
                continue;
            }
            auto parsed = sxpe::resources::parse_dir(*body);
            if (!parsed) {
                issues.push_back("dir_corrupt");
                continue;
            }
            dir["records"] = parsed->size();
            dir["recordBytes"] = body->size() % 20 == 0 ? 20 : 16;
            std::uint32_t missing = 0;
            for (const auto& d : *parsed) {
                bool found = false;
                for (std::uint32_t j = 0; j < s.pkg.count(); ++j) {
                    const auto& ee = s.pkg.entry(j);
                    if (ee.tgi.type == d.tgi.type && ee.tgi.group == d.tgi.group &&
                        ee.tgi.instance == d.tgi.instance && ee.mem_size == d.mem_size) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ++missing;
                }
            }
            dir["unmatched"] = missing;
            if (missing) {
                issues.push_back("dir_unmatched");
            }
        }
        for (const auto& [key, cnt] : tgi_counts) {
            if (cnt < 2) {
                continue;
            }
            // NMAP concat can leave a single TGI; multi-ordinal elsewhere is a hotspot.
            const auto [t, g, inst] = key;
            if (t == kNmap) {
                continue;
            }
            saw_dup = true;
            conflict_hotspots.push_back({{"kind", "duplicate_tgi"},
                                         {"type", t},
                                         {"group", g},
                                         {"instance", inst},
                                         {"count", cnt},
                                         {"reason", "same TGI appears with multiple ordinals"}});
        }
        if (saw_leftover) {
            issues.push_back("leftover_manifest");
        }
        if (saw_dup) {
            issues.push_back("duplicate_tgi");
        }
        const bool valid = issues.empty();
        const bool locked = s.pkg.layout_locked();
        const auto kind = s.pkg.path_kind();
        return envelope_ok({{"ok", valid},
                            {"issues", issues},
                            {"indexCount", s.pkg.count()},
                            {"dir", dir},
                            {"conflictHotspots", conflict_hotspots},
                            {"layoutLocked", locked},
                            {"pathKind", kind},
                            {"summary", validate_summary_json(valid, s.pkg.count(), dir, issues,
                                                              locked, kind, conflict_hotspots)}});
    }
    if (cmd == "package.save" || cmd == "package.compact") {
        if (cmd == "package.compact" && neighborhood_file(s.pkg.path())) {
            return envelope_err(err(ErrorCode::refused,
                                    "neighborhood / world layout lock: compact rebuilds the file and is not supported for .nhd/.world/.dbc"));
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"path", s.pkg.path().string()}});
        }
        auto r = s.pkg.save();
        if (!r) {
            return envelope_err(r.error(), true, "none");
        }
        return envelope_ok({{"path", s.pkg.path().string()}, {"indexCount", s.pkg.count()}});
    }
    if (cmd == "package.saveAs" || cmd == "package.saveCopyAs") {
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        if (std::filesystem::exists(*path) && !force(args)) {
            return envelope_err(err(ErrorCode::refused, "exists; pass force"), false);
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"path", path->string()}});
        }
        auto r = (cmd == "package.saveCopyAs") ? s.pkg.save_copy_as(*path) : s.pkg.save_as(*path);
        if (!r) {
            return envelope_err(r.error(), true, "none");
        }
        return envelope_ok({{"path", path->string()}});
    }
    if (cmd == "resource.list") {
        const auto& names = name_index(s);
        json filter = args.value("filter", json::object());
        std::uint32_t limit = args.value("limit", kListDefault);
        if (limit == 0 || limit > kListMax) {
            limit = kListMax;
        }
        std::uint32_t start = 0;
        if (args.contains("cursor") && args["cursor"].is_string() &&
            !args["cursor"].get<std::string>().empty()) {
            start = static_cast<std::uint32_t>(std::stoul(args["cursor"].get<std::string>()));
        }
        json items = json::array();
        std::uint32_t match = 0;
        bool more = false;
        std::uint32_t i = 0;
        for (; i < s.pkg.count(); ++i) {
            const auto& e = s.pkg.entry(i);
            if (filter.contains("type") && as_u64(filter["type"]) != e.tgi.type) {
                continue;
            }
            if (filter.contains("group") && as_u64(filter["group"]) != e.tgi.group) {
                continue;
            }
            if (filter.contains("instance") && as_u64(filter["instance"]) != e.tgi.instance) {
                continue;
            }
            if (filter.contains("compressed") &&
                filter["compressed"].get<bool>() != (e.compressed == 0xFFFF)) {
                continue;
            }
            if (filter.contains("deleted") && filter["deleted"].get<bool>() != s.pkg.deleted(i)) {
                continue;
            }
            const auto tag = std::string(sxpe::resources::tag_for(e.tgi.type));
            if (filter.contains("tag")) {
                auto want = filter["tag"].get<std::string>();
                if (!want.empty() && want.front() == '_') {
                    /* keep */
                }
                if (tag != want && tag != "_" + want && want != tag) {
                    bool ok = tag.find(want) != std::string::npos;
                    if (want == "IMG" && tag == "_IMG") {
                        ok = true;
                    }
                    if (!ok) {
                        continue;
                    }
                }
            }
            if (filter.contains("nameContains")) {
                auto it = names.find(e.tgi.instance);
                const auto& nm = it == names.end() ? std::string{} : it->second;
                auto sub = filter["nameContains"].get<std::string>();
                if (nm.find(sub) == std::string::npos) {
                    continue;
                }
            }
            if (match++ < start) {
                continue;
            }
            if (items.size() >= limit) {
                more = true;
                break;
            }
            items.push_back(item_meta(s.pkg, i, names));
        }
        json data{{"items", items},
                  {"truncated", more},
                  {"nextCursor", more ? std::to_string(start + static_cast<std::uint32_t>(items.size()))
                                      : ""}};
        return envelope_ok(data);
    }

    auto need_idx = [&]() -> Result<std::uint32_t> { return idx(s, args); };

    if (cmd == "resource.read") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        const auto& names = name_index(s);
        json data = item_meta(s.pkg, *i, names);
        if (args.value("includePayload", false)) {
            const auto& e = s.pkg.entry(*i);
            if (e.mem_size > sxpe::core::caps::kMaxResourceBytes) {
                return envelope_err(
                    err(ErrorCode::cap_exceeded,
                        "resource exceeds decode cap (" + std::to_string(e.mem_size) +
                            " bytes); refuse includePayload — metadata only"));
            }
            std::uint32_t maxb = args.value("maxBytes", 0);
            if (maxb == 0 || maxb > kPayloadCap) {
                maxb = kPayloadCap;
            }
            // peek: uncompressed resources are mmap-sliced; compressed refuse above live preview.
            auto body = s.pkg.peek(*i, maxb);
            if (!body) {
                return envelope_err(body.error());
            }
            data["payloadB64"] = b64_encode(*body);
            data["truncated"] = e.mem_size > maxb || body->size() >= maxb;
            data["bytes"] = body->size();
        }
        return envelope_ok(data);
    }
    if (cmd == "resource.export" || cmd == "resource.extract" || cmd == "dds.export" ||
        cmd == "vid.export") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        if (std::filesystem::exists(*path) && !force(args)) {
            return envelope_err(err(ErrorCode::refused, "exists; pass force"));
        }
        Result<std::vector<std::byte>> body =
            args.value("raw", false) ? [&]() -> Result<std::vector<std::byte>> {
            auto r = s.pkg.raw(*i);
            if (!r) {
                return std::unexpected(r.error());
            }
            return std::vector<std::byte>(r->begin(), r->end());
        }()
                                     : s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        if (auto w = write_file(*path, *body); !w) {
            return envelope_err(w.error(), true);
        }
        return envelope_ok({{"path", path->string()}, {"bytes", body->size()}});
    }
    if (cmd == "resource.add") {
        Tgi t = tgi_from(args.at("resourceId"));
        auto raw = payload_from_args(args);
        if (!raw) {
            return envelope_err(raw.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"wouldAdd", tgi_json(t)}});
        }
        auto r = s.pkg.add(t, *raw, args.value("compress", false));
        if (!r) {
            return envelope_err(r.error());
        }
        UndoItem u;
        u.kind = "remove";
        u.index = *r;
        push_undo(s, std::move(u));
        return envelope_ok({{"resourceId", rid_json(s.pkg.entry(*r).tgi, s.pkg.entry(*r).ordinal)}});
    }
    if (cmd == "resource.delete") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"index", *i}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        s.undo.back().kind = "insert";
        auto r = s.pkg.remove(*i);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"deleted", true}});
    }
    if (cmd == "resource.duplicate") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}});
        }
        auto r = s.pkg.duplicate(*i);
        if (!r) {
            return envelope_err(r.error());
        }
        UndoItem u;
        u.kind = "remove";
        u.index = *r;
        push_undo(s, std::move(u));
        return envelope_ok({{"resourceId", rid_json(s.pkg.entry(*r).tgi, s.pkg.entry(*r).ordinal)}});
    }
    if (cmd == "resource.replace") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto raw = payload_from_args(args);
        if (!raw) {
            return envelope_err(raw.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"bytes", raw->size()}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        bool compress = args.value("compress", s.pkg.entry(*i).compressed == 0xFFFF);
        auto r = s.pkg.set_uncompressed(*i, *raw, compress);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"bytes", raw->size()}});
    }
    if (cmd == "resource.replaceInPlace") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        auto bytes = read_file(*path);
        if (!bytes) {
            return envelope_err(bytes.error());
        }
        const auto neu = sxpe::resources::parse_png_ihdr(*bytes);
        if (!neu) {
            return envelope_err(err(ErrorCode::invalid_argument, "not a PNG (need 8-bit RGBA)"));
        }
        if (neu->bit_depth != 8 || neu->color_type != 6) {
            return envelope_err(err(ErrorCode::invalid_argument,
                                    "PNG must be 8-bit RGBA (color type 6), same as game SNAPs"));
        }
        auto cur = s.pkg.uncompressed(*i);
        if (!cur) {
            return envelope_err(cur.error());
        }
        if (const auto old = sxpe::resources::parse_png_ihdr(*cur)) {
            if (old->width != neu->width || old->height != neu->height) {
                return envelope_err(err(ErrorCode::invalid_argument,
                                        "PNG size must stay " + std::to_string(old->width) + "x" +
                                            std::to_string(old->height)));
            }
        }
        const bool compress = s.pkg.entry(*i).compressed == 0xFFFF;
        if (dry(args)) {
            return envelope_ok({{"dryRun", true},
                                {"bytes", bytes->size()},
                                {"width", neu->width},
                                {"height", neu->height},
                                {"hole", s.pkg.entry(*i).payload_capacity},
                                {"compress", compress}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        s.undo.back().kind = "inplace";
        auto r = s.pkg.patch_in_place(*i, *bytes, compress);
        if (!r) {
            s.undo.pop_back();
            return envelope_err(r.error());
        }
        return envelope_ok({{"bytes", bytes->size()},
                            {"width", neu->width},
                            {"height", neu->height},
                            {"inPlace", true}});
    }
    if (cmd == "resource.setFlags") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        if (args.contains("deleted")) {
            auto r = s.pkg.set_deleted(*i, args["deleted"].get<bool>());
            if (!r) {
                return envelope_err(r.error());
            }
        }
        if (args.contains("compressed")) {
            auto body = s.pkg.uncompressed(*i);
            if (!body) {
                return envelope_err(body.error());
            }
            auto r = s.pkg.set_uncompressed(*i, *body, args["compressed"].get<bool>());
            if (!r) {
                return envelope_err(r.error());
            }
        }
        return envelope_ok({{"compressed", s.pkg.entry(*i).compressed == 0xFFFF},
                            {"deleted", s.pkg.deleted(*i)}});
    }
    if (cmd == "resource.rekey") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        Tgi nw = tgi_from(args.at("newId"));
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"newId", tgi_json(nw)}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        auto r = s.pkg.rekey(*i, nw);
        if (!r) {
            return envelope_err(r.error());
        }
        const auto& e = s.pkg.entry(*i);
        return envelope_ok({{"resourceId", rid_json(e.tgi, e.ordinal)}});
    }
    if (cmd == "resource.copy") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        if (body->size() > 32u << 20) {
            return envelope_err(err(ErrorCode::cap_exceeded, "paste cap 32 MiB"));
        }
        json item = rid_json(s.pkg.entry(*i).tgi, 0);
        item["payloadB64"] = b64_encode(*body);
        item["compressed"] = s.pkg.entry(*i).compressed == 0xFFFF;
        if (args.value("append", false) && s.clipboard.is_array()) {
            s.clipboard.push_back(item);
        } else {
            s.clipboard = json::array({item});
        }
        return envelope_ok({{"copied", s.clipboard.size()}});
    }
    if (cmd == "resource.paste") {
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"count", s.clipboard.size()}});
        }
        json added = json::array();
        for (auto& it : s.clipboard) {
            Tgi t = tgi_from(it);
            auto raw = b64_decode(it.at("payloadB64").get<std::string>());
            if (!raw) {
                return envelope_err(raw.error());
            }
            auto r = s.pkg.add(t, *raw, it.value("compressed", false));
            if (!r) {
                return envelope_err(r.error());
            }
            added.push_back(rid_json(s.pkg.entry(*r).tgi, s.pkg.entry(*r).ordinal));
        }
        return envelope_ok({{"items", added}});
    }
    if (cmd == "resource.importFiles") {
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        auto bytes = read_file(*path);
        if (!bytes) {
            return envelope_err(bytes.error());
        }
        Tgi t{};
        std::string nm;
        if (args.contains("resourceId")) {
            t = tgi_from(args["resourceId"]);
        } else if (!parse_community_name(path->filename().string(), t, nm)) {
            return envelope_err(err(ErrorCode::invalid_argument, "need resourceId or S3_ filename"));
        }
        std::uint32_t ordinal = 0;
        if (args.contains("resourceId") && args["resourceId"].is_object()) {
            ordinal = args["resourceId"].value("ordinal", 0u);
        }
        auto existing = s.pkg.find(t, ordinal);
        if (existing && !force(args)) {
            return envelope_err(err(ErrorCode::refused, "exists; pass force"));
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"resourceId", tgi_json(t)}});
        }
        if (existing) {
            snapshot(s, *existing);
            auto r = s.pkg.set_uncompressed(*existing, *bytes, args.value("compress", false));
            if (!r) {
                return envelope_err(r.error());
            }
            return envelope_ok({{"replaced", true}, {"resourceId", rid_json(t, 0)}});
        }
        auto r = s.pkg.add(t, *bytes, args.value("compress", false));
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"resourceId", rid_json(s.pkg.entry(*r).tgi, s.pkg.entry(*r).ordinal)}});
    }
    if (cmd == "resource.importPackage" || cmd == "resource.importDbc") {
        std::vector<std::string> paths;
        if (args.contains("paths") && args["paths"].is_array()) {
            for (const auto& p : args["paths"]) {
                if (p.is_string()) {
                    paths.push_back(p.get<std::string>());
                }
            }
        }
        if (args.contains("path") && args["path"].is_string()) {
            paths.push_back(args["path"].get<std::string>());
        }
        if (paths.empty()) {
            return envelope_err(err(ErrorCode::invalid_argument, "need path or paths"));
        }
        auto merge_u32 = [&](const char* key, std::uint32_t def) -> std::uint32_t {
            if (!args.contains(key)) {
                return def;
            }
            const auto v = as_u64(args.at(key));
            if (v > 0xFFFFFFFFull) {
                return def;
            }
            return static_cast<std::uint32_t>(v);
        };
        auto merge_u64 = [&](const char* key, std::uint64_t def) -> std::uint64_t {
            if (!args.contains(key)) {
                return def;
            }
            return as_u64(args.at(key));
        };
        const auto max_packages =
            merge_u32("maxPackages", sxpe::core::caps::kMergeMaxPackages);
        const auto max_total_bytes =
            merge_u64("maxTotalBytes", sxpe::core::caps::kMergeMaxTotalBytes);
        const auto max_resources =
            merge_u32("maxResources", sxpe::core::caps::kMergeMaxResources);
        const bool report_progress = args.value("reportProgress", true);
        const bool checkpoint_between = args.value("checkpointBetweenPackages", false);
        std::optional<std::filesystem::path> checkpoint_path;
        if (args.contains("checkpointPath") && args["checkpointPath"].is_string()) {
            auto cp = check_path(args["checkpointPath"].get<std::string>());
            if (!cp) {
                return envelope_err(cp.error());
            }
            checkpoint_path = *cp;
        }
        if (checkpoint_between && !checkpoint_path) {
            return envelope_err(err(ErrorCode::invalid_argument,
                                    "checkpointBetweenPackages requires checkpointPath "
                                    "(explicit save; SXPE never autosaves mid-merge)"));
        }
        if (paths.size() > max_packages) {
            return envelope_err(
                err(ErrorCode::cap_exceeded,
                    "too many packages (" + std::to_string(paths.size()) + " > maxPackages " +
                        std::to_string(max_packages) + "); split the job"));
        }
        // Preflight on-disk sizes so we refuse before copying into RAM.
        std::uint64_t total_bytes = 0;
        for (const auto& rawp : paths) {
            auto path = check_path(rawp);
            if (!path) {
                continue;
            }
            std::error_code ec;
            const auto sz = std::filesystem::file_size(*path, ec);
            if (!ec) {
                total_bytes += sz;
            }
        }
        if (total_bytes > max_total_bytes) {
            return envelope_err(
                err(ErrorCode::cap_exceeded,
                    "total input bytes (" + std::to_string(total_bytes) +
                        ") exceed maxTotalBytes (" + std::to_string(max_total_bytes) +
                        "); split the job"));
        }
        json packages = json::array();
        json errors = json::array();
        json sources = json::array();
        json progress_log = json::array();
        const auto baseline_count = s.pkg.count();
        const bool baseline_dirty = s.pkg.dirty();
        std::vector<MutSnap> mut_snaps;
        std::unordered_set<std::uint64_t> mut_seen;
        clear_cancel_state();
        const bool write_man = args.value("writeMergeManifest", false);
        std::string dir_policy;
        if (args.contains("dirPolicy") && args["dirPolicy"].is_string()) {
            dir_policy = args["dirPolicy"].get<std::string>();
        } else {
            dir_policy = write_man ? "strip" : "copy-through";
        }
        if (dir_policy != "strip" && dir_policy != "copy-through" && dir_policy != "rebuild") {
            return envelope_err(err(ErrorCode::invalid_argument,
                                    "dirPolicy must be strip, copy-through, or rebuild"));
        }
        if (dir_policy == "rebuild") {
            return envelope_err(err(ErrorCode::refused,
                                    "dirPolicy 'rebuild' is not yet implemented; use 'strip' or "
                                    "'copy-through'"));
        }
        std::string leftover_policy;
        if (args.contains("leftoverManifestPolicy") && args["leftoverManifestPolicy"].is_string()) {
            leftover_policy = args["leftoverManifestPolicy"].get<std::string>();
        } else {
            leftover_policy = "strip";  // auto-strip documented allowlist (issue #64)
        }
        if (leftover_policy != "strip" && leftover_policy != "keep" && leftover_policy != "warn") {
            return envelope_err(err(ErrorCode::invalid_argument,
                                    "leftoverManifestPolicy must be strip, keep, or warn"));
        }
        std::string dup_policy;
        if (args.contains("duplicateTgiPolicy") && args["duplicateTgiPolicy"].is_string()) {
            dup_policy = args["duplicateTgiPolicy"].get<std::string>();
        } else {
            dup_policy = force(args) ? "force" : "fail";
        }
        if (dup_policy != "force" && dup_policy != "skip" && dup_policy != "fail") {
            return envelope_err(err(ErrorCode::invalid_argument,
                                    "duplicateTgiPolicy must be force, skip, or fail"));
        }
        json stripped_leftovers = json::array();
        json duplicate_warnings = json::array();
        json hygiene_warnings = json::array();
        auto push_progress = [&](json ev) {
            if (!report_progress) {
                return;
            }
            progress_log.push_back(ev);
            emit_progress(ev);
        };
        const auto packages_total = static_cast<std::uint32_t>(paths.size());
        std::uint32_t imported = 0;
        std::uint32_t would = 0;
        int src_n = 0;
        std::uint32_t packages_done = 0;
        auto abort_cancelled = [&](const char* where) -> json {
            push_progress({{"command", cmd},
                           {"phase", "cancelled"},
                           {"where", where},
                           {"packagesDone", packages_done},
                           {"packagesTotal", packages_total},
                           {"imported", imported}});
            auto rb = rollback_import(s, baseline_count, baseline_dirty, mut_snaps);
            clear_cancel_state();
            if (!rb) {
                return envelope_err(
                    err(ErrorCode::corrupt,
                        std::string("cancelled but rollback failed: ") + rb.error().message),
                    false, "unknown");
            }
            auto env = envelope_err(err(ErrorCode::refused, "cancelled"), false, "none");
            env["data"] = {{"cancelled", true},
                           {"rolledBack", true},
                           {"packagesDone", packages_done},
                           {"packagesTotal", packages_total},
                           {"baselineCount", baseline_count},
                           {"indexCount", s.pkg.count()}};
            if (report_progress) {
                env["data"]["progress"] = std::move(progress_log);
            }
            return env;
        };
        push_progress({{"command", cmd},
                       {"phase", "start"},
                       {"packagesDone", 0},
                       {"packagesTotal", packages_total},
                       {"imported", 0},
                       {"totalInputBytes", total_bytes}});
        for (const auto& rawp : paths) {
            if (cancelled()) {
                return abort_cancelled("before_package");
            }
            auto path = check_path(rawp);
            if (!path) {
                errors.push_back({{"path", rawp}, {"message", path.error().message}});
                push_progress({{"command", cmd},
                               {"phase", "package"},
                               {"path", rawp},
                               {"ok", false},
                               {"packagesDone", packages_done},
                               {"packagesTotal", packages_total},
                               {"imported", imported},
                               {"message", path.error().message}});
                continue;
            }
            auto src = Package::open(*path, false);
            if (!src) {
                errors.push_back({{"path", path->string()}, {"message", src.error().message}});
                push_progress({{"command", cmd},
                               {"phase", "package"},
                               {"path", path->string()},
                               {"ok", false},
                               {"packagesDone", packages_done},
                               {"packagesTotal", packages_total},
                               {"imported", imported},
                               {"message", src.error().message}});
                continue;
            }
            would += src->count();
            if (dry(args)) {
                packages.push_back({{"path", path->string()}, {"count", src->count()}});
                ++packages_done;
                push_progress({{"command", cmd},
                               {"phase", "package"},
                               {"path", path->string()},
                               {"ok", true},
                               {"dryRun", true},
                               {"count", src->count()},
                               {"packagesDone", packages_done},
                               {"packagesTotal", packages_total},
                               {"imported", imported}});
                continue;
            }
            if (imported + src->count() > max_resources) {
                errors.push_back(
                    {{"path", path->string()},
                     {"message",
                      "resource count would exceed maxResources (" +
                          std::to_string(max_resources) + "); split the job"}});
                push_progress({{"command", cmd},
                               {"phase", "package"},
                               {"path", path->string()},
                               {"ok", false},
                               {"packagesDone", packages_done},
                               {"packagesTotal", packages_total},
                               {"imported", imported},
                               {"message", "maxResources"}});
                break;
            }
            std::uint32_t n = 0;
            bool file_ok = true;
            json recs = json::array();
            ++src_n;
            for (std::uint32_t i = 0; i < src->count(); ++i) {
                const auto t = src->entry(i).tgi;
                if (write_man && t.type == sxpe::resources::kSxmm) {
                    continue;
                }
                if (t.type == sxpe::resources::kDir && dir_policy == "strip") {
                    continue;
                }
                if (sxpe::resources::is_leftover_manifest_tgi(t.type, t.group, t.instance)) {
                    json hit{{"path", path->string()},
                             {"type", t.type},
                             {"group", t.group},
                             {"instance", t.instance},
                             {"reason", std::string(sxpe::resources::leftover_manifest_reason(
                                            t.type, t.instance))}};
                    if (leftover_policy == "strip") {
                        hit["action"] = "strip";
                        stripped_leftovers.push_back(hit);
                        continue;
                    }
                    if (leftover_policy == "warn") {
                        hit["action"] = "warn";
                        hygiene_warnings.push_back(hit);
                        // fall through and copy
                    }
                    // keep: copy silently
                }
                if (cancelled()) {
                    return abort_cancelled("mid_package");
                }
                auto ex = s.pkg.find(t, src->entry(i).ordinal);
                if (ex && t.type == kNmap) {
                    if (auto cap = capture_mut(s, *ex, mut_snaps, mut_seen); !cap) {
                        errors.push_back(
                            {{"path", path->string()}, {"message", cap.error().message}});
                        file_ok = false;
                        break;
                    }
                    auto wr = merge_nmap_from(s.pkg, *ex, *src, i);
                    if (!wr) {
                        errors.push_back(
                            {{"path", path->string()}, {"message", wr.error().message}});
                        file_ok = false;
                        break;
                    }
                    recs.push_back(rid_json(s.pkg.entry(*ex).tgi, s.pkg.entry(*ex).ordinal));
                    ++n;
                    continue;
                }
                if (ex) {
                    json dup{{"path", path->string()},
                             {"type", t.type},
                             {"group", t.group},
                             {"instance", t.instance},
                             {"ordinal", src->entry(i).ordinal}};
                    if (dup_policy == "fail") {
                        dup["action"] = "fail";
                        duplicate_warnings.push_back(dup);
                        errors.push_back({{"path", path->string()},
                                          {"message",
                                           "duplicate TGI; pass force or duplicateTgiPolicy=force|skip"}});
                        file_ok = false;
                        break;
                    }
                    if (dup_policy == "skip") {
                        dup["action"] = "skip";
                        duplicate_warnings.push_back(dup);
                        continue;
                    }
                    // force
                    dup["action"] = "force";
                    duplicate_warnings.push_back(dup);
                    if (auto cap = capture_mut(s, *ex, mut_snaps, mut_seen); !cap) {
                        errors.push_back(
                            {{"path", path->string()}, {"message", cap.error().message}});
                        file_ok = false;
                        break;
                    }
                    auto wr = replace_resource_through(s.pkg, *ex, *src, i);
                    if (!wr) {
                        errors.push_back(
                            {{"path", path->string()}, {"message", wr.error().message}});
                        file_ok = false;
                        break;
                    }
                    recs.push_back(rid_json(s.pkg.entry(*ex).tgi, s.pkg.entry(*ex).ordinal));
                } else {
                    auto r = copy_resource_through(s.pkg, *src, i);
                    if (!r) {
                        errors.push_back(
                            {{"path", path->string()}, {"message", r.error().message}});
                        file_ok = false;
                        break;
                    }
                    recs.push_back(rid_json(s.pkg.entry(*r).tgi, s.pkg.entry(*r).ordinal));
                }
                ++n;
            }
            if (file_ok) {
                imported += n;
                packages.push_back({{"path", path->string()}, {"imported", n}});
                json src_ent{{"id", "src-" + std::to_string(src_n)},
                             {"originalFileName", path->filename().string()},
                             {"resources", recs}};
                auto snap = snapshot_source_nmap(*src);
                if (snap.is_object() && !snap.empty()) {
                    src_ent["nameMap"] = std::move(snap);
                }
                sources.push_back(std::move(src_ent));
                ++packages_done;
                push_progress({{"command", cmd},
                               {"phase", "package"},
                               {"path", path->string()},
                               {"ok", true},
                               {"importedThisPackage", n},
                               {"packagesDone", packages_done},
                               {"packagesTotal", packages_total},
                               {"imported", imported}});
                // Explicit checkpoint: flush to disk and remap so overrides do not pile up.
                if (checkpoint_between && checkpoint_path) {
                    // SXMM is written once at end; mid-merge checkpoints are raw content only.
                    if (auto pin = pin_nmap_front(s.pkg); !pin) {
                        return envelope_err(pin.error());
                    }
                    if (auto sv = s.pkg.save_as(*checkpoint_path); !sv) {
                        errors.push_back({{"path", checkpoint_path->string()},
                                          {"message", "checkpoint failed: " + sv.error().message}});
                        push_progress({{"command", cmd},
                                       {"phase", "checkpoint"},
                                       {"path", checkpoint_path->string()},
                                       {"ok", false},
                                       {"packagesDone", packages_done},
                                       {"packagesTotal", packages_total},
                                       {"imported", imported},
                                       {"message", sv.error().message}});
                        break;
                    }
                    push_progress({{"command", cmd},
                                   {"phase", "checkpoint"},
                                   {"path", checkpoint_path->string()},
                                   {"ok", true},
                                   {"packagesDone", packages_done},
                                   {"packagesTotal", packages_total},
                                   {"imported", imported}});
                }
            } else {
                push_progress({{"command", cmd},
                               {"phase", "package"},
                               {"path", path->string()},
                               {"ok", false},
                               {"packagesDone", packages_done},
                               {"packagesTotal", packages_total},
                               {"imported", imported}});
            }
        }
        if (dry(args)) {
            clear_cancel_state();
            json out{{"dryRun", true},
                     {"packages", packages.size()},
                     {"count", would},
                     {"totalInputBytes", total_bytes},
                     {"maxPackages", max_packages},
                     {"maxTotalBytes", max_total_bytes},
                     {"maxResources", max_resources}};
            if (report_progress) {
                out["progress"] = std::move(progress_log);
            }
            return envelope_ok(std::move(out));
        }
        if (write_man && imported > 0) {
            json man{{"format", "sxpe.mergeManifest"},
                     {"version", 1},
                     {"sources", sources},
                     {"notes",
                      {{"forceOverwriteOnDuplicateTgi", force(args) || dup_policy == "force"},
                       {"duplicateTgiPolicy", dup_policy},
                       {"leftoverManifestPolicy", leftover_policy},
                       {"dirPolicy", dir_policy},
                       {"nmapPolicy", "concat"}}}};
            const auto dumped = man.dump();
            std::vector<std::byte> mb(dumped.size());
            for (std::size_t i = 0; i < dumped.size(); ++i) {
                mb[i] = static_cast<std::byte>(static_cast<unsigned char>(dumped[i]));
            }
            Tgi mt{};
            mt.type = sxpe::resources::kSxmm;
            mt.group = 0;
            mt.instance = 1;
            bool replaced = false;
            for (std::uint32_t i = 0; i < s.pkg.count(); ++i) {
                if (s.pkg.entry(i).tgi.type == sxpe::resources::kSxmm) {
                    if (auto wr = s.pkg.set_uncompressed(i, mb, false); !wr) {
                        errors.push_back({{"message", wr.error().message}});
                    }
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                if (auto addm = s.pkg.add(mt, mb, false); !addm) {
                    errors.push_back({{"message", addm.error().message}});
                }
            }
        }
        if (imported > 0) {
            if (auto pin = pin_nmap_front(s.pkg); !pin) {
                return envelope_err(pin.error());
            }
        }
        if (cancelled()) {
            return abort_cancelled("before_finalize");
        }
        push_progress({{"command", cmd},
                       {"phase", "done"},
                       {"packagesDone", packages_done},
                       {"packagesTotal", packages_total},
                       {"imported", imported},
                       {"failed", errors.size()}});
        clear_cancel_state();
        json out{{"imported", imported},
                 {"packages", packages.size()},
                 {"failed", errors.size()},
                 {"errors", errors},
                 {"mergeManifest", write_man},
                 {"dirPolicy", dir_policy},
                 {"leftoverManifestPolicy", leftover_policy},
                 {"duplicateTgiPolicy", dup_policy},
                 {"strippedLeftovers", stripped_leftovers},
                 {"duplicates", duplicate_warnings},
                 {"warnings", hygiene_warnings},
                 {"totalInputBytes", total_bytes},
                 {"maxPackages", max_packages},
                 {"maxTotalBytes", max_total_bytes},
                 {"maxResources", max_resources},
                 {"checkpointBetweenPackages", checkpoint_between}};
        if (checkpoint_path) {
            out["checkpointPath"] = checkpoint_path->string();
        }
        if (report_progress) {
            out["progress"] = std::move(progress_log);
        }
        if (imported == 0 && !errors.empty()) {
            return envelope_err(err(ErrorCode::refused, errors[0].value("message", "import failed")),
                                false);
        }
        return envelope_ok(std::move(out));
    }
    if (cmd == "resource.exportToPackage") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"path", path->string()}});
        }
        Package dest = Package::create_new();
        if (std::filesystem::exists(*path)) {
            auto o = Package::open(*path, true);
            if (!o) {
                return envelope_err(o.error());
            }
            dest = std::move(*o);
        }
        auto r = copy_resource_through(dest, s.pkg, *i);
        if (!r) {
            return envelope_err(r.error());
        }
        auto sv = dest.path().empty() ? dest.save_as(*path) : dest.save();
        if (!sv) {
            return envelope_err(sv.error());
        }
        return envelope_ok({{"path", path->string()}});
    }
    if (cmd == "stbl.get") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        auto t = sxpe::resources::parse_stbl(*body);
        if (!t) {
            return envelope_err(t.error());
        }
        json arr = json::array();
        for (const auto& e : t->entries) {
            arr.push_back({{"id", e.id}, {"text", e.text}});
        }
        return envelope_ok({{"version", t->version}, {"entries", arr}});
    }
    if (cmd == "stbl.set" || cmd == "stbl.delete") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        auto t = sxpe::resources::parse_stbl(*body);
        if (!t) {
            return envelope_err(t.error());
        }
        const auto sid = as_u64(args.at("id"));
        if (cmd == "stbl.set") {
            const auto text = args.at("text").get<std::string>();
            bool found = false;
            for (auto& e : t->entries) {
                if (e.id == sid) {
                    e.text = text;
                    found = true;
                    break;
                }
            }
            if (!found) {
                t->entries.push_back({sid, text});
            }
        } else {
            t->entries.erase(std::remove_if(t->entries.begin(), t->entries.end(),
                                            [&](const auto& e) { return e.id == sid; }),
                             t->entries.end());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"count", t->entries.size()}});
        }
        snapshot(s, *i);
        auto out = sxpe::resources::write_stbl(*t);
        if (!out) {
            return envelope_err(out.error());
        }
        auto r = s.pkg.set_uncompressed(*i, *out, s.pkg.entry(*i).compressed == 0xFFFF);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"count", t->entries.size()}});
    }
    if (cmd == "nmap.get" || cmd == "nmap.list") {
        sxpe::resources::Nmap names;
        if (args.contains("resourceId")) {
            auto i = need_idx();
            if (!i) {
                return envelope_err(i.error());
            }
            if (s.pkg.entry(*i).tgi.type != kNmap) {
                return envelope_err(err(ErrorCode::invalid_argument, "resourceId is not an NMAP"));
            }
            auto body = s.pkg.uncompressed(*i);
            if (!body) {
                return envelope_err(body.error());
            }
            auto n = sxpe::resources::parse_nmap(*body);
            if (!n) {
                return envelope_err(n.error());
            }
            names = std::move(*n);
        } else {
            names = load_nmap(s.pkg);
        }
        json arr = json::array();
        for (const auto& e : names.entries) {
            arr.push_back({{"instance", e.instance}, {"name", e.name}});
        }
        json dups = json::array();
        for (const auto& d : sxpe::resources::nmap_duplicates(names)) {
            dups.push_back({{"instance", d.instance},
                            {"count", d.count},
                            {"effectiveName", d.effective_name}});
        }
        return envelope_ok({{"entries", arr},
                            {"duplicates", dups},
                            {"duplicatePolicy", "last-wins"}});
    }
    if (cmd == "nmap.replace") {
        if (!args.contains("entries") || !args["entries"].is_array()) {
            return envelope_err(err(ErrorCode::invalid_argument, "entries must be an array"));
        }
        if (args["entries"].size() > sxpe::core::caps::kMaxTableEntries) {
            return envelope_err(err(ErrorCode::cap_exceeded, "nmap count"));
        }
        std::optional<std::uint32_t> ni;
        if (args.contains("resourceId")) {
            auto i = need_idx();
            if (!i) {
                return envelope_err(i.error());
            }
            if (s.pkg.entry(*i).tgi.type != kNmap) {
                return envelope_err(err(ErrorCode::invalid_argument, "resourceId is not an NMAP"));
            }
            ni = *i;
        } else {
            bool have = false;
            for (std::uint32_t i = 0; i < s.pkg.count(); ++i) {
                if (s.pkg.entry(i).tgi.type == kNmap) {
                    have = true;
                    break;
                }
            }
            if (!have && dry(args)) {
                return envelope_ok({{"dryRun", true},
                                    {"count", args["entries"].size()},
                                    {"wouldCreateMap", true}});
            }
            auto found = ensure_nmap_index(s.pkg);
            if (!found) {
                return envelope_err(found.error());
            }
            ni = *found;
        }
        sxpe::resources::Nmap n;
        n.version = 1;
        if (!dry(args)) {
            auto body = s.pkg.uncompressed(*ni);
            if (body) {
                if (auto cur = sxpe::resources::parse_nmap(*body)) {
                    n.version = cur->version ? cur->version : 1;
                }
            }
        }
        n.entries.reserve(args["entries"].size());
        for (const auto& row : args["entries"]) {
            if (!row.is_object() || !row.contains("instance") || !row.contains("name")) {
                return envelope_err(
                    err(ErrorCode::invalid_argument, "each entry needs instance and name"));
            }
            const auto name = row.at("name").get<std::string>();
            if (name.size() > sxpe::core::caps::kMaxNameBytes) {
                return envelope_err(err(ErrorCode::cap_exceeded, "nmap name"));
            }
            n.entries.push_back({as_u64(row.at("instance")), name});
        }
        json dups = json::array();
        for (const auto& d : sxpe::resources::nmap_duplicates(n)) {
            dups.push_back({{"instance", d.instance},
                            {"count", d.count},
                            {"effectiveName", d.effective_name}});
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true},
                                {"count", n.entries.size()},
                                {"duplicates", dups},
                                {"duplicatePolicy", "last-wins"}});
        }
        snapshot(s, *ni);
        auto out = sxpe::resources::write_nmap(n);
        if (!out) {
            return envelope_err(out.error());
        }
        auto r = s.pkg.set_uncompressed(*ni, *out, false);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"count", n.entries.size()},
                            {"duplicates", dups},
                            {"duplicatePolicy", "last-wins"}});
    }
    if (cmd == "nmap.delete") {
        const auto inst = as_u64(args.at("instance"));
        std::optional<std::uint32_t> ni;
        if (args.contains("resourceId")) {
            auto i = need_idx();
            if (!i) {
                return envelope_err(i.error());
            }
            if (s.pkg.entry(*i).tgi.type != kNmap) {
                return envelope_err(err(ErrorCode::invalid_argument, "resourceId is not an NMAP"));
            }
            ni = *i;
        } else {
            // Never create an NMAP from delete — only set/replace/rename may add maps.
            for (std::uint32_t i = 0; i < s.pkg.count(); ++i) {
                if (s.pkg.entry(i).tgi.type == kNmap) {
                    ni = i;
                    break;
                }
            }
            if (!ni) {
                json out{{"instance", inst}, {"removed", 0}};
                if (dry(args)) {
                    out["dryRun"] = true;
                }
                return envelope_ok(std::move(out));
            }
        }
        auto body = s.pkg.uncompressed(*ni);
        if (!body) {
            return envelope_err(body.error());
        }
        auto n = sxpe::resources::parse_nmap(*body);
        if (!n) {
            return envelope_err(n.error());
        }
        const auto before = n->entries.size();
        n->entries.erase(std::remove_if(n->entries.begin(), n->entries.end(),
                                        [&](const auto& e) { return e.instance == inst; }),
                         n->entries.end());
        const auto removed = before - n->entries.size();
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"instance", inst}, {"removed", removed}});
        }
        if (removed == 0) {
            return envelope_ok({{"instance", inst}, {"removed", 0}});
        }
        snapshot(s, *ni);
        auto out = sxpe::resources::write_nmap(*n);
        if (!out) {
            return envelope_err(out.error());
        }
        auto r = s.pkg.set_uncompressed(*ni, *out, false);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"instance", inst}, {"removed", removed}});
    }
    if (cmd == "nmap.set" || cmd == "resource.rename") {
        std::uint64_t inst = 0;
        if (cmd == "resource.rename") {
            auto i = need_idx();
            if (!i) {
                return envelope_err(i.error());
            }
            inst = s.pkg.entry(*i).tgi.instance;
        } else {
            inst = as_u64(args.at("instance"));
        }
        const auto name = args.at("name").get<std::string>();
        std::optional<std::uint32_t> ni;
        if (cmd == "nmap.set" && args.contains("resourceId")) {
            auto i = need_idx();
            if (!i) {
                return envelope_err(i.error());
            }
            if (s.pkg.entry(*i).tgi.type != kNmap) {
                return envelope_err(err(ErrorCode::invalid_argument, "resourceId is not an NMAP"));
            }
            ni = *i;
        } else {
            bool have = false;
            for (std::uint32_t i = 0; i < s.pkg.count(); ++i) {
                if (s.pkg.entry(i).tgi.type == kNmap) {
                    have = true;
                    break;
                }
            }
            if (!have && dry(args)) {
                return envelope_ok(
                    {{"dryRun", true}, {"instance", inst}, {"name", name}, {"wouldCreateMap", true}});
            }
            auto found = ensure_nmap_index(s.pkg);
            if (!found) {
                return envelope_err(found.error());
            }
            ni = *found;
        }
        auto body = s.pkg.uncompressed(*ni);
        if (!body) {
            return envelope_err(body.error());
        }
        auto n = sxpe::resources::parse_nmap(*body);
        if (!n) {
            return envelope_err(n.error());
        }
        int last = -1;
        for (std::size_t i = 0; i < n->entries.size(); ++i) {
            if (n->entries[i].instance == inst) {
                last = static_cast<int>(i);
            }
        }
        if (last >= 0) {
            n->entries[static_cast<std::size_t>(last)].name = name;
        } else {
            n->entries.push_back({inst, name});
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"instance", inst}, {"name", name}});
        }
        snapshot(s, *ni);
        auto out = sxpe::resources::write_nmap(*n);
        if (!out) {
            return envelope_err(out.error());
        }
        auto r = s.pkg.set_uncompressed(*ni, *out, false);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"instance", inst}, {"name", name}});
    }
    if (cmd == "dds.info" || cmd == "dds.decode") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        auto inf = sxpe::resources::parse_dds(*body);
        if (!inf) {
            return envelope_err(inf.error());
        }
        json data{{"width", inf->width},
                  {"height", inf->height},
                  {"format", inf->format},
                  {"compressed", inf->compressed},
                  {"cubemap", inf->cubemap},
                  {"volume", inf->volume},
                  {"mipmapCount", inf->mipmap_count},
                  {"decodeSupported", inf->decode_supported}};
        if (cmd == "dds.decode") {
            auto pix = sxpe::resources::decode_dds_rgba(*body);
            if (!pix) {
                return envelope_err(pix.error());
            }
            data["rgbaBytes"] = pix->size();
        }
        return envelope_ok(data);
    }
    if (cmd == "dds.replace") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        auto bytes = read_file(*path);
        if (!bytes) {
            return envelope_err(bytes.error());
        }
        auto inf = sxpe::resources::validate_dds_replace(*bytes);
        if (!inf) {
            return envelope_err(inf.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true},
                                {"bytes", bytes->size()},
                                {"width", inf->width},
                                {"height", inf->height},
                                {"format", inf->format}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        bool compress = args.value("compress", s.pkg.entry(*i).compressed == 0xFFFF);
        auto r = s.pkg.set_uncompressed(*i, *bytes, compress);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"bytes", bytes->size()},
                            {"width", inf->width},
                            {"height", inf->height},
                            {"format", inf->format}});
    }
    if (cmd == "objd.set" || cmd == "casp.set") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        const auto type = s.pkg.entry(*i).tgi.type;
        const bool compress = s.pkg.entry(*i).compressed == 0xFFFF;
        if (cmd == "objd.set" && type != sxpe::resources::kObjd) {
            return envelope_err(err(ErrorCode::invalid_argument, "resourceId is not an OBJD"));
        }
        if (cmd == "casp.set" && type != sxpe::resources::kCasp) {
            return envelope_err(err(ErrorCode::invalid_argument, "resourceId is not a CASP"));
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        if (cmd == "objd.set") {
            sxpe::resources::ObjdPatch patch;
            if (args.contains("nameGuid")) {
                patch.name_guid = as_u64(args.at("nameGuid"));
            }
            if (args.contains("descGuid")) {
                patch.desc_guid = as_u64(args.at("descGuid"));
            }
            if (args.contains("internalName") && args["internalName"].is_string()) {
                patch.internal_name = args["internalName"].get<std::string>();
            }
            if (args.contains("internalDesc") && args["internalDesc"].is_string()) {
                patch.internal_desc = args["internalDesc"].get<std::string>();
            }
            if (args.contains("price")) {
                const auto& pj = args.at("price");
                if (pj.is_number()) {
                    patch.price = pj.get<float>();
                } else if (pj.is_string()) {
                    patch.price = std::stof(pj.get<std::string>());
                }
            }
            if (args.contains("thumbIid")) {
                patch.thumb_iid = as_u64(args.at("thumbIid"));
            }
            if (args.contains("instanceName") && args["instanceName"].is_string()) {
                patch.instance_name = args["instanceName"].get<std::string>();
            }
            auto out = sxpe::resources::apply_objd(*body, patch);
            if (!out) {
                return envelope_err(out.error());
            }
            if (dry(args)) {
                return envelope_ok({{"dryRun", true}, {"bytes", out->size()}});
            }
            if (auto u = snapshot(s, *i); !u) {
                return envelope_err(u.error());
            }
            auto r = s.pkg.set_uncompressed(*i, *out, compress);
            if (!r) {
                return envelope_err(r.error());
            }
            auto parsed = sxpe::resources::parse_objd(*out);
            if (!parsed) {
                return envelope_err(parsed.error());
            }
            return envelope_ok({{"bytes", out->size()},
                                {"nameGuid", parsed->name_guid},
                                {"descGuid", parsed->desc_guid},
                                {"internalName", parsed->internal_name},
                                {"internalDesc", parsed->internal_desc},
                                {"price", parsed->price},
                                {"thumbIid", parsed->thumb_iid},
                                {"instanceName", parsed->instance_name}});
        }
        // casp.set
        sxpe::resources::CaspPatch patch;
        if (args.contains("name") && args["name"].is_string()) {
            patch.name = args["name"].get<std::string>();
        }
        if (args.contains("sortPriority")) {
            const auto& sj = args.at("sortPriority");
            if (sj.is_number()) {
                patch.sort_priority = sj.get<float>();
            } else if (sj.is_string()) {
                patch.sort_priority = std::stof(sj.get<std::string>());
            }
        }
        if (args.contains("clothingType")) {
            patch.clothing_type = static_cast<std::uint32_t>(as_u64(args.at("clothingType")));
        }
        if (args.contains("typeFlags")) {
            patch.type_flags = static_cast<std::uint32_t>(as_u64(args.at("typeFlags")));
        }
        if (args.contains("ageGender")) {
            patch.age_gender = static_cast<std::uint32_t>(as_u64(args.at("ageGender")));
        }
        if (args.contains("ageFlags")) {
            patch.age_flags = static_cast<std::uint8_t>(as_u64(args.at("ageFlags")));
        }
        if (args.contains("species")) {
            patch.species = static_cast<std::uint8_t>(as_u64(args.at("species")));
        }
        if (args.contains("genderFlags")) {
            patch.gender_flags = static_cast<std::uint8_t>(as_u64(args.at("genderFlags")));
        }
        if (args.contains("handedness")) {
            patch.handedness = static_cast<std::uint16_t>(as_u64(args.at("handedness")));
        }
        if (args.contains("clothingCategory")) {
            patch.clothing_category = static_cast<std::uint32_t>(as_u64(args.at("clothingCategory")));
        }
        if (args.contains("tgis")) {
            if (!args["tgis"].is_array()) {
                return envelope_err(err(ErrorCode::invalid_argument, "tgis must be an array"));
            }
            if (args["tgis"].size() > 255) {
                return envelope_err(err(ErrorCode::cap_exceeded, "casp tgi count"));
            }
            std::vector<sxpe::games::sims3::Tgi> rows;
            rows.reserve(args["tgis"].size());
            for (const auto& row : args["tgis"]) {
                rows.push_back(tgi_from(row));
            }
            patch.tgis = std::move(rows);
        }
        auto out = sxpe::resources::apply_casp(*body, patch);
        if (!out) {
            return envelope_err(out.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"bytes", out->size()}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        auto r = s.pkg.set_uncompressed(*i, *out, compress);
        if (!r) {
            return envelope_err(r.error());
        }
        auto parsed = sxpe::resources::parse_casp(*out);
        if (!parsed) {
            return envelope_err(parsed.error());
        }
        json tgi_rows = json::array();
        for (const auto& t : parsed->tgis) {
            tgi_rows.push_back(tgi_json(t));
        }
        return envelope_ok({{"bytes", out->size()},
                            {"name", parsed->name},
                            {"sortPriority", parsed->sort_priority},
                            {"clothingType", parsed->clothing_type},
                            {"typeFlags", parsed->type_flags},
                            {"ageGender", parsed->age_gender},
                            {"ageFlags", parsed->age_flags},
                            {"species", parsed->species},
                            {"genderFlags", parsed->gender_flags},
                            {"clothingCategory", parsed->clothing_category},
                            {"tgis", tgi_rows}});
    }
    if (cmd == "refs.set") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        const auto type = s.pkg.entry(*i).tgi.type;
        const bool compress = s.pkg.entry(*i).compressed == 0xFFFF;
        if (type != sxpe::resources::kRefs) {
            return envelope_err(err(ErrorCode::invalid_argument, "resourceId is not a REFS"));
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        if (!args.contains("entries") && !args.contains("indices")) {
            return envelope_err(err(ErrorCode::invalid_argument, "entries or indices required"));
        }
        sxpe::resources::RefsPatch patch;
        if (args.contains("entries")) {
            if (!args["entries"].is_array()) {
                return envelope_err(err(ErrorCode::invalid_argument, "entries must be an array"));
            }
            if (args["entries"].size() > sxpe::core::caps::kMaxTableEntries) {
                return envelope_err(err(ErrorCode::cap_exceeded, "refs entry count"));
            }
            std::vector<sxpe::resources::RefsEntry> rows;
            rows.reserve(args["entries"].size());
            for (const auto& row : args["entries"]) {
                sxpe::resources::RefsEntry e;
                e.tgi = tgi_from(row);
                if (row.contains("aux")) {
                    e.aux = static_cast<std::uint32_t>(as_u64(row.at("aux")));
                }
                rows.push_back(e);
            }
            patch.entries = std::move(rows);
        }
        if (args.contains("indices")) {
            if (!args["indices"].is_array()) {
                return envelope_err(err(ErrorCode::invalid_argument, "indices must be an array"));
            }
            if (args["indices"].size() > sxpe::core::caps::kMaxTableEntries) {
                return envelope_err(err(ErrorCode::cap_exceeded, "refs index count"));
            }
            std::vector<std::uint16_t> idxs;
            idxs.reserve(args["indices"].size());
            for (const auto& v : args["indices"]) {
                idxs.push_back(static_cast<std::uint16_t>(as_u64(v)));
            }
            patch.indices = std::move(idxs);
        }
        auto out = sxpe::resources::apply_refs(*body, patch);
        if (!out) {
            return envelope_err(out.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"bytes", out->size()}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        auto r = s.pkg.set_uncompressed(*i, *out, compress);
        if (!r) {
            return envelope_err(r.error());
        }
        auto parsed = sxpe::resources::parse_refs(*out);
        if (!parsed) {
            return envelope_err(parsed.error());
        }
        json entries = json::array();
        for (const auto& e : parsed->entries) {
            auto row = tgi_json(e.tgi);
            row["aux"] = e.aux;
            entries.push_back(std::move(row));
        }
        json indices = json::array();
        for (auto ix : parsed->indices) {
            indices.push_back(ix);
        }
        return envelope_ok({{"bytes", out->size()},
                            {"version", parsed->version},
                            {"entryCount", parsed->entries.size()},
                            {"entries", entries},
                            {"indices", indices},
                            {"auxIsDword", parsed->aux_is_dword},
                            {"hasThingy", parsed->has_thingy},
                            {"thingy", parsed->thingy}});
    }
    if (cmd == "rcol.replaceChunk") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        if (!args.contains("chunkIndex")) {
            return envelope_err(err(ErrorCode::invalid_argument, "chunkIndex required"));
        }
        const auto chunk_index = static_cast<std::uint32_t>(as_u64(args.at("chunkIndex")));
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        auto raw = payload_from_args(args);
        if (!raw) {
            return envelope_err(raw.error());
        }
        auto oldb = sxpe::resources::extract_rcol_chunk(*body, chunk_index);
        if (!oldb) {
            return envelope_err(oldb.error());
        }
        auto out = sxpe::resources::replace_rcol_chunk(*body, chunk_index, *raw);
        if (!out) {
            return envelope_err(out.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true},
                                {"chunkIndex", chunk_index},
                                {"oldBytes", oldb->size()},
                                {"newBytes", raw->size()},
                                {"resourceBytes", out->size()}});
        }
        bool backed_up = false;
        if (args.contains("backupPath") && args["backupPath"].is_string() &&
            !args["backupPath"].get<std::string>().empty()) {
            auto bp = check_path(args["backupPath"].get<std::string>());
            if (!bp) {
                return envelope_err(bp.error());
            }
            if (auto w = write_file(*bp, *oldb); !w) {
                return envelope_err(w.error());
            }
            backed_up = true;
        }
        const bool compress = s.pkg.entry(*i).compressed == 0xFFFF;
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        auto r = s.pkg.set_uncompressed(*i, *out, compress);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"chunkIndex", chunk_index},
                            {"oldBytes", oldb->size()},
                            {"newBytes", raw->size()},
                            {"bytes", out->size()},
                            {"backedUp", backed_up}});
    }
    if (cmd == "objk.get" || cmd == "vpxy.get" || cmd == "objd.get" || cmd == "casp.get" ||
        cmd == "refs.get" || cmd == "clip.info" || cmd == "rcol.summary" || cmd == "graph.get") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        const auto type = s.pkg.entry(*i).tgi.type;
        const auto tag = std::string(sxpe::resources::tag_for(type));
        if (cmd == "objk.get" || (cmd == "graph.get" && type == sxpe::resources::kObjk)) {
            auto o = sxpe::resources::parse_objk(*body);
            if (!o) {
                if (cmd == "objk.get") {
                    return envelope_err(o.error());
                }
            } else {
                json comps = json::array();
                json data = json::array();
                json nodes = json::array();
                nodes.push_back({{"id", "version"},
                                 {"label", "version"},
                                 {"valueKind", "u32"},
                                 {"value", o->version},
                                 {"children", json::array()}});
                for (auto c : o->components) {
                    comps.push_back(c);
                    nodes.push_back({{"id", "component/" + std::to_string(c)},
                                     {"label", "component"},
                                     {"valueKind", "u32"},
                                     {"value", c},
                                     {"children", json::array()}});
                }
                for (const auto& d : o->data) {
                    json row{{"key", d.key}, {"type", d.type}};
                    if (!d.text.empty()) {
                        row["text"] = d.text;
                    } else {
                        row["number"] = d.number;
                    }
                    data.push_back(row);
                    nodes.push_back({{"id", "data/" + d.key},
                                     {"label", d.key},
                                     {"valueKind", d.text.empty() ? "u32" : "string"},
                                     {"value", d.text.empty() ? json(d.number) : json(d.text)},
                                     {"children", json::array()}});
                }
                nodes.push_back({{"id", "visibility"},
                                 {"label", "visibility"},
                                 {"valueKind", "u8"},
                                 {"value", o->visibility},
                                 {"children", json::array()}});
                return envelope_ok({{"type", "OBJK"},
                                    {"version", o->version},
                                    {"components", comps},
                                    {"data", data},
                                    {"visibility", o->visibility},
                                    {"tgiCount", o->tgi_count},
                                    {"rawSize", body->size()},
                                    {"nodes", nodes}});
            }
        }
        if (cmd == "vpxy.get" || (cmd == "graph.get" && type == sxpe::resources::kVpxy)) {
            auto v = sxpe::resources::parse_vpxy(*body);
            if (!v) {
                if (cmd == "vpxy.get") {
                    return envelope_err(v.error());
                }
            } else {
                json ents = json::array();
                json nodes = json::array();
                nodes.push_back({{"id", "version"},
                                 {"label", "version"},
                                 {"valueKind", "u32"},
                                 {"value", v->version},
                                 {"children", json::array()}});
                for (std::size_t ei = 0; ei < v->entries.size(); ++ei) {
                    const auto& e = v->entries[ei];
                    json row{{"type", e.type}, {"id", e.id}, {"indices", e.indices}};
                    ents.push_back(row);
                    nodes.push_back({{"id", "entry/" + std::to_string(ei)},
                                     {"label", "entry"},
                                     {"valueKind", "u8"},
                                     {"value", e.type},
                                     {"children", json::array()}});
                }
                json bbox = json::array();
                if (v->has_bbox) {
                    for (float f : v->bbox) {
                        bbox.push_back(f);
                    }
                    nodes.push_back({{"id", "bbox"},
                                     {"label", "bbox"},
                                     {"valueKind", "floats"},
                                     {"value", bbox},
                                     {"children", json::array()}});
                }
                return envelope_ok({{"type", "VPXY"},
                                    {"version", v->version},
                                    {"entries", ents},
                                    {"bbox", bbox},
                                    {"modular", v->modular},
                                    {"tgiCount", v->tgi_count},
                                    {"rawSize", body->size()},
                                    {"nodes", nodes}});
            }
        }
        if (cmd == "objd.get" || (cmd == "graph.get" && type == sxpe::resources::kObjd)) {
            auto o = sxpe::resources::parse_objd(*body);
            if (!o) {
                if (cmd == "objd.get") {
                    return envelope_err(o.error());
                }
            } else {
                json nodes = json::array();
                nodes.push_back({{"id", "nameGuid"},
                                 {"label", "nameGuid"},
                                 {"valueKind", "u64"},
                                 {"value", o->name_guid},
                                 {"children", json::array()}});
                nodes.push_back({{"id", "price"},
                                 {"label", "price"},
                                 {"valueKind", "f32"},
                                 {"value", o->price},
                                 {"children", json::array()}});
                return envelope_ok({{"type", "OBJD"},
                                    {"version", o->version},
                                    {"commonVersion", o->common_version},
                                    {"nameGuid", o->name_guid},
                                    {"descGuid", o->desc_guid},
                                    {"internalName", o->internal_name},
                                    {"internalDesc", o->internal_desc},
                                    {"price", o->price},
                                    {"thumbIid", o->thumb_iid},
                                    {"instanceName", o->instance_name},
                                    {"partial", o->partial},
                                    {"rawSize", body->size()},
                                    {"nodes", nodes}});
            }
        }
        if (cmd == "casp.get" || (cmd == "graph.get" && type == sxpe::resources::kCasp)) {
            auto c = sxpe::resources::parse_casp(*body);
            if (!c) {
                if (cmd == "casp.get") {
                    return envelope_err(c.error());
                }
            } else {
                json ages = json::array();
                for (const auto& a : sxpe::resources::casp_age_names(c->age_flags)) {
                    ages.push_back(a);
                }
                json genders = json::array();
                for (const auto& g : sxpe::resources::casp_gender_names(c->gender_flags)) {
                    genders.push_back(g);
                }
                const char* ctn = sxpe::resources::casp_clothing_type_name(c->clothing_type);
                const char* spn = sxpe::resources::casp_species_name(c->species);
                json nodes = json::array();
                nodes.push_back({{"id", "clothingType"},
                                 {"label", "clothingType"},
                                 {"valueKind", "u32"},
                                 {"value", c->clothing_type},
                                 {"children", json::array()}});
                json tgi_rows = json::array();
                for (const auto& t : c->tgis) {
                    tgi_rows.push_back(tgi_json(t));
                }
                return envelope_ok({{"type", "CASP"},
                                    {"version", c->version},
                                    {"name", c->name},
                                    {"sortPriority", c->sort_priority},
                                    {"clothingType", c->clothing_type},
                                    {"clothingTypeName", ctn ? ctn : ""},
                                    {"typeFlags", c->type_flags},
                                    {"ageGender", c->age_gender},
                                    {"ageFlags", c->age_flags},
                                    {"ages", ages},
                                    {"species", c->species},
                                    {"speciesName", spn ? spn : ""},
                                    {"genderFlags", c->gender_flags},
                                    {"genders", genders},
                                    {"clothingCategory", c->clothing_category},
                                    {"tgiCount", c->tgis.size()},
                                    {"tgis", tgi_rows},
                                    {"partial", c->partial},
                                    {"rawSize", body->size()},
                                    {"nodes", nodes}});
            }
        }
                if (cmd == "refs.get" || (cmd == "graph.get" && type == sxpe::resources::kRefs)) {
            auto rr = sxpe::resources::parse_refs(*body);
            if (!rr) {
                if (cmd == "refs.get") {
                    return envelope_err(rr.error());
                }
            } else {
                json entries = json::array();
                json nodes = json::array();
                nodes.push_back({{"id", "version"},
                                 {"label", "version"},
                                 {"valueKind", "u16"},
                                 {"value", rr->version},
                                 {"children", json::array()}});
                for (std::size_t ei = 0; ei < rr->entries.size(); ++ei) {
                    const auto& e = rr->entries[ei];
                    auto row = tgi_json(e.tgi);
                    row["aux"] = e.aux;
                    entries.push_back(row);
                    nodes.push_back({{"id", "entry/" + std::to_string(ei)},
                                     {"label", "entry"},
                                     {"valueKind", "tgi"},
                                     {"value", row},
                                     {"children", json::array()}});
                }
                json indices = json::array();
                for (auto ix : rr->indices) {
                    indices.push_back(ix);
                }
                return envelope_ok({{"type", "REFS"},
                                    {"version", rr->version},
                                    {"hasThingy", rr->has_thingy},
                                    {"thingy", rr->thingy},
                                    {"auxIsDword", rr->aux_is_dword},
                                    {"entryCount", rr->entries.size()},
                                    {"entries", entries},
                                    {"indices", indices},
                                    {"partial", rr->partial},
                                    {"rawSize", body->size()},
                                    {"nodes", nodes}});
            }
        }
        if (cmd == "clip.info" || (cmd == "graph.get" && type == sxpe::resources::kClip)) {
            auto c = sxpe::resources::parse_clip(*body);
            if (!c) {
                if (cmd == "clip.info") {
                    return envelope_err(c.error());
                }
            } else {
                json hashes = json::array();
                for (auto h : c->track_hashes) {
                    hashes.push_back(h);
                }
                json nodes = json::array();
                nodes.push_back({{"id", "duration"},
                                 {"label", "durationSeconds"},
                                 {"valueKind", "f32"},
                                 {"value", c->duration_seconds},
                                 {"children", json::array()}});
                nodes.push_back({{"id", "animName"},
                                 {"label", "animName"},
                                 {"valueKind", "string"},
                                 {"value", c->anim_name},
                                 {"children", json::array()}});
                nodes.push_back({{"id", "sourceFile"},
                                 {"label", "sourceFile"},
                                 {"valueKind", "string"},
                                 {"value", c->source_file},
                                 {"children", json::array()}});
                nodes.push_back({{"id", "actorName"},
                                 {"label", "actorName"},
                                 {"valueKind", "string"},
                                 {"value", c->actor_name},
                                 {"children", json::array()}});
                json track_children = json::array();
                for (std::size_t ti = 0; ti < hashes.size(); ++ti) {
                    track_children.push_back({{"id", "track/" + std::to_string(ti)},
                                              {"label", "hash"},
                                              {"valueKind", "u32"},
                                              {"value", hashes[ti]},
                                              {"children", json::array()}});
                }
                nodes.push_back({{"id", "tracks"},
                                 {"label", "trackHashes"},
                                 {"valueKind", "array"},
                                 {"value", c->track_count},
                                 {"children", track_children}});
                return envelope_ok({{"type", "CLIP"},
                                    {"version", c->version},
                                    {"frameDuration", c->frame_duration},
                                    {"frameCount", c->frame_count},
                                    {"durationSeconds", c->duration_seconds},
                                    {"animName", c->anim_name},
                                    {"sourceFile", c->source_file},
                                    {"actorName", c->actor_name},
                                    {"trackCount", c->track_count},
                                    {"trackHashes", hashes},
                                    {"safeFields",
                                     json::array({"animName", "sourceFile", "actorName", "trackHashes"})},
                                    {"partial", c->partial},
                                    {"rawSize", body->size()},
                                    {"nodes", nodes}});
            }
        }
        if (cmd == "rcol.summary" ||
            (cmd == "graph.get" && (type == sxpe::resources::kModl || type == sxpe::resources::kMlod ||
                                    type == sxpe::resources::kGeom || type == sxpe::resources::kMatd))) {
            auto r = sxpe::resources::parse_rcol_summary(*body);
            if (!r) {
                if (cmd == "rcol.summary") {
                    return envelope_err(r.error());
                }
            } else {
                auto tgi_row = [](const sxpe::games::sims3::Tgi& t) {
                    return json{{"type", t.type}, {"group", t.group}, {"instance", t.instance}};
                };
                auto tex_row = [&](const sxpe::resources::RcolTextureRef& tex) {
                    json row{{"paramHash", tex.param_hash},
                             {"paramName", tex.param_name},
                             {"resolved", tex.resolved},
                             {"rcolRef", tex.rcol_ref}};
                    if (tex.resolved) {
                        row["type"] = tex.tgi.type;
                        row["group"] = tex.tgi.group;
                        row["instance"] = tex.tgi.instance;
                    }
                    return row;
                };
                json chunks = json::array();
                json nodes = json::array();
                for (std::size_t ci = 0; ci < r->chunks.size(); ++ci) {
                    const auto& ch = r->chunks[ci];
                    json row{{"index", ci},
                             {"type", ch.type},
                             {"tag", ch.tag},
                             {"size", ch.size},
                             {"vertexCount", ch.vertex_count},
                             {"faceCount", ch.face_count},
                             {"groupCount", ch.group_count}};
                    if (ch.has_matd) {
                        row["shaderHash"] = ch.shader_hash;
                        row["shaderName"] = ch.shader_name;
                        row["materialNameHash"] = ch.material_name_hash;
                        row["matdVersion"] = ch.matd_version;
                        json mats = json::array();
                        for (const auto& tex : ch.textures) {
                            mats.push_back(tex_row(tex));
                        }
                        row["textures"] = mats;
                    }
                    chunks.push_back(row);
                    nodes.push_back({{"id", "chunk/" + std::to_string(ci) + "/" +
                                             (ch.tag.empty() ? std::to_string(ch.type) : ch.tag)},
                                     {"label", ch.tag.empty() ? "chunk" : ch.tag},
                                     {"valueKind", "u32"},
                                     {"value", ch.size},
                                     {"children", json::array()}});
                }
                json externals = json::array();
                for (const auto& t : r->external_tgis) {
                    externals.push_back(tgi_row(t));
                }
                json textures = json::array();
                for (const auto& tex : r->textures) {
                    textures.push_back(tex_row(tex));
                }
                return envelope_ok({{"type", "RCOL"},
                                    {"version", r->version},
                                    {"internalCount", r->internal_count},
                                    {"externalCount", r->external_count},
                                    {"chunks", chunks},
                                    {"externalTgis", externals},
                                    {"textures", textures},
                                    {"totalVertices", r->total_vertices},
                                    {"totalFaces", r->total_faces},
                                    {"lodGroups", r->lod_groups},
                                    {"partial", r->partial},
                                    {"rawSize", body->size()},
                                    {"nodes", nodes}});
            }
        }
        json nodes = json::array();
        if (type == kStbl) {
            auto t = sxpe::resources::parse_stbl(*body);
            if (t) {
                for (const auto& e : t->entries) {
                    nodes.push_back({{"id", "stbl/" + std::to_string(e.id)},
                                     {"label", std::to_string(e.id)},
                                     {"valueKind", "string"},
                                     {"value", e.text},
                                     {"children", json::array()}});
                }
            }
        } else {
            nodes.push_back({{"id", "blob"},
                             {"label", tag.empty() ? "bytes" : tag},
                             {"valueKind", "bytes"},
                             {"value", static_cast<int>(body->size())},
                             {"children", json::array()}});
        }
        return envelope_ok({{"type", tag}, {"rawSize", body->size()}, {"nodes", nodes}});
    }
    if (cmd == "graph.set") {
        args["id"] = json();
        const auto node = args.at("nodeId").get<std::string>();
        if (node.rfind("stbl/", 0) != 0) {
            return envelope_err(err(ErrorCode::refused, "only stbl nodes are writable"));
        }
        args["id"] = std::stoull(node.substr(5));
        args["text"] = args.at("value").get<std::string>();
        return exec("stbl.set", args);
    }
    if (cmd == "clip.exportAs" || cmd == "clip.exportAsBatch") {
        auto export_one = [&](std::uint32_t idx, const std::string& name,
                              bool dry_run) -> json {
            auto body = s.pkg.uncompressed(idx);
            if (!body) {
                return envelope_err(body.error());
            }
            Tgi t = s.pkg.entry(idx).tgi;
            t.type = sxpe::resources::kClip;
            t.instance = sxpe::games::sims3::fnv64_clip(name);
            const auto filename =
                sxpe::games::sims3::community_filename(t, name, "CLIP.animation");
            if (dry_run) {
                return envelope_ok({{"dryRun", true},
                                    {"resourceId", tgi_json(t)},
                                    {"filename", filename},
                                    {"name", name}});
            }
            auto r = s.pkg.add(t, *body, s.pkg.entry(idx).compressed == 0xFFFF);
            if (!r) {
                return envelope_err(r.error());
            }
            return envelope_ok(
                {{"resourceId", rid_json(s.pkg.entry(*r).tgi, s.pkg.entry(*r).ordinal)},
                 {"filename", filename},
                 {"name", name}});
        };
        if (cmd == "clip.exportAs") {
            auto i = need_idx();
            if (!i) {
                return envelope_err(i.error());
            }
            const auto name = args.at("name").get<std::string>();
            if (name.empty()) {
                return envelope_err(err(ErrorCode::invalid_argument, "name is required"));
            }
            return export_one(*i, name, dry(args));
        }
        if (!args.contains("items") || !args["items"].is_array()) {
            return envelope_err(err(ErrorCode::invalid_argument, "items must be an array"));
        }
        const auto& items = args["items"];
        constexpr std::size_t kMaxBatch = 256;
        if (items.size() > kMaxBatch) {
            return envelope_err(err(ErrorCode::cap_exceeded, "clip.exportAsBatch cap 256 items"));
        }
        const bool dry_run = dry(args);
        json results = json::array();
        std::size_t ok_n = 0;
        std::size_t fail_n = 0;
        for (const auto& it : items) {
            if (!it.is_object() || !it.contains("name") || !it["name"].is_string()) {
                results.push_back({{"ok", false},
                                   {"error",
                                    {{"code", "invalid_argument"},
                                     {"message", "item needs name string"}}}});
                ++fail_n;
                continue;
            }
            const auto name = it["name"].get<std::string>();
            if (name.empty()) {
                results.push_back({{"ok", false},
                                   {"error",
                                    {{"code", "invalid_argument"},
                                     {"message", "name is required"}}}});
                ++fail_n;
                continue;
            }
            json item_args = args;
            if (it.contains("resourceId")) {
                item_args["resourceId"] = it["resourceId"];
            } else if (it.contains("type") || it.contains("group") || it.contains("instance")) {
                item_args["resourceId"] = {{"type", it.value("type", 0)},
                                           {"group", it.value("group", 0)},
                                           {"instance", it.value("instance", 0)}};
                if (it.contains("ordinal")) {
                    item_args["resourceId"]["ordinal"] = it["ordinal"];
                }
            }
            auto item_i = this->idx(s, item_args);
            if (!item_i) {
                results.push_back(envelope_err(item_i.error()));
                ++fail_n;
                continue;
            }
            if (s.pkg.entry(*item_i).tgi.type != sxpe::resources::kClip) {
                results.push_back(envelope_err(
                    err(ErrorCode::invalid_argument, "resourceId is not a CLIP")));
                ++fail_n;
                continue;
            }
            auto one = export_one(*item_i, name, dry_run);
            if (one.value("ok", false)) {
                ++ok_n;
            } else {
                ++fail_n;
            }
            results.push_back(std::move(one));
        }
        return envelope_ok({{"dryRun", dry_run},
                            {"count", items.size()},
                            {"succeeded", ok_n},
                            {"failed", fail_n},
                            {"results", results}});
    }
    if (cmd == "clip.set") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        if (s.pkg.entry(*i).tgi.type != sxpe::resources::kClip) {
            return envelope_err(err(ErrorCode::invalid_argument, "resourceId is not a CLIP"));
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        const bool compress = s.pkg.entry(*i).compressed == 0xFFFF;
        sxpe::resources::ClipPatch patch;
        if (args.contains("animName") && args["animName"].is_string()) {
            patch.anim_name = args["animName"].get<std::string>();
        }
        if (args.contains("sourceFile") && args["sourceFile"].is_string()) {
            patch.source_file = args["sourceFile"].get<std::string>();
        }
        if (args.contains("actorName") && args["actorName"].is_string()) {
            patch.actor_name = args["actorName"].get<std::string>();
        }
        if (args.contains("trackHashes")) {
            if (!args["trackHashes"].is_array()) {
                return envelope_err(err(ErrorCode::invalid_argument, "trackHashes must be an array"));
            }
            for (const auto& row : args["trackHashes"]) {
                if (!row.is_object() || !row.contains("index") || !row.contains("hash")) {
                    return envelope_err(
                        err(ErrorCode::invalid_argument, "trackHashes entries need index+hash"));
                }
                const auto idx = static_cast<std::uint32_t>(as_u64(row.at("index")));
                const auto hash = static_cast<std::uint32_t>(as_u64(row.at("hash")));
                patch.track_hashes.emplace_back(idx, hash);
            }
        }
        auto out = sxpe::resources::apply_clip(*body, patch);
        if (!out) {
            return envelope_err(out.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"bytes", out->size()}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        auto r = s.pkg.set_uncompressed(*i, *out, compress);
        if (!r) {
            return envelope_err(r.error());
        }
        auto parsed = sxpe::resources::parse_clip(*out);
        if (!parsed) {
            return envelope_err(parsed.error());
        }
        json hashes = json::array();
        for (auto h : parsed->track_hashes) {
            hashes.push_back(h);
        }
        return envelope_ok({{"bytes", out->size()},
                            {"version", parsed->version},
                            {"frameDuration", parsed->frame_duration},
                            {"frameCount", parsed->frame_count},
                            {"durationSeconds", parsed->duration_seconds},
                            {"animName", parsed->anim_name},
                            {"sourceFile", parsed->source_file},
                            {"actorName", parsed->actor_name},
                            {"trackCount", parsed->track_count},
                            {"trackHashes", hashes},
                            {"partial", parsed->partial}});
    }
    if (cmd == "s3sa.info" || cmd == "s3sa.exportDll" || cmd == "s3sa.view" ||
        cmd == "s3sa.importDll") {
        if (cmd == "s3sa.importDll") {
            auto path = check_path(args.at("path").get<std::string>());
            if (!path) {
                return envelope_err(path.error());
            }
            auto pe = read_file(*path);
            if (!pe) {
                return envelope_err(pe.error());
            }
            auto wrapped = sxpe::resources::wrap_s3sa_v1(*pe);
            if (!wrapped) {
                return envelope_err(wrapped.error());
            }
            const auto fname = path->filename().string();
            Tgi t{};
            t.type = sxpe::resources::kS3sa;
            t.group = 0;
            t.instance = sxpe::games::sims3::fnv1_64(fname, true);
            std::optional<std::uint32_t> replace;
            if (args.contains("resourceId")) {
                auto i = need_idx();
                if (!i) {
                    return envelope_err(i.error());
                }
                replace = *i;
                t = s.pkg.entry(*i).tgi;
            }
            if (args.contains("group")) {
                t.group = static_cast<std::uint32_t>(as_u64(args["group"]));
            }
            if (args.contains("instance")) {
                t.instance = as_u64(args["instance"]);
            }
            if (dry(args)) {
                return envelope_ok({{"dryRun", true},
                                    {"resourceId", tgi_json(t)},
                                    {"bytes", wrapped->size()},
                                    {"name", fname}});
            }
            bool had_nmap = false;
            for (std::uint32_t i = 0; i < s.pkg.count(); ++i) {
                if (s.pkg.entry(i).tgi.type == kNmap) {
                    had_nmap = true;
                    break;
                }
            }
            std::optional<std::vector<std::byte>> prev_s3sa;
            std::uint16_t prev_s3sa_comp = 0;
            if (replace) {
                auto prev = s.pkg.uncompressed(*replace);
                if (!prev) {
                    return envelope_err(prev.error());
                }
                prev_s3sa = std::move(*prev);
                prev_s3sa_comp = s.pkg.entry(*replace).compressed;
            }
            std::optional<std::vector<std::byte>> prev_nmap;
            std::optional<std::uint32_t> nmap_idx_before;
            if (had_nmap) {
                for (std::uint32_t i = 0; i < s.pkg.count(); ++i) {
                    if (s.pkg.entry(i).tgi.type == kNmap) {
                        nmap_idx_before = i;
                        auto b = s.pkg.uncompressed(i);
                        if (!b) {
                            return envelope_err(b.error());
                        }
                        prev_nmap = std::move(*b);
                        break;
                    }
                }
            }

            std::uint32_t idx = 0;
            if (replace) {
                auto r = s.pkg.set_uncompressed(*replace, *wrapped, false);
                if (!r) {
                    return envelope_err(r.error());
                }
                idx = *replace;
            } else {
                auto r = s.pkg.add(t, *wrapped, false);
                if (!r) {
                    return envelope_err(r.error());
                }
                idx = *r;
            }

            json nmap_args{{"sessionId", s.id}, {"instance", s.pkg.entry(idx).tgi.instance},
                           {"name", fname}};
            // Apply nmap.set, then drop any undo it pushed so failure rollback stays clean.
            const auto undo_sz = s.undo.size();
            auto nm = exec("nmap.set", nmap_args);
            while (s.undo.size() > undo_sz) {
                s.undo.pop_back();
            }
            s.redo.clear();
            if (!nm.value("ok", false)) {
                const std::string detail =
                    nm.contains("error") && nm["error"].contains("message")
                        ? nm["error"]["message"].get<std::string>()
                        : "nmap.set failed";
                if (replace) {
                    (void)s.pkg.set_uncompressed(idx, *prev_s3sa, prev_s3sa_comp == 0xFFFF);
                } else {
                    (void)s.pkg.remove(idx);
                }
                if (!had_nmap) {
                    for (std::uint32_t i = s.pkg.count(); i-- > 0;) {
                        if (s.pkg.entry(i).tgi.type == kNmap) {
                            (void)s.pkg.remove(i);
                        }
                    }
                } else if (prev_nmap && nmap_idx_before && *nmap_idx_before < s.pkg.count() &&
                           s.pkg.entry(*nmap_idx_before).tgi.type == kNmap) {
                    (void)s.pkg.set_uncompressed(*nmap_idx_before, *prev_nmap, false);
                }
                return envelope_err(
                    err(ErrorCode::io, "S3SA import rolled back; NMAP update failed: " + detail));
            }
            return envelope_ok({{"resourceId", rid_json(s.pkg.entry(idx).tgi, s.pkg.entry(idx).ordinal)},
                                {"bytes", wrapped->size()},
                                {"name", fname},
                                {"nmapName", fname},
                                {"loadLibrary", false}});
        }
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        auto names = load_nmap(s.pkg);
        auto inf = sxpe::resources::inspect_s3sa(*body, sxpe::resources::lookup_name(
                                                           names, s.pkg.entry(*i).tgi.instance));
        if (cmd == "s3sa.info") {
            json j{{"size", inf.size},
                   {"moduleHint", inf.module_hint},
                   {"parsed", inf.parsed},
                   {"loadLibrary", false}};
            if (inf.pe_offset) {
                j["peOffset"] = *inf.pe_offset;
            }
            if (inf.parsed) {
                j["version"] = inf.version;
                if (!inf.game_version.empty()) {
                    j["gameVersion"] = inf.game_version;
                }
                j["checksumType"] = inf.checksum_type;
                j["checksumZero"] = inf.checksum_zero;
                j["blockCount"] = inf.block_count;
                j["keyTableZero"] = inf.key_table_zero;
                j["assemblyBytes"] = inf.assembly_bytes;
            }
            return envelope_ok(j);
        }
        auto pe = sxpe::resources::export_pe(*body);
        if (!pe) {
            return envelope_err(pe.error());
        }
        if (cmd == "s3sa.view") {
            std::string viewer = args.value("viewer", "");
            const bool has_path =
                args.contains("path") && !args.at("path").get<std::string>().empty();
            const bool keep_temp = args.contains("keepTemp")
                                       ? args.value("keepTemp", false)
                                       : !viewer.empty();  // default true when spawning viewer
            if (!has_path && viewer.empty() && !keep_temp) {
                return envelope_err(err(
                    ErrorCode::invalid_argument,
                    "s3sa.view needs path (CLI/MCP) or viewer, or keepTemp:true for a GUI temp"));
            }
            std::filesystem::path outp;
            bool used_temp = false;
            if (has_path) {
                auto path = check_path(args.at("path").get<std::string>());
                if (!path) {
                    return envelope_err(path.error());
                }
                outp = *path;
            } else {
                outp = make_temp_s3sa_dll_path();
                auto path = check_path(outp.string());
                if (!path) {
                    return envelope_err(path.error());
                }
                outp = *path;
                used_temp = true;
            }
            if (std::filesystem::exists(outp) && !force(args)) {
                return envelope_err(err(ErrorCode::refused, "exists; pass force"));
            }
            if (auto w = write_file(outp, *pe); !w) {
                return envelope_err(w.error());
            }
            bool spawned = false;
            if (!viewer.empty()) {
                const auto native = outp.string();
                std::string cmd_line = viewer;
                const auto ph = cmd_line.find("{path}");
                if (ph != std::string::npos) {
                    cmd_line.replace(ph, 6, native);
                } else {
                    cmd_line.push_back(' ');
                    cmd_line += native;
                }
                spawned = spawn_viewer_detached(cmd_line);
            }
            // Avoid temp leaks when nobody will clean up (no viewer + keepTemp false).
            if (used_temp && !keep_temp && viewer.empty()) {
                std::error_code ec;
                std::filesystem::remove(outp, ec);
                return envelope_err(err(
                    ErrorCode::invalid_argument,
                    "s3sa.view temp was auto-deleted; pass path or keepTemp:true"));
            }
            json j{{"path", outp.string()},
                   {"bytes", pe->size()},
                   {"spawned", spawned},
                   {"keepTemp", keep_temp || !used_temp},
                   {"loadLibrary", false},
                   {"note",
                    "PE for an external viewer. GUI: Settings → External programs "
                    "(ext/s3sa) then Editors → View S3SA… (passes keepTemp). CLI/MCP: pass "
                    "path and/or viewer with {path}; keepTemp leaves a temp for the viewer. "
                    "Never LoadLibrary."}};
            return envelope_ok(std::move(j));
        }
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        if (std::filesystem::exists(*path) && !force(args)) {
            return envelope_err(err(ErrorCode::refused, "exists; pass force"));
        }
        if (auto w = write_file(*path, *pe); !w) {
            return envelope_err(w.error());
        }
        return envelope_ok({{"path", path->string()}, {"bytes", pe->size()}, {"loadLibrary", false}});
    }
    if (cmd == "hex.get") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        std::uint32_t n = args.value("maxBytes", 256);
        auto body = s.pkg.peek(*i, n);
        if (!body) {
            return envelope_err(body.error());
        }
        std::string hex;
        hex.reserve(body->size() * 2);
        static constexpr char kHex[] = "0123456789abcdef";
        for (auto b : *body) {
            const auto u = static_cast<unsigned>(b);
            hex.push_back(kHex[(u >> 4) & 0xF]);
            hex.push_back(kHex[u & 0xF]);
        }
        return envelope_ok({{"hex", hex}, {"bytes", body->size()}});
    }
    if (cmd == "xml.get" || cmd == "xml.set") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        const auto type = s.pkg.entry(*i).tgi.type;
        const bool compress = s.pkg.entry(*i).compressed == 0xFFFF;
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        const bool typed = sxpe::resources::is_xml_editor_type(type);
        const bool like = sxpe::resources::looks_like_xml(*body);
        if (!typed && !like) {
            return envelope_err(err(ErrorCode::invalid_argument,
                                    "resource is not `_XML`/`ITUN` and payload does not look like XML"));
        }
        if (body->size() > sxpe::core::caps::kMaxXmlEditorBytes) {
            return envelope_err(err(ErrorCode::cap_exceeded,
                                    "XML editor cap exceeded (" +
                                        std::to_string(sxpe::core::caps::kMaxXmlEditorBytes) +
                                        " bytes); use resource.export / resource.replace"));
        }
        if (cmd == "xml.get") {
            auto text = sxpe::resources::decode_xml_text(*body);
            if (!text) {
                return envelope_err(text.error());
            }
            const auto enc = sxpe::resources::sniff_xml_encoding(*body);
            return envelope_ok({{"text", *text},
                                {"encoding", std::string(sxpe::resources::xml_encoding_name(enc))},
                                {"bytes", body->size()},
                                {"type", type},
                                {"tag", std::string(sxpe::resources::tag_for(type))}});
        }
        // xml.set
        if (!args.contains("text") || !args["text"].is_string()) {
            return envelope_err(err(ErrorCode::invalid_argument, "text required"));
        }
        const auto text = args["text"].get<std::string>();
        sxpe::resources::XmlEncoding enc = sxpe::resources::sniff_xml_encoding(*body);
        if (args.contains("encoding") && args["encoding"].is_string()) {
            auto parsed = sxpe::resources::xml_encoding_from_name(args["encoding"].get<std::string>());
            if (!parsed) {
                return envelope_err(err(ErrorCode::invalid_argument,
                                        "encoding must be utf-8, utf-8-bom, utf-16le, utf-16le-bom, "
                                        "utf-16be, or utf-16be-bom"));
            }
            enc = *parsed;
        }
        auto raw = sxpe::resources::encode_xml_text(text, enc);
        if (!raw) {
            return envelope_err(raw.error());
        }
        if (raw->size() > sxpe::core::caps::kMaxXmlEditorBytes) {
            return envelope_err(err(ErrorCode::cap_exceeded,
                                    "XML editor cap exceeded (" +
                                        std::to_string(sxpe::core::caps::kMaxXmlEditorBytes) +
                                        " bytes); refuse write"));
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true},
                                {"bytes", raw->size()},
                                {"encoding", std::string(sxpe::resources::xml_encoding_name(enc))}});
        }
        if (auto u = snapshot(s, *i); !u) {
            return envelope_err(u.error());
        }
        auto r = s.pkg.set_uncompressed(*i, *raw, compress);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"bytes", raw->size()},
                            {"encoding", std::string(sxpe::resources::xml_encoding_name(enc))}});
    }
    if (cmd == "text.get") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        std::uint32_t n = args.value("maxBytes", 4096);
        auto body = s.pkg.peek(*i, n);
        if (!body) {
            return envelope_err(body.error());
        }
        std::string t(reinterpret_cast<const char*>(body->data()), body->size());
        return envelope_ok({{"text", t}, {"bytes", body->size()}});
    }

    if (cmd == "resource.listRefs") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        const auto limit = static_cast<std::size_t>(args.value("limit", 500));
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        const auto type = s.pkg.entry(*i).tgi.type;
        const auto& names = name_index(s);
        json refs = json::array();
        std::string kind;
        auto push_tgi = [&](const Tgi& t, const char* reason, std::int64_t index,
                            std::optional<std::uint32_t> aux = std::nullopt) {
            if (refs.size() >= limit) {
                return;
            }
            json h = tgi_json(t);
            h["reason"] = reason;
            if (index >= 0) {
                h["index"] = index;
            }
            if (aux) {
                h["aux"] = *aux;
            }
            refs.push_back(std::move(h));
        };
        if (type == sxpe::resources::kRefs) {
            kind = "REFS";
            auto parsed = sxpe::resources::parse_refs(*body);
            if (!parsed) {
                return envelope_err(parsed.error());
            }
            for (std::size_t ei = 0; ei < parsed->entries.size() && refs.size() < limit; ++ei) {
                push_tgi(parsed->entries[ei].tgi, "refs.entry", static_cast<std::int64_t>(ei),
                         parsed->entries[ei].aux);
            }
        } else if (type == sxpe::resources::kObjk) {
            kind = "OBJK";
            auto parsed = sxpe::resources::parse_objk(*body);
            if (!parsed) {
                return envelope_err(parsed.error());
            }
            for (std::size_t ei = 0; ei < parsed->tgis.size() && refs.size() < limit; ++ei) {
                push_tgi(parsed->tgis[ei], "objk.tgi", static_cast<std::int64_t>(ei));
            }
        } else if (type == sxpe::resources::kVpxy) {
            kind = "VPXY";
            auto parsed = sxpe::resources::parse_vpxy(*body);
            if (!parsed) {
                return envelope_err(parsed.error());
            }
            for (std::size_t ei = 0; ei < parsed->tgis.size() && refs.size() < limit; ++ei) {
                push_tgi(parsed->tgis[ei], "vpxy.tgi", static_cast<std::int64_t>(ei));
            }
        } else if (type == sxpe::resources::kCasp) {
            kind = "CASP";
            auto parsed = sxpe::resources::parse_casp(*body);
            if (!parsed) {
                return envelope_err(parsed.error());
            }
            for (std::size_t ei = 0; ei < parsed->tgis.size() && refs.size() < limit; ++ei) {
                push_tgi(parsed->tgis[ei], "casp.tgi", static_cast<std::int64_t>(ei));
            }
        } else {
            return envelope_err(err(ErrorCode::invalid_argument,
                                    "resourceId type has no known outbound TGI table "
                                    "(supported: REFS, OBJK, VPXY, CASP)"));
        }
        const bool truncated = refs.size() >= limit;
        json data{{"source", item_meta(s.pkg, *i, names)},
                  {"kind", kind},
                  {"refs", refs},
                  {"count", refs.size()},
                  {"truncated", truncated}};
        json summary = json::array();
        summary.push_back("Outbound from " + kind + ": " + std::to_string(refs.size()) +
                          (truncated ? " (truncated)" : ""));
        if (refs.empty()) {
            summary.push_back("No outbound TGI entries.");
        } else {
            for (const auto& h : refs) {
                std::string line = "  ";
                if (h.contains("typeHex")) {
                    line += h.value("typeHex", "") + " " + h.value("groupHex", "") + " " +
                            h.value("instanceHex", "");
                }
                if (h.contains("reason")) {
                    line += "  (" + h.value("reason", std::string{}) + ")";
                }
                summary.push_back(std::move(line));
            }
        }
        data["summary"] = std::move(summary);
        return envelope_ok(std::move(data));
    }

    if (cmd == "resource.findRefs") {
        if (!args.contains("resourceId")) {
            return envelope_err(err(ErrorCode::invalid_argument, "resourceId required"));
        }
        const Tgi target = tgi_from(args["resourceId"]);
        const auto limit = static_cast<std::size_t>(args.value("limit", 200));
        const bool byte_scan = args.value("byteScan", false);
        const auto bs_max_bytes =
            static_cast<std::uint32_t>(args.value("byteScanMaxBytes", 1u << 20));
        const auto bs_max_res =
            static_cast<std::uint32_t>(args.value("byteScanMaxResources", 500));
        const auto& names = name_index(s);
        json hits = json::array();
        std::uint32_t scanned_refs = 0;
        std::uint32_t scanned_objk = 0;
        std::uint32_t scanned_vpxy = 0;
        std::uint32_t scanned_casp = 0;
        std::uint32_t bs_scanned = 0;

        auto push_hit = [&](std::uint32_t i, const char* reason, std::int64_t index = -1) {
            if (hits.size() >= limit) {
                return;
            }
            json h;
            h["source"] = item_meta(s.pkg, i, names);
            h["reason"] = reason;
            if (index >= 0) {
                h["index"] = index;
            }
            hits.push_back(std::move(h));
        };

        auto matches = [&](const Tgi& t) {
            return t.type == target.type && t.group == target.group && t.instance == target.instance;
        };

        for (std::uint32_t i = 0; i < s.pkg.count() && hits.size() < limit; ++i) {
            const auto& e = s.pkg.entry(i);
            if (e.tgi.type == target.type && e.tgi.group == target.group &&
                e.tgi.instance == target.instance) {
                continue;
            }
            if (e.tgi.type != sxpe::resources::kRefs && e.tgi.type != sxpe::resources::kObjk &&
                e.tgi.type != sxpe::resources::kVpxy && e.tgi.type != sxpe::resources::kCasp) {
                continue;
            }
            if (e.mem_size > sxpe::core::caps::kMaxResourceBytes) {
                continue;
            }
            auto body = s.pkg.uncompressed(i);
            if (!body) {
                continue;
            }
            if (e.tgi.type == sxpe::resources::kRefs) {
                ++scanned_refs;
                auto parsed = sxpe::resources::parse_refs(*body);
                if (!parsed) {
                    continue;
                }
                for (std::size_t ei = 0; ei < parsed->entries.size() && hits.size() < limit; ++ei) {
                    if (matches(parsed->entries[ei].tgi)) {
                        push_hit(i, "refs.entry", static_cast<std::int64_t>(ei));
                    }
                }
            } else if (e.tgi.type == sxpe::resources::kObjk) {
                ++scanned_objk;
                auto parsed = sxpe::resources::parse_objk(*body);
                if (!parsed) {
                    continue;
                }
                for (std::size_t ei = 0; ei < parsed->tgis.size() && hits.size() < limit; ++ei) {
                    if (matches(parsed->tgis[ei])) {
                        push_hit(i, "objk.tgi", static_cast<std::int64_t>(ei));
                    }
                }
            } else if (e.tgi.type == sxpe::resources::kVpxy) {
                ++scanned_vpxy;
                auto parsed = sxpe::resources::parse_vpxy(*body);
                if (!parsed) {
                    continue;
                }
                for (std::size_t ei = 0; ei < parsed->tgis.size() && hits.size() < limit; ++ei) {
                    if (matches(parsed->tgis[ei])) {
                        push_hit(i, "vpxy.tgi", static_cast<std::int64_t>(ei));
                    }
                }
            } else if (e.tgi.type == sxpe::resources::kCasp) {
                ++scanned_casp;
                auto parsed = sxpe::resources::parse_casp(*body);
                if (!parsed) {
                    continue;
                }
                for (std::size_t ei = 0; ei < parsed->tgis.size() && hits.size() < limit; ++ei) {
                    if (matches(parsed->tgis[ei])) {
                        push_hit(i, "casp.tgi", static_cast<std::int64_t>(ei));
                    }
                }
            }
        }

        json byte_scan_info{{"enabled", byte_scan},
                            {"maxBytesPerResource", bs_max_bytes},
                            {"maxResources", bs_max_res},
                            {"resourcesScanned", 0}};
        if (byte_scan && hits.size() < limit) {
            std::vector<std::byte> needle(16);
            auto put_u32 = [&](std::size_t o, std::uint32_t v) {
                std::memcpy(needle.data() + o, &v, 4);
            };
            auto put_u64 = [&](std::size_t o, std::uint64_t v) {
                std::memcpy(needle.data() + o, &v, 8);
            };
            put_u32(0, target.type);
            put_u32(4, target.group);
            put_u64(8, target.instance);
            std::vector<std::byte> needle_hi(16);
            std::memcpy(needle_hi.data(), needle.data(), 8);
            const auto hi = static_cast<std::uint32_t>(target.instance >> 32);
            const auto lo = static_cast<std::uint32_t>(target.instance);
            std::memcpy(needle_hi.data() + 8, &hi, 4);
            std::memcpy(needle_hi.data() + 12, &lo, 4);

            for (std::uint32_t i = 0; i < s.pkg.count() && hits.size() < limit &&
                                     bs_scanned < bs_max_res;
                 ++i) {
                const auto& e = s.pkg.entry(i);
                if (e.tgi.type == target.type && e.tgi.group == target.group &&
                    e.tgi.instance == target.instance) {
                    continue;
                }
                if (e.tgi.type == sxpe::resources::kRefs || e.tgi.type == sxpe::resources::kObjk ||
                    e.tgi.type == sxpe::resources::kVpxy || e.tgi.type == sxpe::resources::kCasp) {
                    continue;
                }
                if (e.mem_size == 0 || e.mem_size > bs_max_bytes) {
                    continue;
                }
                auto body = s.pkg.uncompressed(i);
                if (!body) {
                    continue;
                }
                ++bs_scanned;
                auto it = std::search(body->begin(), body->end(), needle.begin(), needle.end());
                const char* reason = "byteScan";
                std::int64_t off = -1;
                if (it != body->end()) {
                    off = static_cast<std::int64_t>(it - body->begin());
                } else {
                    it = std::search(body->begin(), body->end(), needle_hi.begin(), needle_hi.end());
                    if (it != body->end()) {
                        off = static_cast<std::int64_t>(it - body->begin());
                        reason = "byteScan.hiLo";
                    }
                }
                if (off >= 0) {
                    json h;
                    h["source"] = item_meta(s.pkg, i, names);
                    h["reason"] = reason;
                    h["offset"] = off;
                    hits.push_back(std::move(h));
                }
            }
            byte_scan_info["resourcesScanned"] = bs_scanned;
        }

        const bool truncated = hits.size() >= limit;
        json data{{"target", tgi_json(target)},
                  {"hits", hits},
                  {"scanned",
                   {{"refs", scanned_refs},
                    {"objk", scanned_objk},
                    {"vpxy", scanned_vpxy},
                    {"casp", scanned_casp}}},
                  {"byteScan", std::move(byte_scan_info)},
                  {"truncated", truncated}};
        data["summary"] = find_refs_summary_json(data);
        return envelope_ok(std::move(data));
    }

    if (cmd == "search.bytes") {
        std::vector<std::byte> needle;
        if (args.contains("hex")) {
            auto hs = args["hex"].get<std::string>();
            if (hs.size() % 2) {
                return envelope_err(err(ErrorCode::invalid_argument, "hex length"));
            }
            for (std::size_t i = 0; i < hs.size(); i += 2) {
                needle.push_back(std::byte{static_cast<unsigned char>(
                    std::stoul(hs.substr(i, 2), nullptr, 16))});
            }
        } else if (args.contains("text")) {
            auto ts = args["text"].get<std::string>();
            needle.resize(ts.size());
            for (std::size_t i = 0; i < ts.size(); ++i) {
                needle[i] = static_cast<std::byte>(static_cast<unsigned char>(ts[i]));
            }
        } else {
            return envelope_err(err(ErrorCode::invalid_argument, "hex or text"));
        }
        json hits = json::array();
        const auto limit = args.value("limit", 100);
        const auto& names = name_index(s);
        for (std::uint32_t i = 0; i < s.pkg.count() && hits.size() < limit; ++i) {
            if (s.pkg.entry(i).mem_size > 16u << 20) {
                continue;
            }
            auto body = s.pkg.uncompressed(i);
            if (!body) {
                continue;
            }
            auto it = std::search(body->begin(), body->end(), needle.begin(), needle.end());
            if (it != body->end()) {
                json h = item_meta(s.pkg, i, names);
                h["offset"] = static_cast<int>(it - body->begin());
                hits.push_back(h);
            }
        }
        return envelope_ok({{"hits", hits}});
    }
    if (cmd == "plan") {
        auto inner = args.at("args");
        inner["dryRun"] = true;
        if (args.contains("sessionId")) {
            inner["sessionId"] = args["sessionId"];
        }
        return exec(args.at("command").get<std::string>(), inner);
    }
    if (cmd == "undo" || cmd == "redo") {
        auto& src = (cmd == "undo") ? s.undo : s.redo;
        auto& dst = (cmd == "undo") ? s.redo : s.undo;
        if (src.empty()) {
            return envelope_err(err(ErrorCode::not_found,
                                    cmd == "undo" ? "nothing to undo" : "nothing to redo"));
        }
        auto u = std::move(src.back());
        src.pop_back();
        UndoItem inverse;
        if (u.kind == "insert") {
            inverse.kind = "remove";
        } else if (u.kind == "remove") {
            auto cap = capture_item(s, u.index, "insert");
            if (!cap) {
                return envelope_err(cap.error());
            }
            inverse = std::move(*cap);
        } else {
            auto cap = capture_item(s, u.index, u.kind);
            if (!cap) {
                return envelope_err(cap.error());
            }
            inverse = std::move(*cap);
        }
        if (auto r = apply_item(s, u); !r) {
            return envelope_err(r.error());
        }
        if (inverse.kind == "remove") {
            inverse.index = u.index;
        }
        push_stack(dst, std::move(inverse));
        json data;
        data[cmd == "undo" ? "undone" : "redone"] = true;
        return envelope_ok(std::move(data));
    }
    return envelope_err(err(ErrorCode::not_found, "unknown command " + cmd));
}

}  // namespace sxpe::commands
