#pragma once

#include <cstdint>
#include <string_view>

namespace sxpe::games::sims3 {

std::uint32_t fnv1_32(std::string_view s, bool lowercase = true);
std::uint64_t fnv1_64(std::string_view s, bool lowercase = true);

/// CLIP instance: FNV-1 64 after SimsWiki age-letter substitution
/// (https://simswiki.info/wiki.php?title=Sims_3:0x6B20C4F3).
std::uint64_t fnv64_clip(std::string_view clip_name);

}  // namespace sxpe::games::sims3
