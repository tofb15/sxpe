#include "check.hpp"
#include "sxpe/error.hpp"
#include "sxpe/games/sims3/refpack.hpp"
#include "sxpe/games/sims3/sims3_game_profile.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

using sxpe::ErrorCode;
using sxpe::games::sims3::refpack_compress;
using sxpe::games::sims3::refpack_decompress;

const std::filesystem::path kSynth{SXPE_SYNTHETIC_DIR};

std::vector<std::byte> read_all(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::vector<char> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::byte> out(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        out[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
    }
    return out;
}

std::string as_text(const std::vector<std::byte>& b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

}  // namespace

int main() {
    auto hello = read_all(kSynth / "refpack-hello.bin");
    auto dec = refpack_decompress(hello, 2);
    CHECK(dec.has_value());
    if (dec) {
        CHECK(as_text(*dec) == "Hi");
    }

    const char msg[] = "The quick brown fox jumps over the lazy dog. 0123456789";
    auto in = std::as_bytes(std::span{msg, sizeof(msg) - 1});
    auto enc = refpack_compress(in);
    CHECK(enc.has_value());
    if (enc) {
        CHECK(enc->size() >= 6);
        CHECK((*enc)[0] == std::byte{0x10});
        CHECK((*enc)[1] == std::byte{0xFB});
        auto back = refpack_decompress(*enc, static_cast<std::uint32_t>(in.size()));
        CHECK(back.has_value());
        if (back) {
            CHECK(as_text(*back) == msg);
        }
    }

    // Real compressor: repetitive payload must shrink materially and round-trip.
    {
        std::vector<std::byte> rep;
        const char chunk[] = "SXPE-RefPack-pattern-0123456789abcdef";
        const std::size_t chunk_n = sizeof(chunk) - 1;
        rep.reserve(chunk_n * 100);
        for (int i = 0; i < 100; ++i) {
            for (std::size_t j = 0; j < chunk_n; ++j) {
                rep.push_back(static_cast<std::byte>(static_cast<unsigned char>(chunk[j])));
            }
        }
        auto enc2 = refpack_compress(rep);
        CHECK(enc2.has_value());
        if (enc2) {
            CHECK(enc2->size() * 4 < rep.size());  // compressed << uncompressed
            CHECK((*enc2)[0] == std::byte{0x10} && (*enc2)[1] == std::byte{0xFB});
            auto back2 = refpack_decompress(*enc2, static_cast<std::uint32_t>(rep.size()));
            CHECK(back2.has_value());
            if (back2) {
                CHECK(*back2 == rep);
            }
        }
    }

    // Incompressible / short still round-trips.
    {
        std::vector<std::byte> rnd(64);
        for (std::size_t i = 0; i < rnd.size(); ++i) {
            rnd[i] = std::byte{static_cast<unsigned char>(i * 37 + 11)};
        }
        auto enc3 = refpack_compress(rnd);
        CHECK(enc3.has_value());
        if (enc3) {
            auto back3 = refpack_decompress(*enc3, static_cast<std::uint32_t>(rnd.size()));
            CHECK(back3.has_value() && *back3 == rnd);
        }
    }

    std::vector<std::byte> bomb{std::byte{0x10}, std::byte{0xFB}, std::byte{0xFF}, std::byte{0xFF},
                                std::byte{0xFF}};
    auto boom = refpack_decompress(bomb);
    CHECK(!boom);
    if (!boom) {
        CHECK(boom.error().code == ErrorCode::cap_exceeded);
    }

    auto bad = refpack_decompress(std::span<const std::byte>{});
    CHECK(!bad);
    if (!bad) {
        CHECK(bad.error().code == ErrorCode::refpack);
    }

    sxpe::games::sims3::Sims3Compression codec;
    CHECK(codec.name() == "RefPack");
    if (enc) {
        std::vector<std::byte> out(in.size() + 8);
        std::size_t n = 0;
        CHECK(codec.try_decompress(*enc, out, n));
        CHECK(n == in.size());
    }

    for (int i = 0; i < 32; ++i) {
        std::vector<std::byte> fuzz(static_cast<std::size_t>(i + 1));
        for (std::size_t j = 0; j < fuzz.size(); ++j) {
            fuzz[j] = std::byte{static_cast<unsigned char>(i * 17 + j * 31)};
        }
        auto r = refpack_decompress(fuzz);
        (void)r;
    }

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
