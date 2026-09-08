#include "check.hpp"
#include <cstring>
#include "sxpe/resources/casp.hpp"
#include "sxpe/resources/clip.hpp"
#include "sxpe/resources/objd.hpp"
#include "sxpe/resources/rcol.hpp"
#include "sxpe/resources/types.hpp"

#include <cstring>
#include <string_view>
#include <vector>

namespace {

void wu8(std::vector<std::byte>& o, std::uint8_t v) { o.push_back(std::byte{v}); }
void wu16(std::vector<std::byte>& o, std::uint16_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 2);
}
void wu32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}
void wu64(std::vector<std::byte>& o, std::uint64_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 8);
}
void wf32(std::vector<std::byte>& o, float v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}
void w7str(std::vector<std::byte>& o, std::string_view s) {
    // single-byte 7-bit length
    wu8(o, static_cast<std::uint8_t>(s.size()));
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    o.insert(o.end(), p, p + s.size());
}
void w7utf16be(std::vector<std::byte>& o, std::string_view ascii) {
    wu8(o, static_cast<std::uint8_t>(ascii.size()));
    for (char c : ascii) {
        wu8(o, 0);
        wu8(o, static_cast<std::uint8_t>(c));
    }
}
void wcstr(std::vector<std::byte>& o, std::string_view s) {
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    o.insert(o.end(), p, p + s.size());
    wu8(o, 0);
}

}  // namespace

int main() {
    {
        // OBJD: version 0x16, empty materials, instance name, Common v0x0C
        std::vector<std::byte> body;
        wu32(body, 0);  // materials count
        w7str(body, "Inst");
        wu32(body, 0x0C);  // common version
        wu64(body, 0x1122334455667788ull);  // name guid
        wu64(body, 0x99AABBCCDDEEFF00ull);  // desc guid
        w7str(body, "Chair");
        w7str(body, "A chair");
        wf32(body, 125.5f);
        wf32(body, 1.0f);
        wf32(body, 0.0f);
        wu8(body, 1);
        wu64(body, 0xABCDEF0123456789ull);  // thumb
        // pad so tgi_off is valid
        const auto tgi_off = static_cast<std::uint32_t>(body.size());
        wu8(body, 0);

        std::vector<std::byte> bytes;
        wu32(bytes, 0x16);
        wu32(bytes, tgi_off);
        wu32(bytes, 1);
        bytes.insert(bytes.end(), body.begin(), body.end());

        auto p = sxpe::resources::parse_objd(bytes);
        CHECK(p.has_value());
        CHECK(p->version == 0x16);
        CHECK(p->common_version == 0x0C);
        CHECK(p->name_guid == 0x1122334455667788ull);
        CHECK(p->desc_guid == 0x99AABBCCDDEEFF00ull);
        CHECK(p->internal_name == "Chair");
        CHECK(p->price == 125.5f);
        CHECK(p->thumb_iid == 0xABCDEF0123456789ull);
        CHECK(p->instance_name == "Inst");
    }

    {
        auto bad = sxpe::resources::parse_objd(std::span<const std::byte>{});
        CHECK(!bad.has_value());
    }

    {
        // CASP: version, offset, 0 presets, name, fields + I64GT TGI table
        std::vector<std::byte> bytes;
        wu32(bytes, 0x12);
        const auto ref_at = bytes.size();
        wu32(bytes, 0);  // placeholder
        wu32(bytes, 0);  // presets
        w7utf16be(bytes, "Top_Shirt");
        wf32(bytes, 10.0f);
        wu8(bytes, 0);
        wu32(bytes, 5);  // Top
        wu32(bytes, 0);
        const std::uint32_t age_gender = 0x30u | (0x31u << 8);
        wu32(bytes, age_gender);
        wu32(bytes, 0x2);  // category Everyday bit
        for (int i = 0; i < 8; ++i) {
            wu8(bytes, 0);
        }
        const auto tgi_at = bytes.size();
        const auto ref_off = static_cast<std::uint32_t>(tgi_at - 8);
        std::memcpy(bytes.data() + ref_at, &ref_off, 4);
        wu8(bytes, 1);
        wu64(bytes, 0xABCDull);
        wu32(bytes, 0x11);
        wu32(bytes, 0x0333406Cu);

        auto p = sxpe::resources::parse_casp(bytes);
        CHECK(p.has_value());
        CHECK(p->clothing_type == 5);
        CHECK(p->name == "Top_Shirt");
        CHECK(p->age_flags == 0x30);
        CHECK(p->species == 1);
        CHECK(p->gender_flags == 3);
        CHECK(p->tgis.size() == 1);
        CHECK(p->tgis[0].type == 0x0333406Cu);
        CHECK(p->tgis[0].group == 0x11);
        CHECK(p->tgis[0].instance == 0xABCDull);
        auto ages = sxpe::resources::casp_age_names(p->age_flags);
        CHECK(ages.size() == 2);
        CHECK(std::string(sxpe::resources::casp_clothing_type_name(5)) == "Top");
        CHECK(std::string(sxpe::resources::casp_species_name(1)) == "Human");
    }

    {
        // OBJD apply: change price + internal name length; preserve trailing marker bytes
        std::vector<std::byte> body;
        wu32(body, 0);  // materials
        w7str(body, "Inst");
        wu32(body, 0x0C);
        wu64(body, 0x1111ull);
        wu64(body, 0x2222ull);
        w7str(body, "A");
        w7str(body, "B");
        wf32(body, 10.0f);
        wf32(body, 1.0f);
        wf32(body, 0.0f);
        wu8(body, 1);
        wu64(body, 0x3333ull);
        // unknown mid
        wu32(body, 0xDEADBEEFu);
        const auto tgi_off = static_cast<std::uint32_t>(body.size());
        wu8(body, 0xAB);  // tgi stub
        wu8(body, 0xCD);  // trailing after tgi

        std::vector<std::byte> bytes;
        wu32(bytes, 0x16);
        wu32(bytes, tgi_off);
        wu32(bytes, 1);
        bytes.insert(bytes.end(), body.begin(), body.end());

        sxpe::resources::ObjdPatch patch;
        patch.price = 99.5f;
        patch.internal_name = std::string("ChairX");
        patch.name_guid = 0xAAAAull;
        auto out = sxpe::resources::apply_objd(bytes, patch);
        CHECK(out.has_value());
        auto p2 = sxpe::resources::parse_objd(*out);
        CHECK(p2.has_value());
        CHECK(p2->price == 99.5f);
        CHECK(p2->internal_name == "ChairX");
        CHECK(p2->name_guid == 0xAAAAull);
        CHECK(p2->desc_guid == 0x2222ull);
        CHECK(p2->instance_name == "Inst");
        // trailing marker after tgi stub still present
        CHECK(out->size() >= 2);
        CHECK(static_cast<unsigned>(*out->rbegin()) == 0xCD);
        CHECK(static_cast<unsigned>(*(out->rbegin() + 1)) == 0xAB);
        // DEADBEEF mid preserved: search for it
        bool found = false;
        for (std::size_t i = 0; i + 4 <= out->size(); ++i) {
            std::uint32_t v = 0;
            std::memcpy(&v, out->data() + i, 4);
            if (v == 0xDEADBEEFu) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }

    {
        // CASP apply: rename + clothing + TGI replace; preserve mid padding
        std::vector<std::byte> bytes;
        wu32(bytes, 0x12);
        const auto ref_at = bytes.size();
        wu32(bytes, 0);
        wu32(bytes, 0);
        w7utf16be(bytes, "Top_Shirt");
        wf32(bytes, 10.0f);
        wu8(bytes, 0x7E);  // unused
        wu32(bytes, 5);
        wu32(bytes, 0);
        const std::uint32_t age_gender = 0x30u | (0x31u << 8);
        wu32(bytes, age_gender);
        wu32(bytes, 0x2);
        for (int i = 0; i < 8; ++i) {
            wu8(bytes, static_cast<std::uint8_t>(0xA0 + i));
        }
        const auto tgi_at = bytes.size();
        const auto ref_off = static_cast<std::uint32_t>(tgi_at - 8);
        std::memcpy(bytes.data() + ref_at, &ref_off, 4);
        wu8(bytes, 1);
        wu64(bytes, 0xABCDull);
        wu32(bytes, 0x11);
        wu32(bytes, 0x0333406Cu);
        wu8(bytes, 0xEE);  // trailing

        sxpe::resources::CaspPatch patch;
        patch.name = std::string("Top_New");
        patch.clothing_type = 6u;
        patch.age_flags = 0x20;
        patch.species = 1;
        patch.gender_flags = 2;
        std::vector<sxpe::games::sims3::Tgi> rows;
        sxpe::games::sims3::Tgi t{};
        t.type = 0x00B2D882u;
        t.group = 0x22;
        t.instance = 0x1234ull;
        rows.push_back(t);
        patch.tgis = rows;
        auto out = sxpe::resources::apply_casp(bytes, patch);
        CHECK(out.has_value());
        auto p2 = sxpe::resources::parse_casp(*out);
        CHECK(p2.has_value());
        CHECK(p2->name == "Top_New");
        CHECK(p2->clothing_type == 6);
        CHECK(p2->age_flags == 0x20);
        CHECK(p2->species == 1);
        CHECK(p2->gender_flags == 2);
        CHECK(p2->tgis.size() == 1);
        CHECK(p2->tgis[0].type == 0x00B2D882u);
        CHECK(static_cast<unsigned>(out->back()) == 0xEE);
        // unused byte preserved
        bool unused_ok = false;
        for (std::size_t i = 0; i < out->size(); ++i) {
            if (static_cast<unsigned>((*out)[i]) == 0x7E) {
                unused_ok = true;
                break;
            }
        }
        CHECK(unused_ok);
    }

    {
        // CLIP synthetic
        std::vector<std::byte> s3;
        // magic _S3Clip_
        const char mag[] = "_S3Clip_";
        s3.insert(s3.end(), reinterpret_cast<const std::byte*>(mag),
                  reinterpret_cast<const std::byte*>(mag) + 8);
        wu32(s3, 2);     // version
        wu32(s3, 0);     // blank
        wf32(s3, 1.0f / 30.0f);
        wu16(s3, 60);    // frames
        wu16(s3, 0);
        wu32(s3, 2);     // track count
        wu32(s3, 0);     // indexed floats
        const auto rules_off_field = s3.size();
        wu32(s3, 0);  // offset1 placeholder
        wu32(s3, 0);  // offset2
        const auto anim_off_field = s3.size();
        wu32(s3, 0);
        const auto src_off_field = s3.size();
        wu32(s3, 0);

        // names
        auto patch_u32 = [&](std::size_t at, std::uint32_t v) {
            std::memcpy(s3.data() + at, &v, 4);
        };
        const auto anim_at = s3.size();
        wcstr(s3, "a_walk");
        const auto src_at = s3.size();
        wcstr(s3, "walk.mb");
        const auto rules_at = s3.size();
        for (int i = 0; i < 2; ++i) {
            wu32(s3, 0);           // frame data off
            wu32(s3, 0x11110000u + i);  // hash
            wf32(s3, 0.0f);
            wf32(s3, 1.0f);
            wu16(s3, 1);
            wu16(s3, 0x112);
        }
        patch_u32(rules_off_field, static_cast<std::uint32_t>(rules_at));
        patch_u32(anim_off_field, static_cast<std::uint32_t>(anim_at));
        patch_u32(src_off_field, static_cast<std::uint32_t>(src_at));

        std::vector<std::byte> bytes;
        wu32(bytes, sxpe::resources::kClip);
        wu32(bytes, 0);  // linked
        wu32(bytes, static_cast<std::uint32_t>(s3.size()));
        // clip_off relative to field at 12
        wu32(bytes, 0);  // will patch: section starts after 16 blank trailing of header
        wu32(bytes, 0);  // slot
        wu32(bytes, 0);  // actor
        wu32(bytes, 0);  // event
        wu32(bytes, 0);
        wu32(bytes, 1);
        wu32(bytes, 0);  // end
        for (int i = 0; i < 16; ++i) {
            wu8(bytes, 0);
        }
        // clip_off field is at offset 12; absolute = 12 + clip_off = bytes.size() before append
        // so clip_off = current_size - 12 ... but we already wrote past. Rebuild header cleanly.
        bytes.clear();
        std::vector<std::byte> header;
        wu32(header, sxpe::resources::kClip);
        wu32(header, 0);
        wu32(header, static_cast<std::uint32_t>(s3.size()));
        const std::uint32_t clip_off = 36;  // 12 + 36 = 48 = end of 48-byte header+pad? 
        // Header fields: 10 dwords = 40, plus 16 blank = 56.
        // clip_off at byte 12; want 12+clip_off = 56 => clip_off = 44
        wu32(header, 44);
        wu32(header, 0);
        wu32(header, 0);
        wu32(header, 0);
        wu32(header, 0);
        wu32(header, 1);
        wu32(header, 0);
        for (int i = 0; i < 16; ++i) {
            wu8(header, 0);
        }
        CHECK(header.size() == 56);
        bytes = header;
        bytes.insert(bytes.end(), s3.begin(), s3.end());

        auto p = sxpe::resources::parse_clip(bytes);
        CHECK(p.has_value());
        CHECK(p->frame_count == 60);
        CHECK(p->track_count == 2);
        CHECK(p->anim_name == "a_walk");
        CHECK(p->source_file == "walk.mb");
        CHECK(p->track_hashes.size() == 2);
        CHECK(p->duration_seconds > 1.9f && p->duration_seconds < 2.1f);

        sxpe::resources::ClipPatch patch;
        patch.anim_name = "t_walk_long_name";
        patch.source_file = "new_src.mb";
        patch.actor_name = "x";
        patch.track_hashes.push_back({0, 0xDEADBEEFu});
        auto edited = sxpe::resources::apply_clip(bytes, patch);
        CHECK(edited.has_value());
        auto p2 = sxpe::resources::parse_clip(*edited);
        CHECK(p2.has_value());
        CHECK(p2->anim_name == "t_walk_long_name");
        CHECK(p2->source_file == "new_src.mb");
        CHECK(p2->actor_name == "x");
        CHECK(p2->track_hashes.size() >= 1);
        CHECK(p2->track_hashes[0] == 0xDEADBEEFu);
        CHECK(p2->track_hashes[1] == 0x11110001u);
        CHECK(p2->frame_count == 60);
    }

    {
        // Bare GEOM chunk
        std::vector<std::byte> g;
        g.insert(g.end(), {std::byte{'G'}, std::byte{'E'}, std::byte{'O'}, std::byte{'M'}});
        wu32(g, 5);
        wu32(g, 0);  // tgi off
        wu32(g, 0);  // tgi size
        wu32(g, 0);  // embedded
        wu32(g, 0);  // merge
        wu32(g, 0);  // sort
        wu32(g, 3);  // verts
        wu32(g, 1);  // format count
        wu32(g, 1);  // position
        wu32(g, 1);  // subtype float
        wu8(g, 12);  // bytes
        for (int v = 0; v < 3; ++v) {
            wf32(g, 0);
            wf32(g, 0);
            wf32(g, 0);
        }
        wu32(g, 1);  // item count
        wu8(g, 2);   // bytes per face point
        wu32(g, 6);  // 2 faces * 3
        for (int i = 0; i < 6; ++i) {
            wu16(g, static_cast<std::uint16_t>(i % 3));
        }
        wu32(g, 0);  // skin index
        wu32(g, 0);  // bone count

        auto p = sxpe::resources::parse_rcol_summary(g);
        CHECK(p.has_value());
        CHECK(p->chunks.size() == 1);
        CHECK(p->chunks[0].tag == "GEOM");
        CHECK(p->total_vertices == 3);
        CHECK(p->total_faces == 2);
    }

    {
        // Minimal RCOL with one MLOD-tagged internal chunk (empty groups)
        std::vector<std::byte> chunk;
        chunk.insert(chunk.end(), {std::byte{'M'}, std::byte{'L'}, std::byte{'O'}, std::byte{'D'}});
        wu32(chunk, 0x201);
        wu32(chunk, 0);  // groups

        std::vector<std::byte> bytes;
        wu32(bytes, 3);  // rcol version
        wu32(bytes, 1);  // public
        wu32(bytes, 0);  // index3
        wu32(bytes, 0);  // external
        wu32(bytes, 1);  // internal
        wu64(bytes, 1);
        wu32(bytes, sxpe::resources::kMlod);
        wu32(bytes, 0);
        // locs later — need absolute positions. Build then patch.
        const auto loc_at = bytes.size();
        wu32(bytes, 0);  // pos placeholder
        wu32(bytes, static_cast<std::uint32_t>(chunk.size()));
        const auto pos = static_cast<std::uint32_t>(bytes.size());
        std::memcpy(bytes.data() + loc_at, &pos, 4);
        bytes.insert(bytes.end(), chunk.begin(), chunk.end());

        auto p = sxpe::resources::parse_rcol_summary(bytes);
        CHECK(p.has_value());
        CHECK(p->internal_count == 1);
        CHECK(p->chunks.size() == 1);
        CHECK(p->chunks[0].tag == "MLOD");
    }


    {
        // Synthetic RCOL: external _IMG + MATD chunk with DiffuseMap -> delayed external ref
        std::vector<std::byte> matd;
        matd.insert(matd.end(), {std::byte{'M'}, std::byte{'A'}, std::byte{'T'}, std::byte{'D'}});
        wu32(matd, 0x103);
        wu32(matd, 0);
        wu32(matd, 0xB9105A6Du);  // Phong
        wu32(matd, 0);
        wu32(matd, 0);
        wu32(matd, 0);
        const auto mtnf_at = matd.size();
        matd.insert(matd.end(), {std::byte{'M'}, std::byte{'T'}, std::byte{'N'}, std::byte{'F'}});
        wu32(matd, 0);
        wu32(matd, 4);
        wu32(matd, 1);
        wu32(matd, 0x6CC0FD85u);  // DiffuseMap
        wu32(matd, 4);
        wu32(matd, 4);
        wu32(matd, 32);
        while (matd.size() < mtnf_at + 32) {
            wu8(matd, 0);
        }
        CHECK(matd.size() == mtnf_at + 32);
        wu32(matd, 0x30000001u);
        wu32(matd, 0);
        wu32(matd, 0);
        wu32(matd, 0);

        std::vector<std::byte> chunk_a;
        chunk_a.insert(chunk_a.end(), {std::byte{'M'}, std::byte{'O'}, std::byte{'D'}, std::byte{'L'}});
        for (int i = 0; i < 12; ++i) {
            wu8(chunk_a, static_cast<std::uint8_t>(0xA0 + i));
        }

        std::vector<std::byte> bytes;
        wu32(bytes, 3);
        wu32(bytes, 1);
        wu32(bytes, 0);
        wu32(bytes, 1);
        wu32(bytes, 2);
        wu64(bytes, 0x1111ull);
        wu32(bytes, sxpe::resources::kModl);
        wu32(bytes, 0);
        wu64(bytes, 0x2222ull);
        wu32(bytes, sxpe::resources::kMatd);
        wu32(bytes, 0);
        wu64(bytes, 0xABCDEF0123456789ull);
        wu32(bytes, sxpe::resources::kImg);
        wu32(bytes, 0x12);
        const auto loc_at = bytes.size();
        wu32(bytes, 0);
        wu32(bytes, static_cast<std::uint32_t>(chunk_a.size()));
        wu32(bytes, 0);
        wu32(bytes, static_cast<std::uint32_t>(matd.size()));
        const auto pos0 = static_cast<std::uint32_t>(bytes.size());
        bytes.insert(bytes.end(), chunk_a.begin(), chunk_a.end());
        const auto pos1 = static_cast<std::uint32_t>(bytes.size());
        bytes.insert(bytes.end(), matd.begin(), matd.end());
        std::memcpy(bytes.data() + loc_at, &pos0, 4);
        std::memcpy(bytes.data() + loc_at + 8, &pos1, 4);

        auto p = sxpe::resources::parse_rcol_summary(bytes);
        CHECK(p.has_value());
        CHECK(p->internal_count == 2);
        CHECK(p->external_count == 1);
        CHECK(p->external_tgis.size() == 1);
        CHECK(p->external_tgis[0].type == sxpe::resources::kImg);
        CHECK(p->external_tgis[0].instance == 0xABCDEF0123456789ull);
        CHECK(p->chunks.size() == 2);
        CHECK(p->chunks[0].tag == "MODL");
        CHECK(p->chunks[1].tag == "MATD");
        CHECK(p->chunks[1].has_matd);
        CHECK(p->chunks[1].shader_hash == 0xB9105A6Du);
        CHECK(p->chunks[1].shader_name == "Phong");
        CHECK(p->chunks[1].textures.size() == 1);
        CHECK(p->chunks[1].textures[0].param_name == "DiffuseMap");
        CHECK(p->chunks[1].textures[0].resolved);
        CHECK(p->chunks[1].textures[0].tgi.type == sxpe::resources::kImg);
        CHECK(p->chunks[1].textures[0].tgi.instance == 0xABCDEF0123456789ull);
        CHECK(p->textures.size() == 1);

        std::vector<std::byte> neu;
        neu.insert(neu.end(), {std::byte{'M'}, std::byte{'O'}, std::byte{'D'}, std::byte{'L'}});
        wu32(neu, 0xDEADBEEFu);
        auto replaced = sxpe::resources::replace_rcol_chunk(bytes, 0, neu);
        CHECK(replaced.has_value());
        auto p2 = sxpe::resources::parse_rcol_summary(*replaced);
        CHECK(p2.has_value());
        CHECK(p2->chunks.size() == 2);
        CHECK(p2->chunks[0].size == neu.size());
        CHECK(p2->chunks[1].tag == "MATD");
        CHECK(p2->chunks[1].shader_name == "Phong");
        CHECK(p2->textures.size() == 1);
        CHECK(p2->textures[0].resolved);

        auto extracted = sxpe::resources::extract_rcol_chunk(*replaced, 0);
        CHECK(extracted.has_value());
        CHECK(extracted->size() == neu.size());
        CHECK(std::memcmp(extracted->data(), neu.data(), neu.size()) == 0);

        auto old0 = sxpe::resources::extract_rcol_chunk(bytes, 0);
        CHECK(old0.has_value());
        auto back = sxpe::resources::replace_rcol_chunk(*replaced, 0, *old0);
        CHECK(back.has_value());
        auto p3 = sxpe::resources::parse_rcol_summary(*back);
        CHECK(p3.has_value());
        CHECK(p3->chunks[0].size == chunk_a.size());
        CHECK(p3->textures[0].tgi.instance == 0xABCDEF0123456789ull);

        auto bad = sxpe::resources::replace_rcol_chunk(bytes, 9, neu);
        CHECK(!bad.has_value());
    }

    {
        std::vector<std::byte> g;
        g.insert(g.end(), {std::byte{'G'}, std::byte{'E'}, std::byte{'O'}, std::byte{'M'}});
        wu32(g, 1);
        std::vector<std::byte> neu{std::byte{'G'}, std::byte{'E'}, std::byte{'O'}, std::byte{'M'},
                                   std::byte{0x11}};
        auto out = sxpe::resources::replace_rcol_chunk(g, 0, neu);
        CHECK(out.has_value());
        CHECK(out->size() == 5);
        auto bad = sxpe::resources::replace_rcol_chunk(g, 1, neu);
        CHECK(!bad.has_value());
    }

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
