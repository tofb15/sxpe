#pragma once

#include "sxpe/error.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace sxpe::games::sims3 {

/// One archive payload described by a <PackagedFile> in the Sims3Pack XML.
struct Sims3PackEntry {
    std::uint32_t index{0};
    std::string name;
    std::uint64_t length{0};
    std::uint64_t offset{0};  // relative to archive section start
    std::string crc;
    std::string guid;
    std::string content_type;
    bool looks_like_package{false};  // name ends with .package or payload sniffs DBPF
};

struct Sims3PackMeta {
    std::string path;
    std::uint16_t header_version{0};
    std::uint32_t xml_length{0};
    std::uint64_t archive_offset{0};  // absolute file offset of archive section
    std::uint64_t archive_size{0};
    std::uint64_t file_size{0};
    std::string package_type;     // Sims3Package @Type
    std::string package_subtype;  // Sims3Package @SubType
    std::string archive_version;
    std::string display_name;
    std::string description;
    std::string package_id;
    std::vector<Sims3PackEntry> entries;
};

/// Read-only Sims3Pack inspect (SimsWiki TS3Pack layout). No Store download / DRM.
Result<Sims3PackMeta> open_sims3pack(const std::filesystem::path& path);

/// Absolute file offset of entry payload (= archive_offset + entry.offset).
[[nodiscard]] inline std::uint64_t entry_abs_offset(const Sims3PackMeta& m,
                                                    const Sims3PackEntry& e) {
    return m.archive_offset + e.offset;
}

/// Extract one entry (by index) into out_dir / sanitized name. Creates out_dir.
/// Returns written path. Refuses path escape / overwrite unless force.
Result<std::filesystem::path> extract_sims3pack_entry(const Sims3PackMeta& meta,
                                                      std::uint32_t index,
                                                      const std::filesystem::path& out_dir,
                                                      bool force);

}  // namespace sxpe::games::sims3
