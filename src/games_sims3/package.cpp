#include "sxpe/games/sims3/package.hpp"

#include "sxpe/core/caps.hpp"
#include "sxpe/games/sims3/refpack.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>

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

int popcnt(std::uint32_t x) { return std::popcount(x); }

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
        entries_.push_back(e);
    }
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        std::uint32_t ord = 0;
        for (std::size_t j = 0; j < i; ++j) {
            if (entries_[j].tgi == entries_[i].tgi) {
                ++ord;
            }
        }
        entries_[i].ordinal = ord;
    }
    overrides_.assign(entries_.size(), std::nullopt);
    return ok();
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
    const auto idx = static_cast<std::uint32_t>(entries_.size() - 1);
    if (auto r = set_uncompressed(idx, data, compress); !r) {
        entries_.pop_back();
        overrides_.pop_back();
        return std::unexpected(r.error());
    }
    return idx;
}

VoidResult Package::write_file(const std::filesystem::path& dest) const {
    std::vector<std::byte> payloads;
    std::vector<IndexEntry> out_e = entries_;
    std::uint32_t off = kHeaderSize;
    for (std::uint32_t i = 0; i < entries_.size(); ++i) {
        auto disk = payload_on_disk(i);
        if (!disk) {
            return std::unexpected(disk.error());
        }
        if (disk->size() > kMaxResourceBytes) {
            return std::unexpected(err(ErrorCode::cap_exceeded, "payload"));
        }
        out_e[i].chunk_offset = off;
        out_e[i].file_size = static_cast<std::uint32_t>(disk->size());
        payloads.insert(payloads.end(), disk->begin(), disk->end());
        off += out_e[i].file_size;
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

VoidResult Package::save_as(const std::filesystem::path& dest) {
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
    return save_as(path_);
}

}  // namespace sxpe::games::sims3
