#pragma once

namespace sxpe::games {

class GameProfile;

class PackageCodec {
public:
    virtual ~PackageCodec() = default;
    virtual const GameProfile& profile() const = 0;
};

}  // namespace sxpe::games
