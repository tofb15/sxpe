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

/// Metadata subset written into the Sims3Pack XML (limited authoring).
struct Sims3PackCreateOptions {
    std::string package_type{"Object"};
    std::string package_subtype{"0x00000000"};
    std::string archive_version{"1.4"};
    std::string display_name;
    std::string description;
    std::string package_id;
};

/// One source file to embed in the archive section.
struct Sims3PackPackItem {
    std::filesystem::path source_path;
    std::string name;          // empty => basename of source_path
    std::string guid;          // empty => placeholder from index
    std::string content_type;  // empty => infer (.package -> package)
    std::string crc;           // empty => "00000000" (algorithm unknown)
};

/// Non-recursive collect of *.package under source_dir (sorted by filename).
Result<std::vector<Sims3PackPackItem>> collect_sims3pack_packages(
    const std::filesystem::path& source_dir);

/// Best-effort load of DisplayName / Description / PackageId / Type / SubType /
/// ArchiveVersion from a metadata XML subset (Sims3Package root or fragment).
Result<Sims3PackCreateOptions> load_sims3pack_meta_subset(const std::filesystem::path& xml_path);

/// Limited TS3Pack authoring: write header + XML + concatenated payloads.
/// No Store upload, no DRM/DBPP, CRC left as placeholder zeros. Overwrite needs force.
/// On success returns meta as if re-opened (path = out_path).
Result<Sims3PackMeta> pack_sims3pack(const std::filesystem::path& out_path,
                                     const std::vector<Sims3PackPackItem>& items,
                                     const Sims3PackCreateOptions& opts,
                                     bool force);

}  // namespace sxpe::games::sims3
