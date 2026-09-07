#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sxpe::commands {

/// Shared human lines for CLI `--format text`, GUI Scan folder, and MCP clients
/// that print `data.summary` from `folder.scan`.
///
/// Read-only: never deletes or renames. Duplicate TGI = same type+group+instance
/// in two or more *.package files under the scanned root (sample sizes in
/// data.limits).
inline std::vector<std::string> format_folder_scan_summary(const nlohmann::json& data) {
    std::vector<std::string> summary;
    const auto issues = data.value("issues", nlohmann::json::array());
    const auto duplicates = data.value("duplicates", nlohmann::json::array());
    const auto files = data.value("filesScanned", 0u);
    const auto bytes = data.value("bytesScanned", 0ull);
    const auto ok_count = data.value("okCount", 0u);
    const auto issue_count = data.value("issueCount", 0u);
    const auto dup_total = data.value("duplicateTgiCount", 0u);
    const bool capped = data.value("capped", false);

    if (issue_count == 0 && dup_total == 0) {
        summary.push_back("Result: clean — no empty/corrupt/wrong-game packages or cross-file "
                          "duplicate TGIs in sample.");
    } else {
        summary.push_back("Result: hygiene findings (read-only; nothing deleted).");
    }
    summary.push_back("Folder: " + data.value("path", std::string{}));
    summary.push_back("Scanned: " + std::to_string(files) + " *.package file(s), " +
                      std::to_string(bytes) + " byte(s); OK: " + std::to_string(ok_count) +
                      " | issues: " + std::to_string(issue_count));
    if (capped) {
        summary.push_back("Capped: " + data.value("capReason", std::string{"limit reached"}));
    }
    if (data.contains("limits") && data["limits"].is_object()) {
        const auto& lim = data["limits"];
        summary.push_back(
            "Limits: maxFiles=" + std::to_string(lim.value("maxFiles", 0u)) +
            " maxTotalBytes=" + std::to_string(lim.value("maxTotalBytes", 0ull)) +
            " maxDuplicateSamples=" + std::to_string(lim.value("maxDuplicateSamples", 0u)) +
            " maxPathsPerDuplicate=" + std::to_string(lim.value("maxPathsPerDuplicate", 0u)));
    }
    summary.push_back("Duplicate TGI groups (type+group+instance across files): " +
                      std::to_string(dup_total) +
                      (data.value("duplicatesTruncated", false)
                           ? " (showing sample of " + std::to_string(duplicates.size()) + ")"
                           : ""));

    if (!issues.is_array() || issues.empty()) {
        summary.push_back("File issues: none");
    } else {
        summary.push_back("File issues (" + std::to_string(issues.size()) + "):");
        for (const auto& issue : issues) {
            std::string line = "  • [";
            line += issue.value("kind", std::string{"?"});
            line += "] ";
            line += issue.value("path", std::string{});
            if (issue.contains("message") && !issue.value("message", std::string{}).empty()) {
                line += " — ";
                line += issue.value("message", std::string{});
            }
            summary.push_back(std::move(line));
        }
    }

    if (!duplicates.is_array() || duplicates.empty()) {
        summary.push_back("Duplicate samples: none");
    } else {
        summary.push_back("Duplicate samples (" + std::to_string(duplicates.size()) + "):");
        for (const auto& d : duplicates) {
            std::string line = "  • ";
            if (d.contains("typeHex")) {
                line += d.value("typeHex", "");
            } else {
                line += std::to_string(d.value("type", 0u));
            }
            line += " ";
            if (d.contains("groupHex")) {
                line += d.value("groupHex", "");
            } else {
                line += std::to_string(d.value("group", 0u));
            }
            line += " ";
            if (d.contains("instanceHex")) {
                line += d.value("instanceHex", "");
            } else {
                line += std::to_string(d.value("instance", 0ull));
            }
            line += " in " + std::to_string(d.value("fileCount", 0u)) + " file(s)";
            if (d.value("pathsTruncated", false)) {
                line += " (paths truncated)";
            }
            summary.push_back(std::move(line));
            if (d.contains("paths") && d["paths"].is_array()) {
                for (const auto& p : d["paths"]) {
                    if (p.is_string()) {
                        summary.push_back("      " + p.get<std::string>());
                    }
                }
            }
        }
    }
    return summary;
}

inline nlohmann::json folder_scan_summary_json(const nlohmann::json& data) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& line : format_folder_scan_summary(data)) {
        arr.push_back(line);
    }
    return arr;
}

}  // namespace sxpe::commands
