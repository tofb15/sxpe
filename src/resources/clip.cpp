#include "sxpe/resources/clip.hpp"

#include "sxpe/resources/binary.hpp"
#include "sxpe/resources/types.hpp"

#include <algorithm>

namespace sxpe::resources {
namespace {

constexpr std::uint64_t kS3ClipMagic = 0x5F70696C4333535Full;  // "_S3Clip_" LE qword

bool read_name_at(std::span<const std::byte> bytes, std::size_t base, std::uint32_t rel,
                  std::string& out) {
    if (rel == 0) {
        out.clear();
        return true;
    }
    std::size_t o = base + rel;
    if (o >= bytes.size()) {
        return false;
    }
    return bin::take_cstr(bytes, o, out, 512);
}

}  // namespace

Result<ClipInfo> parse_clip(std::span<const std::byte> bytes) {
    using namespace bin;
    if (bytes.size() < 48) {
        return std::unexpected(err(ErrorCode::corrupt, "clip too short"));
    }
    ClipInfo info;
    std::size_t p = 0;
    std::uint32_t tid = 0;
    std::uint32_t linked = 0;
    std::uint32_t s3_size = 0;
    std::uint32_t clip_off = 0;
    std::uint32_t slot_off = 0;
    std::uint32_t actor_off = 0;
    std::uint32_t event_off = 0;
    std::uint32_t unk0 = 0;
    std::uint32_t unk1 = 0;
    std::uint32_t end_off = 0;
    if (!take_u32(bytes, p, tid) || !take_u32(bytes, p, linked) || !take_u32(bytes, p, s3_size) ||
        !take_u32(bytes, p, clip_off) || !take_u32(bytes, p, slot_off) ||
        !take_u32(bytes, p, actor_off) || !take_u32(bytes, p, event_off) ||
        !take_u32(bytes, p, unk0) || !take_u32(bytes, p, unk1) || !take_u32(bytes, p, end_off)) {
        return std::unexpected(err(ErrorCode::corrupt, "clip header"));
    }
    // Relative field offsets — see docs/spec/preview-wave2.md (CLIP).
    const std::size_t clip_field = 12;
    const std::size_t actor_field = 20;
    std::size_t clip_at = clip_field + clip_off;
    if (tid == kClip) {
        // ok
    } else if (tid != 0) {
        // Some packs omit TID; treat start as clip section if magic matches later.
        clip_at = 0;
        info.partial = true;
    }
    if (clip_at + 48 > bytes.size()) {
        // Fallback: scan for _S3Clip_ magic.
        clip_at = bytes.size();
        for (std::size_t i = 0; i + 8 <= bytes.size(); ++i) {
            if (ru64(bytes, i) == kS3ClipMagic) {
                clip_at = i;
                break;
            }
        }
        if (clip_at + 48 > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "clip s3 section"));
        }
        info.partial = true;
    }
    if (ru64(bytes, clip_at) != kS3ClipMagic) {
        return std::unexpected(err(ErrorCode::corrupt, "clip magic"));
    }
    std::size_t cp = clip_at + 8;
    std::uint32_t blank = 0;
    if (!take_u32(bytes, cp, info.version) || !take_u32(bytes, cp, blank) ||
        !take_f32(bytes, cp, info.frame_duration) || !take_u16(bytes, cp, info.frame_count)) {
        return std::unexpected(err(ErrorCode::corrupt, "clip s3 header"));
    }
    std::uint16_t unk_w = 0;
    std::uint32_t count1 = 0;
    std::uint32_t count2 = 0;
    std::uint32_t offset1 = 0;
    std::uint32_t offset2 = 0;
    std::uint32_t anim_off = 0;
    std::uint32_t src_off = 0;
    if (!take_u16(bytes, cp, unk_w) || !take_u32(bytes, cp, count1) || !take_u32(bytes, cp, count2) ||
        !take_u32(bytes, cp, offset1) || !take_u32(bytes, cp, offset2) ||
        !take_u32(bytes, cp, anim_off) || !take_u32(bytes, cp, src_off)) {
        return std::unexpected(err(ErrorCode::corrupt, "clip s3 fields"));
    }
    info.track_count = count1;
    info.duration_seconds = info.frame_duration * static_cast<float>(info.frame_count);
    if (!read_name_at(bytes, clip_at, anim_off, info.anim_name)) {
        info.partial = true;
    }
    if (!read_name_at(bytes, clip_at, src_off, info.source_file)) {
        info.partial = true;
    }
    if (actor_off != 0) {
        std::size_t ap = actor_field + actor_off;
        if (ap < bytes.size()) {
            bin::take_cstr(bytes, ap, info.actor_name, 256);
        }
    }
    constexpr std::uint32_t kMaxTracks = 4096;
    if (count1 > 0 && count1 <= kMaxTracks && offset1 != 0) {
        std::size_t tp = clip_at + offset1;
        const std::uint32_t n = std::min(count1, 64u);
        info.track_hashes.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            if (tp + 20 > bytes.size()) {
                info.partial = true;
                break;
            }
            // Skip to joint-rule hash; layout in docs/spec/preview-wave2.md.
            tp += 4;
            std::uint32_t hash = 0;
            if (!take_u32(bytes, tp, hash)) {
                info.partial = true;
                break;
            }
            info.track_hashes.push_back(hash);
            tp += 4 + 4 + 2 + 2;
        }
    }
    (void)s3_size;
    (void)slot_off;
    (void)event_off;
    (void)end_off;
    (void)linked;
    (void)count2;
    (void)offset2;
    return info;
}

}  // namespace sxpe::resources
