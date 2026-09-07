#include "check.hpp"
#include "sxpe/games/sims3/fnv.hpp"
#include "sxpe/games/sims3/tgi.hpp"

int main() {
    using namespace sxpe::games::sims3;

    CHECK(fnv1_32("", false) == 0x811C9DC5u);
    CHECK(fnv1_64("", false) == 0xCBF29CE484222325ull);

    const auto a32 = fnv1_32("a", true);
    std::uint32_t expect32 = 0x811C9DC5u;
    expect32 *= 0x01000193u;
    expect32 ^= static_cast<std::uint32_t>('a');
    CHECK(a32 == expect32);

    const auto a64 = fnv1_64("A", true);
    std::uint64_t expect64 = 0xCBF29CE484222325ull;
    expect64 *= 0x00000100000001B3ull;
    expect64 ^= static_cast<std::uint64_t>('a');
    CHECK(a64 == expect64);

    CHECK(fnv1_32("Hello", true) == fnv1_32("HELLO", true));
    CHECK(fnv64_clip("walk") == fnv1_64("walk", true));

    const auto a_walk = fnv1_64("a_walk", true) & ~(1ull << 63);
    CHECK(fnv64_clip("a_walk") == a_walk);
    CHECK(fnv64_clip("A_WALK") == a_walk);
    auto t_walk = fnv1_64("a_walk", true) | (1ull << 63);
    {
        auto hi = static_cast<std::uint8_t>(t_walk >> 56);
        hi = static_cast<std::uint8_t>(hi ^ 0x04);
        t_walk = (t_walk & 0x00FFFFFFFFFFFFFFull) | (static_cast<std::uint64_t>(hi) << 56);
    }
    CHECK(fnv64_clip("t_walk") == t_walk);
    CHECK(fnv64_clip("t_walk") != fnv64_clip("a_walk"));

    const auto a2a = fnv1_64("a2a_sit", true) & ~(1ull << 63);
    CHECK(fnv64_clip("a2a_sit") == a2a);
    auto t2c = fnv1_64("a2a_sit", true) | (1ull << 63);
    {
        auto hi = static_cast<std::uint8_t>(t2c >> 56);
        auto mid = static_cast<std::uint8_t>(t2c >> 48);
        hi = static_cast<std::uint8_t>(hi ^ 0x04);
        mid = static_cast<std::uint8_t>(mid ^ 0x03);
        t2c = (t2c & 0x0000FFFFFFFFFFFFull) | (static_cast<std::uint64_t>(hi) << 56) |
              (static_cast<std::uint64_t>(mid) << 48);
    }
    CHECK(fnv64_clip("t2c_sit") == t2c);

    Tgi tgi{0x220557DA, 0, 1};
    CHECK(community_filename(tgi, "Hello", "stbl") ==
          "S3_220557DA_00000000_0000000000000001_Hello%%+stbl");
    CHECK(community_filename(tgi, "bad/name", ".CLIP.animation") ==
          "S3_220557DA_00000000_0000000000000001_badname%%+CLIP.animation");

    TgiKey key{tgi};
    CHECK(key.game() == sxpe::games::GameId::Sims3);
    CHECK(key.to_display_string() == "220557DA-00000000-0000000000000001");

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
