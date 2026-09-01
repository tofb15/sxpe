#include "sxpe/commands/bus.hpp"

#include <CLI11.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
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
            } else if (!v.empty() && (std::isdigit(static_cast<unsigned char>(v[0])) || v[0] == '-')) {
                try {
                    if (v.find('.') == std::string::npos) {
                        args[key] = std::stoll(v);
                    } else {
                        args[key] = v;
                    }
                } catch (...) {
                    args[key] = v;
                }
            } else if (!v.empty() && (v.front() == '{' || v.front() == '[')) {
                args[key] = json::parse(v);
            } else {
                args[key] = v;
            }
        } else {
            args[key] = true;
        }
    }
    return args;
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

}  // namespace

int main(int argc, char** argv) {
    CLI::App app{"SXPE — Sims 3 package editor (CLI)"};
    app.set_help_all_flag("--help-all");
    std::string format;
    std::string session;
    std::string package;
    std::string game;
    std::string filter;
    std::string id_json;
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
    app.add_option("--id", id_json, "resourceId JSON");
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

    if (format.empty()) {
        format = stdout_tty() ? "text" : (is_list(noun + (verb.empty() ? "" : "." + verb)) ? "jsonl" : "json");
        if (!stdout_tty() && format == "text") {
            format = "json";
        }
        if (!stdout_tty()) {
            format = "json";
            const auto idguess = verb.empty() ? noun : noun + "." + verb;
            if (is_list(idguess)) {
                format = "jsonl";
            }
        }
    }

    if (noun.empty()) {
        std::cerr << "usage: sxpe <noun> <verb> [options]\n";
        return 2;
    }
    const std::string cmd = verb.empty() ? noun : noun + "." + verb;

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
    if (!filter.empty() && filter.front() == '{') {
        args["filter"] = json::parse(filter);
    }
    if (!id_json.empty()) {
        args["resourceId"] = json::parse(id_json);
    }
    args = parse_rest(app.remaining(), std::move(args));

    // kebab verbs: save-as -> saveAs
    std::string id = noun;
    if (!verb.empty()) {
        auto v = camel(verb);
        if (!v.empty()) {
            v[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(v[0])));
        }
        id = noun + "." + v;
    }

    Bus bus;
    std::string opened;
    const bool oneshot = !package.empty() && !args.contains("sessionId") && id != "package.open" &&
                         id != "session.start";
    if (oneshot || (id == "package.open" && args.contains("path") == false && !package.empty())) {
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

    auto env = bus.execute(id, args);
    if (oneshot && env.value("ok", false) && is_mutating(bus, id) && !dry) {
        auto sv = bus.execute("package.save", json{{"sessionId", opened}});
        if (!sv.value("ok", false)) {
            std::cerr << sv.dump() << '\n';
            return print_result(sv, "json", false);
        }
    }
    if (oneshot && !opened.empty()) {
        bus.execute("package.close", json{{"sessionId", opened}});
    }
    return print_result(env, format, is_list(id));
}
