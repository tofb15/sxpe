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
inline constexpr std::uint32_t kCasp = 0x034AEECB;
inline constexpr std::uint32_t kXml = 0x0333406C;
inline constexpr std::uint32_t kItun = 0x03B33DDF;
inline constexpr std::uint32_t kObjd = 0x319E4F1D;
inline constexpr std::uint32_t kObjn = 0x4D1A5589;
inline constexpr std::uint32_t kGeom = 0x015A1849;
inline constexpr std::uint32_t kImag = 0x2F7D0004;
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

inline constexpr TypeInfo kTypes[] = {
    {kStbl, "STBL", "String table"},
    {kNmap, "NMAP", "Name map"},
    {kImg, "_IMG", "DDS image"},
    {kImgAlt, "_IMG", "DDS image (alt)"},
    {kMlod, "MLOD", "Mesh LOD"},
    {kModl, "MODL", "Model"},
    {kGeom, "GEOM", "Body geometry"},
    {kObjk, "OBJK", "Object key"},
    {kObjd, "OBJD", "Object definition"},
    {kObjn, "OBJN", "Object instances"},
    {kVpxy, "VPXY", "Visual proxy"},
    {kClip, "CLIP", "Animation clip"},
    {kS3sa, "S3SA", "Script assembly"},
    {kDir, "DIR", "Compression directory"},
    {kCasp, "CASP", "CAS part"},
    {kXml, "_XML", "XML"},
    {kItun, "ITUN", "Interaction tuning"},
    {kSimo, "SIMO", "Sim outfit"},
    {kFamd, "FAMD", "Household"},
    {kDetl, "DETL", "Lot / world detail"},
    {kRefs, "REFS", "Reference store"},
    {kImag, "IMAG", "PNG image"},
    {kIcon, "ICON", "PNG thumbnail"},
    {0x2E75C765, "ICON", "PNG thumbnail"},
    {0x2E75C766, "ICON", "PNG thumbnail"},
    {kSnap, "SNAP", "PNG snapshot"},
    {kSnapSmall, "SNAP", "PNG snapshot (small)"},
    {kSnapLarge, "SNAP", "PNG snapshot (large)"},
    {kSnapFamily, "SNAP", "PNG family snapshot"},
    {kTsnp, "TSNP", "PNG travel snapshot"},
    {kThum, "THUM", "PNG thumbnail"},
    {0x0580A2B5, "THUM", "PNG thumbnail"},
    {0x0580A2B6, "THUM", "PNG thumbnail"},
};

inline std::string_view tag_for(std::uint32_t type) {
    for (const auto& t : kTypes) {
        if (t.id == type) {
            return t.tag;
        }
    }
    return "";
}

inline bool is_dds_image(std::uint32_t type) { return type == kImg || type == kImgAlt; }

inline bool is_png_image(std::uint32_t type) {
    const auto tag = tag_for(type);
    return tag == "SNAP" || tag == "THUM" || tag == "IMAG" || tag == "ICON" || tag == "TSNP";
}

}  // namespace sxpe::resources
