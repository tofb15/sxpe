#include "sxpe/commands/bus.hpp"
#include "sxpe/version.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace {

using nlohmann::json;
using sxpe::commands::Bus;

std::string mcp_name(std::string id) {
    std::replace(id.begin(), id.end(), '.', '_');
    return id;
}

std::string dotted(std::string name) {
    std::replace(name.begin(), name.end(), '_', '.');
    return name;
}

std::optional<json> read_message() {
    std::string header;
    int c = std::cin.peek();
    if (c == EOF) {
        return std::nullopt;
    }
    if (c == '{') {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        return json::parse(line);
    }
    std::size_t len = 0;
    while (std::getline(std::cin, header)) {
        if (!header.empty() && header.back() == '\r') {
            header.pop_back();
        }
        if (header.empty()) {
            break;
        }
        auto pos = header.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        auto key = header.substr(0, pos);
        auto val = header.substr(pos + 1);
        while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
            val.erase(val.begin());
        }
        for (char& ch : key) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (key == "content-length") {
            len = static_cast<std::size_t>(std::stoul(val));
        }
    }
    if (len == 0) {
        return std::nullopt;
    }
    std::string body(len, '\0');
    std::cin.read(body.data(), static_cast<std::streamsize>(len));
    if (std::cin.gcount() != static_cast<std::streamsize>(len)) {
        return std::nullopt;
    }
    return json::parse(body);
}

void write_message(const json& msg) {
    const auto body = msg.dump();
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

json initialize_result() {
    return {{"protocolVersion", "2026-07-28"},
            {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "sxpe"}, {"version", SXPE_VERSION}}}};
}

json tools_list(Bus& bus, const json& params) {
    json tools = json::array();
    auto catalog = bus.tools();
    std::uint32_t start = 0;
    if (params.contains("cursor") && params["cursor"].is_string() &&
        !params["cursor"].get<std::string>().empty()) {
        start = static_cast<std::uint32_t>(std::stoul(params["cursor"].get<std::string>()));
    }
    constexpr std::uint32_t kPage = 100;
    std::uint32_t i = start;
    for (; i < catalog.size() && tools.size() < kPage; ++i) {
        const auto& t = catalog[i];
        tools.push_back({{"name", mcp_name(t.id)},
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
    json r{{"tools", tools}};
    if (i < catalog.size()) {
        r["nextCursor"] = std::to_string(i);
    }
    return r;
}

json call_tool(Bus& bus, const json& params) {
    const auto name = params.at("name").get<std::string>();
    json args = params.value("arguments", json::object());
    json progress_token;
    if (params.contains("_meta") && params["_meta"].is_object() &&
        params["_meta"].contains("progressToken")) {
        progress_token = params["_meta"]["progressToken"];
    }
    if (!progress_token.is_null()) {
        bus.set_progress_handler([progress_token](const json& ev) {
            const double done = static_cast<double>(ev.value("packagesDone", 0));
            const double total =
                std::max(1.0, static_cast<double>(ev.value("packagesTotal", 1)));
            write_message({{"jsonrpc", "2.0"},
                           {"method", "notifications/progress"},
                           {"params",
                            {{"progressToken", progress_token},
                             {"progress", done},
                             {"total", total},
                             {"message", ev.dump()}}}});
        });
    }
    auto env = bus.execute(dotted(name), args);
    if (!progress_token.is_null()) {
        bus.clear_progress_handler();
    }
    const bool ok = env.value("ok", false);
    json result;
    result["content"] = json::array({json{{"type", "text"}, {"text", env.dump()}}});
    result["structuredContent"] = env;
    result["isError"] = !ok;
    return result;
}

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    Bus bus;
    while (true) {
        std::optional<json> msg;
        try {
            msg = read_message();
        } catch (const std::exception& e) {
            write_message({{"jsonrpc", "2.0"},
                           {"id", nullptr},
                           {"error", {{"code", -32700}, {"message", e.what()}}}});
            continue;
        }
        if (!msg) {
            break;
        }
        if (!msg->contains("method")) {
            continue;
        }
        const auto method = (*msg)["method"].get<std::string>();
        json id = msg->contains("id") ? (*msg)["id"] : json(nullptr);
        try {
            if (method == "initialize") {
                write_message({{"jsonrpc", "2.0"}, {"id", id}, {"result", initialize_result()}});
            } else if (method == "notifications/initialized" || method == "initialized") {
                continue;
            } else if (method == "ping") {
                write_message({{"jsonrpc", "2.0"}, {"id", id}, {"result", json::object()}});
            } else if (method == "tools/list") {
                json params = msg->value("params", json::object());
                write_message({{"jsonrpc", "2.0"}, {"id", id}, {"result", tools_list(bus, params)}});
            } else if (method == "tools/call") {
                json params = msg->value("params", json::object());
                write_message({{"jsonrpc", "2.0"}, {"id", id}, {"result", call_tool(bus, params)}});
            } else {
                write_message({{"jsonrpc", "2.0"},
                               {"id", id},
                               {"error", {{"code", -32601}, {"message", "method not found"}}}});
            }
        } catch (const json::exception& e) {
            write_message({{"jsonrpc", "2.0"},
                           {"id", id},
                           {"error", {{"code", -32602}, {"message", e.what()}}}});
        } catch (const std::exception& e) {
            write_message({{"jsonrpc", "2.0"},
                           {"id", id},
                           {"error", {{"code", -32603}, {"message", e.what()}}}});
        }
    }
    return 0;
}
