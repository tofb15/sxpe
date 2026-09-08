#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace sxpe::commands {

/// resource.findRefs -> find-refs (CLI verb).
inline std::string kebab_from_camel(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isupper(c) && i > 0) {
            out.push_back('-');
        }
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

/// kebab or snake -> camel: find-refs / find_refs -> findRefs.
inline std::string camel_from_kebab(std::string_view s) {
    std::string out;
    bool up = false;
    for (char c : s) {
        if (c == '-' || c == '_') {
            up = true;
            continue;
        }
        out.push_back(up ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c);
        up = false;
    }
    return out;
}

/// resource.findRefs -> resource_find-refs (MCP tool name).
inline std::string mcp_tool_name(std::string_view bus_id) {
    const auto dot = bus_id.find('.');
    if (dot == std::string_view::npos) {
        return kebab_from_camel(bus_id);
    }
    std::string out;
    out.append(bus_id.substr(0, dot));
    out.push_back('_');
    out += kebab_from_camel(bus_id.substr(dot + 1));
    return out;
}

/// resource_find-refs -> resource.findRefs. First '_' splits noun/verb.
inline std::string bus_id_from_mcp_name(std::string_view mcp) {
    const auto us = mcp.find('_');
    if (us == std::string_view::npos) {
        auto v = camel_from_kebab(mcp);
        if (!v.empty()) {
            v[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(v[0])));
        }
        return v;
    }
    std::string verb = camel_from_kebab(mcp.substr(us + 1));
    if (!verb.empty()) {
        verb[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(verb[0])));
    }
    std::string out;
    out.append(mcp.substr(0, us));
    out.push_back('.');
    out += verb;
    return out;
}

/// LSP/MCP header: exactly one CRLF after the length line, then the body.
inline std::string mcp_frame(std::string_view body) {
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

inline std::string ascii_lower(std::string_view s) {
    std::string o(s);
    for (char& c : o) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return o;
}

inline int edit_distance(std::string_view a, std::string_view b) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    if (n == 0) {
        return static_cast<int>(m);
    }
    if (m == 0) {
        return static_cast<int>(n);
    }
    std::vector<int> prev(m + 1), cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) {
        prev[j] = static_cast<int>(j);
    }
    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        prev.swap(cur);
    }
    return prev[m];
}

/// Closest catalog id, or empty if nothing is within 3 edits (verb/kebab forms included).
inline std::string nearest_command(std::string_view typed, const std::vector<std::string>& ids) {
    const auto needle = ascii_lower(typed);
    int best = 4;
    std::string pick;
    auto consider = [&](std::string_view cand, const std::string& id) {
        const int d = edit_distance(needle, ascii_lower(cand));
        if (d < best) {
            best = d;
            pick = id;
        }
    };
    for (const auto& id : ids) {
        consider(id, id);
        const auto dot = id.find('.');
        const std::string verb = (dot == std::string::npos) ? id : id.substr(dot + 1);
        const auto kebab_verb = kebab_from_camel(verb);
        consider(verb, id);
        consider(kebab_verb, id);
        consider(mcp_tool_name(id), id);
        if (dot != std::string::npos) {
            consider(id.substr(0, dot) + "." + kebab_verb, id);
        }
    }
    if (pick.empty() || pick == typed) {
        return {};
    }
    return pick;
}

inline bool looks_like_search(std::string_view typed) {
    return ascii_lower(typed).find("search") != std::string::npos;
}

inline std::string unknown_command_message(std::string_view typed, const std::vector<std::string>& ids) {
    std::string msg = "unknown command: '" + std::string(typed) + "'";
    const auto near = nearest_command(typed, ids);
    if (!near.empty()) {
        msg += ". Did you mean '" + near + "'?";
    } else {
        msg += ".";
    }
    if (looks_like_search(typed)) {
        msg += " Search: resource.list --nameContains (metadata) or search.bytes (payload).";
    }
    return msg;
}

}  // namespace sxpe::commands
