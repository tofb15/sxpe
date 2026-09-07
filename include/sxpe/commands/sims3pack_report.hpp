#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sxpe::commands {

/// Shared human lines for CLI `--format text`, GUI Inspect Sims3Pack, and MCP
/// clients that print `data.summary` from sims3pack.info / list / extract.
inline std::vector<std::string> format_sims3pack_summary(const nlohmann::json& data) {
    std::vector<std::string> summary;
    summary.push_back("Sims3Pack (read-only TS3Pack inspect; no Store download / DRM)");
    if (data.contains("path")) {
        summary.push_back("Path: " + data.value("path", std::string{}));
    }
    if (data.contains("displayName") && !data.value("displayName", std::string{}).empty()) {
        summary.push_back("DisplayName: " + data.value("displayName", std::string{}));
    }
    if (data.contains("description") && !data.value("description", std::string{}).empty()) {
        summary.push_back("Description: " + data.value("description", std::string{}));
    }
    if (data.contains("packageType") || data.contains("packageSubType")) {
        summary.push_back("Type: " + data.value("packageType", std::string{}) +
                          "  SubType: " + data.value("packageSubType", std::string{}));
    }
    if (data.contains("archiveVersion") &&
        !data.value("archiveVersion", std::string{}).empty()) {
        summary.push_back("ArchiveVersion: " + data.value("archiveVersion", std::string{}));
    }
    if (data.contains("headerVersion")) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04x",
                      static_cast<unsigned>(data.value("headerVersion", 0u)));
        summary.push_back(std::string("Header version word: 0x") + buf);
    }
    if (data.contains("entryCount") || data.contains("entries")) {
        const auto n = data.contains("entryCount")
                           ? data.value("entryCount", 0u)
                           : (data.contains("entries") && data["entries"].is_array()
                                  ? static_cast<unsigned>(data["entries"].size())
                                  : 0u);
        summary.push_back("Entries: " + std::to_string(n));
    }
    if (data.contains("fileSize")) {
        summary.push_back("File size: " + std::to_string(data.value("fileSize", 0ull)) +
                          " bytes; archive @" +
                          std::to_string(data.value("archiveOffset", 0ull)) + " (" +
                          std::to_string(data.value("archiveSize", 0ull)) + " bytes)");
    }
    if (data.contains("limitations") && data["limitations"].is_array()) {
        for (const auto& line : data["limitations"]) {
            if (line.is_string()) {
                summary.push_back("Limit: " + line.get<std::string>());
            }
        }
    }
    if (data.contains("entries") && data["entries"].is_array()) {
        summary.push_back("Packaged files:");
        for (const auto& e : data["entries"]) {
            std::string line = "  [" + std::to_string(e.value("index", 0u)) + "] ";
            line += e.value("name", std::string{"?"});
            line += "  len=" + std::to_string(e.value("length", 0ull));
            line += "  off=" + std::to_string(e.value("offset", 0ull));
            if (e.value("looksLikePackage", false)) {
                line += "  [package]";
            }
            if (e.contains("contentType") && !e.value("contentType", std::string{}).empty()) {
                line += "  type=" + e.value("contentType", std::string{});
            }
            summary.push_back(std::move(line));
        }
    }
    if (data.contains("written") && data["written"].is_array()) {
        summary.push_back("Extracted:");
        for (const auto& w : data["written"]) {
            if (w.is_string()) {
                summary.push_back("  " + w.get<std::string>());
            } else if (w.is_object() && w.contains("path")) {
                summary.push_back("  " + w.value("path", std::string{}));
            }
        }
    }
    if (data.contains("writtenPath")) {
        summary.push_back("Wrote: " + data.value("writtenPath", std::string{}));
    }
    return summary;
}

inline nlohmann::json sims3pack_summary_json(const nlohmann::json& data) {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& line : format_sims3pack_summary(data)) {
        arr.push_back(line);
    }
    return arr;
}

}  // namespace sxpe::commands
