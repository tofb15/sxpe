#include "sxpe/commands/bus.hpp"

#include <CLI11.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef SXPE_VERSION
#define SXPE_VERSION "0.0.0"
#endif

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

using nlohmann::json;
using sxpe::commands::Bus;

bool stdout_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

std::string camel(std::string k) {
    std::string o;
    bool up = false;
    for (char c : k) {
        if (c == '-' || c == '_') {
            up = true;
            continue;
        }
        o.push_back(up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        up = false;
    }
    return o;
}

bool plain_int(std::string_view v) {
    if (v.empty()) {
        return false;
    }
    std::size_t i = 0;
    if (v[0] == '-' || v[0] == '+') {
        ++i;
    }
    if (i >= v.size()) {
        return false;
    }
    for (; i < v.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(v[i]))) {
            return false;
        }
    }
    return true;
}

json parse_rest(const std::vector<std::string>& extra, json args) {
    for (std::size_t i = 0; i < extra.size(); ++i) {
        const auto& a = extra[i];
        if (a.rfind("--", 0) != 0) {
            continue;
        }
        auto key = camel(a.substr(2));
        if (i + 1 < extra.size() && extra[i + 1].rfind("--", 0) != 0) {
            const auto& v = extra[++i];
            if (v == "true") {
                args[key] = true;
            } else if (v == "false") {
                args[key] = false;
            } else if (plain_int(v)) {
                try {
                    args[key] = std::stoll(v);
                } catch (...) {
                    args[key] = v;
                }
            } else if (!v.empty() && (v.front() == '{' || v.front() == '[')) {
                try {
                    args[key] = json::parse(v);
                } catch (const json::exception& e) {
                    throw std::runtime_error(std::string("invalid JSON for --") + key + ": " + e.what());
                }
            } else {
                args[key] = v;
            }
        } else {
            args[key] = true;
        }
    }
    return args;
}

std::optional<sxpe::commands::Tool> find_tool(const Bus& bus, std::string_view id) {
    for (const auto& t : bus.tools()) {
        if (t.id == id) {
            return t;
        }
    }
    return std::nullopt;
}

void print_tool_line(const sxpe::commands::Tool& t) {
    std::cout << "  " << t.id;
    if (t.input_schema.contains("required") && t.input_schema["required"].is_array() &&
        !t.input_schema["required"].empty()) {
        std::cout << "  (";
        bool first = true;
        for (const auto& r : t.input_schema["required"]) {
            const auto s = r.get<std::string>();
            if (s == "sessionId") {
                continue;
            }
            if (!first) {
                std::cout << ", ";
            }
            first = false;
            std::cout << s;
        }
        if (first) {
            std::cout << "session)";
        } else {
            std::cout << ")";
        }
    }
    std::cout << "\n      " << t.title;
    if (!t.description.empty()) {
        std::cout << " — " << t.description << '\n';
    } else {
        std::cout << '\n';
    }
}

void print_catalog(const Bus& bus, std::string_view filter) {
    std::cout << "Commands";
    if (!filter.empty()) {
        std::cout << " matching '" << filter << "'";
    }
    std::cout << ":\n";
    bool any = false;
    for (const auto& t : bus.tools()) {
        if (!filter.empty() && t.id.find(filter) == std::string::npos) {
            continue;
        }
        any = true;
        print_tool_line(t);
    }
    if (!any) {
        std::cout << "  (no tools match)\n";
    }
}

void print_global_help(const Bus& bus) {
    std::cout << "SXPE " << SXPE_VERSION << " — Sims 3 package editor (CLI)\n\n";
    std::cout << "usage: sxpe <noun> <verb> [options]\n";
    std::cout << "       sxpe help [noun[.verb]]\n";
    std::cout << "       sxpe <noun> <verb> --help\n";
    std::cout << "       sxpe --version\n\n";
    std::cout << "How it works:\n";
    std::cout << "  Commands are noun + verb (resource list, package info).\n";
    std::cout << "  --package PATH is one-shot: open, run, save if it writes, close.\n";
    std::cout << "  Every result is JSON {ok, data|error}. --format text|table is a human view.\n";
    std::cout << "  --type/--group/--instance (hex 0x… ok) is an alternative to --id JSON.\n";
    std::cout << "  Destructive commands need --force; --dry-run reports without writing.\n\n";
    std::cout << "One-shot (no session):\n";
    std::cout << "  --package PATH     Open this file, run the command, save if it writes, close\n";
    std::cout << "  --path PATH        Export dest, add/replace payload file, save-as dest\n";
    std::cout << "  --file PATH        Alias of --path for payload files\n";
    std::cout << "  --id JSON          resourceId {type,group,instance,ordinal}\n";
    std::cout << "  --type --group --instance --ordinal\n";
    std::cout << "                     Alternative to --id (hex 0xAABB is ok); do not mix with --id\n";
    std::cout << "  --name TEXT        NMAP display name (resource.rename / nmap.set)\n";
    std::cout << "  --text TEXT        hash.fnv / search.bytes\n";
    std::cout << "  --force --dry-run --writable --include-payload --limit N\n";
    std::cout << "  --format json|jsonl|text|table\n\n";
    std::cout << "Examples:\n";
    std::cout << "  sxpe package info --package mod.package\n";
    std::cout << "  sxpe resource list --package mod.package --limit 20\n";
    std::cout << "  sxpe resource export --package mod.package --type 0x0333406C --group 0 "
                 "--instance 0x1 --path out.xml --force\n";
    std::cout << "  sxpe resource add --package mod.package --type 0x0166038C --group 0 "
                 "--instance 0 --path nmap.bin --force\n";
    std::cout << "  sxpe resource rename --package door.package --type 0x0333406C --group 0 "
                 "--instance 0x1 --name NRaas.NoCD --force\n";
    std::cout << "  sxpe nmap set --package mod.package --instance 0x1 --name NRaas.NoCD --force\n";
    std::cout << "  sxpe package new --package new.package --force\n";
    std::cout << "  sxpe help resource\n";
    std::cout << "  sxpe resource rename --help\n\n";
    print_catalog(bus, {});
}

void print_command_help(const Bus& bus, std::string_view id) {
    const auto t = find_tool(bus, id);
    if (!t) {
        std::cout << "unknown command: '" << id << "'\nTry: sxpe help\n";
        return;
    }
    std::cout << t->id << " — " << t->title << '\n';
    if (!t->description.empty()) {
        std::cout << t->description << "\n\n";
    }
    std::string noun = t->id;
    std::string verb;
    if (const auto dot = t->id.find('.'); dot != std::string::npos) {
        noun = t->id.substr(0, dot);
        verb = t->id.substr(dot + 1);
    }
    std::cout << "usage: sxpe " << noun;
    if (!verb.empty()) {
        std::cout << ' ' << verb;
    }
    std::cout << " [options]\n\n";
    bool needs_session = false;
    bool needs_rid = false;
    if (t->input_schema.contains("required") && t->input_schema["required"].is_array()) {
        std::cout << "Required:\n";
        for (const auto& r : t->input_schema["required"]) {
            const auto s = r.get<std::string>();
            if (s == "sessionId") {
                needs_session = true;
                std::cout << "  sessionId     --package PATH  (one-shot) or --session ID\n";
            } else if (s == "resourceId") {
                needs_rid = true;
                std::cout << "  resourceId    --id JSON  or  --type --group --instance [--ordinal]\n";
            } else {
                std::cout << "  " << s << "\n";
            }
        }
        std::cout << '\n';
    }
    if (t->input_schema.contains("properties") && t->input_schema["properties"].is_object()) {
        std::cout << "Options (from schema):\n";
        for (auto it = t->input_schema["properties"].begin();
             it != t->input_schema["properties"].end(); ++it) {
            if (it.key() == "sessionId" || it.key() == "resourceId") {
                continue;
            }
            std::cout << "  " << it.key();
            if (it.value().contains("type")) {
                std::cout << " (" << it.value()["type"].dump() << ")";
            }
            if (it.value().contains("default")) {
                std::cout << " default=" << it.value()["default"].dump();
            }
            std::cout << '\n';
        }
        std::cout << '\n';
    }
    if (needs_session) {
        std::cout << "One-shot: --package PATH fills sessionId; writes save unless --dry-run.\n";
    }
    if (needs_rid) {
        std::cout << "TGI: pass --id JSON or --type/--group/--instance, not both.\n";
    }
    std::cout << "Also: --force --dry-run --format json|jsonl|text|table\n";
}

std::string cell(const json& j, const char* key, const char* alt = nullptr) {
    auto one = [](const json& v) -> std::string {
        if (v.is_string()) {
            auto s = v.get<std::string>();
            if (s.size() > 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) {
                return s.substr(2);
            }
            return s;
        }
        if (v.is_boolean()) {
            return v.get<bool>() ? "Y" : "";
        }
        if (v.is_number()) {
            return v.dump();
        }
        return {};
    };
    if (j.contains(key)) {
        return one(j[key]);
    }
    if (alt && j.contains(alt)) {
        return one(j[alt]);
    }
    return {};
}

void print_rows_table(const json& rows) {
    if (!rows.is_array() || rows.empty()) {
        std::cout << "(none)\n";
        return;
    }
    const bool list_items = rows[0].contains("typeHex") || rows[0].contains("type");
    const bool nmap_rows = rows[0].contains("name") && rows[0].contains("instance") && !list_items;
    const bool stbl_rows = rows[0].contains("text") && rows[0].contains("id");
    if (list_items) {
        std::cout << "TAG   TYPE      GROUP     INSTANCE          NAME                     SIZE\n";
        for (const auto& it : rows) {
            auto name = cell(it, "name");
            if (name.size() > 24) {
                name.resize(21);
                name += "...";
            }
            std::cout << std::left << std::setw(6) << cell(it, "tag")
                      << std::setw(10) << cell(it, "typeHex", "type")
                      << std::setw(10) << cell(it, "groupHex", "group")
                      << std::setw(18) << cell(it, "instanceHex", "instance")
                      << std::setw(25) << name << cell(it, "memSize", "fileSize") << '\n';
        }
        return;
    }
    if (nmap_rows) {
        std::cout << "INSTANCE          NAME\n";
        for (const auto& it : rows) {
            std::cout << std::left << std::setw(18) << cell(it, "instance") << cell(it, "name")
                      << '\n';
        }
        return;
    }
    if (stbl_rows) {
        std::cout << "ID                  TEXT\n";
        for (const auto& it : rows) {
            std::cout << std::left << std::setw(20) << cell(it, "id") << cell(it, "text") << '\n';
        }
        return;
    }
    for (const auto& it : rows) {
        std::cout << it.dump() << '\n';
    }
}

void print_object_text(const json& data) {
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (it.key() == "items" || it.key() == "entries" || it.key() == "tools") {
            continue;
        }
        std::cout << it.key() << ": ";
        if (it.value().is_string() || it.value().is_number() || it.value().is_boolean()) {
            std::cout << (it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
        } else {
            std::cout << it.value().dump();
        }
        std::cout << '\n';
    }
}

int print_result(const json& env, const std::string& format, bool list_like) {
    const bool human = format == "text" || format == "table";
    if (format == "jsonl" && list_like && env.value("ok", false)) {
        const auto& data = env["data"];
        if (data.contains("items") && data["items"].is_array()) {
            for (const auto& it : data["items"]) {
                std::cout << it.dump() << '\n';
            }
            if (data.value("truncated", false)) {
                std::cout << json({{"nextCursor", data.value("nextCursor", "")},
                                   {"truncated", true}})
                                 .dump()
                          << '\n';
            }
            return 0;
        }
        if (data.contains("tools") && data["tools"].is_array()) {
            for (const auto& it : data["tools"]) {
                std::cout << it.dump() << '\n';
            }
            return 0;
        }
    }
    if (human) {
        if (!env.value("ok", false)) {
            std::string msg = "error";
            if (env.contains("error") && env["error"].contains("message")) {
                msg = env["error"]["message"].get<std::string>();
            }
            std::cerr << msg << '\n';
        } else {
            const auto& data = env["data"];
            if (data.contains("items") && data["items"].is_array()) {
                print_rows_table(data["items"]);
                if (data.value("truncated", false)) {
                    std::cout << "(truncated; nextCursor " << data.value("nextCursor", "") << ")\n";
                }
                return 0;
            }
            if (data.contains("entries") && data["entries"].is_array()) {
                print_rows_table(data["entries"]);
                return 0;
            }
            if (data.contains("tools") && data["tools"].is_array()) {
                print_rows_table(data["tools"]);
                return 0;
            }
            if (data.is_object()) {
                print_object_text(data);
                return 0;
            }
            std::cout << data.dump(2) << '\n';
            return 0;
        }
    } else {
        std::cout << env.dump() << '\n';
        if (env.value("ok", false)) {
            return 0;
        }
    }
    try {
        const auto code = env["error"]["code"].get<std::string>();
        if (code == "invalid_argument") {
            return 2;
        }
        if (code == "not_found") {
            return 3;
        }
        if (code == "refused") {
            return 4;
        }
        if (code == "unsupported_game_or_format" || code == "protected_or_encrypted") {
            return 5;
        }
        if (code == "io" || code == "cap_exceeded") {
            return 6;
        }
        if (code == "corrupt" || code == "refpack") {
            return 7;
        }
    } catch (...) {
    }
    return 1;
}

bool is_mutating(Bus& bus, const std::string& id) {
    for (const auto& t : bus.tools()) {
        if (t.id == id) {
            return !t.read_only;
        }
    }
    return true;
}

bool is_list(const std::string& id) {
    return id == "resource.list" || id == "manifest" || id == "handler.list" || id == "editor.list" ||
           id == "search.bytes" || id == "stbl.get" || id == "nmap.get";
}

bool skip_oneshot_open(const std::string& id) {
    return id == "package.open" || id == "session.start" || id == "package.new" || id == "manifest" ||
           id == "hash.fnv" || id == "s3sa.wrap" || id == "package.unmerge" || id == "help";
}

json make_resource_id(const std::string& type_s, const std::string& group_s,
                      const std::string& inst_s, const std::string& ord_s) {
    json rid = json::object();
    auto put = [&](const char* k, const std::string& v, const char* fallback) {
        rid[k] = v.empty() ? fallback : v;
    };
    put("type", type_s, "0");
    put("group", group_s, "0");
    put("instance", inst_s, "0");
    if (!ord_s.empty()) {
        rid["ordinal"] = ord_s;
    }
    return rid;
}

}  // namespace

int main(int argc, char** argv) {
    bool want_help = false;
    bool want_version = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help" || a == "--help-all") {
            want_help = true;
        }
        if (a == "-V" || a == "--version") {
            want_version = true;
        }
    }
    if (want_version && !want_help) {
        std::cout << "sxpe " << SXPE_VERSION << '\n';
        return 0;
    }

    CLI::App app{"SXPE — Sims 3 package editor (CLI)"};
    app.set_help_flag();  // handled above
    app.set_help_all_flag();
    std::string format;
    std::string session;
    std::string package;
    std::string game;
    std::string filter;
    std::string id_json;
    std::string path;
    std::string file;
    std::string name;
    std::string text;
    std::string type_s;
    std::string group_s;
    std::string inst_s;
    std::string ord_s;
    bool dry = false;
    bool force = false;
    bool writable = false;
    bool include_payload = false;
    int limit = 0;
    app.add_option("--format", format, "json | jsonl | text | table");
    app.add_flag("--dry-run", dry);
    app.add_flag("--force", force);
    app.add_option("--session", session);
    app.add_option("--package", package, "One-shot package path (open/exec/close)");
    app.add_option("--game", game);
    app.add_option("--filter", filter, "JSON filter object");
    app.add_option("--id", id_json, "resourceId JSON {type,group,instance,ordinal}");
    app.add_option("--path", path, "File path (export dest, add/replace bytes, save-as dest)");
    app.add_option("--file", file, "Alias of --path for payload files");
    app.add_option("--name", name, "NMAP display name or CLIP name");
    app.add_option("--text", text, "Text for hash.fnv / search.bytes");
    app.add_option("--type", type_s, "Resource type (decimal or 0x hex)");
    app.add_option("--group", group_s, "Resource group (decimal or 0x hex)");
    app.add_option("--instance", inst_s, "Resource instance (decimal or 0x hex)");
    app.add_option("--ordinal", ord_s, "Duplicate TGI ordinal (default 0)");
    app.add_flag("--writable", writable);
    app.add_flag("--include-payload", include_payload);
    app.add_option("--limit", limit);
    std::string cursor;
    app.add_option("--cursor", cursor);
    app.allow_extras();
    std::string noun;
    std::string verb;
    app.add_option("noun", noun);
    app.add_option("verb", verb);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    Bus bus;
    if (want_help || noun.empty() || noun == "help") {
        std::string filter;
        if (noun == "help") {
            filter = verb;
        } else if (want_help && !noun.empty()) {
            filter = verb.empty() ? noun : noun + "." + verb;
        }
        if (filter.find('.') != std::string::npos) {
            print_command_help(bus, filter);
            return 0;
        }
        if (!filter.empty()) {
            print_catalog(bus, filter);
            return 0;
        }
        print_global_help(bus);
        return (want_help || noun == "help") ? 0 : 2;
    }

    if (format.empty()) {
        format = stdout_tty() ? "text" : "json";
        const auto idguess = verb.empty() ? noun : noun + "." + verb;
        if (!stdout_tty() && is_list(idguess)) {
            format = "jsonl";
        }
    }

    json args = json::object();
    if (!session.empty()) {
        args["sessionId"] = session;
    }
    if (dry) {
        args["dryRun"] = true;
    }
    if (force) {
        args["force"] = true;
    }
    if (writable) {
        args["writable"] = true;
    }
    if (include_payload) {
        args["includePayload"] = true;
    }
    if (limit > 0) {
        args["limit"] = limit;
    }
    if (!cursor.empty()) {
        args["cursor"] = cursor;
    }
    if (!game.empty()) {
        args["game"] = game;
    }
    if (!name.empty()) {
        args["name"] = name;
    }
    if (!text.empty()) {
        args["text"] = text;
    }
    if (!filter.empty() && filter.front() == '{') {
        try {
            args["filter"] = json::parse(filter);
        } catch (const json::exception& e) {
            std::cerr << "{\"ok\":false,\"error\":{\"code\":\"invalid_argument\",\"message\":\""
                      << e.what() << "\"}}\n";
            return 2;
        }
    }
    if (!id_json.empty()) {
        try {
            args["resourceId"] = json::parse(id_json);
        } catch (const json::exception& e) {
            std::cerr << "{\"ok\":false,\"error\":{\"code\":\"invalid_argument\",\"message\":"
                         "\"--id is not JSON: "
                      << e.what() << "\"}}\n";
            return 2;
        }
    } else if (!type_s.empty() || !group_s.empty() || !inst_s.empty()) {
        args["resourceId"] = make_resource_id(type_s, group_s, inst_s, ord_s);
    }
    const auto payload_path = !file.empty() ? file : path;
    try {
        args = parse_rest(app.remaining(), std::move(args));
    } catch (const std::exception& e) {
        std::cerr << "{\"ok\":false,\"error\":{\"code\":\"invalid_argument\",\"message\":\""
                  << e.what() << "\"}}\n";
        return 2;
    }

    std::string id = noun;
    if (!verb.empty()) {
        auto v = camel(verb);
        if (!v.empty()) {
            v[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(v[0])));
        }
        id = noun + "." + v;
    }
    if (!find_tool(bus, id)) {
        const json env = {{"ok", false},
                          {"schemaVersion", 1},
                          {"error",
                           {{"code", "invalid_argument"},
                            {"message", "unknown command: '" + id + "'"},
                            {"retryable", false},
                            {"side_effects", "none"}}}};
        if (format == "text" || format == "table") {
            std::cerr << "unknown command: '" << id << "'\nTry: sxpe help\n";
            return 2;
        }
        return print_result(env, format, false);
    }

    if (!payload_path.empty() && !args.contains("path")) {
        args["path"] = payload_path;
    } else if (!path.empty()) {
        args["path"] = path;
    }

    std::string opened;
    const bool oneshot = !package.empty() && !args.contains("sessionId") && !skip_oneshot_open(id);
    if (oneshot || (id == "package.open" && !args.contains("path") && !package.empty())) {
        json o{{"path", package}, {"writable", writable || is_mutating(bus, id)}};
        if (!game.empty()) {
            o["game"] = game;
        }
        auto env = bus.execute("package.open", o);
        if (!env.value("ok", false)) {
            return print_result(env, format == "jsonl" ? "json" : format, false);
        }
        opened = env["data"]["sessionId"].get<std::string>();
        args["sessionId"] = opened;
        if (id == "package.open") {
            return print_result(env, format, false);
        }
    }
    if (id == "package.open" && !package.empty() && !args.contains("path")) {
        args["path"] = package;
    }

    if (id == "package.new") {
        auto env = bus.execute("package.new", json::object());
        if (!env.value("ok", false)) {
            return print_result(env, format == "jsonl" ? "json" : format, false);
        }
        const auto sid = env["data"]["sessionId"].get<std::string>();
        const auto dest = !path.empty() ? path : package;
        if (!dest.empty()) {
            json sa{{"sessionId", sid}, {"path", dest}};
            if (force) {
                sa["force"] = true;
            }
            auto saved = bus.execute("package.saveAs", sa);
            bus.execute("package.close", json{{"sessionId", sid}});
            return print_result(saved, format == "jsonl" ? "json" : format, false);
        }
        return print_result(env, format, false);
    }

    auto env = bus.execute(id, args);
    if (oneshot && env.value("ok", false) && is_mutating(bus, id) && !dry) {
        auto sv = bus.execute("package.save", json{{"sessionId", opened}});
        if (!sv.value("ok", false)) {
            std::cerr << sv.dump() << '\n';
            if (!opened.empty()) {
                bus.execute("package.close", json{{"sessionId", opened}});
            }
            return print_result(sv, "json", false);
        }
    }
    if (oneshot && !opened.empty()) {
        bus.execute("package.close", json{{"sessionId", opened}});
    }
    return print_result(env, format, is_list(id));
}
