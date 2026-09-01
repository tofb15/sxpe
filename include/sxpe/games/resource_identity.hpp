#pragma once

#include "sxpe/games/game_id.hpp"

#include <string>

namespace sxpe::games {

class ResourceIdentity {
public:
    virtual ~ResourceIdentity() = default;
    virtual GameId game() const = 0;
    virtual std::string to_display_string() const = 0;
};

}  // namespace sxpe::games
