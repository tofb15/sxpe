#include "check.hpp"
#include "sxpe/core/default_registry.hpp"
#include "sxpe/games/game_id.hpp"

#include <cstdint>
#include <iostream>
#include <span>

int main() {
    using sxpe::games::GameId;
    using sxpe::core::DefaultRegistry;

    DefaultRegistry registry;
    CHECK(registry.profiles().size() == 1);
    CHECK(registry.profiles()[0]->id() == GameId::Sims3);
    CHECK(registry.find(GameId::Sims3) != nullptr);
    CHECK(registry.find(GameId::Unknown) == nullptr);

    CHECK(static_cast<std::uint8_t>(GameId::Unknown) == 0);
    CHECK(static_cast<std::uint8_t>(GameId::Sims3) == 1);

    const auto& profile = *registry.find(GameId::Sims3);
    CHECK(profile.display_name() == "The Sims 3");
    bool saw_package = false;
    for (auto ext : profile.file_extensions()) {
        if (ext == ".package") {
            saw_package = true;
        }
    }
    CHECK(saw_package);

    const std::byte header[] = {std::byte{'D'}, std::byte{'B'}, std::byte{'P'},
                                std::byte{'F'}};
    CHECK(registry.sniff(std::span<const std::byte>(header)) != nullptr);

    const std::byte ts3[] = {std::byte{'D'}, std::byte{'B'}, std::byte{'P'}, std::byte{'F'},
                             std::byte{2},  std::byte{0},   std::byte{0},   std::byte{0}};
    CHECK(registry.sniff(std::span<const std::byte>(ts3)) != nullptr);

    const std::byte other[] = {std::byte{'D'}, std::byte{'B'}, std::byte{'P'}, std::byte{'P'}};
    CHECK(registry.sniff(std::span<const std::byte>(other)) == nullptr);

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
