#pragma once

#include "sxpe/error.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace sxpe::resources {

struct S3saInfo {
    std::size_t size{0};
    std::optional<std::size_t> pe_offset;
    std::string module_hint;
    std::uint8_t version{0};
    std::string game_version;
    std::uint32_t checksum_type{0};
    bool checksum_zero{true};
    std::uint16_t block_count{0};
    bool key_table_zero{true};
    std::size_t assembly_bytes{0};
    bool parsed{false};
};

struct S3saParsed {
    std::uint8_t version{1};
    std::string game_version;
    std::uint32_t checksum_type{0};
    std::vector<std::byte> checksum;
    std::uint16_t block_count{0};
    std::vector<std::uint64_t> key_table;
    std::vector<std::byte> cipher;
};

/// Never LoadLibrary / map as executable. Bytes only.
S3saInfo inspect_s3sa(std::span<const std::byte> bytes, std::string_view nmap_name);
Result<S3saParsed> parse_s3sa(std::span<const std::byte> bytes);
Result<std::vector<std::byte>> decrypt_s3sa(const S3saParsed& p);
Result<std::vector<std::byte>> wrap_s3sa_v1(std::span<const std::byte> pe);
Result<std::vector<std::byte>> export_pe(std::span<const std::byte> resource);
std::optional<std::size_t> pe_file_size(std::span<const std::byte> pe);

}  // namespace sxpe::resources
