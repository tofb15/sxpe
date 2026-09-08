#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sxpe::commands {

/// Human-readable issue codes from `package.validate`.
inline std::string validate_issue_label(const std::string& code) {
    if (code == "major") {
        return "Unsupported DBPF major version (expected 2)";
    }
    if (code == "dir_unreadable") {
        return "DIR resource could not be read";
    }
    if (code == "dir_corrupt") {
        return "DIR resource is corrupt or has an invalid record size";
    }
    if (code == "dir_unmatched") {
        return "DIR has entries that do not match the index (TGI + mem_size)";
    }
    if (code == "leftover_manifest") {
        return "Leftover Sims3Pack manifest TGI(s) present (conflict hotspot; strip on merge)";
    }
    if (code == "duplicate_tgi") {
        return "Duplicate TGI(s) in package (same type+group+instance, multiple ordinals)";
    }
    return code;
}

/// Shared text summary for CLI `--format text`, GUI Validate dialog, and MCP clients
/// that print `data.summary` from `package.validate`.
/// When `layout_locked` is true, names the neighborhood / world layout lock and what is safe.
inline std::vector<std::string> format_validate_summary(bool valid, std::uint32_t index_count,
                                                        const nlohmann::json& dir,
                                                        const nlohmann::json& issues,
                                                        bool layout_locked = false,
                                                        const std::string& path_kind = {},
                                                        const nlohmann::json& conflict_hotspots =
                                                            nlohmann::json::array()) {
    std::vector<std::string> summary;
    summary.push_back(valid ? "Result: OK — no issues found." : "Result: issues found.");
    summary.push_back("Resources (index): " + std::to_string(index_count));
    if (layout_locked) {
        const std::string kind = path_kind.empty() ? "neighborhood" : path_kind;
        summary.push_back("Layout: neighborhood / world layout lock (pathKind=" + kind + ")");
        summary.push_back("  Safe: in-place payload replace within existing hole capacity");
        summary.push_back(
            "  Not supported: add, delete, reorder, compact, create NMAP (index layout changes)");
    } else if (!path_kind.empty() && path_kind != "package") {
        summary.push_back("pathKind: " + path_kind);
    }
    if (!dir.is_object() || !dir.value("present", false)) {
        summary.push_back("DIR: not present");
    } else {
        summary.push_back("DIR: present");
        if (dir.contains("records")) {
            summary.push_back("  Records: " + std::to_string(dir.value("records", 0ull)));
        }
        if (dir.contains("recordBytes")) {
            summary.push_back("  Record size: " + std::to_string(dir.value("recordBytes", 0ull)) +
                              " bytes");
        }
        if (dir.contains("unmatched")) {
            summary.push_back("  Unmatched: " + std::to_string(dir.value("unmatched", 0ull)));
        }
    }
    if (conflict_hotspots.is_array() && !conflict_hotspots.empty()) {
        summary.push_back("Conflict hotspots (" + std::to_string(conflict_hotspots.size()) + "):");
        for (const auto& h : conflict_hotspots) {
            if (!h.is_object()) {
                summary.push_back("  • " + h.dump());
                continue;
            }
            const auto kind = h.value("kind", std::string{});
            const auto reason = h.value("reason", std::string{});
            const auto t = h.value("type", 0u);
            const auto g = h.value("group", 0u);
            const auto inst = h.value("instance", 0ull);
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%08X-%08X-%016llX", static_cast<unsigned>(t),
                          static_cast<unsigned>(g), static_cast<unsigned long long>(inst));
            std::string line = std::string("  • [") + (kind.empty() ? "hotspot" : kind) + "] " + buf;
            if (!reason.empty()) {
                line += " — " + reason;
            }
            summary.push_back(std::move(line));
        }
    } else {
        summary.push_back("Conflict hotspots: none");
    }
    if (!issues.is_array() || issues.empty()) {
        summary.push_back("Issues: none");
    } else {
        summary.push_back("Issues (" + std::to_string(issues.size()) + "):");
        for (const auto& issue : issues) {
            if (issue.is_string()) {
                summary.push_back("  • " + validate_issue_label(issue.get<std::string>()));
            } else {
                summary.push_back("  • " + issue.dump());
            }
        }
    }
    return summary;
}

inline nlohmann::json validate_summary_json(bool valid, std::uint32_t index_count,
                                            const nlohmann::json& dir,
                                            const nlohmann::json& issues,
                                            bool layout_locked = false,
                                            const std::string& path_kind = {},
                                            const nlohmann::json& conflict_hotspots =
                                                nlohmann::json::array()) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& line : format_validate_summary(valid, index_count, dir, issues, layout_locked,
                                                    path_kind, conflict_hotspots)) {
        arr.push_back(line);
    }
    return arr;
}

}  // namespace sxpe::commands
