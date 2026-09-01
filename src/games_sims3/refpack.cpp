#include "sxpe/games/sims3/refpack.hpp"

#include "sxpe/core/caps.hpp"
#include "sxpe/games/compression_codec.hpp"
#include "sxpe/games/sims3/sims3_game_profile.hpp"

#include <algorithm>

namespace sxpe::games::sims3 {
namespace {

std::uint8_t u8(std::byte b) { return static_cast<std::uint8_t>(b); }

bool need(std::span<const std::byte> in, std::size_t i, std::size_t n) {
    return i + n <= in.size();
}

}  // namespace

Result<std::vector<std::byte>> refpack_decompress(std::span<const std::byte> input,
                                                  std::uint32_t expected_mem_size) {
    using core::caps::kMaxCompressRatio;
    using core::caps::kMaxResourceBytes;

    if (input.size() < 5) {
        return std::unexpected(err(ErrorCode::refpack, "refpack too short"));
    }
    std::size_t i = 0;
    if (input.size() >= 6 && u8(input[0]) != 0x10 && u8(input[4]) == 0x10 &&
        u8(input[5]) == 0xFB) {
        i = 4;
    }
    if (!need(input, i, 5) || u8(input[i]) != 0x10 || u8(input[i + 1]) != 0xFB) {
        return std::unexpected(err(ErrorCode::refpack, "missing 10 FB signature"));
    }
    i += 2;
    const std::uint32_t uncomp = (static_cast<std::uint32_t>(u8(input[i])) << 16) |
                                 (static_cast<std::uint32_t>(u8(input[i + 1])) << 8) |
                                 u8(input[i + 2]);
    i += 3;
    if (uncomp > kMaxResourceBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "refpack mem_size cap"));
    }
    if (expected_mem_size != 0 && expected_mem_size != uncomp) {
        // Prefer stream size; still cap.
    }
    if (input.size() > 0 && uncomp / std::max<std::size_t>(input.size(), 1) > kMaxCompressRatio) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "refpack ratio cap"));
    }

    std::vector<std::byte> out(uncomp);
    std::size_t o = 0;
    auto push_lit = [&](std::size_t n) -> bool {
        if (!need(input, i, n) || o + n > out.size()) {
            return false;
        }
        std::copy_n(input.begin() + static_cast<std::ptrdiff_t>(i), n, out.begin() + static_cast<std::ptrdiff_t>(o));
        i += n;
        o += n;
        return true;
    };
    auto push_copy = [&](std::size_t len, std::size_t dist) -> bool {
        if (dist == 0 || dist > o || o + len > out.size()) {
            return false;
        }
        for (std::size_t k = 0; k < len; ++k) {
            out[o] = out[o - dist];
            ++o;
        }
        return true;
    };

    bool stopped = false;
    while (i < input.size() && o <= out.size()) {
        const std::uint8_t b0 = u8(input[i]);
        if (b0 >= 0xFC) {
            const std::size_t lit = b0 & 3;
            ++i;
            if (!push_lit(lit)) {
                return std::unexpected(err(ErrorCode::refpack, "stop literals"));
            }
            stopped = true;
            break;
        }
        if ((b0 & 0x80) == 0) {
            if (!need(input, i, 2)) {
                return std::unexpected(err(ErrorCode::refpack, "2-byte cmd"));
            }
            const std::uint8_t b1 = u8(input[i + 1]);
            i += 2;
            const std::size_t lit = b0 & 3;
            const std::size_t clen = static_cast<std::size_t>(((b0 & 0x1C) >> 2) + 3);
            const std::size_t dist = static_cast<std::size_t>(((b0 & 0x60) << 3) + b1 + 1);
            if (!push_lit(lit) || !push_copy(clen, dist)) {
                return std::unexpected(err(ErrorCode::refpack, "2-byte copy"));
            }
        } else if ((b0 & 0xC0) == 0x80) {
            if (!need(input, i, 3)) {
                return std::unexpected(err(ErrorCode::refpack, "3-byte cmd"));
            }
            const std::uint8_t b1 = u8(input[i + 1]);
            const std::uint8_t b2 = u8(input[i + 2]);
            i += 3;
            const std::size_t lit = (b1 & 0xC0) >> 6;
            const std::size_t clen = static_cast<std::size_t>((b0 & 0x3F) + 4);
            const std::size_t dist = static_cast<std::size_t>(((b1 & 0x3F) << 8) + b2 + 1);
            if (!push_lit(lit) || !push_copy(clen, dist)) {
                return std::unexpected(err(ErrorCode::refpack, "3-byte copy"));
            }
        } else if ((b0 & 0xE0) == 0xC0) {
            if (!need(input, i, 4)) {
                return std::unexpected(err(ErrorCode::refpack, "4-byte cmd"));
            }
            const std::uint8_t b1 = u8(input[i + 1]);
            const std::uint8_t b2 = u8(input[i + 2]);
            const std::uint8_t b3 = u8(input[i + 3]);
            i += 4;
            const std::size_t lit = b0 & 3;
            const std::size_t clen = static_cast<std::size_t>(((b0 & 0x0C) << 6) + b3 + 5);
            const std::size_t dist =
                static_cast<std::size_t>(((b0 & 0x10) << 12) + (static_cast<std::size_t>(b1) << 8) + b2 + 1);
            if (!push_lit(lit) || !push_copy(clen, dist)) {
                return std::unexpected(err(ErrorCode::refpack, "4-byte copy"));
            }
        } else {
            ++i;
            const std::size_t lit = static_cast<std::size_t>(((b0 & 0x1F) + 1) << 2);
            if (!push_lit(lit)) {
                return std::unexpected(err(ErrorCode::refpack, "long literals"));
            }
        }
    }
    if (!stopped && o != out.size()) {
        return std::unexpected(err(ErrorCode::refpack, "truncated stream"));
    }
    if (o != out.size()) {
        return std::unexpected(err(ErrorCode::refpack, "size mismatch"));
    }
    return out;
}

Result<std::vector<std::byte>> refpack_compress(std::span<const std::byte> input) {
    using core::caps::kMaxResourceBytes;
    if (input.size() > kMaxResourceBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "compress input cap"));
    }
    std::vector<std::byte> out;
    out.reserve(input.size() + 16);
    out.push_back(std::byte{0x10});
    out.push_back(std::byte{0xFB});
    const auto n = static_cast<std::uint32_t>(input.size());
    out.push_back(std::byte{(n >> 16) & 0xFF});
    out.push_back(std::byte{(n >> 8) & 0xFF});
    out.push_back(std::byte{n & 0xFF});

    std::size_t p = 0;
    while (input.size() - p >= 4) {
        std::size_t lit = std::min<std::size_t>(112, (input.size() - p) & ~std::size_t{3});
        if (lit < 4) {
            break;
        }
        const auto chunks = lit / 4;
        out.push_back(std::byte{static_cast<std::uint8_t>(0xE0 + (chunks - 1))});
        out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(p),
                   input.begin() + static_cast<std::ptrdiff_t>(p + lit));
        p += lit;
    }
    const std::size_t rest = input.size() - p;
    out.push_back(std::byte{static_cast<std::uint8_t>(0xFC | rest)});
    out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(p), input.end());
    return out;
}

std::string_view Sims3Compression::name() const { return "RefPack"; }

bool Sims3Compression::try_decompress(std::span<const std::byte> input, std::span<std::byte> output,
                                      std::size_t& bytes_written) const {
    auto r = refpack_decompress(input, static_cast<std::uint32_t>(output.size()));
    if (!r) {
        bytes_written = 0;
        return false;
    }
    if (r->size() > output.size()) {
        bytes_written = 0;
        return false;
    }
    std::copy(r->begin(), r->end(), output.begin());
    bytes_written = r->size();
    return true;
}

bool Sims3Compression::try_compress(std::span<const std::byte> input, std::span<std::byte> output,
                                    std::size_t& bytes_written) const {
    auto r = refpack_compress(input);
    if (!r || r->size() > output.size()) {
        bytes_written = 0;
        return false;
    }
    std::copy(r->begin(), r->end(), output.begin());
    bytes_written = r->size();
    return true;
}

}  // namespace sxpe::games::sims3
