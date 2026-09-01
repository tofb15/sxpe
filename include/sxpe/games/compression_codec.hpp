#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace sxpe::games {

class CompressionCodec {
public:
    virtual ~CompressionCodec() = default;
    virtual std::string_view name() const = 0;
    virtual bool try_decompress(std::span<const std::byte> input,
                                std::span<std::byte> output,
                                std::size_t& bytes_written) const = 0;
    virtual bool try_compress(std::span<const std::byte> input,
                              std::span<std::byte> output,
                              std::size_t& bytes_written) const = 0;
};

}  // namespace sxpe::games
