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

    constexpr std::uint64_t kFrozenAWalk = 0x11a06ab91bca6bdeull;
    constexpr std::uint64_t kFrozenTWalk = 0x95a06ab91bca6bdeull;
    constexpr std::uint64_t kFrozenA2aSit = 0x3b27e4eac9eb9af8ull;  // a2a_sit, bit 63 clear
    constexpr std::uint64_t kFrozenT2cSit = 0xbf24e4eac9eb9af8ull;
    CHECK(fnv64_clip("a_walk") == kFrozenAWalk);
    CHECK(fnv64_clip("A_WALK") == kFrozenAWalk);
    CHECK(fnv64_clip("t_walk") == kFrozenTWalk);
    CHECK(fnv64_clip("t_walk") != fnv64_clip("a_walk"));
    CHECK(fnv64_clip("a2a_sit") == kFrozenA2aSit);
    CHECK(fnv64_clip("t2c_sit") == kFrozenT2cSit);

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
