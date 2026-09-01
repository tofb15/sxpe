#pragma once

#include "sxpe/games/compression_codec.hpp"
#include "sxpe/games/game_profile.hpp"
#include "sxpe/games/package_codec.hpp"

#include <array>
#include <string_view>

namespace sxpe::games::sims3 {

class Sims3Compression final : public CompressionCodec {
public:
    std::string_view name() const override { return "RefPack"; }
    bool try_decompress(std::span<const std::byte>, std::span<std::byte>,
                        std::size_t& bytes_written) const override {
        bytes_written = 0;
        return false;
    }
    bool try_compress(std::span<const std::byte>, std::span<std::byte>,
                      std::size_t& bytes_written) const override {
        bytes_written = 0;
        return false;
    }
};

class Sims3GameProfile;

class Sims3PackageCodec final : public PackageCodec {
public:
    explicit Sims3PackageCodec(const GameProfile& profile) : profile_(&profile) {}
    const GameProfile& profile() const override { return *profile_; }

private:
    const GameProfile* profile_;
};

class Sims3GameProfile final : public GameProfile {
public:
    Sims3GameProfile();
    GameId id() const override { return GameId::Sims3; }
    std::string_view display_name() const override { return "The Sims 3"; }
    std::span<const std::string_view> file_extensions() const override;
    const PackageCodec& package_codec() const override { return codec_; }
    const CompressionCodec& compression() const override { return compression_; }
    bool try_sniff(std::span<const std::byte> header, float& confidence) const override;

private:
    Sims3Compression compression_{};
    Sims3PackageCodec codec_;
    static constexpr std::array<std::string_view, 4> kExts{
        ".package", ".world", ".dbc", ".nhd"};
};

}  // namespace sxpe::games::sims3
