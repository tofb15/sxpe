#pragma once

#include <cstdint>
#include <string_view>

namespace sxpe::resources {

inline constexpr std::uint32_t kStbl = 0x220557DA;
inline constexpr std::uint32_t kNmap = 0x0166038C;
inline constexpr std::uint32_t kImg = 0x00B2D882;
inline constexpr std::uint32_t kImgAlt = 0x8FFB80F6;
inline constexpr std::uint32_t kMlod = 0x01D10F34;
inline constexpr std::uint32_t kModl = 0x01661233;
inline constexpr std::uint32_t kObjk = 0x02DC343F;
inline constexpr std::uint32_t kVpxy = 0x736884F1;
inline constexpr std::uint32_t kClip = 0x6B20C4F3;
inline constexpr std::uint32_t kS3sa = 0x073FAA07;
inline constexpr std::uint32_t kDir = 0xE86B1EEF;
inline constexpr std::uint32_t kSxmm = 0x53584D4D;  // 'SXMM' SXPE merge manifest
inline constexpr std::uint32_t kSims3PackLeftoverManifest = 0x73E93EEB;  // Sims3Pack leftover manifest XML
inline constexpr std::uint32_t kCasp = 0x034AEECB;
inline constexpr std::uint32_t kXml = 0x0333406C;
inline constexpr std::uint32_t kItun = 0x03B33DDF;
inline constexpr std::uint32_t kObjd = 0x319E4F1D;
inline constexpr std::uint32_t kObjn = 0x4D1A5589;
inline constexpr std::uint32_t kGeom = 0x015A1849;
inline constexpr std::uint32_t kImag = 0x2F7D0004;
inline constexpr std::uint32_t kImagJpeg = 0x2F7D0002;
inline constexpr std::uint32_t kIcon = 0x2E75C764;
inline constexpr std::uint32_t kSnap = 0x0580A2CD;
inline constexpr std::uint32_t kSnapSmall = 0x0580A2CE;
inline constexpr std::uint32_t kSnapLarge = 0x0580A2CF;
inline constexpr std::uint32_t kSnapFamily = 0x6B6D837E;
inline constexpr std::uint32_t kTsnp = 0x54372472;
inline constexpr std::uint32_t kThum = 0x0580A2B4;
inline constexpr std::uint32_t kFamd = 0x062853A8;
inline constexpr std::uint32_t kDetl = 0x03D86EA4;
inline constexpr std::uint32_t kSimo = 0x025ED6F4;
inline constexpr std::uint32_t kRefs = 0x05ED1226;

struct TypeInfo {
    std::uint32_t id;
    std::string_view tag;
    std::string_view name;
};

// Display tags for the resource list (s3pe-style 4-char names). Public
// community type IDs (SimsWiki PackedFileTypes / catalog resources), not an
// s3pi ExtList dump. Unknown types still show an empty tag.
// Human descriptions of each tag: docs/tags.md
inline constexpr TypeInfo kTypes[] = {
    {0x00AE6C67, "BONE", "Skeleton / bone"},
    {kImg, "_IMG", "DDS image"},
    {0x00B552EA, "_SPT", "SpeedTree"},
    {kGeom, "GEOM", "Body geometry"},
    {kNmap, "NMAP", "Name map"},
    {kModl, "MODL", "Model"},
    {0x01A527DB, "_AUD", "Audio (voice)"},
    {0x01D0E6FB, "VBUF", "Vertex buffer"},
    {0x01D0E70F, "IBUF", "Index buffer"},
    {0x01D0E723, "VRTF", "Vertex format"},
    {0x01D0E75D, "MATD", "Material definition"},
    {0x01D0E76B, "SKIN", "Skinning"},
    {kMlod, "MLOD", "Mesh LOD"},
    {0x01EEF63A, "_AUD", "Audio (fx/music)"},
    {0x02019972, "MTST", "Material set"},
    {0x021D7E8C, "SPT2", "SpeedTree data"},
    {0x025C90A6, "_CSS", "CSS"},
    {0x025C95B6, "LAYO", "UI layout"},
    {kSimo, "SIMO", "Sim outfit"},
    {0x029E333B, "VOCE", "Voice mix"},
    {0x02C9EFF2, "MIXR", "Audio mixer"},
    {0x02D5DF13, "JAZZ", "Animation sequence"},
    {kObjk, "OBJK", "Object key"},
    {0x033260E3, "TKMK", "Animation track mask"},
    {kXml, "_XML", "XML"},
    {0x033A1435, "TXTC", "Texture compositor"},
    {0x0341ACC9, "TXTF", "Fabric compositor"},
    {kCasp, "CASP", "CAS part"},
    {0x0354796A, "TONE", "Skin tone"},
    {0x03555BA8, "TONE", "Hair tone"},
    {0x0355E0A6, "BOND", "Bone delta"},
    {0x0358B08A, "FACE", "Face blend"},
    {kItun, "ITUN", "Interaction tuning"},
    {0x03B4C61D, "LITE", "Light"},
    {0x03D843C2, "CCHE", "Compositor cache"},
    {kDetl, "DETL", "Lot / world detail"},
    {0x0418FE2A, "CFEN", "Catalog fence"},
    {0x044AE110, "COMP", "Complate preset"},
    {0x049CA4CD, "CSTR", "Catalog stairs"},
    {0x04AC5D93, "CPRX", "Catalog proxy product"},
    {0x04B30669, "CTTL", "Catalog terrain brush"},
    {0x04C58103, "CRAL", "Catalog railing"},
    {0x04D82D90, "CMRU", "Compositor MRU"},
    {0x04ED4BB2, "CTPT", "Catalog terrain paint"},
    {0x04F3CC01, "CFIR", "Catalog fireplace"},
    {0x04F51033, "SBNO", "Bin outfit"},
    {0x04F88964, "SIME", "Townie / sim export"},
    {0x051DF2DD, "CBLN", "Face piece preset"},
    {kThum, "THUM", "PNG thumbnail"},
    {0x0580A2B5, "THUM", "PNG thumbnail"},
    {0x0580A2B6, "THUM", "PNG thumbnail"},
    {kSnap, "SNAP", "PNG snapshot"},
    {kSnapSmall, "SNAP", "PNG snapshot (small)"},
    {kSnapLarge, "SNAP", "PNG snapshot (large)"},
    {0x0589DC44, "THUM", "PNG thumbnail"},
    {0x0589DC45, "THUM", "PNG thumbnail"},
    {0x0589DC46, "THUM", "PNG thumbnail"},
    {0x0589DC47, "THUM", "PNG thumbnail"},
    {0x05B17698, "THUM", "PNG thumbnail"},
    {0x05B17699, "THUM", "PNG thumbnail"},
    {0x05B1769A, "THUM", "PNG thumbnail"},
    {0x05B1B524, "THUM", "PNG thumbnail"},
    {0x05B1B525, "THUM", "PNG thumbnail"},
    {0x05B1B526, "THUM", "PNG thumbnail"},
    {0x2653E3C8, "THUM", "PNG thumbnail (fence)"},
    {0x2653E3C9, "THUM", "PNG thumbnail (fence)"},
    {0x2653E3CA, "THUM", "PNG thumbnail (fence)"},
    {0x2D4284F0, "THUM", "PNG thumbnail"},
    {0x2D4284F1, "THUM", "PNG thumbnail"},
    {0x2D4284F2, "THUM", "PNG thumbnail"},
    {0x5DE9DBA0, "THUM", "PNG thumbnail"},
    {0x5DE9DBA1, "THUM", "PNG thumbnail"},
    {0x5DE9DBA2, "THUM", "PNG thumbnail"},
    {0x626F60CC, "THUM", "PNG thumbnail (CAS small)"},
    {0x626F60CD, "THUM", "PNG thumbnail (CAS medium)"},
    {0x626F60CE, "THUM", "PNG thumbnail (CAS large)"},
    {0x0591B1AF, "UPST", "User CASt preset"},
    {kRefs, "REFS", "Reference store"},
    {0x05FF6BA4, "2ARY", "World binary"},
    {0x0604ABDA, "DMTR", "Dreams and promises tree"},
    {0x060B390C, "CWAT", "Catalog water brush"},
    {kFamd, "FAMD", "Household"},
    {0x062C8204, "BBLN", "Body blend"},
    {0x06302271, "CINF", "Color information"},
    {0x063261DA, "HINF", "Hair color"},
    {0x06326213, "OBCI", "Object color"},
    {0x067CAA11, "BGEO", "Blend geometry"},
    {kS3sa, "S3SA", "Script assembly"},
    {0x0A36F07A, "CCFP", "Catalog fountain/pool"},
    {0x0B2CB440, "_VID", "AVI video"},
    {kImag, "IMAG", "PNG image"},
    {kImagJpeg, "IMAG", "JPEG image"},
    {kIcon, "ICON", "PNG thumbnail"},
    {0x2E75C765, "ICON", "PNG thumbnail"},
    {0x2E75C766, "ICON", "PNG thumbnail"},
    {0x2E75C767, "ICON", "PNG thumbnail (very large)"},
    {0x316C78F2, "CFND", "Catalog foundation"},
    {kObjd, "OBJD", "Catalog object"},
    {kObjn, "OBJN", "Object instances"},
    {0x515CA4CD, "CWAL", "Catalog wall/floor pattern"},
    {kTsnp, "TSNP", "PNG travel snapshot"},
    {kClip, "CLIP", "Animation clip"},
    {0x6B6D837D, "SNAP", "PNG family snapshot (small)"},
    {kSnapFamily, "SNAP", "PNG family snapshot"},
    {0x6B6D837F, "SNAP", "PNG family snapshot (large)"},
    {kVpxy, "VPXY", "Visual proxy"},
    {kImgAlt, "_IMG", "DDS image (alt)"},
    {0x9151E6BC, "CWST", "Catalog wall style"},
    {0x91EDBD3E, "CRST", "Catalog roof style"},
    {kStbl, "STBL", "String table"},
    {0xB52F5055, "BLND", "Blend unit"},
    {0xDEA2951C, "COAT", "Coat set"},
    {kDir, "DIR", "Compression directory"},
    {kSxmm, "SXMM", "SXPE merge manifest"},
    {kSims3PackLeftoverManifest, "S3MF", "Sims3Pack leftover manifest XML"},
    {0xF1EDBD86, "CRMT", "Catalog roof pattern"},
};

inline std::string_view tag_for(std::uint32_t type) {
    for (const auto& t : kTypes) {
        if (t.id == type) {
            return t.tag;
        }
    }
    return "";
}

inline std::string_view name_for(std::uint32_t type) {
    for (const auto& t : kTypes) {
        if (t.id == type) {
            return t.name;
        }
    }
    return "";
}

inline bool is_dds_image(std::uint32_t type) { return type == kImg || type == kImgAlt; }

inline bool is_png_image(std::uint32_t type) {
    if (type == kImagJpeg) {
        return false;
    }
    const auto tag = tag_for(type);
    return tag == "SNAP" || tag == "THUM" || tag == "IMAG" || tag == "ICON" || tag == "TSNP";
}

}  // namespace sxpe::resources
