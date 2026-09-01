#pragma once

#include "sxpe/games/game_id.hpp"
#include "sxpe/games/game_profile.hpp"

#include <span>
#include <vector>

namespace sxpe::games {

class GameProfileRegistry {
public:
    virtual ~GameProfileRegistry() = default;
    virtual std::span<const GameProfile* const> profiles() const = 0;
    virtual const GameProfile* find(GameId id) const = 0;
    virtual const GameProfile* sniff(std::span<const std::byte> header) const = 0;
};

}  // namespace sxpe::games
