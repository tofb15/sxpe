#pragma once

#include "sxpe/error.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace sxpe::resources {

struct S3saInfo {
    std::size_t size{0};
    std::optional<std::size_t> pe_offset;
    std::string module_hint;
};

S3saInfo inspect_s3sa(std::span<const std::byte> bytes, std::string_view nmap_name);

}  // namespace sxpe::resources
