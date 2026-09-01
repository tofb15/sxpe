#pragma once

#include "sxpe/games/game_profile_registry.hpp"
#include "sxpe/games/sims3/sims3_game_profile.hpp"

#include <array>

namespace sxpe::core {

/// v1 registers The Sims 3 only.
class DefaultRegistry final : public games::GameProfileRegistry {
public:
    DefaultRegistry() : profiles_{&sims3_} {}
    DefaultRegistry(const DefaultRegistry&) = delete;
    DefaultRegistry& operator=(const DefaultRegistry&) = delete;

    std::span<const games::GameProfile* const> profiles() const override {
        return profiles_;
    }

    const games::GameProfile* find(games::GameId id) const override {
        return id == games::GameId::Sims3 ? &sims3_ : nullptr;
    }

    const games::GameProfile* sniff(std::span<const std::byte> header) const override {
        float best = 0.f;
        const games::GameProfile* chosen = nullptr;
        for (const games::GameProfile* p : profiles_) {
            float c = 0.f;
            if (p->try_sniff(header, c) && c > best) {
                best = c;
                chosen = p;
            }
        }
        return chosen;
    }

private:
    games::sims3::Sims3GameProfile sims3_{};
    std::array<const games::GameProfile*, 1> profiles_;
};

}  // namespace sxpe::core
