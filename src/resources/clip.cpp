#include "sxpe/resources/clip.hpp"

#include "sxpe/resources/binary.hpp"
#include "sxpe/resources/types.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace sxpe::resources {
namespace {

constexpr std::uint64_t kS3ClipMagic = 0x5F70696C4333535Full;  // "_S3Clip_" LE qword
constexpr std::uint32_t kMaxTracks = 4096;
constexpr std::uint32_t kMaxNameBytes = 512;

struct ClipLayout {
    ClipInfo info;
    std::size_t clip_at{0};
    std::size_t s3_size_field{8};
    std::size_t clip_off_field{12};
    std::size_t actor_off_field{20};
    std::size_t end_off_field{36};
    std::size_t anim_off_field{0};
    std::size_t src_off_field{0};
    std::size_t offset1_field{0};
    std::uint32_t s3_size{0};
    std::uint32_t clip_off{0};
    std::uint32_t actor_off{0};
    std::uint32_t end_off{0};
    std::uint32_t offset1{0};
    std::uint32_t anim_off{0};
    std::uint32_t src_off{0};
    std::size_t anim_at{static_cast<std::size_t>(-1)};
    std::size_t src_at{static_cast<std::size_t>(-1)};
    std::size_t actor_at{static_cast<std::size_t>(-1)};
    std::size_t anim_bytes{0};   // including NUL
    std::size_t src_bytes{0};
    std::size_t actor_bytes{0};  // including NUL + 0x7e pad to DWORD
    std::size_t track_table_at{0};
    std::size_t end_at{static_cast<std::size_t>(-1)};
};

bool read_name_at(std::span<const std::byte> bytes, std::size_t base, std::uint32_t rel,
                  std::string& out, std::size_t& abs_out, std::size_t& nbytes) {
    abs_out = static_cast<std::size_t>(-1);
    nbytes = 0;
    if (rel == 0) {
        out.clear();
        return true;
    }
    std::size_t o = base + rel;
    if (o >= bytes.size()) {
        return false;
    }
    abs_out = o;
    if (!bin::take_cstr(bytes, o, out, kMaxNameBytes)) {
        return false;
    }
    nbytes = out.size() + 1;  // NUL
    return true;
}

/// Actor name: NUL-terminated, padded to DWORD with 0x7e (SimsWiki).
bool read_actor_at(std::span<const std::byte> bytes, std::size_t at, std::string& out,
                   std::size_t& nbytes) {
    out.clear();
    nbytes = 0;
    if (at >= bytes.size()) {
        return false;
    }
    std::size_t o = at;
    if (!bin::take_cstr(bytes, o, out, kMaxNameBytes)) {
        return false;
    }
    std::size_t padded = out.size() + 1;
    while (padded % 4 != 0) {
        ++padded;
    }
    if (at + padded > bytes.size()) {
        // Still accept the C-string; mark length as unpadded.
        nbytes = out.size() + 1;
        return true;
    }
    nbytes = padded;
    return true;
}

std::vector<std::byte> encode_actor(std::string_view name) {
    std::vector<std::byte> out;
    out.reserve(name.size() + 4);
    for (char c : name) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    out.push_back(std::byte{0});
    while (out.size() % 4 != 0) {
        out.push_back(std::byte{0x7e});
    }
    return out;
}

std::vector<std::byte> encode_cstr(std::string_view name) {
    std::vector<std::byte> out;
    out.reserve(name.size() + 1);
    for (char c : name) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
    out.push_back(std::byte{0});
    return out;
}

void patch_u32(std::vector<std::byte>& out, std::size_t at, std::uint32_t v) {
    if (at + 4 > out.size()) {
        return;
    }
    std::memcpy(out.data() + at, &v, 4);
}

Result<ClipLayout> parse_clip_layout(std::span<const std::byte> bytes) {
    using namespace bin;
    if (bytes.size() < 48) {
        return std::unexpected(err(ErrorCode::corrupt, "clip too short"));
    }
    ClipLayout L;
    std::size_t p = 0;
    std::uint32_t tid = 0;
    std::uint32_t linked = 0;
    std::uint32_t slot_off = 0;
    std::uint32_t event_off = 0;
    std::uint32_t unk0 = 0;
    std::uint32_t unk1 = 0;
    if (!take_u32(bytes, p, tid) || !take_u32(bytes, p, linked) || !take_u32(bytes, p, L.s3_size) ||
        !take_u32(bytes, p, L.clip_off) || !take_u32(bytes, p, slot_off) ||
        !take_u32(bytes, p, L.actor_off) || !take_u32(bytes, p, event_off) ||
        !take_u32(bytes, p, unk0) || !take_u32(bytes, p, unk1) || !take_u32(bytes, p, L.end_off)) {
        return std::unexpected(err(ErrorCode::corrupt, "clip header"));
    }
    (void)linked;
    (void)slot_off;
    (void)event_off;
    (void)unk0;
    (void)unk1;

    const std::size_t clip_field = 12;
    L.clip_at = clip_field + L.clip_off;
    if (tid == kClip) {
        // ok
    } else if (tid != 0) {
        L.clip_at = 0;
        L.info.partial = true;
    }
    if (L.clip_at + 48 > bytes.size()) {
        L.clip_at = bytes.size();
        for (std::size_t i = 0; i + 8 <= bytes.size(); ++i) {
            if (ru64(bytes, i) == kS3ClipMagic) {
                L.clip_at = i;
                break;
            }
        }
        if (L.clip_at + 48 > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "clip s3 section"));
        }
        L.info.partial = true;
    }
    if (ru64(bytes, L.clip_at) != kS3ClipMagic) {
        return std::unexpected(err(ErrorCode::corrupt, "clip magic"));
    }
    std::size_t cp = L.clip_at + 8;
    std::uint32_t blank = 0;
    if (!take_u32(bytes, cp, L.info.version) || !take_u32(bytes, cp, blank) ||
        !take_f32(bytes, cp, L.info.frame_duration) || !take_u16(bytes, cp, L.info.frame_count)) {
        return std::unexpected(err(ErrorCode::corrupt, "clip s3 header"));
    }
    std::uint16_t unk_w = 0;
    std::uint32_t count1 = 0;
    std::uint32_t count2 = 0;
    std::uint32_t offset2 = 0;
    if (!take_u16(bytes, cp, unk_w) || !take_u32(bytes, cp, count1) || !take_u32(bytes, cp, count2) ||
        !take_u32(bytes, cp, L.offset1) || !take_u32(bytes, cp, offset2) ||
        !take_u32(bytes, cp, L.anim_off) || !take_u32(bytes, cp, L.src_off)) {
        return std::unexpected(err(ErrorCode::corrupt, "clip s3 fields"));
    }
    // After magic: version blank fdur fcount unkw count1 count2 | off1 off2 anim src
    L.offset1_field = L.clip_at + 8 + 4 + 4 + 4 + 2 + 2 + 4 + 4;
    L.anim_off_field = L.offset1_field + 4 + 4;  // after offset1 + offset2
    L.src_off_field = L.anim_off_field + 4;
    (void)count2;
    (void)offset2;
    (void)blank;
    (void)unk_w;

    L.info.track_count = count1;
    L.info.duration_seconds =
        L.info.frame_duration * static_cast<float>(L.info.frame_count);

    if (!read_name_at(bytes, L.clip_at, L.anim_off, L.info.anim_name, L.anim_at, L.anim_bytes)) {
        L.info.partial = true;
    }
    if (!read_name_at(bytes, L.clip_at, L.src_off, L.info.source_file, L.src_at, L.src_bytes)) {
        L.info.partial = true;
    }
    if (L.actor_off != 0) {
        L.actor_at = L.actor_off_field + L.actor_off;
        if (L.actor_at < bytes.size()) {
            if (!read_actor_at(bytes, L.actor_at, L.info.actor_name, L.actor_bytes)) {
                L.info.partial = true;
            }
        }
    }
    if (L.end_off != 0) {
        L.end_at = L.end_off_field + L.end_off;
        if (L.end_at + 16 > bytes.size()) {
            L.end_at = static_cast<std::size_t>(-1);
            L.info.partial = true;
        }
    }

    if (count1 > 0 && count1 <= kMaxTracks && L.offset1 != 0) {
        L.track_table_at = L.clip_at + L.offset1;
        std::size_t tp = L.track_table_at;
        const std::uint32_t n = std::min(count1, 64u);
        L.info.track_hashes.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) {
            if (tp + 20 > bytes.size()) {
                L.info.partial = true;
                break;
            }
            tp += 4;
            std::uint32_t hash = 0;
            if (!take_u32(bytes, tp, hash)) {
                L.info.partial = true;
                break;
            }
            L.info.track_hashes.push_back(hash);
            tp += 4 + 4 + 2 + 2;
        }
    }
    return L;
}

}  // namespace

Result<ClipInfo> parse_clip(std::span<const std::byte> bytes) {
    auto L = parse_clip_layout(bytes);
    if (!L) {
        return std::unexpected(L.error());
    }
    return L->info;
}

Result<std::vector<std::byte>> apply_clip(std::span<const std::byte> bytes, const ClipPatch& patch) {
    if (!patch.anim_name && !patch.source_file && !patch.actor_name && patch.track_hashes.empty()) {
        return std::unexpected(err(ErrorCode::invalid_argument, "clip.set: no fields to patch"));
    }
    auto layout = parse_clip_layout(bytes);
    if (!layout) {
        return std::unexpected(layout.error());
    }
    ClipLayout L = *layout;
    std::vector<std::byte> out(bytes.begin(), bytes.end());

    auto rewrite_clip_string = [&](const std::optional<std::string>& value, std::size_t off_field,
                                   std::uint32_t& cur_off, std::size_t cur_at,
                                   std::size_t cur_nbytes) -> VoidResult {
        if (!value) {
            return ok();
        }
        if (value->size() >= kMaxNameBytes) {
            return std::unexpected(err(ErrorCode::invalid_argument, "clip name too long"));
        }
        auto encoded = encode_cstr(*value);
        if (cur_off != 0 && cur_at != static_cast<std::size_t>(-1) && encoded.size() <= cur_nbytes &&
            cur_at + cur_nbytes <= out.size()) {
            std::fill(out.begin() + static_cast<std::ptrdiff_t>(cur_at),
                      out.begin() + static_cast<std::ptrdiff_t>(cur_at + cur_nbytes), std::byte{0});
            std::memcpy(out.data() + cur_at, encoded.data(), encoded.size());
            return ok();
        }
        std::vector<std::byte> end_block;
        std::size_t insert_at = out.size();
        if (L.end_at != static_cast<std::size_t>(-1) && L.end_at + 16 <= out.size()) {
            end_block.assign(out.begin() + static_cast<std::ptrdiff_t>(L.end_at),
                             out.begin() + static_cast<std::ptrdiff_t>(L.end_at + 16));
            out.resize(L.end_at);
            insert_at = out.size();
        }
        if (insert_at < L.clip_at) {
            return std::unexpected(err(ErrorCode::corrupt, "clip string insert before s3"));
        }
        const auto new_rel = static_cast<std::uint32_t>(insert_at - L.clip_at);
        out.insert(out.end(), encoded.begin(), encoded.end());
        if (!end_block.empty()) {
            L.end_at = out.size();
            out.insert(out.end(), end_block.begin(), end_block.end());
            L.end_off = static_cast<std::uint32_t>(L.end_at - L.end_off_field);
            patch_u32(out, L.end_off_field, L.end_off);
        }
        patch_u32(out, off_field, new_rel);
        cur_off = new_rel;
        const std::size_t str_end = insert_at + encoded.size();
        const std::size_t s3_end = L.clip_at + L.s3_size;
        if (str_end > s3_end) {
            L.s3_size = static_cast<std::uint32_t>(str_end - L.clip_at);
            patch_u32(out, L.s3_size_field, L.s3_size);
        }
        return ok();
    };

    auto rewrite_actor = [&](const std::optional<std::string>& value) -> VoidResult {
        if (!value) {
            return ok();
        }
        if (value->size() >= kMaxNameBytes) {
            return std::unexpected(err(ErrorCode::invalid_argument, "clip actor name too long"));
        }
        auto encoded = encode_actor(*value);
        if (L.actor_off != 0 && L.actor_at != static_cast<std::size_t>(-1) &&
            encoded.size() <= L.actor_bytes && L.actor_at + L.actor_bytes <= out.size()) {
            std::fill(out.begin() + static_cast<std::ptrdiff_t>(L.actor_at),
                      out.begin() + static_cast<std::ptrdiff_t>(L.actor_at + L.actor_bytes),
                      std::byte{0x7e});
            std::memcpy(out.data() + L.actor_at, encoded.data(), encoded.size());
            return ok();
        }
        std::vector<std::byte> end_block;
        std::size_t insert_at = out.size();
        if (L.end_at != static_cast<std::size_t>(-1) && L.end_at + 16 <= out.size()) {
            end_block.assign(out.begin() + static_cast<std::ptrdiff_t>(L.end_at),
                             out.begin() + static_cast<std::ptrdiff_t>(L.end_at + 16));
            out.resize(L.end_at);
            insert_at = out.size();
        }
        if (insert_at < L.actor_off_field) {
            return std::unexpected(err(ErrorCode::corrupt, "clip actor insert before field"));
        }
        const auto new_rel = static_cast<std::uint32_t>(insert_at - L.actor_off_field);
        out.insert(out.end(), encoded.begin(), encoded.end());
        if (!end_block.empty()) {
            L.end_at = out.size();
            out.insert(out.end(), end_block.begin(), end_block.end());
            L.end_off = static_cast<std::uint32_t>(L.end_at - L.end_off_field);
            patch_u32(out, L.end_off_field, L.end_off);
        }
        patch_u32(out, L.actor_off_field, new_rel);
        L.actor_off = new_rel;
        L.actor_at = insert_at;
        return ok();
    };

    if (auto r = rewrite_clip_string(patch.anim_name, L.anim_off_field, L.anim_off, L.anim_at,
                                     L.anim_bytes);
        !r) {
        return std::unexpected(r.error());
    }
    // Re-parse positions for src after possible resize — use updated L.end_at; src offsets still valid
    // for in-place; for append we used current out. Refresh cur positions from original L for src
    // only when src wasn't moved (it wasn't).
    if (auto r = rewrite_clip_string(patch.source_file, L.src_off_field, L.src_off, L.src_at,
                                     L.src_bytes);
        !r) {
        return std::unexpected(r.error());
    }
    if (auto r = rewrite_actor(patch.actor_name); !r) {
        return std::unexpected(r.error());
    }

    if (!patch.track_hashes.empty()) {
        if (L.offset1 == 0 || L.track_table_at == 0) {
            return std::unexpected(err(ErrorCode::corrupt, "clip has no track table"));
        }
        for (const auto& [idx, hash] : patch.track_hashes) {
            if (idx >= L.info.track_count || idx >= 64u) {
                return std::unexpected(err(ErrorCode::invalid_argument, "clip track index out of range"));
            }
            const std::size_t hash_at = L.track_table_at + static_cast<std::size_t>(idx) * 20 + 4;
            if (hash_at + 4 > out.size()) {
                return std::unexpected(err(ErrorCode::corrupt, "clip track hash OOB"));
            }
            patch_u32(out, hash_at, hash);
        }
    }

    auto check = parse_clip(out);
    if (!check) {
        return std::unexpected(check.error());
    }
    return out;
}

}  // namespace sxpe::resources
