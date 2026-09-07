#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
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
    return code;
}

/// Shared text summary for CLI `--format text`, GUI Validate dialog, and MCP clients
/// that print `data.summary` from `package.validate`.
/// When `layout_locked` is true, names the neighborhood / world layout lock and what is safe.
inline std::vector<std::string> format_validate_summary(bool valid, std::uint32_t index_count,
                                                        const nlohmann::json& dir,
                                                        const nlohmann::json& issues,
                                                        bool layout_locked = false,
                                                        const std::string& path_kind = {}) {
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
                                            const std::string& path_kind = {}) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& line :
         format_validate_summary(valid, index_count, dir, issues, layout_locked, path_kind)) {
        arr.push_back(line);
    }
    return arr;
}

}  // namespace sxpe::commands
