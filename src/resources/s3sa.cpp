#include "sxpe/resources/s3sa.hpp"

#include "sxpe/core/caps.hpp"

#include <cstring>

namespace sxpe::resources {
namespace {

std::uint16_t ru16(std::span<const std::byte> s, std::size_t o) {
    std::uint16_t v = 0;
    std::memcpy(&v, s.data() + o, 2);
    return v;
}
std::uint32_t ru32(std::span<const std::byte> s, std::size_t o) {
    std::uint32_t v = 0;
    std::memcpy(&v, s.data() + o, 4);
    return v;
}
std::uint64_t ru64(std::span<const std::byte> s, std::size_t o) {
    std::uint64_t v = 0;
    std::memcpy(&v, s.data() + o, 8);
    return v;
}
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

bool mz(std::span<const std::byte> b, std::size_t i) {
    return i + 1 < b.size() && b[i] == std::byte{'M'} && b[i + 1] == std::byte{'Z'};
}

}  // namespace

std::optional<std::size_t> pe_file_size(std::span<const std::byte> pe) {
    if (!mz(pe, 0) || pe.size() < 0x40) {
        return std::nullopt;
    }
    const auto e_lfanew = ru32(pe, 0x3C);
    if (e_lfanew + 24 > pe.size()) {
        return std::nullopt;
    }
    if (pe[e_lfanew] != std::byte{'P'} || pe[e_lfanew + 1] != std::byte{'E'} ||
        pe[e_lfanew + 2] != std::byte{0} || pe[e_lfanew + 3] != std::byte{0}) {
        return std::nullopt;
    }
    const auto nsec = ru16(pe, e_lfanew + 6);
    const auto opt = ru16(pe, e_lfanew + 20);
    const std::size_t sect = static_cast<std::size_t>(e_lfanew) + 24 + opt;
    if (nsec == 0 || nsec > 96) {
        return std::nullopt;
    }
    if (sect + static_cast<std::size_t>(nsec) * 40 > pe.size()) {
        return std::nullopt;
    }
    std::size_t end = 0;
    for (std::uint16_t i = 0; i < nsec; ++i) {
        const auto raw = ru32(pe, sect + i * 40 + 16);
        const auto ptr = ru32(pe, sect + i * 40 + 20);
        const auto e = static_cast<std::size_t>(ptr) + raw;
        if (e > end) {
            end = e;
        }
    }
    if (end == 0 || end > pe.size()) {
        return std::nullopt;
    }
    return end;
}

Result<S3saParsed> parse_s3sa(std::span<const std::byte> bytes) {
    if (bytes.size() > sxpe::core::caps::kMaxResourceBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "s3sa size"));
    }
    if (bytes.empty()) {
        return std::unexpected(err(ErrorCode::corrupt, "s3sa empty"));
    }
    S3saParsed p;
    std::size_t o = 0;
    p.version = static_cast<std::uint8_t>(bytes[o++]);
    if (p.version == 0 || p.version > 2) {
        return std::unexpected(err(ErrorCode::corrupt, "s3sa version"));
    }
    if (p.version >= 2) {
        if (o + 4 > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "s3sa gameVersion"));
        }
        const auto nchars = ru32(bytes, o);
        o += 4;
        if (nchars > 256 || o + nchars * 2 > bytes.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "s3sa gameVersion chars"));
        }
        p.game_version.resize(nchars);
        for (std::uint32_t i = 0; i < nchars; ++i) {
            const auto cu = ru16(bytes, o + i * 2);
            p.game_version[i] = static_cast<char>(cu < 128 ? cu : '?');
        }
        o += nchars * 2;
    }
    if (o + 4 + 64 + 2 > bytes.size()) {
        return std::unexpected(err(ErrorCode::corrupt, "s3sa header"));
    }
    p.checksum_type = ru32(bytes, o);
    o += 4;
    p.checksum.assign(bytes.begin() + static_cast<std::ptrdiff_t>(o),
                      bytes.begin() + static_cast<std::ptrdiff_t>(o + 64));
    o += 64;
    p.block_count = ru16(bytes, o);
    o += 2;
    if (p.block_count == 0) {
        return std::unexpected(err(ErrorCode::corrupt, "s3sa blockCount 0"));
    }
    const std::size_t keys = static_cast<std::size_t>(p.block_count) * 8;
    const std::size_t cipher = static_cast<std::size_t>(p.block_count) * 512;
    if (o + keys + cipher != bytes.size()) {
        return std::unexpected(err(ErrorCode::corrupt, "s3sa cipher size"));
    }
    p.key_table.resize(p.block_count);
    for (std::uint16_t i = 0; i < p.block_count; ++i) {
        p.key_table[i] = ru64(bytes, o);
        o += 8;
    }
    p.cipher.assign(bytes.begin() + static_cast<std::ptrdiff_t>(o), bytes.end());
    return p;
}

Result<std::vector<std::byte>> decrypt_s3sa(const S3saParsed& p) {
    const std::size_t klen = p.key_table.size() * 8;
    if (klen == 0 || p.cipher.size() != static_cast<std::size_t>(p.block_count) * 512) {
        return std::unexpected(err(ErrorCode::corrupt, "s3sa decrypt size"));
    }
    std::vector<std::byte> key_bytes(klen);
    std::memcpy(key_bytes.data(), p.key_table.data(), klen);
    std::uint64_t sum = 0;
    for (auto w : p.key_table) {
        sum += w;
    }
    std::size_t seed = static_cast<std::size_t>(sum & (klen - 1));
    std::vector<std::byte> out;
    out.reserve(p.cipher.size());
    std::size_t ci = 0;
    for (auto word : p.key_table) {
        if (word & 1ull) {
            out.insert(out.end(), 512, std::byte{0});
            continue;
        }
        if (ci + 512 > p.cipher.size()) {
            return std::unexpected(err(ErrorCode::corrupt, "s3sa cipher truncated"));
        }
        for (std::size_t i = 0; i < 512; ++i) {
            const auto b = static_cast<std::uint8_t>(p.cipher[ci + i]);
            const auto x = static_cast<std::uint8_t>(key_bytes[seed]);
            out.push_back(std::byte{static_cast<std::uint8_t>(b ^ x)});
            seed = (seed + b) % klen;
        }
        ci += 512;
    }
    return out;
}

Result<std::vector<std::byte>> wrap_s3sa_v1(std::span<const std::byte> pe) {
    if (pe.size() < 2 || pe[0] != std::byte{'M'} || pe[1] != std::byte{'Z'}) {
        return std::unexpected(err(ErrorCode::invalid_argument, "PE must start with MZ"));
    }
    if (pe.size() > sxpe::core::caps::kMaxResourceBytes) {
        return std::unexpected(err(ErrorCode::cap_exceeded, "s3sa pe size"));
    }
    const auto blocks = static_cast<std::uint16_t>((pe.size() + 511) / 512);
    if (blocks == 0) {
        return std::unexpected(err(ErrorCode::invalid_argument, "empty PE"));
    }
    std::vector<std::byte> o;
    wu8(o, 1);
    wu32(o, 0x2BC4F79Fu);
    o.insert(o.end(), 64, std::byte{0});
    wu16(o, blocks);
    for (std::uint16_t i = 0; i < blocks; ++i) {
        wu64(o, 0);
    }
    o.insert(o.end(), pe.begin(), pe.end());
    const auto need = static_cast<std::size_t>(blocks) * 512;
    if (o.size() < 71 + static_cast<std::size_t>(blocks) * 8 + need) {
        o.insert(o.end(), 71 + static_cast<std::size_t>(blocks) * 8 + need - o.size(), std::byte{0});
    }
    return o;
}

Result<std::vector<std::byte>> export_pe(std::span<const std::byte> resource) {
    auto p = parse_s3sa(resource);
    if (!p) {
        return std::unexpected(p.error());
    }
    auto plain = decrypt_s3sa(*p);
    if (!plain) {
        return std::unexpected(plain.error());
    }
    if (const auto n = pe_file_size(*plain)) {
        if (*n <= plain->size()) {
            plain->resize(*n);
        }
    }
    return plain;
}

S3saInfo inspect_s3sa(std::span<const std::byte> bytes, std::string_view nmap_name) {
    S3saInfo inf;
    inf.size = bytes.size();
    inf.module_hint = std::string(nmap_name);
    if (inf.module_hint.empty()) {
        inf.module_hint = "assembly.dll";
    } else if (inf.module_hint.find('.') == std::string::npos) {
        inf.module_hint += ".dll";
    }
    if (auto p = parse_s3sa(bytes)) {
        inf.parsed = true;
        inf.version = p->version;
        inf.game_version = p->game_version;
        inf.checksum_type = p->checksum_type;
        inf.block_count = p->block_count;
        inf.checksum_zero = true;
        for (auto b : p->checksum) {
            if (b != std::byte{0}) {
                inf.checksum_zero = false;
                break;
            }
        }
        inf.key_table_zero = true;
        for (auto w : p->key_table) {
            if (w != 0) {
                inf.key_table_zero = false;
                break;
            }
        }
        if (auto plain = decrypt_s3sa(*p)) {
            if (const auto n = pe_file_size(*plain)) {
                inf.assembly_bytes = *n;
            } else {
                inf.assembly_bytes = plain->size();
            }
            if (mz(*plain, 0)) {
                inf.pe_offset = 0;
            } else {
                for (std::size_t i = 0; i + 1 < plain->size(); ++i) {
                    if (mz(*plain, i)) {
                        inf.pe_offset = i;
                        break;
                    }
                }
            }
        }
        return inf;
    }
    for (std::size_t i = 0; i + 1 < bytes.size(); ++i) {
        if (mz(bytes, i)) {
            inf.pe_offset = i;
            break;
        }
    }
    return inf;
}

}  // namespace sxpe::resources
