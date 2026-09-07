#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace sxpe::commands {

/// Shared human lines for CLI `--format text`, GUI Compare dialog, and MCP clients
/// that print `data.summary` from `package.diff`.
///
/// Payload equality uses SHA-256 of the *uncompressed* resource body (not on-disk
/// RefPack bytes). Keys are type + group + instance + ordinal.
inline std::vector<std::string> format_package_diff_summary(const nlohmann::json& data) {
    std::vector<std::string> summary;
    const auto only_a = data.value("onlyInA", nlohmann::json::array());
    const auto only_b = data.value("onlyInB", nlohmann::json::array());
    const auto different = data.value("different", nlohmann::json::array());
    const auto same = data.value("sameCount", 0u);
    const auto total_a = data.value("countA", 0u);
    const auto total_b = data.value("countB", 0u);
    const auto algo = data.value("hashAlgorithm", std::string{"sha256-uncompressed"});

    const bool identical = only_a.empty() && only_b.empty() && different.empty();
    summary.push_back(identical ? "Result: packages match (same TGI keys + payloads)."
                                : "Result: differences found.");
    summary.push_back("Hash: " + algo + " (uncompressed payload)");
    summary.push_back("A resources: " + std::to_string(total_a) +
                      " | B resources: " + std::to_string(total_b));
    summary.push_back("Same: " + std::to_string(same) + " | Only in A: " +
                      std::to_string(only_a.size()) + " | Only in B: " +
                      std::to_string(only_b.size()) + " | Different payload: " +
                      std::to_string(different.size()));

    auto push_rid = [&](const char* label, const nlohmann::json& arr) {
        if (!arr.is_array() || arr.empty()) {
            return;
        }
        summary.push_back(std::string(label) + " (" + std::to_string(arr.size()) + "):");
        for (const auto& it : arr) {
            std::string line = "  ";
            if (it.contains("typeHex")) {
                line += it.value("typeHex", "");
            } else {
                line += std::to_string(it.value("type", 0u));
            }
            line += " ";
            if (it.contains("groupHex")) {
                line += it.value("groupHex", "");
            } else {
                line += std::to_string(it.value("group", 0u));
            }
            line += " ";
            if (it.contains("instanceHex")) {
                line += it.value("instanceHex", "");
            } else {
                line += std::to_string(it.value("instance", 0ull));
            }
            line += " #" + std::to_string(it.value("ordinal", 0u));
            if (it.contains("hashA") && it.contains("hashB")) {
                line += "  A=" + it.value("hashA", std::string{}).substr(0, 12) + "…";
                line += " B=" + it.value("hashB", std::string{}).substr(0, 12) + "…";
            }
            summary.push_back(std::move(line));
        }
    };
    push_rid("Only in A", only_a);
    push_rid("Only in B", only_b);
    push_rid("Different payload", different);
    return summary;
}

inline nlohmann::json package_diff_summary_json(const nlohmann::json& data) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& line : format_package_diff_summary(data)) {
        arr.push_back(line);
    }
    return arr;
}

}  // namespace sxpe::commands
