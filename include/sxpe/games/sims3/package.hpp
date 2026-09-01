#pragma once

#include "sxpe/core/mapped_file.hpp"
#include "sxpe/error.hpp"
#include "sxpe/games/sims3/tgi.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
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
    [[nodiscard]] std::uint32_t count() const {
        return static_cast<std::uint32_t>(entries_.size());
    }
    [[nodiscard]] const IndexEntry& entry(std::uint32_t i) const { return entries_.at(i); }
    [[nodiscard]] std::span<const IndexEntry> entries() const { return entries_; }

    Result<std::span<const std::byte>> raw(std::uint32_t i) const;
    Result<std::vector<std::byte>> uncompressed(std::uint32_t i) const;
    VoidResult set_uncompressed(std::uint32_t i, std::span<const std::byte> data, bool compress);
    Result<std::uint32_t> add(Tgi tgi, std::span<const std::byte> data, bool compress);

    VoidResult save();
    VoidResult save_as(const std::filesystem::path& dest);

private:
    Package() = default;
    VoidResult parse_mapped();
    VoidResult write_file(const std::filesystem::path& dest) const;
    Result<std::vector<std::byte>> payload_on_disk(std::uint32_t i) const;

    std::filesystem::path path_;
    bool writable_{false};
    core::MappedFile map_;
    std::array<std::byte, 96> header_{};
    std::uint32_t index_type_{0};
    std::vector<IndexEntry> entries_;
    std::vector<std::optional<std::vector<std::byte>>> overrides_;
};

}  // namespace sxpe::games::sims3
