#pragma once

#include "sxpe/core/mapped_file.hpp"
#include "sxpe/error.hpp"
#include "sxpe/games/sims3/tgi.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sxpe::games::sims3 {

struct IndexEntry {
    Tgi tgi{};
    std::uint32_t chunk_offset{0};
    std::uint32_t file_size{0};
    bool file_size_high_bit{false};
    std::uint32_t mem_size{0};
    std::uint16_t compressed{0};
    std::uint16_t unknown2{0};
    std::uint32_t ordinal{0};
    std::uint32_t payload_capacity{0};
};

class Package {
public:
    static Result<Package> open(const std::filesystem::path& path, bool writable);
    static Package create_new();

    Package(Package&&) noexcept = default;
    Package& operator=(Package&&) noexcept = default;
    Package(const Package&) = delete;
    Package& operator=(const Package&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] bool writable() const { return writable_; }
    /// True when the on-disk mapping holds an exclusive write lock (issue #68).
    [[nodiscard]] bool holds_exclusive_lock() const { return map_.holds_exclusive_lock(); }
    [[nodiscard]] std::uint32_t count() const {
        return static_cast<std::uint32_t>(entries_.size());
    }
    [[nodiscard]] const IndexEntry& entry(std::uint32_t i) const { return entries_.at(i); }
    [[nodiscard]] std::span<const IndexEntry> entries() const { return entries_; }

    Result<std::span<const std::byte>> raw(std::uint32_t i) const;
    Result<std::vector<std::byte>> uncompressed(std::uint32_t i) const;
    /// First max_bytes of uncompressed payload. Uncompressed resources are a mmap slice copy.
    Result<std::vector<std::byte>> peek(std::uint32_t i, std::uint32_t max_bytes) const;
    VoidResult set_uncompressed(std::uint32_t i, std::span<const std::byte> data, bool compress);
    /// Overwrite the on-disk hole for entry i. Does not rebuild the package.
    /// New on-disk bytes must fit in payload_capacity (space until the next chunk).
    VoidResult patch_in_place(std::uint32_t i, std::span<const std::byte> uncompressed,
                              bool compress);
    Result<std::uint32_t> add(Tgi tgi, std::span<const std::byte> data, bool compress);
    /// Copy on-disk bytes as-is (no decompress/recompress). Preserves compression flags.
    Result<std::uint32_t> add_raw(Tgi tgi, std::span<const std::byte> disk, std::uint32_t mem_size,
                                  std::uint16_t compressed, std::uint16_t unknown2 = 1,
                                  bool file_size_high_bit = true);
    VoidResult set_raw(std::uint32_t i, std::span<const std::byte> disk, std::uint32_t mem_size,
                       std::uint16_t compressed, std::uint16_t unknown2 = 1,
                       bool file_size_high_bit = true);
    VoidResult remove(std::uint32_t i);
    /// Reorder index rows. `to` is the destination index after removal of `from`.
    VoidResult move(std::uint32_t from, std::uint32_t to);
    Result<std::uint32_t> duplicate(std::uint32_t i);
    VoidResult rekey(std::uint32_t i, Tgi tgi);
    VoidResult set_deleted(std::uint32_t i, bool deleted);
    [[nodiscard]] bool deleted(std::uint32_t i) const;
    [[nodiscard]] std::optional<std::uint32_t> find(Tgi tgi, std::uint32_t ordinal) const;

    VoidResult save();
    VoidResult save_as(const std::filesystem::path& dest);
    VoidResult save_copy_as(const std::filesystem::path& dest);

    [[nodiscard]] bool dirty() const { return dirty_; }
    void set_dirty(bool d) { dirty_ = d; }
    [[nodiscard]] std::uint32_t major() const;
    [[nodiscard]] std::uint32_t minor() const;
    [[nodiscard]] std::uint32_t index_version() const;
    [[nodiscard]] std::uint32_t compressed_count() const;
    [[nodiscard]] std::uint32_t deleted_count() const;
    [[nodiscard]] bool dir_present() const;
    [[nodiscard]] std::uint64_t mapped_bytes() const { return map_.size(); }
    /// Neighborhood .nhd/.world/.dbc: in-place holes only; cannot add index rows.
    [[nodiscard]] bool layout_locked() const;
    /// Extension kind: "nhd" | "world" | "dbc" | "package".
    [[nodiscard]] std::string path_kind() const;

private:
    Package() = default;
    VoidResult parse_mapped();
    VoidResult write_file(const std::filesystem::path& dest) const;
    Result<std::vector<std::byte>> payload_on_disk(std::uint32_t i) const;
    void compute_payload_capacities();
    VoidResult flush_layout();

    std::filesystem::path path_;
    bool writable_{false};
    core::MappedFile map_;
    std::array<std::byte, 96> header_{};
    std::uint32_t index_type_{0};
    std::uint32_t index_pos_{0};
    std::uint32_t original_count_{0};
    std::vector<IndexEntry> entries_;
    std::vector<std::optional<std::vector<std::byte>>> overrides_;
    std::vector<char> deleted_;
    bool dirty_{false};

    void recompute_ordinals();
};

}  // namespace sxpe::games::sims3
