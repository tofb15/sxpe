#pragma once

#include "sxpe/error.hpp"
#include "sxpe/games/sims3/tgi.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace sxpe::resources {

/// Sims 3 key-table TGI list (wiki Key table): BYTE or DWORD count, then
/// type/group/instance(LE qword) per entry. Used by OBJK/VPXY wrappers and similar.
Result<std::vector<sxpe::games::sims3::Tgi>> parse_key_table_tgis(std::span<const std::byte> bytes,
                                                                  bool count_is_dword = false);

}  // namespace sxpe::resources
