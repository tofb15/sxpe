#pragma once

#include "sxpe/games/resource_identity.hpp"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace sxpe::games::sims3 {

struct Tgi {
    std::uint32_t type{0};
    std::uint32_t group{0};
    std::uint64_t instance{0};

    friend bool operator==(const Tgi&, const Tgi&) = default;
};

struct ResourceId {
    Tgi tgi{};
    std::uint32_t ordinal{0};
};

class TgiKey final : public ResourceIdentity {
public:
    explicit TgiKey(Tgi t) : tgi_(t) {}
    GameId game() const override { return GameId::Sims3; }
    std::string to_display_string() const override {
        return std::format("{:08X}-{:08X}-{:016X}", tgi_.type, tgi_.group, tgi_.instance);
    }
    Tgi tgi() const { return tgi_; }

private:
    Tgi tgi_{};
};

std::string community_filename(Tgi tgi, std::string_view name, std::string_view ext);

}  // namespace sxpe::games::sims3
