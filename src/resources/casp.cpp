#include "sxpe/resources/casp.hpp"

#include "sxpe/resources/binary.hpp"

#include <cstring>

namespace sxpe::resources {
namespace {

void decode_age_gender(Casp& c) {
    c.age_flags = static_cast<std::uint8_t>(c.age_gender & 0xFF);
    const auto sg = static_cast<std::uint8_t>((c.age_gender >> 8) & 0xFF);
    c.species = static_cast<std::uint8_t>(sg & 0x0F);
    c.gender_flags = static_cast<std::uint8_t>((sg >> 4) & 0x0F);
    c.handedness = static_cast<std::uint16_t>((c.age_gender >> 16) & 0xFFFF);
}

struct CaspLayout {
    Casp value;
    std::uint32_t ref_off{0};
    std::size_t after_presets{0};
    std::size_t name_at{0};
    std::size_t sort_at{0};
    std::size_t unused_at{0};
    std::size_t clothing_type_at{0};
    std::size_t type_flags_at{0};
    std::size_t age_gender_at{0};
    std::size_t clothing_category_at{0};
    std::size_t after_header_fields{0};
    std::size_t tgi_at{0};
    std::size_t after_tgi{0};
    bool tgi_ok{false};
};

Result<CaspLayout> parse_casp_layout(std::span<const std::byte> bytes) {
    using namespace bin;
    if (bytes.size() < 16) {
        return std::unexpected(err(ErrorCode::corrupt, "casp too short"));
    }
    CaspLayout L;
    Casp& c = L.value;
    std::size_t p = 0;
    if (!take_u32(bytes, p, c.version)) {
        return std::unexpected(err(ErrorCode::corrupt, "casp version"));
    }
    if (!take_u32(bytes, p, L.ref_off)) {
        return std::unexpected(err(ErrorCode::corrupt, "casp offset"));
    }
    L.tgi_at = static_cast<std::size_t>(L.ref_off) + 8;

    auto parse_i64gt = [&](std::size_t at) {
        if (at >= bytes.size()) {
            return;
        }
        std::size_t tp = at;
        std::uint8_t count = 0;
        if (!take_u8(bytes, tp, count)) {
            return;
        }
        if (count > sxpe::core::caps::kMaxTableEntries) {
            c.partial = true;
            return;
        }
        const std::size_t need = static_cast<std::size_t>(count) * 16;
        if (tp + need > bytes.size()) {
            c.partial = true;
            return;
        }
        c.tgis.reserve(count);
        for (std::uint8_t i = 0; i < count; ++i) {
            sxpe::games::sims3::Tgi t{};
            std::uint64_t inst = 0;
            std::uint32_t group = 0;
            std::uint32_t type = 0;
            if (!take_u64(bytes, tp, inst) || !take_u32(bytes, tp, group) ||
                !take_u32(bytes, tp, type)) {
                c.partial = true;
                return;
            }
            t.instance = inst;
            t.group = group;
            t.type = type;
            c.tgis.push_back(t);
        }
        L.after_tgi = tp;
        L.tgi_ok = true;
    };

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
        const std::size_t chars = static_cast<std::size_t>(len) * 2;
        if (len > sxpe::core::caps::kMaxNameBytes || p + chars + 4 > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "casp preset xml"));
        }
        p += chars + 4;
    }
    L.after_presets = p;
    L.name_at = p;
    if (!take_7bit_utf16be(bytes, p, c.name)) {
        c.partial = true;
        parse_i64gt(L.tgi_at);
        L.after_header_fields = p;
        return L;
    }
    L.sort_at = p;
    std::uint8_t unused = 0;
    if (!take_f32(bytes, p, c.sort_priority)) {
        c.partial = true;
        parse_i64gt(L.tgi_at);
        L.after_header_fields = p;
        return L;
    }
    L.unused_at = p;
    if (!take_u8(bytes, p, unused)) {
        c.partial = true;
        parse_i64gt(L.tgi_at);
        L.after_header_fields = p;
        return L;
    }
    L.clothing_type_at = p;
    if (!take_u32(bytes, p, c.clothing_type)) {
        c.partial = true;
        parse_i64gt(L.tgi_at);
        L.after_header_fields = p;
        return L;
    }
    L.type_flags_at = p;
    if (!take_u32(bytes, p, c.type_flags)) {
        c.partial = true;
        parse_i64gt(L.tgi_at);
        L.after_header_fields = p;
        return L;
    }
    L.age_gender_at = p;
    if (!take_u32(bytes, p, c.age_gender)) {
        c.partial = true;
        parse_i64gt(L.tgi_at);
        L.after_header_fields = p;
        return L;
    }
    L.clothing_category_at = p;
    if (!take_u32(bytes, p, c.clothing_category)) {
        c.partial = true;
        parse_i64gt(L.tgi_at);
        L.after_header_fields = p;
        return L;
    }
    L.after_header_fields = p;
    decode_age_gender(c);
    parse_i64gt(L.tgi_at);
    return L;
}

void patch_u32(std::vector<std::byte>& out, std::size_t at, std::uint32_t v) {
    std::memcpy(out.data() + at, &v, 4);
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
    auto L = parse_casp_layout(bytes);
    if (!L) {
        return std::unexpected(L.error());
    }
    return L->value;
}

Result<std::vector<std::byte>> apply_casp(std::span<const std::byte> bytes, const CaspPatch& patch) {
    using namespace bin;
    auto Lr = parse_casp_layout(bytes);
    if (!Lr) {
        return std::unexpected(Lr.error());
    }
    const CaspLayout& L = *Lr;
    if (L.value.partial && L.after_header_fields <= L.name_at) {
        return std::unexpected(err(ErrorCode::corrupt, "casp partial; refuse edit"));
    }
    Casp c = L.value;

    if (patch.name) {
        c.name = *patch.name;
    }
    if (patch.sort_priority) {
        c.sort_priority = *patch.sort_priority;
    }
    if (patch.clothing_type) {
        c.clothing_type = *patch.clothing_type;
    }
    if (patch.type_flags) {
        c.type_flags = *patch.type_flags;
    }
    if (patch.clothing_category) {
        c.clothing_category = *patch.clothing_category;
    }
    if (patch.age_gender) {
        c.age_gender = *patch.age_gender;
        decode_age_gender(c);
    } else {
        if (patch.age_flags) {
            c.age_flags = *patch.age_flags;
        }
        if (patch.species) {
            c.species = *patch.species;
        }
        if (patch.gender_flags) {
            c.gender_flags = *patch.gender_flags;
        }
        if (patch.handedness) {
            c.handedness = *patch.handedness;
        }
        c.age_gender = pack_casp_age_gender(c.age_flags, c.species, c.gender_flags, c.handedness);
    }
    if (patch.tgis) {
        if (patch.tgis->size() > 255) {
            return std::unexpected(err(ErrorCode::cap_exceeded, "casp tgi count"));
        }
        c.tgis = *patch.tgis;
    }

    // Rebuild name + fixed header fields.
    std::vector<std::byte> fields;
    if (!put_7bit_utf16be(fields, c.name)) {
        return std::unexpected(err(ErrorCode::invalid_argument, "casp name encoding"));
    }
    put_f32(fields, c.sort_priority);
    // Preserve unused byte when present.
    if (L.unused_at < bytes.size() && L.unused_at + 1 <= L.clothing_type_at) {
        fields.push_back(bytes[L.unused_at]);
    } else {
        put_u8(fields, 0);
    }
    put_u32(fields, c.clothing_type);
    put_u32(fields, c.type_flags);
    put_u32(fields, c.age_gender);
    put_u32(fields, c.clothing_category);

    // Mid bytes: after header fields → TGI (unknown). Shift with fields_delta.
    std::vector<std::byte> mid;
    if (L.after_header_fields < L.tgi_at && L.tgi_at <= bytes.size()) {
        mid.insert(mid.end(), bytes.begin() + static_cast<std::ptrdiff_t>(L.after_header_fields),
                   bytes.begin() + static_cast<std::ptrdiff_t>(L.tgi_at));
    }

    std::vector<std::byte> tgi_block;
    if (patch.tgis || L.tgi_ok) {
        const auto& rows = patch.tgis ? *patch.tgis : c.tgis;
        put_u8(tgi_block, static_cast<std::uint8_t>(rows.size()));
        for (const auto& t : rows) {
            put_u64(tgi_block, t.instance);
            put_u32(tgi_block, t.group);
            put_u32(tgi_block, t.type);
        }
    } else if (L.tgi_at < bytes.size()) {
        // Keep raw TGI region if we couldn't parse it.
        const auto end = L.after_tgi ? L.after_tgi : bytes.size();
        tgi_block.insert(tgi_block.end(), bytes.begin() + static_cast<std::ptrdiff_t>(L.tgi_at),
                         bytes.begin() + static_cast<std::ptrdiff_t>(end));
    }

    std::vector<std::byte> trailing;
    if (L.tgi_ok && L.after_tgi < bytes.size()) {
        trailing.insert(trailing.end(), bytes.begin() + static_cast<std::ptrdiff_t>(L.after_tgi),
                        bytes.end());
    } else if (!L.tgi_ok && L.tgi_at >= bytes.size()) {
        // no tgi
    }

    std::vector<std::byte> out;
    out.reserve(bytes.size() + fields.size());
    out.insert(out.end(), bytes.begin(),
               bytes.begin() + static_cast<std::ptrdiff_t>(L.after_presets));
    out.insert(out.end(), fields.begin(), fields.end());
    out.insert(out.end(), mid.begin(), mid.end());
    out.insert(out.end(), tgi_block.begin(), tgi_block.end());
    out.insert(out.end(), trailing.begin(), trailing.end());

    // ref_off is relative to end of version+offset (=8). New tgi_at = after_presets + fields + mid.
    const auto new_tgi_at = L.after_presets + fields.size() + mid.size();
    const auto new_ref = static_cast<std::uint32_t>(new_tgi_at - 8);
    patch_u32(out, 4, new_ref);
    return out;
}

}  // namespace sxpe::resources
