#include "sxpe/resources/rcol.hpp"

#include "sxpe/resources/binary.hpp"
#include "sxpe/resources/types.hpp"

#include <algorithm>
#include <cstring>

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
        case 0x01D0E75D:
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

void parse_geom_chunk(std::span<const std::byte> chunk, RcolChunk& out) {
    using namespace bin;
    // Standalone GEOM chunk body often starts with 'GEOM' tag.
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
        // Skip vertex formats + vertex data to reach faces (best-effort size walk).
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
        // Only first ItemCount entry's BytesPerFacePoint is read above — wiki shows
        // repetition of BYTE BytesPerFacePoint. Read remaining if any.
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
        // After subset_bytes field, layout has NameHash..PrimitiveCount before bbox.
        // VertexCount @ +40, PrimitiveCount @ +44 from start of subset body
        // (11 dwords before VertexCount: name..MinVertexIndex).
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

}  // namespace

Result<RcolSummary> parse_rcol_summary(std::span<const std::byte> bytes) {
    using namespace bin;
    RcolSummary sum;
    if (bytes.size() < 20) {
        // May be a bare GEOM chunk without RCOL wrapper.
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
        return std::unexpected(err(ErrorCode::corrupt, "rcol too short"));
    }
    std::size_t p = 0;
    if (!take_u32(bytes, p, sum.version) || !take_u32(bytes, p, sum.public_chunks)) {
        return std::unexpected(err(ErrorCode::corrupt, "rcol header"));
    }
    std::uint32_t index3 = 0;
    if (!take_u32(bytes, p, index3) || !take_u32(bytes, p, sum.external_count) ||
        !take_u32(bytes, p, sum.internal_count)) {
        return std::unexpected(err(ErrorCode::corrupt, "rcol counts"));
    }
    constexpr std::uint32_t kMaxChunks = 4096;
    if (sum.internal_count > kMaxChunks || sum.external_count > kMaxChunks) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "rcol chunks"));
    }
    struct Ref {
        std::uint64_t inst;
        std::uint32_t type;
        std::uint32_t group;
    };
    std::vector<Ref> internals;
    internals.reserve(sum.internal_count);
    for (std::uint32_t i = 0; i < sum.internal_count; ++i) {
        Ref r{};
        if (!take_u64(bytes, p, r.inst) || !take_u32(bytes, p, r.type) ||
            !take_u32(bytes, p, r.group)) {
            return std::unexpected(err(ErrorCode::corrupt, "rcol internal tgi"));
        }
        internals.push_back(r);
    }
    for (std::uint32_t i = 0; i < sum.external_count; ++i) {
        std::uint64_t inst = 0;
        std::uint32_t type = 0;
        std::uint32_t group = 0;
        if (!take_u64(bytes, p, inst) || !take_u32(bytes, p, type) || !take_u32(bytes, p, group)) {
            return std::unexpected(err(ErrorCode::corrupt, "rcol external tgi"));
        }
    }
    struct Loc {
        std::uint32_t pos;
        std::uint32_t size;
    };
    std::vector<Loc> locs;
    locs.reserve(sum.internal_count);
    for (std::uint32_t i = 0; i < sum.internal_count; ++i) {
        Loc l{};
        if (!take_u32(bytes, p, l.pos) || !take_u32(bytes, p, l.size)) {
            return std::unexpected(err(ErrorCode::corrupt, "rcol locs"));
        }
        locs.push_back(l);
    }
    for (std::uint32_t i = 0; i < sum.internal_count; ++i) {
        RcolChunk ch;
        ch.type = internals[static_cast<std::size_t>(i)].type;
        if (const char* t = chunk_tag(ch.type)) {
            ch.tag = t;
        }
        ch.size = locs[static_cast<std::size_t>(i)].size;
        const auto pos = locs[static_cast<std::size_t>(i)].pos;
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
        } else if (fourcc_eq(chunk, 0, "MLOD")) {
            parse_mlod_chunk(chunk, ch);
            ch.tag = "MLOD";
            sum.lod_groups += ch.group_count;
        }
        if (ch.has_mesh_counts) {
            sum.total_vertices += ch.vertex_count;
            sum.total_faces += ch.face_count;
        }
        sum.chunks.push_back(std::move(ch));
    }
    // Bare GEOM resources sometimes put the chunk at offset 0 without a useful RCOL table.
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

}  // namespace sxpe::resources
