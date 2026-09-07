#include "sxpe/games/sims3/refpack.hpp"

#include "sxpe/core/caps.hpp"
#include "sxpe/games/compression_codec.hpp"
#include "sxpe/games/sims3/sims3_game_profile.hpp"

#include <algorithm>
#include <cstdint>

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
    // TS3 uses the 3-byte big-endian uncompressed size form after 10 FB.
    if (input.size() > 0x00FFFFFFu) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "refpack 3-byte size limit"));
    }

    constexpr std::size_t kMinMatch = 3;
    constexpr std::size_t kMaxMatch = 1028;
    constexpr std::size_t kMaxDist = 131072;
    constexpr std::size_t kHashBits = 15;
    constexpr std::size_t kHashSize = std::size_t{1} << kHashBits;
    constexpr std::size_t kMaxChain = 128;
    constexpr std::size_t kNiceLen = 128;

    const std::size_t n = input.size();
    std::vector<std::byte> out;
    out.reserve(n / 2 + 64);
    out.push_back(std::byte{0x10});
    out.push_back(std::byte{0xFB});
    const auto uncomp = static_cast<std::uint32_t>(n);
    out.push_back(std::byte{(uncomp >> 16) & 0xFF});
    out.push_back(std::byte{(uncomp >> 8) & 0xFF});
    out.push_back(std::byte{uncomp & 0xFF});

    auto push_bytes = [&](std::size_t from, std::size_t count) {
        out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(from),
                   input.begin() + static_cast<std::ptrdiff_t>(from + count));
    };

    auto emit_long_literals = [&](std::size_t from, std::size_t count) -> std::size_t {
        std::size_t p = from;
        std::size_t left = count;
        while (left >= 4) {
            const std::size_t block = std::min<std::size_t>(112, left & ~std::size_t{3});
            out.push_back(std::byte{static_cast<std::uint8_t>(0xE0 + (block / 4 - 1))});
            push_bytes(p, block);
            p += block;
            left -= block;
        }
        return left;
    };

    auto emit_match = [&](std::size_t lit_from, std::size_t lit_count, std::size_t match_len,
                          std::size_t dist) {
        std::size_t from = lit_from;
        std::size_t lit = lit_count;
        if (lit > 3) {
            const std::size_t flush = lit - (lit & 3);
            emit_long_literals(from, flush);
            from += flush;
            lit -= flush;
        }
        const std::size_t d = dist - 1;
        if (match_len <= 10 && dist <= 1024) {
            out.push_back(std::byte{static_cast<std::uint8_t>(((d >> 8) << 5) |
                                                              ((match_len - 3) << 2) | lit)});
            out.push_back(std::byte{static_cast<std::uint8_t>(d & 0xFF)});
        } else if (match_len <= 67 && dist <= 16384) {
            out.push_back(std::byte{static_cast<std::uint8_t>(0x80 | (match_len - 4))});
            out.push_back(std::byte{static_cast<std::uint8_t>((lit << 6) | ((d >> 8) & 0x3F))});
            out.push_back(std::byte{static_cast<std::uint8_t>(d & 0xFF)});
        } else {
            out.push_back(std::byte{static_cast<std::uint8_t>(
                0xC0 | (((d >> 16) & 1) << 4) | (((match_len - 5) >> 8) << 2) | lit)});
            out.push_back(std::byte{static_cast<std::uint8_t>((d >> 8) & 0xFF)});
            out.push_back(std::byte{static_cast<std::uint8_t>(d & 0xFF)});
            out.push_back(std::byte{static_cast<std::uint8_t>((match_len - 5) & 0xFF)});
        }
        if (lit != 0) {
            push_bytes(from, lit);
        }
    };

    if (n < kMinMatch) {
        const std::size_t rem = emit_long_literals(0, n);
        out.push_back(std::byte{static_cast<std::uint8_t>(0xFC | rem)});
        if (rem != 0) {
            push_bytes(n - rem, rem);
        }
        return out;
    }

    std::vector<std::int32_t> head(kHashSize, -1);
    std::vector<std::int32_t> prev(n, -1);

    auto hash_at = [&](std::size_t i) -> std::size_t {
        const auto a = static_cast<std::uint32_t>(u8(input[i]));
        const auto b = static_cast<std::uint32_t>(u8(input[i + 1]));
        const auto c = static_cast<std::uint32_t>(u8(input[i + 2]));
        return static_cast<std::size_t>((a << 10) ^ (b << 5) ^ c) & (kHashSize - 1);
    };

    // Insert pos into the hash chain; return previous head (candidates before pos).
    auto insert_get_prev = [&](std::size_t i) -> std::int32_t {
        if (i + kMinMatch > n) {
            return -1;
        }
        const auto h = hash_at(i);
        const std::int32_t older = head[h];
        prev[i] = older;
        head[h] = static_cast<std::int32_t>(i);
        return older;
    };

    auto find_match = [&](std::int32_t cur, std::size_t pos, std::size_t& out_dist) -> std::size_t {
        out_dist = 0;
        std::size_t best_len = 0;
        std::size_t best_dist = 0;
        std::size_t chain = kMaxChain;
        const std::size_t max_len = std::min(kMaxMatch, n - pos);
        const std::size_t oldest = pos > kMaxDist ? pos - kMaxDist : 0;

        while (cur >= 0 && chain-- > 0) {
            const std::size_t mpos = static_cast<std::size_t>(cur);
            if (mpos < oldest) {
                break;
            }
            const std::size_t dist = pos - mpos;
            if (dist == 0 || dist > kMaxDist) {
                cur = prev[mpos];
                continue;
            }
            if (input[mpos] != input[pos] || input[mpos + 1] != input[pos + 1]) {
                cur = prev[mpos];
                continue;
            }
            std::size_t len = 2;
            while (len < max_len && input[mpos + len] == input[pos + len]) {
                ++len;
            }
            if (len < kMinMatch) {
                cur = prev[mpos];
                continue;
            }

            std::size_t enc = len;
            if (dist > 16384) {
                if (len < 5) {
                    cur = prev[mpos];
                    continue;
                }
                enc = std::min(len, kMaxMatch);
            } else if (dist > 1024) {
                if (len < 4) {
                    cur = prev[mpos];
                    continue;
                }
                enc = std::min(len, std::size_t{67});
            }

            if (enc > best_len) {
                best_len = enc;
                best_dist = dist;
                if (best_len >= kNiceLen) {
                    break;
                }
            }
            cur = prev[mpos];
        }

        if (best_len >= kMinMatch && best_dist > 0) {
            out_dist = best_dist;
            return best_len;
        }
        return 0;
    };

    std::size_t pos = 0;
    std::size_t lit_start = 0;

    while (pos < n) {
        std::size_t dist = 0;
        std::size_t match_len = 0;
        std::int32_t chain_head = -1;
        if (pos + kMinMatch <= n) {
            chain_head = insert_get_prev(pos);
            match_len = find_match(chain_head, pos, dist);
        }

        if (match_len >= kMinMatch) {
            if (match_len < kNiceLen && pos + 1 + kMinMatch <= n) {
                // Probe without inserting so a taken match does not double-hash pos+1.
                std::size_t dist2 = 0;
                const std::size_t next_len = find_match(head[hash_at(pos + 1)], pos + 1, dist2);
                if (next_len > match_len) {
                    ++pos;
                    continue;
                }
            }

            emit_match(lit_start, pos - lit_start, match_len, dist);
            const std::size_t end = pos + match_len;
            ++pos;
            while (pos < end) {
                if (pos + kMinMatch <= n) {
                    (void)insert_get_prev(pos);
                }
                ++pos;
            }
            lit_start = pos;
            continue;
        }

        ++pos;
    }

    const std::size_t left = emit_long_literals(lit_start, n - lit_start);
    out.push_back(std::byte{static_cast<std::uint8_t>(0xFC | left)});
    if (left != 0) {
        push_bytes(n - left, left);
    }
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
