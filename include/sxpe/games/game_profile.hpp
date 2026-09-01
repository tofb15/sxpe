#pragma once

#include "sxpe/games/game_id.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace sxpe::games {

class PackageCodec;
class CompressionCodec;

class GameProfile {
public:
    virtual ~GameProfile() = default;
    virtual GameId id() const = 0;
    virtual std::string_view display_name() const = 0;
    virtual std::span<const std::string_view> file_extensions() const = 0;
    virtual const PackageCodec& package_codec() const = 0;
    virtual const CompressionCodec& compression() const = 0;

    /// Sims 3: ASCII DBPF and major 2. Other magics return false.
    virtual bool try_sniff(std::span<const std::byte> header, float& confidence) const = 0;
};

}  // namespace sxpe::games
