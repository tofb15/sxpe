#include "sxpe/commands/bus.hpp"

#include "sxpe/core/caps.hpp"
#include "sxpe/games/sims3/fnv.hpp"
#include "sxpe/games/sims3/package.hpp"
#include "sxpe/games/sims3/tgi.hpp"
#include "sxpe/resources/dds.hpp"
#include "sxpe/resources/dir.hpp"
#include "sxpe/resources/nmap.hpp"
#include "sxpe/resources/objk.hpp"
#include "sxpe/resources/s3sa.hpp"
#include "sxpe/resources/png.hpp"
#include "sxpe/resources/stbl.hpp"
#include "sxpe/resources/types.hpp"
#include "sxpe/resources/vpxy.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sxpe::commands {
namespace {

using nlohmann::json;
using sxpe::games::sims3::Package;
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
};

sxpe::resources::Nmap load_nmap(Package& pkg) {
    sxpe::resources::Nmap merged;
    merged.version = 1;
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
        for (auto& e : n->entries) {
            merged.entries.push_back(std::move(e));
        }
    }
    return merged;
}

std::unordered_map<std::uint64_t, std::string> name_index(Package& pkg) {
    auto names = load_nmap(pkg);
    std::unordered_map<std::uint64_t, std::string> m;
    m.reserve(names.entries.size() * 2 + 1);
    for (auto& e : names.entries) {
        m.insert_or_assign(e.instance, std::move(e.name));
    }
    return m;
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
                                   "this neighborhood file has no name map and cannot gain one"));
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
         "Open a DBPF file via mmap after sniffing Sims 3. Example: {\"path\":\"mod.package\"}. Do not use for Sims 4.",
         obj_schema({{"path", {{"type", "string"}}},
                     {"writable", {{"type", "boolean"}, {"default", false}}},
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
         "keep on-disk layout. Does not save other open packages.",
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
    add({"package.info", "Package info", "Header summary for a session.",
         obj_schema({{"sessionId", sess_prop()}}, json::array({"sessionId"})), env_out, true, false,
         true, false});
    add({"package.validate", "Validate", "Sniff + cap checks on an open session.",
         obj_schema({{"sessionId", sess_prop()}}, json::array({"sessionId"})), env_out, true, false,
         true, false});
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
         "writeMergeManifest records SXMM so package.unmerge can reverse an SXPE merge.",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"paths", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                     {"writeMergeManifest", {{"type", "boolean"}, {"default", false}}},
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
         "Treat .dbc/DBPF files as packages and copy resources. Pass path or paths[].",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"paths", {{"type", "array"}, {"items", {{"type", "string"}}}}},
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
         "Merged name-map entries. These strings are the Name column on resource.list.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId"})),
         env_out, true, false, true, false});
    add({"nmap.set", "NMAP set",
         "Set the display name for an instance (the Name column). Creates a name map "
         "if the package has none. Prefer resource.rename when you have a resourceId.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"instance", {{"type", "integer"}}},
                     {"name", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "instance", "name"})),
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
    add({"dds.info", "DDS info", "Width/height/format from a DDS resource.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"dds.decode", "DDS decode",
         "Decode DXT1, DXT5, or 24/32-bit RGB(A) to RGBA byte count. Other DDS formats "
         "return unsupported. Does not embed pixels in MCP (use dds.export for the file).",
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
    add({"clip.exportAs", "CLIP export as new name",
         "Copy CLIP with instance = fnv64_clip (age-letter masks, SimsWiki 0x6B20C4F3).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"name", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "name"})),
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
    add({"editor.list", "List editors", "Headless editors (STBL, NMAP, DDS, S3SA, CLIP).",
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
                {"game", "sims3"}};
    }

    json exec(std::string_view id, json args);
};

Bus::Bus() : impl_(std::make_unique<Impl>()) {}
Bus::~Bus() = default;
Bus::Bus(Bus&&) noexcept = default;
Bus& Bus::operator=(Bus&&) noexcept = default;

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
    auto names = name_index(s->pkg);
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
        const bool wr = args.value("writable", false);
        if (auto* existing = find_by_path(*path)) {
            auto out = info(*existing);
            out["alreadyOpen"] = true;
            return envelope_ok(out);
        }
        auto p = Package::open(*path, wr);
        if (!p) {
            return envelope_err(p.error(), p.error().code == ErrorCode::io, "none");
        }
        auto idr = add_session(std::move(*p));
        if (!idr) {
            return envelope_err(idr.error());
        }
        auto* s = find(*idr);
        auto out = info(*s);
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
        for (std::uint32_t i = 0; i < s.pkg.count(); ++i) {
            if (s.pkg.entry(i).tgi.type != sxpe::resources::kDir) {
                continue;
            }
            dir["present"] = true;
            auto body = s.pkg.uncompressed(i);
            if (!body) {
                issues.push_back("dir_unreadable");
                break;
            }
            auto parsed = sxpe::resources::parse_dir(*body);
            if (!parsed) {
                issues.push_back("dir_corrupt");
                break;
            }
            dir["records"] = parsed->size();
            dir["recordBytes"] = body->size() % 20 == 0 ? 20 : 16;
            std::uint32_t missing = 0;
            for (const auto& d : *parsed) {
                bool found = false;
                for (std::uint32_t j = 0; j < s.pkg.count(); ++j) {
                    const auto& e = s.pkg.entry(j);
                    if (e.tgi.type == d.tgi.type && e.tgi.group == d.tgi.group &&
                        e.tgi.instance == d.tgi.instance && e.mem_size == d.mem_size) {
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
            break;
        }
        return envelope_ok({{"ok", issues.empty()},
                            {"issues", issues},
                            {"indexCount", s.pkg.count()},
                            {"dir", dir}});
    }
    if (cmd == "package.save" || cmd == "package.compact") {
        if (cmd == "package.compact" && neighborhood_file(s.pkg.path())) {
            return envelope_err(err(ErrorCode::refused,
                                    "compact rebuilds the file; not supported for .nhd/.world/.dbc"));
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
        auto names = name_index(s.pkg);
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
        auto names = name_index(s.pkg);
        json data = item_meta(s.pkg, *i, names);
        if (args.value("includePayload", false)) {
            auto body = s.pkg.uncompressed(*i);
            if (!body) {
                return envelope_err(body.error());
            }
            std::uint32_t maxb = args.value("maxBytes", 0);
            if (maxb == 0 || maxb > kPayloadCap) {
                maxb = kPayloadCap;
            }
            if (body->size() > maxb) {
                data["payloadB64"] = b64_encode(std::span<const std::byte>(body->data(), maxb));
                data["truncated"] = true;
            } else {
                data["payloadB64"] = b64_encode(*body);
                data["truncated"] = false;
            }
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
        json packages = json::array();
        json errors = json::array();
        json sources = json::array();
        const bool write_man = args.value("writeMergeManifest", false);
        std::uint32_t imported = 0;
        std::uint32_t would = 0;
        int src_n = 0;
        for (const auto& rawp : paths) {
            auto path = check_path(rawp);
            if (!path) {
                errors.push_back({{"path", rawp}, {"message", path.error().message}});
                continue;
            }
            auto src = Package::open(*path, false);
            if (!src) {
                errors.push_back({{"path", path->string()}, {"message", src.error().message}});
                continue;
            }
            would += src->count();
            if (dry(args)) {
                packages.push_back({{"path", path->string()}, {"count", src->count()}});
                continue;
            }
            std::uint32_t n = 0;
            bool file_ok = true;
            json recs = json::array();
            ++src_n;
            for (std::uint32_t i = 0; i < src->count(); ++i) {
                const auto t = src->entry(i).tgi;
                if (write_man && (t.type == sxpe::resources::kSxmm || t.type == sxpe::resources::kDir)) {
                    continue;
                }
                auto ex = s.pkg.find(t, src->entry(i).ordinal);
                if (ex && !force(args)) {
                    errors.push_back({{"path", path->string()},
                                      {"message", "duplicate TGI; pass force"}});
                    file_ok = false;
                    break;
                }
                if (ex) {
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
                sources.push_back({{"id", "src-" + std::to_string(src_n)},
                                   {"originalFileName", path->filename().string()},
                                   {"resources", recs}});
            }
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"packages", packages.size()}, {"count", would}});
        }
        if (write_man && imported > 0) {
            json man{{"format", "sxpe.mergeManifest"},
                     {"version", 1},
                     {"sources", sources},
                     {"notes",
                      {{"forceOverwriteOnDuplicateTgi", force(args)}, {"dirPolicy", "strip"}}}};
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
        json out{{"imported", imported},
                 {"packages", packages.size()},
                 {"failed", errors.size()},
                 {"errors", errors},
                 {"mergeManifest", write_man}};
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
    if (cmd == "nmap.get") {
        json arr = json::array();
        auto names = load_nmap(s.pkg);
        for (const auto& e : names.entries) {
            arr.push_back({{"instance", e.instance}, {"name", e.name}});
        }
        return envelope_ok({{"entries", arr}});
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
        bool found = false;
        for (auto& e : n->entries) {
            if (e.instance == inst) {
                e.name = name;
                found = true;
                break;
            }
        }
        if (!found) {
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
                  {"compressed", inf->compressed}};
        if (cmd == "dds.decode") {
            auto pix = sxpe::resources::decode_dds_rgba(*body);
            if (!pix) {
                return envelope_err(pix.error());
            }
            data["rgbaBytes"] = pix->size();
        }
        return envelope_ok(data);
    }
    if (cmd == "objk.get" || cmd == "vpxy.get" || cmd == "graph.get") {
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
    if (cmd == "clip.exportAs") {
        auto i = need_idx();
        if (!i) {
            return envelope_err(i.error());
        }
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
        }
        const auto name = args.at("name").get<std::string>();
        Tgi t = s.pkg.entry(*i).tgi;
        t.type = sxpe::resources::kClip;
        t.instance = sxpe::games::sims3::fnv64_clip(name);
        if (dry(args)) {
            return envelope_ok({{"dryRun", true},
                                {"resourceId", tgi_json(t)},
                                {"filename", sxpe::games::sims3::community_filename(
                                                 t, name, "CLIP.animation")}});
        }
        auto r = s.pkg.add(t, *body, s.pkg.entry(*i).compressed == 0xFFFF);
        if (!r) {
            return envelope_err(r.error());
        }
        return envelope_ok({{"resourceId", rid_json(s.pkg.entry(*r).tgi, s.pkg.entry(*r).ordinal)},
                            {"filename", sxpe::games::sims3::community_filename(
                                             t, name, "CLIP.animation")}});
    }
    if (cmd == "s3sa.info" || cmd == "s3sa.exportDll" || cmd == "s3sa.importDll") {
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
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        if (std::filesystem::exists(*path) && !force(args)) {
            return envelope_err(err(ErrorCode::refused, "exists; pass force"));
        }
        auto pe = sxpe::resources::export_pe(*body);
        if (!pe) {
            return envelope_err(pe.error());
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
        auto names = name_index(s.pkg);
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
