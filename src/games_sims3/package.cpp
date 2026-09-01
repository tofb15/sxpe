#include "sxpe/games/sims3/package.hpp"

#include "sxpe/core/caps.hpp"
#include "sxpe/games/sims3/refpack.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace sxpe::games::sims3 {
namespace {

using sxpe::core::caps::kHeaderSize;
using sxpe::core::caps::kMaxIndexEntries;
using sxpe::core::caps::kMaxResourceBytes;

std::uint32_t rd_u32(std::span<const std::byte> s, std::size_t off) {
    std::uint32_t v = 0;
    std::memcpy(&v, s.data() + off, 4);
    if constexpr (std::endian::native != std::endian::little) {
        v = std::byteswap(v);
    }
    return v;
}

void wr_u32(std::vector<std::byte>& out, std::uint32_t v) {
    if constexpr (std::endian::native != std::endian::little) {
        v = std::byteswap(v);
    }
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    out.insert(out.end(), p, p + 4);
}

void wr_u32_at(std::array<std::byte, 96>& h, std::size_t off, std::uint32_t v) {
    if constexpr (std::endian::native != std::endian::little) {
        v = std::byteswap(v);
    }
    std::memcpy(h.data() + off, &v, 4);
}

void poke_u32(std::span<std::byte> s, std::size_t off, std::uint32_t v) {
    if constexpr (std::endian::native != std::endian::little) {
        v = std::byteswap(v);
    }
    std::memcpy(s.data() + off, &v, 4);
}

int popcnt(std::uint32_t x) { return std::popcount(x); }

bool neighborhood_path(const std::filesystem::path& p) {
    auto e = p.extension().string();
    for (char& c : e) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return e == ".nhd" || e == ".world" || e == ".dbc";
}

VoidResult replace_file(const std::filesystem::path& dest, const std::filesystem::path& tmp) {
#ifdef _WIN32
    if (std::filesystem::exists(dest)) {
        const auto bak = std::filesystem::path(dest.native() + L".bak");
        if (!ReplaceFileW(dest.c_str(), tmp.c_str(), bak.c_str(), REPLACEFILE_WRITE_THROUGH,
                          nullptr, nullptr)) {
            if (!MoveFileExW(tmp.c_str(), dest.c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                return std::unexpected(err(ErrorCode::io, "ReplaceFile/MoveFile failed"));
            }
        }
        return ok();
    }
    if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_WRITE_THROUGH)) {
        return std::unexpected(err(ErrorCode::io, "MoveFile failed"));
    }
    return ok();
#else
    std::error_code ec;
    if (std::filesystem::exists(dest)) {
        std::filesystem::rename(dest, dest.native() + ".bak", ec);
    }
    std::filesystem::rename(tmp, dest, ec);
    if (ec) {
        return std::unexpected(err(ErrorCode::io, ec.message()));
    }
    return ok();
#endif
}

}  // namespace

Result<Package> Package::open(const std::filesystem::path& path, bool writable) {
    Package p;
    p.path_ = path;
    p.writable_ = writable;
    auto m = core::MappedFile::open(path, writable);
    if (!m) {
        return std::unexpected(m.error());
    }
    p.map_ = std::move(*m);
    if (auto r = p.parse_mapped(); !r) {
        return std::unexpected(r.error());
    }
    return p;
}

Package Package::create_new() {
    Package p;
    p.writable_ = true;
    p.dirty_ = true;
    p.header_.fill(std::byte{0});
    p.header_[0] = std::byte{'D'};
    p.header_[1] = std::byte{'B'};
    p.header_[2] = std::byte{'P'};
    p.header_[3] = std::byte{'F'};
    wr_u32_at(p.header_, 0x04, 2);
    wr_u32_at(p.header_, 0x08, 0);
    wr_u32_at(p.header_, 0x3C, 3);
    wr_u32_at(p.header_, 0x40, kHeaderSize);
    wr_u32_at(p.header_, 0x2C, 4);
    return p;
}

VoidResult Package::parse_mapped() {
    const auto b = map_.bytes();
    if (b.size() < kHeaderSize) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format, "file too small"));
    }
    if (b[0] != std::byte{'D'} || b[1] != std::byte{'B'} || b[2] != std::byte{'P'} ||
        b[3] != std::byte{'F'}) {
        if (b.size() >= 4 && b[0] == std::byte{'D'} && b[1] == std::byte{'B'} &&
            (b[2] == std::byte{'P'} || b[2] == std::byte{'B'}) &&
            (b[3] == std::byte{'P'} || b[3] == std::byte{'F'})) {
            return std::unexpected(
                err(ErrorCode::protected_or_encrypted, "protected or non-DBPF magic"));
        }
        return std::unexpected(err(ErrorCode::unsupported_game_or_format, "not DBPF"));
    }
    const auto major = rd_u32(b, 4);
    if (major != 2) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format, "unsupported DBPF major"));
    }
    std::copy_n(b.begin(), kHeaderSize, header_.begin());
    const auto count = rd_u32(b, 0x24);
    const auto index_size = rd_u32(b, 0x2C);
    const auto index_ver = rd_u32(b, 0x3C);
    const auto index_pos = rd_u32(b, 0x40);
    index_pos_ = index_pos;
    if (index_ver != 3) {
        return std::unexpected(err(ErrorCode::unsupported_game_or_format, "index version"));
    }
    if (count > kMaxIndexEntries) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "index entry cap"));
    }
    if (index_pos > b.size() || index_size > b.size() - index_pos) {
        return std::unexpected(err(ErrorCode::corrupt, "index out of range"));
    }
    auto idx = b.subspan(index_pos, index_size);
    if (idx.size() < 4) {
        return std::unexpected(err(ErrorCode::corrupt, "index truncated"));
    }
    index_type_ = rd_u32(idx, 0);
    const int nshared = popcnt(index_type_ & 0xFFu);
    const int nper = 8 - nshared;
    if (nper < 0) {
        return std::unexpected(err(ErrorCode::corrupt, "bad indexType"));
    }
    std::size_t off = 4;
    std::uint32_t shared[8]{};
    for (int s = 0; s < nshared; ++s) {
        if (off + 4 > idx.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "index header"));
        }
        shared[s] = rd_u32(idx, off);
        off += 4;
    }
    entries_.clear();
    entries_.reserve(count);
    for (std::uint32_t n = 0; n < count; ++n) {
        std::uint32_t per[8]{};
        for (int p = 0; p < nper; ++p) {
            if (off + 4 > idx.size()) {
                return std::unexpected(err(ErrorCode::corrupt, "index entry"));
            }
            per[p] = rd_u32(idx, off);
            off += 4;
        }
        std::uint32_t f[8]{};
        int si = 0, pi = 0;
        for (int bit = 0; bit < 8; ++bit) {
            if (index_type_ & (1u << bit)) {
                f[bit] = shared[si++];
            } else {
                f[bit] = per[pi++];
            }
        }
        IndexEntry e;
        e.tgi.type = f[0];
        e.tgi.group = f[1];
        e.tgi.instance = (static_cast<std::uint64_t>(f[2]) << 32) | f[3];
        e.chunk_offset = f[4];
        e.file_size_high_bit = (f[5] & 0x80000000u) != 0;
        e.file_size = f[5] & 0x7FFFFFFFu;
        e.mem_size = f[6];
        e.compressed = static_cast<std::uint16_t>(f[7] & 0xFFFFu);
        e.unknown2 = static_cast<std::uint16_t>(f[7] >> 16);
        e.ordinal = 0;
        if (e.mem_size > kMaxResourceBytes || e.file_size > kMaxResourceBytes) {
            return std::unexpected(err(ErrorCode::cap_exceeded, "resource size cap"));
        }
        if (static_cast<std::uint64_t>(e.chunk_offset) + e.file_size > map_.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "payload out of range"));
        }
        e.payload_capacity = e.file_size;
        entries_.push_back(e);
    }
    original_count_ = static_cast<std::uint32_t>(entries_.size());
    compute_payload_capacities();
    recompute_ordinals();
    overrides_.assign(entries_.size(), std::nullopt);
    deleted_.assign(entries_.size(), 0);
    dirty_ = false;
    return ok();
}

void Package::compute_payload_capacities() {
    struct Hit {
        std::uint32_t off;
        std::uint32_t i;
    };
    std::vector<Hit> hits;
    hits.reserve(entries_.size());
    for (std::uint32_t i = 0; i < entries_.size(); ++i) {
        hits.push_back({entries_[i].chunk_offset, i});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.off < b.off; });
    const std::uint32_t idx_end = index_pos_ ? index_pos_ : static_cast<std::uint32_t>(map_.size());
    for (std::size_t k = 0; k < hits.size(); ++k) {
        const std::uint32_t next =
            (k + 1 < hits.size()) ? hits[k + 1].off : idx_end;
        auto& e = entries_[hits[k].i];
        if (next > e.chunk_offset) {
            e.payload_capacity = std::max(e.file_size, next - e.chunk_offset);
        } else {
            e.payload_capacity = e.file_size;
        }
    }
}

void Package::recompute_ordinals() {
    struct TgiHash {
        std::size_t operator()(const Tgi& t) const noexcept {
            std::size_t h = static_cast<std::size_t>(t.type);
            h ^= static_cast<std::size_t>(t.group) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(t.instance) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= static_cast<std::size_t>(t.instance >> 32) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<Tgi, std::uint32_t, TgiHash> seen;
    seen.reserve(entries_.size() * 2 + 1);
    for (auto& e : entries_) {
        e.ordinal = seen[e.tgi]++;
    }
}

Result<std::span<const std::byte>> Package::raw(std::uint32_t i) const {
    if (i >= entries_.size()) {
        return std::unexpected(err(ErrorCode::not_found, "index"));
    }
    if (overrides_[i]) {
        return std::span<const std::byte>(*overrides_[i]);
    }
    const auto& e = entries_[i];
    return map_.bytes().subspan(e.chunk_offset, e.file_size);
}

Result<std::vector<std::byte>> Package::peek(std::uint32_t i, std::uint32_t max_bytes) const {
    if (i >= entries_.size()) {
        return std::unexpected(err(ErrorCode::not_found, "index"));
    }
    if (max_bytes == 0) {
        return std::vector<std::byte>{};
    }
    const auto& e = entries_[i];
    if (e.compressed == 0) {
        auto s = raw(i);
        if (!s) {
            return std::unexpected(s.error());
        }
        const auto n = std::min<std::size_t>(max_bytes, s->size());
        return std::vector<std::byte>(s->begin(), s->begin() + static_cast<std::ptrdiff_t>(n));
    }
    if (e.mem_size > sxpe::core::caps::kMaxLivePreviewBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "preview cap"));
    }
    auto u = uncompressed(i);
    if (!u) {
        return std::unexpected(u.error());
    }
    if (u->size() > max_bytes) {
        u->resize(max_bytes);
    }
    return u;
}

Result<std::vector<std::byte>> Package::uncompressed(std::uint32_t i) const {
    auto disk = payload_on_disk(i);
    if (!disk) {
        return std::unexpected(disk.error());
    }
    const auto& e = entries_[i];
    if (e.compressed == 0) {
        return *disk;
    }
    if (e.compressed != 0xFFFF) {
        return std::unexpected(err(ErrorCode::corrupt, "unknown compressed flag"));
    }
    return refpack_decompress(*disk, e.mem_size);
}

Result<std::vector<std::byte>> Package::payload_on_disk(std::uint32_t i) const {
    if (i >= entries_.size()) {
        return std::unexpected(err(ErrorCode::not_found, "index"));
    }
    if (overrides_[i]) {
        return *overrides_[i];
    }
    const auto& e = entries_[i];
    auto s = map_.bytes().subspan(e.chunk_offset, e.file_size);
    return std::vector<std::byte>(s.begin(), s.end());
}

VoidResult Package::set_uncompressed(std::uint32_t i, std::span<const std::byte> data,
                                     bool compress) {
    if (!writable_) {
        return std::unexpected(err(ErrorCode::refused, "read-only"));
    }
    if (i >= entries_.size()) {
        return std::unexpected(err(ErrorCode::not_found, "index"));
    }
    if (data.size() > kMaxResourceBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "resource size"));
    }
    if (compress) {
        auto c = refpack_compress(data);
        if (!c) {
            return std::unexpected(c.error());
        }
        overrides_[i] = std::move(*c);
        entries_[i].compressed = 0xFFFF;
        entries_[i].file_size = static_cast<std::uint32_t>(overrides_[i]->size());
        entries_[i].mem_size = static_cast<std::uint32_t>(data.size());
    } else {
        overrides_[i] = std::vector<std::byte>(data.begin(), data.end());
        entries_[i].compressed = 0;
        entries_[i].file_size = static_cast<std::uint32_t>(data.size());
        entries_[i].mem_size = entries_[i].file_size;
    }
    dirty_ = true;
    return ok();
}

VoidResult Package::patch_in_place(std::uint32_t i, std::span<const std::byte> uncompressed,
                                   bool compress) {
    if (!writable_) {
        return std::unexpected(err(ErrorCode::refused, "read-only"));
    }
    if (i >= entries_.size()) {
        return std::unexpected(err(ErrorCode::not_found, "index"));
    }
    if (index_type_ != 0) {
        return std::unexpected(err(ErrorCode::refused, "in-place replace needs indexType 0"));
    }
    if (map_.writable_bytes().empty()) {
        return std::unexpected(err(ErrorCode::refused, "file is not mapped writable"));
    }
    if (uncompressed.size() > kMaxResourceBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "resource size"));
    }
    std::vector<std::byte> disk;
    if (compress) {
        auto c = refpack_compress(uncompressed);
        if (!c) {
            return std::unexpected(c.error());
        }
        disk = std::move(*c);
    } else {
        disk.assign(uncompressed.begin(), uncompressed.end());
    }
    auto& e = entries_[i];
    if (disk.size() > e.payload_capacity) {
        return std::unexpected(err(ErrorCode::cap_exceeded,
                                   "new resource is " + std::to_string(disk.size()) +
                                       " bytes; in-place hole is " +
                                       std::to_string(e.payload_capacity) +
                                       " bytes. Export a smaller PNG (same pixels, higher PNG compression)."));
    }
    auto mut = map_.writable_bytes();
    const auto start = static_cast<std::size_t>(e.chunk_offset);
    if (start + e.payload_capacity > mut.size()) {
        return std::unexpected(err(ErrorCode::corrupt, "hole out of range"));
    }
    std::memcpy(mut.data() + start, disk.data(), disk.size());
    if (e.payload_capacity > disk.size()) {
        std::memset(mut.data() + start + disk.size(), 0,
                    e.payload_capacity - disk.size());
    }
    e.file_size = static_cast<std::uint32_t>(disk.size());
    e.mem_size = static_cast<std::uint32_t>(uncompressed.size());
    e.compressed = compress ? 0xFFFF : 0;
    const std::size_t rec = static_cast<std::size_t>(index_pos_) + 4 + static_cast<std::size_t>(i) * 32;
    if (rec + 32 > mut.size()) {
        return std::unexpected(err(ErrorCode::corrupt, "index row out of range"));
    }
    poke_u32(mut, rec + 20, e.file_size | (e.file_size_high_bit ? 0x80000000u : 0));
    poke_u32(mut, rec + 24, e.mem_size);
    poke_u32(mut, rec + 28, static_cast<std::uint32_t>(e.compressed) |
                                (static_cast<std::uint32_t>(e.unknown2) << 16));
    overrides_[i].reset();
    bool still = false;
    for (const auto& o : overrides_) {
        if (o) {
            still = true;
            break;
        }
    }
    for (char d : deleted_) {
        if (d) {
            still = true;
            break;
        }
    }
    dirty_ = still;
    map_.flush();
    return ok();
}

Result<std::uint32_t> Package::add(Tgi tgi, std::span<const std::byte> data, bool compress) {
    if (!writable_) {
        return std::unexpected(err(ErrorCode::refused, "read-only"));
    }
    if (entries_.size() >= kMaxIndexEntries) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "index entry cap"));
    }
    IndexEntry e;
    e.tgi = tgi;
    e.ordinal = 0;
    for (const auto& x : entries_) {
        if (x.tgi == tgi) {
            ++e.ordinal;
        }
    }
    entries_.push_back(e);
    overrides_.push_back(std::nullopt);
    deleted_.push_back(0);
    const auto idx = static_cast<std::uint32_t>(entries_.size() - 1);
    if (auto r = set_uncompressed(idx, data, compress); !r) {
        entries_.pop_back();
        overrides_.pop_back();
        deleted_.pop_back();
        return std::unexpected(r.error());
    }
    dirty_ = true;
    return idx;
}

VoidResult Package::write_file(const std::filesystem::path& dest) const {
    std::vector<std::byte> payloads;
    std::vector<IndexEntry> out_e;
    std::uint32_t off = kHeaderSize;
    for (std::uint32_t i = 0; i < entries_.size(); ++i) {
        if (deleted(i)) {
            continue;
        }
        auto disk = payload_on_disk(i);
        if (!disk) {
            return std::unexpected(disk.error());
        }
        if (disk->size() > kMaxResourceBytes) {
            return std::unexpected(err(ErrorCode::cap_exceeded, "payload"));
        }
        auto e = entries_[i];
        e.chunk_offset = off;
        e.file_size = static_cast<std::uint32_t>(disk->size());
        payloads.insert(payloads.end(), disk->begin(), disk->end());
        off += e.file_size;
        out_e.push_back(e);
    }
    std::vector<std::byte> index;
    wr_u32(index, 0);  // indexType = 0
    for (const auto& e : out_e) {
        wr_u32(index, e.tgi.type);
        wr_u32(index, e.tgi.group);
        wr_u32(index, static_cast<std::uint32_t>(e.tgi.instance >> 32));
        wr_u32(index, static_cast<std::uint32_t>(e.tgi.instance));
        wr_u32(index, e.chunk_offset);
        wr_u32(index, e.file_size | (e.file_size_high_bit ? 0x80000000u : 0));
        wr_u32(index, e.mem_size);
        wr_u32(index, static_cast<std::uint32_t>(e.compressed) |
                          (static_cast<std::uint32_t>(e.unknown2) << 16));
    }

    auto hdr = header_;
    wr_u32_at(hdr, 0x24, static_cast<std::uint32_t>(out_e.size()));
    wr_u32_at(hdr, 0x2C, static_cast<std::uint32_t>(index.size()));
    wr_u32_at(hdr, 0x3C, 3);
    wr_u32_at(hdr, 0x40, kHeaderSize + static_cast<std::uint32_t>(payloads.size()));

    std::ofstream f(dest, std::ios::binary | std::ios::trunc);
    if (!f) {
        return std::unexpected(err(ErrorCode::io, "open tmp"));
    }
    f.write(reinterpret_cast<const char*>(hdr.data()), static_cast<std::streamsize>(hdr.size()));
    if (!payloads.empty()) {
        f.write(reinterpret_cast<const char*>(payloads.data()),
                static_cast<std::streamsize>(payloads.size()));
    }
    f.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size()));
    f.flush();
    if (!f) {
        return std::unexpected(err(ErrorCode::io, "write tmp"));
    }
    f.close();
    return ok();
}

bool Package::layout_locked() const { return neighborhood_path(path_); }

VoidResult Package::flush_layout() {
    if (!writable_) {
        return std::unexpected(err(ErrorCode::refused, "read-only"));
    }
    if (index_type_ != 0) {
        return std::unexpected(
            err(ErrorCode::refused, "this package index cannot be saved without a rebuild"));
    }
    if (entries_.size() != original_count_) {
        return std::unexpected(err(ErrorCode::refused,
                                   "adding resources is not supported when saving a neighborhood file"));
    }
    auto mut = map_.writable_bytes();
    if (mut.empty()) {
        return std::unexpected(err(ErrorCode::refused, "file is not mapped writable"));
    }
    for (std::uint32_t i = 0; i < entries_.size(); ++i) {
        if (deleted(i)) {
            return std::unexpected(
                err(ErrorCode::refused, "deleting resources is not supported when saving a neighborhood file"));
        }
        if (!overrides_[i]) {
            continue;
        }
        const auto& disk = *overrides_[i];
        auto& e = entries_[i];
        if (disk.size() > e.payload_capacity) {
            return std::unexpected(err(ErrorCode::cap_exceeded,
                                       "resource is " + std::to_string(disk.size()) +
                                           " bytes; in-place hole is " +
                                           std::to_string(e.payload_capacity)));
        }
        const auto start = static_cast<std::size_t>(e.chunk_offset);
        if (start + e.payload_capacity > mut.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "hole out of range"));
        }
        std::memcpy(mut.data() + start, disk.data(), disk.size());
        if (e.payload_capacity > disk.size()) {
            std::memset(mut.data() + start + disk.size(), 0, e.payload_capacity - disk.size());
        }
        e.file_size = static_cast<std::uint32_t>(disk.size());
        overrides_[i].reset();
    }
    std::size_t rec = static_cast<std::size_t>(index_pos_) + 4;
    for (const auto& e : entries_) {
        if (rec + 32 > mut.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "index row out of range"));
        }
        poke_u32(mut, rec + 0, e.tgi.type);
        poke_u32(mut, rec + 4, e.tgi.group);
        poke_u32(mut, rec + 8, static_cast<std::uint32_t>(e.tgi.instance >> 32));
        poke_u32(mut, rec + 12, static_cast<std::uint32_t>(e.tgi.instance));
        poke_u32(mut, rec + 16, e.chunk_offset);
        poke_u32(mut, rec + 20, e.file_size | (e.file_size_high_bit ? 0x80000000u : 0));
        poke_u32(mut, rec + 24, e.mem_size);
        poke_u32(mut, rec + 28, static_cast<std::uint32_t>(e.compressed) |
                                    (static_cast<std::uint32_t>(e.unknown2) << 16));
        rec += 32;
    }
    dirty_ = false;
    map_.flush();
    return ok();
}

VoidResult Package::save_as(const std::filesystem::path& dest) {
    if (neighborhood_path(path_) || neighborhood_path(dest)) {
        if (auto r = flush_layout(); !r) {
            return r;
        }
        if (dest == path_) {
            return ok();
        }
        map_.close();
        std::error_code ec;
        std::filesystem::copy_file(path_, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return std::unexpected(err(ErrorCode::io, ec.message()));
        }
        path_ = dest;
        writable_ = true;
        auto m = core::MappedFile::open(dest, true);
        if (!m) {
            return std::unexpected(m.error());
        }
        map_ = std::move(*m);
        return parse_mapped();
    }
    auto tmp = dest;
    tmp += ".tmp";
    if (auto r = write_file(tmp); !r) {
        std::filesystem::remove(tmp);
        return r;
    }
    map_.close();
    if (auto r = replace_file(dest, tmp); !r) {
        std::filesystem::remove(tmp);
        return r;
    }
    path_ = dest;
    writable_ = true;
    auto m = core::MappedFile::open(dest, true);
    if (!m) {
        return std::unexpected(m.error());
    }
    map_ = std::move(*m);
    return parse_mapped();
}

VoidResult Package::save() {
    if (path_.empty()) {
        return std::unexpected(err(ErrorCode::refused, "no path; use save_as"));
    }
    if (!writable_) {
        return std::unexpected(err(ErrorCode::refused, "read-only"));
    }
    if (neighborhood_path(path_)) {
        return flush_layout();
    }
    return save_as(path_);
}

std::uint32_t Package::major() const {
    return rd_u32(std::span<const std::byte>(header_), 0x04);
}
std::uint32_t Package::minor() const {
    return rd_u32(std::span<const std::byte>(header_), 0x08);
}
std::uint32_t Package::index_version() const {
    return rd_u32(std::span<const std::byte>(header_), 0x3C);
}

std::uint32_t Package::compressed_count() const {
    std::uint32_t n = 0;
    for (const auto& e : entries_) {
        if (e.compressed == 0xFFFF) {
            ++n;
        }
    }
    return n;
}

std::uint32_t Package::deleted_count() const {
    std::uint32_t n = 0;
    for (char d : deleted_) {
        if (d) {
            ++n;
        }
    }
    return n;
}

bool Package::dir_present() const {
    constexpr std::uint32_t kDir = 0xE86B1EEF;
    for (const auto& e : entries_) {
        if (e.tgi.type == kDir) {
            return true;
        }
    }
    return false;
}

std::optional<std::uint32_t> Package::find(Tgi tgi, std::uint32_t ordinal) const {
    for (std::uint32_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].tgi == tgi && entries_[i].ordinal == ordinal) {
            return i;
        }
    }
    return std::nullopt;
}

bool Package::deleted(std::uint32_t i) const {
    return i < deleted_.size() && deleted_[i] != 0;
}

VoidResult Package::set_deleted(std::uint32_t i, bool del) {
    if (!writable_) {
        return std::unexpected(err(ErrorCode::refused, "read-only"));
    }
    if (i >= entries_.size()) {
        return std::unexpected(err(ErrorCode::not_found, "index"));
    }
    deleted_[i] = del ? 1 : 0;
    dirty_ = true;
    return ok();
}

VoidResult Package::remove(std::uint32_t i) {
    if (!writable_) {
        return std::unexpected(err(ErrorCode::refused, "read-only"));
    }
    if (i >= entries_.size()) {
        return std::unexpected(err(ErrorCode::not_found, "index"));
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
    overrides_.erase(overrides_.begin() + static_cast<std::ptrdiff_t>(i));
    deleted_.erase(deleted_.begin() + static_cast<std::ptrdiff_t>(i));
    recompute_ordinals();
    dirty_ = true;
    return ok();
}

Result<std::uint32_t> Package::duplicate(std::uint32_t i) {
    auto body = uncompressed(i);
    if (!body) {
        return std::unexpected(body.error());
    }
    const auto tgi = entries_[i].tgi;
    const bool compress = entries_[i].compressed == 0xFFFF;
    return add(tgi, *body, compress);
}

VoidResult Package::rekey(std::uint32_t i, Tgi tgi) {
    if (!writable_) {
        return std::unexpected(err(ErrorCode::refused, "read-only"));
    }
    if (i >= entries_.size()) {
        return std::unexpected(err(ErrorCode::not_found, "index"));
    }
    entries_[i].tgi = tgi;
    recompute_ordinals();
    dirty_ = true;
    return ok();
}

VoidResult Package::save_copy_as(const std::filesystem::path& dest) {
    if (dest.empty()) {
        return std::unexpected(err(ErrorCode::invalid_argument, "empty dest"));
    }
    if (neighborhood_path(path_) || neighborhood_path(dest)) {
        if (auto r = flush_layout(); !r) {
            return r;
        }
        std::error_code ec;
        std::filesystem::copy_file(path_, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return std::unexpected(err(ErrorCode::io, ec.message()));
        }
        return ok();
    }
    auto tmp = dest;
    tmp += ".tmp";
    if (auto r = write_file(tmp); !r) {
        std::filesystem::remove(tmp);
        return r;
    }
    if (auto r = replace_file(dest, tmp); !r) {
        std::filesystem::remove(tmp);
        return r;
    }
    return ok();
}

}  // namespace sxpe::games::sims3
