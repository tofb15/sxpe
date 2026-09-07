#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace sxpe::core {

/// FIPS 180-4 SHA-256 (public-domain style compact implementation).
inline std::array<std::uint8_t, 32> sha256(std::span<const std::byte> data) {
    static constexpr std::uint32_t K[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

    auto rotr = [](std::uint32_t x, std::uint32_t n) {
        return (x >> n) | (x << (32 - n));
    };

    std::uint32_t h0 = 0x6a09e667u, h1 = 0xbb67ae85u, h2 = 0x3c6ef372u, h3 = 0xa54ff53au;
    std::uint32_t h4 = 0x510e527fu, h5 = 0x9b05688cu, h6 = 0x1f83d9abu, h7 = 0x5be0cd19u;

    const auto* bytes = reinterpret_cast<const std::uint8_t*>(data.data());
    const std::uint64_t bit_len = static_cast<std::uint64_t>(data.size()) * 8ull;
    const std::size_t len = data.size();
    const std::size_t pad_len = ((len + 9 + 63) / 64) * 64;  // include 0x80 + 8-byte length
    std::vector<std::uint8_t> msg(pad_len, 0);
    if (len) {
        std::memcpy(msg.data(), bytes, len);
    }
    msg[len] = 0x80;
    for (int i = 0; i < 8; ++i) {
        msg[pad_len - 1 - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((bit_len >> (8 * i)) & 0xffu);
    }

    for (std::size_t off = 0; off < pad_len; off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const std::size_t j = off + static_cast<std::size_t>(i) * 4;
            w[i] = (static_cast<std::uint32_t>(msg[j]) << 24) |
                   (static_cast<std::uint32_t>(msg[j + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[j + 2]) << 8) |
                   static_cast<std::uint32_t>(msg[j + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4, f = h5, g = h6, h = h7;
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t t1 = h + S1 + ch + K[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
        h5 += f;
        h6 += g;
        h7 += h;
    }

    std::array<std::uint8_t, 32> out{};
    auto put = [&](int i, std::uint32_t v) {
        out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> 24) & 0xffu);
        out[static_cast<std::size_t>(i + 1)] = static_cast<std::uint8_t>((v >> 16) & 0xffu);
        out[static_cast<std::size_t>(i + 2)] = static_cast<std::uint8_t>((v >> 8) & 0xffu);
        out[static_cast<std::size_t>(i + 3)] = static_cast<std::uint8_t>(v & 0xffu);
    };
    put(0, h0);
    put(4, h1);
    put(8, h2);
    put(12, h3);
    put(16, h4);
    put(20, h5);
    put(24, h6);
    put(28, h7);
    return out;
}

inline std::string sha256_hex(std::span<const std::byte> data) {
    const auto dig = sha256(data);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string s;
    s.resize(64);
    for (std::size_t i = 0; i < 32; ++i) {
        s[i * 2] = kHex[dig[i] >> 4];
        s[i * 2 + 1] = kHex[dig[i] & 0xf];
    }
    return s;
}

}  // namespace sxpe::core
