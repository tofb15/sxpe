#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace sxpe::commands {

/// Shared human lines for CLI `--format text`, GUI Find references, and MCP
/// clients that print `data.summary` from `resource.findRefs`.
inline std::vector<std::string> format_find_refs_summary(const nlohmann::json& data) {
    std::vector<std::string> summary;
    const auto hits = data.value("hits", nlohmann::json::array());
    const auto target = data.value("target", nlohmann::json::object());
    std::string tgt;
    if (target.contains("typeHex")) {
        tgt = target.value("typeHex", "") + " " + target.value("groupHex", "") + " " +
              target.value("instanceHex", "");
    } else {
        tgt = std::to_string(target.value("type", 0u)) + " " +
              std::to_string(target.value("group", 0u)) + " " +
              std::to_string(target.value("instance", 0ull));
    }
    summary.push_back("Target: " + tgt);
    summary.push_back("Hits: " + std::to_string(hits.size()));
    if (data.contains("byteScan") && data["byteScan"].is_object()) {
        const auto& bs = data["byteScan"];
        if (bs.value("enabled", false)) {
            summary.push_back("Byte-scan: on (scanned " +
                              std::to_string(bs.value("resourcesScanned", 0u)) +
                              " resource(s); cap " +
                              std::to_string(bs.value("maxBytesPerResource", 0u)) + " B)");
        }
    }
    if (hits.empty()) {
        summary.push_back("No references found in REFS / OBJK / VPXY" +
                          std::string(data.value("byteScan", nlohmann::json::object())
                                              .value("enabled", false)
                                          ? " / byte-scan"
                                          : "") +
                          ".");
        return summary;
    }
    summary.push_back("Sources:");
    for (const auto& h : hits) {
        std::string line = "  ";
        const auto& src = h.contains("source") ? h["source"] : h;
        if (src.contains("tag") && !src.value("tag", std::string{}).empty()) {
            line += src.value("tag", "") + " ";
        }
        if (src.contains("typeHex")) {
            line += src.value("typeHex", "");
        } else {
            line += std::to_string(src.value("type", 0u));
        }
        line += " ";
        if (src.contains("groupHex")) {
            line += src.value("groupHex", "");
        } else {
            line += std::to_string(src.value("group", 0u));
        }
        line += " ";
        if (src.contains("instanceHex")) {
            line += src.value("instanceHex", "");
        } else {
            line += std::to_string(src.value("instance", 0ull));
        }
        line += " #" + std::to_string(src.value("ordinal", 0u));
        if (h.contains("reason")) {
            line += "  (" + h.value("reason", std::string{}) + ")";
        }
        if (src.contains("name") && !src.value("name", std::string{}).empty()) {
            line += "  " + src.value("name", std::string{});
        }
        summary.push_back(std::move(line));
    }
    return summary;
}

inline nlohmann::json find_refs_summary_json(const nlohmann::json& data) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& line : format_find_refs_summary(data)) {
        arr.push_back(line);
    }
    return arr;
}

}  // namespace sxpe::commands
