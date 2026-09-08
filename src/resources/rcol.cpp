#include "sxpe/resources/rcol.hpp"

#include "sxpe/resources/binary.hpp"
#include "sxpe/resources/types.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace sxpe::resources {
namespace {

const char* chunk_tag(std::uint32_t type) {
    switch (type) {
        case kModl:
            return "MODL";
        case kMlod:
            return "MLOD";
        case kGeom:
            return "GEOM";
        case 0x01D0E6FB:
        case 0x0229684B:
            return "VBUF";
        case 0x01D0E70F:
        case 0x0229684F:
            return "IBUF";
        case 0x01D0E723:
            return "VRTF";
        case kMatd:
            return "MATD";
        case 0x01D0E76B:
            return "SKIN";
        case 0x02019972:
            return "MTST";
        case kVpxy:
            return "VPXY";
        default:
            return nullptr;
    }
}

bool fourcc_eq(std::span<const std::byte> s, std::size_t o, const char* tag) {
    if (o + 4 > s.size()) {
        return false;
    }
    return std::memcmp(s.data() + o, tag, 4) == 0;
}

struct NamedHash {
    std::uint32_t hash;
    const char* name;
};

// FNV-1 32 (lowercase) of SimsWiki Shaders names — community list, not exhaustive.
constexpr NamedHash kShaders[] = {
    {0x5AF16731u, "additive"},
    {0x6AAD2AD5u, "BasinWater"},
    {0x6864A45Eu, "Blueprint"},
    {0x7B036C01u, "BuildingWindow"},
    {0x94B9A835u, "CASRoom"},
    {0xA4172F62u, "Counters"},
    {0xC09C7582u, "DropShadow"},
    {0x67107FE8u, "Fence"},
    {0xA68D9E29u, "FlatMirror"},
    {0xBC84D000u, "Floors"},
    {0x4549E22Eu, "Foliage"},
    {0x14FA335Eu, "FullBright"},
    {0xA063C1D0u, "Gemstones"},
    {0x52986C62u, "GlassForFences"},
    {0x492ECA7Cu, "GlassForObjects"},
    {0x849CF021u, "GlassForObjectsTranslucent"},
    {0x81DD204Du, "GlassForPortals"},
    {0x265FFAA1u, "GlassForRabbitHoles"},
    {0x277CF8EBu, "ImpostorWater"},
    {0x0CB82EB8u, "Instanced"},
    {0x8A60B969u, "Landmark"},
    {0x68601DE3u, "LotImposter"},
    {0x4D26BEC0u, "OutdoorProp"},
    {0xAA495821u, "Painting"},
    {0x460E93F4u, "ParticleAnim"},
    {0xFF5E6908u, "ParticleJet"},
    {0xB9105A6Du, "Phong"},
    {0xFC5FC212u, "phong-alpha"},
    {0xDEF16564u, "Plumbob"},
    {0x213D6300u, "PreviewWallsAndFloors"},
    {0x8D346BBCu, "RabbitHoleHighDetail"},
    {0xAEDE7105u, "RabbitHoleMediumDetail"},
    {0x7BD05F63u, "Roof"},
    {0x2A72B9A1u, "Rug"},
    {0xE5D98507u, "SculptureIce"},
    {0x21FE207Du, "ShadowMap"},
    {0x9D9DA161u, "SimEyelashes"},
    {0xCF8A70B4u, "SimEyes"},
    {0x5EDA9CDEu, "simglass"},
    {0x84FD7152u, "SimHair"},
    {0x548394B9u, "SimSkin"},
    {0x47C6638Cu, "SolidPhong"},
    {0x4CE2F497u, "Stairs"},
    {0x70FDE012u, "StandingWater"},
    {0x0B272CC5u, "Subtractive"},
    {0x3939E094u, "trampoline"},
};

// Common texture-typed shader params (SimsWiki Shaders/Params).
constexpr NamedHash kParams[] = {
    {0x6CC0FD85u, "DiffuseMap"},
    {0xAD528A60u, "SpecularMap"},
    {0x6E56548Au, "NormalMap"},
    {0xC3FAAC4Fu, "AlphaMap"},
    {0xB01CBA60u, "AmbientOcclusionMap"},
    {0xF303D152u, "EmissionMap"},
    {0xCD869A45u, "MultiplyMap"},
    {0x9205DAA8u, "DetailMap"},
    {0x6E067554u, "SelfIlluminationMap"},
    {0x48372E62u, "DirtOverlay"},
    {0xF3F22AC4u, "RevealMap"},
    {0xE19FD579u, "NoiseMap"},
    {0x22AD8507u, "DropShadowAtlas"},
    {0x581835D6u, "ColorRamp"},
    {0x52CE211Bu, "JetTexture"},
    {0x1D90C086u, "SparkleCube"},
    {0x84F6E0FBu, "HaloRamp"},
    {0x4DC0C8BCu, "OverlayTexture"},
    {0xD652FADEu, "SpecCompositeTexture"},
    {0xBDCF71C5u, "imposterTexture"},
    {0x15C9D298u, "imposterTextureAOandSI"},
    {0xBF3FB9FAu, "imposterTextureWater"},
    {0x56E1C6B2u, "ImpostorDetailTexture"},
    {0xE7CA9166u, "RoomLightMap"},
};

std::string_view lookup_name(std::span<const NamedHash> table, std::uint32_t hash) {
    for (const auto& e : table) {
        if (e.hash == hash) {
            return e.name;
        }
    }
    return {};
}

bool resolve_rcol_ref(std::uint32_t ref, const RcolSummary& sum, sxpe::games::sims3::Tgi& out) {
    if (ref == 0) {
        return false;
    }
    const std::uint32_t kind = ref >> 28;
    const std::uint32_t idx1 = ref & 0x0FFFFFFFu;
    if (idx1 == 0) {
        return false;
    }
    const std::size_t i0 = static_cast<std::size_t>(idx1 - 1);
    if (kind == 0x0u || kind == 0x1u) {
        std::size_t i = i0;
        if (kind == 0x1u) {
            i += static_cast<std::size_t>(sum.public_chunks);
        }
        if (i >= sum.internal_tgis.size()) {
            return false;
        }
        out = sum.internal_tgis[i];
        return true;
    }
    if (kind == 0x3u) {
        if (i0 >= sum.external_tgis.size()) {
            return false;
        }
        out = sum.external_tgis[i0];
        return true;
    }
    return false;
}

void parse_geom_chunk(std::span<const std::byte> chunk, RcolChunk& out) {
    using namespace bin;
    std::size_t p = 0;
    if (fourcc_eq(chunk, 0, "GEOM")) {
        p = 4;
        std::uint32_t ver = 0;
        std::uint32_t tgi_off = 0;
        std::uint32_t tgi_size = 0;
        if (!take_u32(chunk, p, ver) || !take_u32(chunk, p, tgi_off) ||
            !take_u32(chunk, p, tgi_size)) {
            return;
        }
        std::uint32_t embedded = 0;
        if (!take_u32(chunk, p, embedded)) {
            return;
        }
        if (embedded != 0) {
            std::uint32_t csize = 0;
            if (!take_u32(chunk, p, csize) || p + csize > chunk.size()) {
                return;
            }
            p += csize;
        }
        std::uint32_t merge = 0;
        std::uint32_t sort = 0;
        if (!take_u32(chunk, p, merge) || !take_u32(chunk, p, sort)) {
            return;
        }
        std::uint32_t nverts = 0;
        std::uint32_t fcount = 0;
        if (!take_u32(chunk, p, nverts) || !take_u32(chunk, p, fcount)) {
            return;
        }
        out.vertex_count = nverts;
        std::size_t stride = 0;
        for (std::uint32_t i = 0; i < fcount; ++i) {
            std::uint32_t dt = 0;
            std::uint32_t st = 0;
            std::uint8_t bpe = 0;
            if (!take_u32(chunk, p, dt) || !take_u32(chunk, p, st) || !take_u8(chunk, p, bpe)) {
                out.has_mesh_counts = true;
                return;
            }
            stride += bpe;
        }
        if (stride > 0) {
            const std::size_t vbytes = static_cast<std::size_t>(nverts) * stride;
            if (p + vbytes > chunk.size()) {
                out.has_mesh_counts = true;
                return;
            }
            p += vbytes;
        }
        std::uint32_t item_count = 0;
        if (!take_u32(chunk, p, item_count) || item_count == 0 || item_count > 16) {
            out.has_mesh_counts = true;
            return;
        }
        std::uint8_t bytes_per = 0;
        if (!take_u8(chunk, p, bytes_per) || bytes_per == 0) {
            out.has_mesh_counts = true;
            return;
        }
        for (std::uint32_t i = 1; i < item_count; ++i) {
            std::uint8_t b = 0;
            if (!take_u8(chunk, p, b)) {
                out.has_mesh_counts = true;
                return;
            }
        }
        std::uint32_t nfaces_pts = 0;
        if (!take_u32(chunk, p, nfaces_pts)) {
            out.has_mesh_counts = true;
            return;
        }
        out.face_count = nfaces_pts / 3;
        out.has_mesh_counts = true;
        return;
    }
}

void parse_mlod_chunk(std::span<const std::byte> chunk, RcolChunk& out) {
    using namespace bin;
    std::size_t p = 0;
    if (fourcc_eq(chunk, 0, "MLOD")) {
        p = 4;
    }
    std::uint32_t ver = 0;
    std::uint32_t groups = 0;
    if (!take_u32(chunk, p, ver) || !take_u32(chunk, p, groups)) {
        return;
    }
    if (groups > 4096) {
        return;
    }
    out.group_count = groups;
    std::uint32_t verts = 0;
    std::uint32_t prims = 0;
    for (std::uint32_t g = 0; g < groups; ++g) {
        std::uint32_t subset_bytes = 0;
        if (!take_u32(chunk, p, subset_bytes)) {
            out.has_mesh_counts = true;
            return;
        }
        const std::size_t start = p;
        if (p + 48 > chunk.size()) {
            out.has_mesh_counts = true;
            return;
        }
        const auto vc = ru32(chunk, p + 40);
        const auto pc = ru32(chunk, p + 44);
        verts += vc;
        prims += pc;
        if (subset_bytes < 4 || start + subset_bytes > chunk.size()) {
            out.has_mesh_counts = true;
            return;
        }
        p = start + subset_bytes;
    }
    out.vertex_count = verts;
    out.face_count = prims;
    out.has_mesh_counts = true;
}

void parse_matd_chunk(std::span<const std::byte> chunk, RcolChunk& out, const RcolSummary& sum) {
    using namespace bin;
    std::size_t p = 0;
    if (!fourcc_eq(chunk, 0, "MATD")) {
        return;
    }
    p = 4;
    std::uint32_t ver = 0;
    std::uint32_t mat_hash = 0;
    std::uint32_t sh_hash = 0;
    std::uint32_t length = 0;
    if (!take_u32(chunk, p, ver) || !take_u32(chunk, p, mat_hash) || !take_u32(chunk, p, sh_hash) ||
        !take_u32(chunk, p, length)) {
        return;
    }
    out.has_matd = true;
    out.matd_version = ver;
    out.material_name_hash = mat_hash;
    out.shader_hash = sh_hash;
    out.shader_name = std::string(rcol_shader_name(sh_hash));

    const std::size_t block_start = p;
    if (ver >= 0x103) {
        std::uint32_t video = 0;
        std::uint32_t paint = 0;
        if (!take_u32(chunk, p, video) || !take_u32(chunk, p, paint)) {
            return;
        }
    }
    if (!fourcc_eq(chunk, p, "MTNF") && !fourcc_eq(chunk, p, "MTRL")) {
        return;
    }
    const std::size_t mtnf_at = p;
    p += 4;  // fourcc
    std::uint32_t zero = 0;
    if (!take_u32(chunk, p, zero)) {
        return;
    }
    if (ver < 0x103) {
        std::uint16_t a = 0;
        std::uint16_t b = 0;
        if (!take_u16(chunk, p, a) || !take_u16(chunk, p, b)) {
            return;
        }
    } else {
        std::uint32_t datasize = 0;
        if (!take_u32(chunk, p, datasize)) {
            return;
        }
    }
    std::uint32_t count = 0;
    if (!take_u32(chunk, p, count) || count > 4096) {
        return;
    }
    struct Parm {
        std::uint32_t name_hash;
        std::uint32_t type_code;
        std::uint32_t size_dwords;
        std::uint32_t offset;
    };
    std::vector<Parm> parms;
    parms.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        Parm pr{};
        if (!take_u32(chunk, p, pr.name_hash) || !take_u32(chunk, p, pr.type_code) ||
            !take_u32(chunk, p, pr.size_dwords) || !take_u32(chunk, p, pr.offset)) {
            return;
        }
        parms.push_back(pr);
    }
    for (const auto& pr : parms) {
        if (pr.type_code != 4) {
            continue;
        }
        const std::size_t data_at = mtnf_at + static_cast<std::size_t>(pr.offset);
        const std::size_t nbytes = static_cast<std::size_t>(pr.size_dwords) * 4;
        if (data_at + nbytes > chunk.size() || nbytes < 4) {
            continue;
        }
        RcolTextureRef tex;
        tex.param_hash = pr.name_hash;
        tex.param_name = std::string(rcol_param_name(pr.name_hash));
        if (pr.size_dwords == 4) {
            tex.rcol_ref = ru32(chunk, data_at);
            sxpe::games::sims3::Tgi t{};
            if (resolve_rcol_ref(tex.rcol_ref, sum, t)) {
                tex.tgi = t;
                tex.resolved = true;
            }
        } else if (pr.size_dwords >= 5) {
            // TextureKey ITG: instance u64, type u32, group u32 (padded to 20).
            tex.tgi.instance = ru64(chunk, data_at);
            tex.tgi.type = ru32(chunk, data_at + 8);
            tex.tgi.group = ru32(chunk, data_at + 12);
            tex.resolved = tex.tgi.type != 0 || tex.tgi.group != 0 || tex.tgi.instance != 0;
        }
        out.textures.push_back(std::move(tex));
    }
    (void)block_start;
    (void)length;
}

struct ParsedRcol {
    std::uint32_t version{0};
    std::uint32_t public_chunks{0};
    std::uint32_t index3{0};
    std::uint32_t external_count{0};
    std::uint32_t internal_count{0};
    std::vector<sxpe::games::sims3::Tgi> internals;
    std::vector<sxpe::games::sims3::Tgi> externals;
    struct Loc {
        std::uint32_t pos;
        std::uint32_t size;
    };
    std::vector<Loc> locs;
    std::size_t header_end{0};  // offset of first byte after loc table
};

Result<ParsedRcol> parse_rcol_table(std::span<const std::byte> bytes) {
    using namespace bin;
    ParsedRcol t;
    if (bytes.size() < 20) {
        return std::unexpected(err(ErrorCode::corrupt, "rcol too short"));
    }
    std::size_t p = 0;
    if (!take_u32(bytes, p, t.version) || !take_u32(bytes, p, t.public_chunks) ||
        !take_u32(bytes, p, t.index3) || !take_u32(bytes, p, t.external_count) ||
        !take_u32(bytes, p, t.internal_count)) {
        return std::unexpected(err(ErrorCode::corrupt, "rcol header"));
    }
    constexpr std::uint32_t kMaxChunks = 4096;
    if (t.internal_count > kMaxChunks || t.external_count > kMaxChunks) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "rcol chunks"));
    }
    t.internals.reserve(t.internal_count);
    for (std::uint32_t i = 0; i < t.internal_count; ++i) {
        sxpe::games::sims3::Tgi r{};
        if (!take_u64(bytes, p, r.instance) || !take_u32(bytes, p, r.type) ||
            !take_u32(bytes, p, r.group)) {
            return std::unexpected(err(ErrorCode::corrupt, "rcol internal tgi"));
        }
        t.internals.push_back(r);
    }
    t.externals.reserve(t.external_count);
    for (std::uint32_t i = 0; i < t.external_count; ++i) {
        sxpe::games::sims3::Tgi r{};
        if (!take_u64(bytes, p, r.instance) || !take_u32(bytes, p, r.type) ||
            !take_u32(bytes, p, r.group)) {
            return std::unexpected(err(ErrorCode::corrupt, "rcol external tgi"));
        }
        t.externals.push_back(r);
    }
    t.locs.reserve(t.internal_count);
    for (std::uint32_t i = 0; i < t.internal_count; ++i) {
        ParsedRcol::Loc l{};
        if (!take_u32(bytes, p, l.pos) || !take_u32(bytes, p, l.size)) {
            return std::unexpected(err(ErrorCode::corrupt, "rcol locs"));
        }
        t.locs.push_back(l);
    }
    t.header_end = p;
    return t;
}

}  // namespace

std::string_view rcol_shader_name(std::uint32_t hash) {
    return lookup_name(kShaders, hash);
}

std::string_view rcol_param_name(std::uint32_t hash) {
    return lookup_name(kParams, hash);
}

Result<RcolSummary> parse_rcol_summary(std::span<const std::byte> bytes) {
    RcolSummary sum;
    if (bytes.size() < 20) {
        if (fourcc_eq(bytes, 0, "GEOM")) {
            RcolChunk ch;
            ch.type = kGeom;
            ch.tag = "GEOM";
            ch.size = static_cast<std::uint32_t>(bytes.size());
            parse_geom_chunk(bytes, ch);
            sum.chunks.push_back(ch);
            sum.total_vertices = ch.vertex_count;
            sum.total_faces = ch.face_count;
            sum.internal_count = 1;
            return sum;
        }
        if (fourcc_eq(bytes, 0, "MATD")) {
            RcolChunk ch;
            ch.type = kMatd;
            ch.tag = "MATD";
            ch.size = static_cast<std::uint32_t>(bytes.size());
            parse_matd_chunk(bytes, ch, sum);
            sum.chunks.push_back(ch);
            sum.textures = ch.textures;
            sum.internal_count = 1;
            return sum;
        }
        return std::unexpected(err(ErrorCode::corrupt, "rcol too short"));
    }

    auto table = parse_rcol_table(bytes);
    if (!table) {
        // May still be a bare GEOM/MATD longer than 20 bytes.
        if (fourcc_eq(bytes, 0, "GEOM")) {
            RcolChunk ch;
            ch.type = kGeom;
            ch.tag = "GEOM";
            ch.size = static_cast<std::uint32_t>(bytes.size());
            parse_geom_chunk(bytes, ch);
            sum.chunks.push_back(ch);
            sum.total_vertices = ch.vertex_count;
            sum.total_faces = ch.face_count;
            sum.internal_count = 1;
            return sum;
        }
        return std::unexpected(table.error());
    }

    sum.version = table->version;
    sum.public_chunks = table->public_chunks;
    sum.external_count = table->external_count;
    sum.internal_count = table->internal_count;
    sum.internal_tgis = table->internals;
    sum.external_tgis = table->externals;

    for (std::uint32_t i = 0; i < table->internal_count; ++i) {
        RcolChunk ch;
        ch.type = table->internals[static_cast<std::size_t>(i)].type;
        if (const char* t = chunk_tag(ch.type)) {
            ch.tag = t;
        }
        ch.size = table->locs[static_cast<std::size_t>(i)].size;
        const auto pos = table->locs[static_cast<std::size_t>(i)].pos;
        if (pos > bytes.size() || ch.size > bytes.size() - pos) {
            sum.partial = true;
            sum.chunks.push_back(ch);
            continue;
        }
        const auto chunk = bytes.subspan(pos, ch.size);
        if (ch.type == kGeom || fourcc_eq(chunk, 0, "GEOM")) {
            parse_geom_chunk(chunk, ch);
            if (ch.tag.empty()) {
                ch.tag = "GEOM";
            }
        } else if (ch.type == kMlod || fourcc_eq(chunk, 0, "MLOD")) {
            parse_mlod_chunk(chunk, ch);
            if (ch.tag.empty()) {
                ch.tag = "MLOD";
            }
            sum.lod_groups += ch.group_count;
        } else if (ch.type == kMatd || fourcc_eq(chunk, 0, "MATD")) {
            parse_matd_chunk(chunk, ch, sum);
            if (ch.tag.empty()) {
                ch.tag = "MATD";
            }
            for (const auto& tex : ch.textures) {
                sum.textures.push_back(tex);
            }
        }
        if (ch.has_mesh_counts) {
            sum.total_vertices += ch.vertex_count;
            sum.total_faces += ch.face_count;
        }
        sum.chunks.push_back(std::move(ch));
    }

    if (sum.chunks.empty() && fourcc_eq(bytes, 0, "GEOM")) {
        RcolChunk ch;
        ch.type = kGeom;
        ch.tag = "GEOM";
        ch.size = static_cast<std::uint32_t>(bytes.size());
        parse_geom_chunk(bytes, ch);
        sum.total_vertices = ch.vertex_count;
        sum.total_faces = ch.face_count;
        sum.chunks.push_back(std::move(ch));
    }
    return sum;
}


Result<std::vector<std::byte>> extract_rcol_chunk(std::span<const std::byte> bytes,
                                                  std::uint32_t chunk_index) {
    auto table = parse_rcol_table(bytes);
    bool use_table = false;
    if (table && table->internal_count > 0 && table->locs.size() == table->internal_count) {
        use_table = true;
        for (const auto& l : table->locs) {
            if (l.pos > bytes.size() || l.size > bytes.size() - l.pos) {
                use_table = false;
                break;
            }
        }
    }
    if (!use_table) {
        if (fourcc_eq(bytes, 0, "GEOM") || fourcc_eq(bytes, 0, "MATD") || fourcc_eq(bytes, 0, "MLOD")) {
            if (chunk_index != 0) {
                return std::unexpected(err(ErrorCode::invalid_argument, "chunk index out of range"));
            }
            return std::vector<std::byte>(bytes.begin(), bytes.end());
        }
        if (!table) {
            return std::unexpected(table.error());
        }
        return std::unexpected(err(ErrorCode::corrupt, "rcol chunk range"));
    }
    if (chunk_index >= table->internal_count) {
        return std::unexpected(err(ErrorCode::invalid_argument, "chunk index out of range"));
    }
    const auto& l = table->locs[static_cast<std::size_t>(chunk_index)];
    const auto span = bytes.subspan(l.pos, l.size);
    return std::vector<std::byte>(span.begin(), span.end());
}

Result<std::vector<std::byte>> replace_rcol_chunk(std::span<const std::byte> bytes,
                                                  std::uint32_t chunk_index,
                                                  std::span<const std::byte> new_payload) {
    using namespace bin;
    if (new_payload.size() > 0x7FFFFFFFu) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "rcol chunk payload"));
    }
    auto table = parse_rcol_table(bytes);
    bool use_table = false;
    if (table && table->internal_count > 0 && table->locs.size() == table->internal_count) {
        use_table = true;
        for (const auto& l : table->locs) {
            if (l.pos > bytes.size() || l.size > bytes.size() - l.pos) {
                use_table = false;
                break;
            }
        }
    }
    if (!use_table) {
        if (fourcc_eq(bytes, 0, "GEOM") || fourcc_eq(bytes, 0, "MATD") || fourcc_eq(bytes, 0, "MLOD")) {
            if (chunk_index != 0) {
                return std::unexpected(err(ErrorCode::invalid_argument, "chunk index out of range"));
            }
            return std::vector<std::byte>(new_payload.begin(), new_payload.end());
        }
        if (!table) {
            return std::unexpected(table.error());
        }
        return std::unexpected(err(ErrorCode::corrupt, "rcol chunk range"));
    }
    if (chunk_index >= table->internal_count) {
        return std::unexpected(err(ErrorCode::invalid_argument, "chunk index out of range"));
    }
    std::vector<std::vector<std::byte>> payloads;
    payloads.reserve(table->internal_count);
    for (std::uint32_t i = 0; i < table->internal_count; ++i) {
        const auto& l = table->locs[static_cast<std::size_t>(i)];
        if (l.pos > bytes.size() || l.size > bytes.size() - l.pos) {
            return std::unexpected(err(ErrorCode::corrupt, "rcol chunk range"));
        }
        if (i == chunk_index) {
            payloads.emplace_back(new_payload.begin(), new_payload.end());
        } else {
            const auto span = bytes.subspan(l.pos, l.size);
            payloads.emplace_back(span.begin(), span.end());
        }
    }

    std::vector<std::byte> out;
    const std::size_t header_bytes =
        20 + static_cast<std::size_t>(table->internal_count) * 16 +
        static_cast<std::size_t>(table->external_count) * 16 +
        static_cast<std::size_t>(table->internal_count) * 8;
    std::size_t body = 0;
    for (const auto& p : payloads) {
        body += p.size();
    }
    out.reserve(header_bytes + body);
    put_u32(out, table->version);
    put_u32(out, table->public_chunks);
    put_u32(out, table->index3);
    put_u32(out, table->external_count);
    put_u32(out, table->internal_count);
    for (const auto& t : table->internals) {
        put_u64(out, t.instance);
        put_u32(out, t.type);
        put_u32(out, t.group);
    }
    for (const auto& t : table->externals) {
        put_u64(out, t.instance);
        put_u32(out, t.type);
        put_u32(out, t.group);
    }
    const std::size_t loc_at = out.size();
    for (std::uint32_t i = 0; i < table->internal_count; ++i) {
        put_u32(out, 0);
        put_u32(out, 0);
    }
    std::vector<std::uint32_t> positions;
    positions.reserve(payloads.size());
    for (const auto& p : payloads) {
        positions.push_back(static_cast<std::uint32_t>(out.size()));
        out.insert(out.end(), p.begin(), p.end());
    }
    for (std::uint32_t i = 0; i < table->internal_count; ++i) {
        const std::size_t at = loc_at + static_cast<std::size_t>(i) * 8;
        const std::uint32_t pos = positions[static_cast<std::size_t>(i)];
        const std::uint32_t sz = static_cast<std::uint32_t>(payloads[static_cast<std::size_t>(i)].size());
        std::memcpy(out.data() + at, &pos, 4);
        std::memcpy(out.data() + at + 4, &sz, 4);
    }
    return out;
}

}  // namespace sxpe::resources
