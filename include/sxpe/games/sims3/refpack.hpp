#pragma once

#include "sxpe/error.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sxpe::games::sims3 {

Result<std::vector<std::byte>> refpack_decompress(std::span<const std::byte> input,
                                                  std::uint32_t expected_mem_size = 0);

Result<std::vector<std::byte>> refpack_compress(std::span<const std::byte> input);

}  // namespace sxpe::games::sims3
