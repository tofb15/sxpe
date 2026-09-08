#pragma once

#include "sxpe/resources/types.hpp"

#include <cstdint>
#include <string_view>

namespace sxpe::resources {

/// Allowlist entry: match type + instance; group is ignored (community packs vary).
/// Documented for `resource.importPackage` leftoverManifestPolicy (issue #64).
struct LeftoverManifestKey {
    std::uint32_t type{0};
    std::uint64_t instance{0};
    std::string_view reason{};
};

/// Known leftover manifest TGIs stripped (or warned) on merge/import.
/// Extend only with documented community-safe leftovers — never strip gameplay XML.
inline constexpr LeftoverManifestKey kLeftoverManifestAllowlist[] = {
    {kSims3PackLeftoverManifest, 0,
     "Sims3Pack leftover manifest XML (type 0x73E93EEB instance 0)"},
};

[[nodiscard]] inline bool is_leftover_manifest_tgi(std::uint32_t type, std::uint32_t /*group*/,
                                                   std::uint64_t instance) {
    for (const auto& k : kLeftoverManifestAllowlist) {
        if (k.type == type && k.instance == instance) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline std::string_view leftover_manifest_reason(std::uint32_t type,
                                                               std::uint64_t instance) {
    for (const auto& k : kLeftoverManifestAllowlist) {
        if (k.type == type && k.instance == instance) {
            return k.reason;
        }
    }
    return {};
}

/// duplicateTgiPolicy values for resource.importPackage / importDbc.
inline constexpr std::string_view kDupPolicyForce = "force";
inline constexpr std::string_view kDupPolicySkip = "skip";
inline constexpr std::string_view kDupPolicyFail = "fail";

/// leftoverManifestPolicy values.
inline constexpr std::string_view kLeftoverStrip = "strip";
inline constexpr std::string_view kLeftoverKeep = "keep";
inline constexpr std::string_view kLeftoverWarn = "warn";

}  // namespace sxpe::resources
