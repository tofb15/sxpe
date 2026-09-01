#include "sxpe/commands/bus.hpp"

#include <CLI11.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

void print_help(const Bus& bus, std::string_view filter) {
    std::cout << "SXPE — Sims 3 package editor (CLI)\n\n";
    std::cout << "usage: sxpe <noun> <verb> [options]\n";
    std::cout << "       sxpe help [noun[.verb]]\n\n";
    std::cout << "One-shot (no session):\n";
    std::cout << "  --package PATH     Open this file, run the command, save if it writes, close\n";
    std::cout << "  --path PATH        Export dest, add/replace payload file, save-as dest\n";
    std::cout << "  --file PATH        Alias of --path for payload files\n";
    std::cout << "  --id JSON          resourceId {type,group,instance,ordinal}\n";
    std::cout << "  --type --group --instance --ordinal\n";
    std::cout << "                     Alternative to --id (hex 0xAABB is ok)\n";
    std::cout << "  --name TEXT        NMAP display name (resource.rename / nmap.set)\n";
    std::cout << "  --text TEXT        hash.fnv / search.bytes\n";
    std::cout << "  --force --dry-run --writable --include-payload --limit N --format json|jsonl|text\n\n";
    std::cout << "Examples:\n";
    std::cout << "  sxpe package info --package mod.package\n";
    std::cout << "  sxpe resource list --package mod.package --limit 20\n";
    std::cout << "  sxpe resource export --package mod.package --type 0x0333406C --group 0 "
                 "--instance 0x1 --path out.xml --force\n";
    std::cout << "  sxpe resource add --package mod.package --type 0x0166038C --group 0 "
                 "--instance 0 --path nmap.bin --force\n";
    std::cout << "  sxpe resource rename --package mod.package --type 0x0333406C --group 0 "
                 "--instance 0x1 --name NRaas.NoCD --force\n";
    std::cout << "  sxpe nmap set --package mod.package --instance 0x1 --name NRaas.NoCD --force\n";
    std::cout << "  sxpe package new --package new.package --force\n\n";
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
    if (!any) {
        std::cout << "  (no tools match)\n";
    }
}

int print_result(const json& env, const std::string& format, bool list_like) {
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
    if (format == "text" && env.value("ok", false)) {
        std::cout << env["data"].dump(2) << '\n';
        return 0;
    }
    std::cout << env.dump() << '\n';
    if (env.value("ok", false)) {
        return 0;
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
           id == "hash.fnv" || id == "help";
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
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help" || a == "--help-all") {
            Bus bus;
            print_help(bus, {});
            return 0;
        }
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
    app.add_option("--format", format, "json | jsonl | text");
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
    if (noun.empty() || noun == "help") {
        print_help(bus, verb);
        return noun.empty() ? 2 : 0;
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
