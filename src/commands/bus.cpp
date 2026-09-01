#include "sxpe/commands/bus.hpp"

#include "sxpe/core/caps.hpp"
#include "sxpe/games/sims3/fnv.hpp"
#include "sxpe/games/sims3/package.hpp"
#include "sxpe/games/sims3/tgi.hpp"
#include "sxpe/resources/dds.hpp"
#include "sxpe/resources/nmap.hpp"
#include "sxpe/resources/s3sa.hpp"
#include "sxpe/resources/png.hpp"
#include "sxpe/resources/stbl.hpp"
#include "sxpe/resources/types.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace sxpe::commands {
namespace {

using nlohmann::json;
using sxpe::games::sims3::Package;
using sxpe::games::sims3::Tgi;
using sxpe::resources::kNmap;
using sxpe::resources::kS3sa;
using sxpe::resources::kStbl;

constexpr int kMaxSessions = 8;
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

Tgi tgi_from(const json& j) {
    Tgi t;
    t.type = static_cast<std::uint32_t>(as_u64(j.at("type")));
    t.group = static_cast<std::uint32_t>(as_u64(j.at("group")));
    t.instance = as_u64(j.at("instance"));
    return t;
}

json tgi_json(Tgi t) {
    return {{"type", t.type}, {"group", t.group}, {"instance", t.instance}};
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
    add({"package.save", "Save", "Unmap then ReplaceFile. dryRun reports the path only.",
         obj_schema({{"sessionId", sess_prop()}, {"dryRun", dry_prop()}}, json::array({"sessionId"})),
         env_out, false, true, false, true});
    add({"package.saveAs", "Save As", "Write to a new path and switch the session to it.",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"dryRun", dry_prop()},
                     {"force", force_prop()}},
                    json::array({"sessionId", "path"})),
         env_out, false, true, false, true});
    add({"package.saveCopyAs", "Save Copy As", "Write a copy without changing the session path.",
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
    add({"resource.add", "Add resource", "Insert bytes with a TGI.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"payloadB64", {{"type", "string"}}},
                     {"compress", {{"type", "boolean"}, {"default", false}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "payloadB64"})),
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
    add({"resource.replace", "Replace payload", "Keep TGI, replace uncompressed body.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"payloadB64", {{"type", "string"}}},
                     {"compress", {{"type", "boolean"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "payloadB64"})),
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
    add({"resource.importPackage", "Import package", "Copy resources from another TS3 package.",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "path"})),
         env_out, false, true, false, true});
    add({"resource.importDbc", "Import DBC",
         "Treat a .dbc/DBPF as a package and copy resources (surveyed: same container as .package).",
         obj_schema({{"sessionId", sess_prop()},
                     {"path", {{"type", "string"}}},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "path"})),
         env_out, false, true, false, true});
    add({"resource.exportToPackage", "Export to package", "Copy one resource into a dest package (created if missing).",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"force", force_prop()},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, false, true, false, true});
    add({"resource.setFlags", "Set flags", "compressed and/or session deleted.",
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
    add({"nmap.get", "NMAP get", "Name map entries.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId"})),
         env_out, true, false, true, false});
    add({"nmap.set", "NMAP set", "Set the name for an instance in a name map.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"instance", {{"type", "integer"}}},
                     {"name", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "instance", "name"})),
         env_out, false, true, false, false});
    add({"dds.info", "DDS info", "Width/height/format from a DDS resource.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"dds.decode", "DDS decode", "Decode to RGBA metadata (size); does not dump pixels in MCP.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"dds.export", "DDS export", "Write the on-disk DDS bytes to a .dds path.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"force", force_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
         env_out, true, false, true, true});
    add({"objk.get", "OBJK get", "Graph of an object-key resource; unknown fields stay bytes.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"vpxy.get", "VPXY get", "Graph of a visual-proxy resource; unknown chunks stay bytes.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"clip.exportAs", "CLIP export as new name", "Copy CLIP with instance = FNV-1 64 of the new name.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"name", {{"type", "string"}}},
                     {"dryRun", dry_prop()}},
                    json::array({"sessionId", "resourceId", "name"})),
         env_out, false, true, false, false});
    add({"s3sa.info", "S3SA info", "Size, PE offset if MZ found, ManifestModule hint from NMAP. Never LoadLibrary.",
         obj_schema({{"sessionId", sess_prop()}, {"resourceId", rid_schema()}},
                    json::array({"sessionId", "resourceId"})),
         env_out, true, false, true, false});
    add({"s3sa.exportDll", "Export DLL", "Write the PE blob (or full payload) using the module hint name unless path is set.",
         obj_schema({{"sessionId", sess_prop()},
                     {"resourceId", rid_schema()},
                     {"path", {{"type", "string"}}},
                     {"force", force_prop()}},
                    json::array({"sessionId", "resourceId", "path"})),
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
    add({"hash.fnv", "FNV-1", "FNV-1 32/64 or CLIP (FNV-1 64 lowercase). Example: {\"text\":\"a\",\"width\":32}.",
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
    add({"redo", "Redo", "Redo last undone mutation.",
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

    Result<std::string> add_session(Package p) {
        if (static_cast<int>(sessions.size()) >= kMaxSessions) {
            return std::unexpected(err(ErrorCode::refused, "session cap (8)"));
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
        return envelope_ok({{"ok", issues.empty()}, {"issues", issues}, {"indexCount", s.pkg.count()}});
    }
    if (cmd == "package.save" || cmd == "package.compact") {
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
        auto raw = b64_decode(args.at("payloadB64").get<std::string>());
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
        auto raw = b64_decode(args.at("payloadB64").get<std::string>());
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
        auto existing = s.pkg.find(t, 0);
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
        auto path = check_path(args.at("path").get<std::string>());
        if (!path) {
            return envelope_err(path.error());
        }
        auto src = Package::open(*path, false);
        if (!src) {
            return envelope_err(src.error());
        }
        if (dry(args)) {
            return envelope_ok({{"dryRun", true}, {"count", src->count()}});
        }
        json copied = json::array();
        for (std::uint32_t i = 0; i < src->count(); ++i) {
            auto body = src->uncompressed(i);
            if (!body) {
                return envelope_err(body.error());
            }
            const auto t = src->entry(i).tgi;
            auto ex = s.pkg.find(t, src->entry(i).ordinal);
            if (ex && !force(args)) {
                return envelope_err(err(ErrorCode::refused, "duplicate TGI; pass force"));
            }
            if (ex) {
                s.pkg.set_uncompressed(*ex, *body, src->entry(i).compressed == 0xFFFF);
            } else {
                auto r = s.pkg.add(t, *body, src->entry(i).compressed == 0xFFFF);
                if (!r) {
                    return envelope_err(r.error());
                }
            }
            copied.push_back(tgi_json(t));
        }
        return envelope_ok({{"imported", copied.size()}});
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
        auto body = s.pkg.uncompressed(*i);
        if (!body) {
            return envelope_err(body.error());
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
        auto r = dest.add(s.pkg.entry(*i).tgi, *body, s.pkg.entry(*i).compressed == 0xFFFF);
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
    if (cmd == "nmap.set") {
        std::optional<std::uint32_t> ni;
        if (args.contains("resourceId")) {
            auto i = need_idx();
            if (!i) {
                return envelope_err(i.error());
            }
            ni = *i;
        } else {
            for (std::uint32_t i = 0; i < s.pkg.count(); ++i) {
                if (s.pkg.entry(i).tgi.type == kNmap) {
                    ni = i;
                    break;
                }
            }
        }
        if (!ni) {
            return envelope_err(err(ErrorCode::not_found, "nmap"));
        }
        auto body = s.pkg.uncompressed(*ni);
        if (!body) {
            return envelope_err(body.error());
        }
        auto n = sxpe::resources::parse_nmap(*body);
        if (!n) {
            return envelope_err(n.error());
        }
        const auto inst = as_u64(args.at("instance"));
        const auto name = args.at("name").get<std::string>();
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
            return envelope_ok({{"dryRun", true}});
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
        const auto tag = std::string(sxpe::resources::tag_for(s.pkg.entry(*i).tgi.type));
        json nodes = json::array();
        if (s.pkg.entry(*i).tgi.type == kStbl) {
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
    if (cmd == "s3sa.info" || cmd == "s3sa.exportDll") {
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
            json j{{"size", inf.size}, {"moduleHint", inf.module_hint}};
            if (inf.pe_offset) {
                j["peOffset"] = *inf.pe_offset;
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
        std::span<const std::byte> blob = *body;
        if (inf.pe_offset) {
            blob = std::span<const std::byte>(body->data() + *inf.pe_offset,
                                              body->size() - *inf.pe_offset);
        }
        if (auto w = write_file(*path, blob); !w) {
            return envelope_err(w.error());
        }
        return envelope_ok({{"path", path->string()}, {"bytes", blob.size()}});
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
    if (cmd == "undo") {
        if (s.undo.empty()) {
            return envelope_err(err(ErrorCode::not_found, "nothing to undo"));
        }
        auto u = std::move(s.undo.back());
        s.undo.pop_back();
        if (u.kind == "remove") {
            s.pkg.remove(u.index);
        } else if (u.kind == "inplace") {
            auto r = s.pkg.patch_in_place(u.index, u.payload, u.compressed == 0xFFFF);
            if (!r) {
                return envelope_err(r.error());
            }
        } else if (u.kind == "insert" || u.kind == "restore") {
            if (u.kind == "insert") {
                auto r = s.pkg.add(u.tgi, u.payload, u.compressed == 0xFFFF);
                if (!r) {
                    return envelope_err(r.error());
                }
            } else {
                s.pkg.rekey(u.index, u.tgi);
                s.pkg.set_uncompressed(u.index, u.payload, u.compressed == 0xFFFF);
                s.pkg.set_deleted(u.index, u.deleted);
            }
        }
        s.redo.push_back(std::move(u));
        return envelope_ok({{"undone", true}});
    }
    if (cmd == "redo") {
        return envelope_err(err(ErrorCode::refused, "redo not replayed; re-issue the command"));
    }
    return envelope_err(err(ErrorCode::not_found, "unknown command " + cmd));
}

}  // namespace sxpe::commands
