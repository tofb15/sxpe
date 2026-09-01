#include "sxpe/games/sims3/sims3_game_profile.hpp"

#include <cstdint>
#include <cstring>

namespace sxpe::games::sims3 {

Sims3GameProfile::Sims3GameProfile() : codec_(*this) {}

std::span<const std::string_view> Sims3GameProfile::file_extensions() const {
    return kExts;
}

bool Sims3GameProfile::try_sniff(std::span<const std::byte> header, float& confidence) const {
    confidence = 0.f;
    if (header.size() < 4) {
        return false;
    }
    if (header[0] != std::byte{'D'} || header[1] != std::byte{'B'} || header[2] != std::byte{'P'} ||
        header[3] != std::byte{'F'}) {
        return false;
    }
    if (header.size() >= 8) {
        std::uint32_t major = 0;
        std::memcpy(&major, header.data() + 4, 4);
        if (major != 2) {
            return false;
        }
        confidence = 1.f;
        return true;
    }
    confidence = 0.5f;
    return true;
}

}  // namespace sxpe::games::sims3
