#include "sxpe/games/sims3/sims3_game_profile.hpp"

namespace sxpe::games::sims3 {

Sims3GameProfile::Sims3GameProfile() : codec_(*this) {}

std::span<const std::string_view> Sims3GameProfile::file_extensions() const {
    return kExts;
}

bool Sims3GameProfile::try_sniff(std::span<const std::byte>, float& confidence) const {
    confidence = 0.f;
    return false;
}

}  // namespace sxpe::games::sims3
