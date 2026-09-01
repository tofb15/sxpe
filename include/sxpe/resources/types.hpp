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
    {kObjk, "OBJK", "Object key"},
    {kVpxy, "VPXY", "Visual proxy"},
    {kClip, "CLIP", "Animation clip"},
    {kS3sa, "S3SA", "Script assembly"},
    {kDir, "DIR", "Compression directory"},
    {kCasp, "CASP", "CAS part"},
};

inline std::string_view tag_for(std::uint32_t type) {
    for (const auto& t : kTypes) {
        if (t.id == type) {
            return t.tag;
        }
    }
    return "";
}

}  // namespace sxpe::resources
