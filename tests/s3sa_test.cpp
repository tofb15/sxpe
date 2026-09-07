#include "check.hpp"
#include "sxpe/resources/s3sa.hpp"

#include <cstring>
#include <vector>

namespace {

void wu32(std::vector<std::byte>& o, std::uint32_t v) {
    const auto* p = reinterpret_cast<const std::byte*>(&v);
    o.insert(o.end(), p, p + 4);
}

std::vector<std::byte> tiny_pe(std::size_t raw) {
    std::vector<std::byte> pe(0x200 + raw, std::byte{0});
    pe[0] = std::byte{'M'};
    pe[1] = std::byte{'Z'};
    const std::uint32_t e_lfanew = 0x80;
    std::memcpy(pe.data() + 0x3C, &e_lfanew, 4);
    pe[0x80] = std::byte{'P'};
    pe[0x81] = std::byte{'E'};
    std::uint16_t nsec = 1;
    std::memcpy(pe.data() + 0x86, &nsec, 2);
    std::uint16_t opt = 0;
    std::memcpy(pe.data() + 0x94, &opt, 2);
    const std::uint32_t size_raw = static_cast<std::uint32_t>(raw);
    const std::uint32_t ptr = 0x200;
    std::memcpy(pe.data() + 0x80 + 24 + 16, &size_raw, 4);
    std::memcpy(pe.data() + 0x80 + 24 + 20, &ptr, 4);
    pe.resize(0x200 + raw);
    pe[0x200] = std::byte{0xCA};
    return pe;
}

}  // namespace

int main() {
    using namespace sxpe::resources;

    std::vector<std::byte> mz{std::byte{'M'}, std::byte{'Z'}, std::byte{1}, std::byte{2}};
    auto wrapped = wrap_s3sa_v1(mz);
    CHECK(wrapped.has_value());
    auto parsed = parse_s3sa(*wrapped);
    CHECK(parsed.has_value());
    CHECK(parsed->version == 1);
    CHECK(parsed->block_count == 1);
    CHECK(parsed->key_table.size() == 1);
    CHECK(parsed->key_table[0] == 0);
    auto plain = decrypt_s3sa(*parsed);
    CHECK(plain.has_value());
    CHECK(plain->size() == 512);
    CHECK((*plain)[0] == std::byte{'M'} && (*plain)[1] == std::byte{'Z'});
    CHECK((*plain)[2] == std::byte{1} && (*plain)[3] == std::byte{2});

    auto bad = wrap_s3sa_v1(std::vector<std::byte>{std::byte{0}, std::byte{1}});
    CHECK(!bad.has_value());

    auto pe = tiny_pe(16);
    CHECK(pe_file_size(pe) == 0x200 + 16);
    auto wpe = wrap_s3sa_v1(pe);
    CHECK(wpe.has_value());
    auto back = export_pe(*wpe);
    CHECK(back.has_value());
    CHECK(*back == pe);

    auto inf = inspect_s3sa(*wpe, "mod");
    CHECK(inf.parsed);
    CHECK(inf.version == 1);
    CHECK(inf.key_table_zero);
    CHECK(inf.checksum_zero);
    CHECK(inf.pe_offset == 0);
    CHECK(inf.assembly_bytes == pe.size());
    CHECK(inf.module_hint == "mod.dll");

    // v2 header: version 2 + empty gameVersion + rest of a v1 wrap without the version byte
    auto v1 = *wrapped;
    std::vector<std::byte> v2;
    v2.push_back(std::byte{2});
    wu32(v2, 0);  // zero chars
    v2.insert(v2.end(), v1.begin() + 1, v1.end());
    auto pv2 = parse_s3sa(v2);
    CHECK(pv2.has_value());
    CHECK(pv2->version == 2);
    CHECK(pv2->game_version.empty());

    // non-zero key table, one block, bit0 clear
    S3saParsed enc;
    enc.version = 1;
    enc.checksum_type = 0x2BC4F79Fu;
    enc.checksum.assign(64, std::byte{0});
    enc.block_count = 1;
    enc.key_table = {0x0102030405060708ull};
    std::vector<std::byte> src(512, std::byte{0x11});
    src[0] = std::byte{'M'};
    src[1] = std::byte{'Z'};
    const std::size_t klen = 8;
    std::uint64_t sum = enc.key_table[0];
    std::size_t seed = static_cast<std::size_t>(sum & (klen - 1));
    std::vector<std::byte> keyb(8);
    std::memcpy(keyb.data(), &enc.key_table[0], 8);
    enc.cipher.resize(512);
    for (std::size_t i = 0; i < 512; ++i) {
        const auto pb = static_cast<std::uint8_t>(src[i]);
        const auto cb = static_cast<std::uint8_t>(pb ^ static_cast<std::uint8_t>(keyb[seed]));
        enc.cipher[i] = std::byte{cb};
        seed = (seed + cb) % klen;
    }
    auto dec = decrypt_s3sa(enc);
    CHECK(dec.has_value());
    CHECK(*dec == src);

    CHECK(!parse_s3sa(std::span<const std::byte>{}).has_value());

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
