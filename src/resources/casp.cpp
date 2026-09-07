#include "sxpe/resources/casp.hpp"

#include "sxpe/resources/binary.hpp"

namespace sxpe::resources {
namespace {

void decode_age_gender(Casp& c) {
    c.age_flags = static_cast<std::uint8_t>(c.age_gender & 0xFF);
    const auto sg = static_cast<std::uint8_t>((c.age_gender >> 8) & 0xFF);
    c.species = static_cast<std::uint8_t>(sg & 0x0F);
    c.gender_flags = static_cast<std::uint8_t>((sg >> 4) & 0x0F);
    c.handedness = static_cast<std::uint16_t>((c.age_gender >> 16) & 0xFFFF);
}

}  // namespace

const char* casp_clothing_type_name(std::uint32_t id) {
    switch (id) {
        case 0:
            return "None";
        case 1:
            return "Hair";
        case 2:
            return "Scalp";
        case 3:
            return "Face";
        case 4:
            return "Body";
        case 5:
            return "Top";
        case 6:
            return "Bottom";
        case 7:
            return "Shoes";
        case 8:
            return "FirstAccessory";
        case 9:
            return "Necklace";
        case 10:
            return "NoseRing";
        case 11:
            return "Earrings";
        case 12:
            return "Glasses";
        case 13:
            return "Bracelets";
        case 14:
            return "RingLt";
        case 15:
            return "RingRt";
        case 16:
            return "Beard";
        case 17:
            return "Lipstick";
        case 18:
            return "Eyeshadow";
        case 19:
            return "Eyeliner";
        case 20:
            return "Blush";
        case 21:
            return "Makeup";
        case 22:
            return "Eyebrow";
        case 23:
            return "EyeColor";
        case 24:
            return "Glove";
        case 25:
            return "Socks";
        case 26:
            return "Mascara";
        case 0x21:
            return "Tattoo";
        case 0x2F:
            return "PetBody";
        default:
            return nullptr;
    }
}

std::vector<std::string> casp_age_names(std::uint8_t age_flags) {
    static constexpr struct {
        std::uint8_t bit;
        const char* name;
    } kAges[] = {
        {0x01, "Baby"}, {0x02, "Toddler"}, {0x04, "Child"},      {0x08, "Teen"},
        {0x10, "YA"},   {0x20, "Adult"},   {0x40, "Elder"},
    };
    std::vector<std::string> out;
    for (const auto& a : kAges) {
        if (age_flags & a.bit) {
            out.emplace_back(a.name);
        }
    }
    return out;
}

std::vector<std::string> casp_gender_names(std::uint8_t gender_flags) {
    std::vector<std::string> out;
    if (gender_flags & 0x1) {
        out.emplace_back("Male");
    }
    if (gender_flags & 0x2) {
        out.emplace_back("Female");
    }
    return out;
}

const char* casp_species_name(std::uint8_t species) {
    switch (species) {
        case 1:
            return "Human";
        case 2:
            return "Horse";
        case 3:
            return "Cat";
        case 4:
            return "Dog";
        case 5:
            return "LittleDog";
        case 6:
            return "Deer";
        case 7:
            return "Raccoon";
        default:
            return nullptr;
    }
}

Result<Casp> parse_casp(std::span<const std::byte> bytes) {
    using namespace bin;
    if (bytes.size() < 16) {
        return std::unexpected(err(ErrorCode::corrupt, "casp too short"));
    }
    Casp c;
    std::size_t p = 0;
    if (!take_u32(bytes, p, c.version)) {
        return std::unexpected(err(ErrorCode::corrupt, "casp version"));
    }
    std::uint32_t ref_off = 0;
    if (!take_u32(bytes, p, ref_off)) {
        return std::unexpected(err(ErrorCode::corrupt, "casp offset"));
    }
    std::uint32_t preset_count = 0;
    if (!take_u32(bytes, p, preset_count)) {
        return std::unexpected(err(ErrorCode::corrupt, "casp presets"));
    }
    constexpr std::uint32_t kMaxPresets = 256;
    if (preset_count > kMaxPresets) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "casp presets"));
    }
    for (std::uint32_t i = 0; i < preset_count; ++i) {
        std::uint32_t len = 0;
        if (!take_u32(bytes, p, len)) {
            return std::unexpected(err(ErrorCode::corrupt, "casp preset len"));
        }
        // char16[length] UTF-16LE + trailing DWORD
        const std::size_t chars = static_cast<std::size_t>(len) * 2;
        if (len > sxpe::core::caps::kMaxNameBytes || p + chars + 4 > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "casp preset xml"));
        }
        p += chars + 4;
    }
    if (!take_7bit_utf16be(bytes, p, c.name)) {
        c.partial = true;
        return c;
    }
    std::uint8_t unused = 0;
    if (!take_f32(bytes, p, c.sort_priority) || !take_u8(bytes, p, unused)) {
        c.partial = true;
        return c;
    }
    if (!take_u32(bytes, p, c.clothing_type) || !take_u32(bytes, p, c.type_flags) ||
        !take_u32(bytes, p, c.age_gender) || !take_u32(bytes, p, c.clothing_category)) {
        c.partial = true;
        return c;
    }
    decode_age_gender(c);
    return c;
}

}  // namespace sxpe::resources
