#include "check.hpp"
#include "sxpe/core/caps.hpp"
#include "sxpe/games/sims3/package.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace {

using sxpe::ErrorCode;
using sxpe::games::sims3::Package;
using sxpe::games::sims3::Tgi;

const std::filesystem::path kSynth{SXPE_SYNTHETIC_DIR};

void write_bytes(const std::filesystem::path& p, std::span<const std::byte> b) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

std::vector<std::byte> fake_header(std::uint32_t count, std::uint32_t major = 2) {
    std::vector<std::byte> h(96, std::byte{0});
    h[0] = std::byte{'D'};
    h[1] = std::byte{'B'};
    h[2] = std::byte{'P'};
    h[3] = std::byte{'F'};
    std::uint32_t v = major;
    std::memcpy(h.data() + 4, &v, 4);
    std::memcpy(h.data() + 0x24, &count, 4);
    v = 4;
    std::memcpy(h.data() + 0x2C, &v, 4);
    v = 3;
    std::memcpy(h.data() + 0x3C, &v, 4);
    v = 96;
    std::memcpy(h.data() + 0x40, &v, 4);
    h.resize(100);
    return h;
}

std::string as_text(std::span<const std::byte> b) {
    return {reinterpret_cast<const char*>(b.data()), b.size()};
}

}  // namespace

int main() {
    auto empty = Package::open(kSynth / "empty.bin", false);
    CHECK(empty.has_value());
    if (empty) {
        CHECK(empty->count() == 0);
    }

    auto one = Package::open(kSynth / "single-blob.bin", false);
    CHECK(one.has_value());
    if (one) {
        CHECK(one->count() == 1);
        CHECK(one->entry(0).tgi.instance == 1);
        CHECK(one->entry(0).compressed == 0);
        auto body = one->uncompressed(0);
        CHECK(body.has_value());
        if (body) {
            CHECK(as_text(*body) == "Hello SXPE\n");
        }
    }

    auto tmp = std::filesystem::temp_directory_path() / "sxpe-m2";
    std::error_code ec;
    std::filesystem::create_directories(tmp, ec);

    auto junk = tmp / "not-dbpf.bin";
    const std::byte bad[] = {std::byte{'X'}, std::byte{'X'}, std::byte{'X'}, std::byte{'X'}};
    write_bytes(junk, bad);
    auto refused = Package::open(junk, false);
    CHECK(!refused);
    if (!refused) {
        CHECK(refused.error().code == ErrorCode::unsupported_game_or_format);
    }

    auto dbpp = tmp / "dbpp.bin";
    auto dbpp_bytes = fake_header(0);
    dbpp_bytes[3] = std::byte{'P'};
    write_bytes(dbpp, dbpp_bytes);
    auto prot = Package::open(dbpp, false);
    CHECK(!prot);
    if (!prot) {
        CHECK(prot.error().code == ErrorCode::protected_or_encrypted);
    }

    auto over = tmp / "overcap.bin";
    auto over_bytes = fake_header(sxpe::core::caps::kMaxIndexEntries + 1);
    write_bytes(over, over_bytes);
    auto over_pkg = Package::open(over, false);
    CHECK(!over_pkg);
    if (!over_pkg) {
        CHECK(over_pkg.error().code == ErrorCode::cap_exceeded);
    }

    auto tiny = tmp / "tiny.bin";
    write_bytes(tiny, std::span<const std::byte>(bad));
    auto too_small = Package::open(tiny, false);
    CHECK(!too_small);

    auto out = tmp / "roundtrip.bin";
    {
        auto created = Package::create_new();
        const char hello[] = "Hello SXPE\n";
        auto payload = std::as_bytes(std::span{hello, sizeof(hello) - 1});
        auto added = created.add(Tgi{0, 0, 1}, payload, false);
        CHECK(added.has_value());
        CHECK(created.save_as(out).has_value());
        CHECK(created.count() == 1);
        auto live = created.uncompressed(0);
        CHECK(live.has_value() && as_text(*live) == "Hello SXPE\n");
    }
    auto again = Package::open(out, false);
    CHECK(again.has_value());
    if (again) {
        CHECK(again->count() == 1);
        auto body = again->uncompressed(0);
        CHECK(body.has_value() && as_text(*body) == "Hello SXPE\n");
    }

    auto inplace = tmp / "inplace.bin";
    std::filesystem::copy_file(kSynth / "single-blob.bin", inplace,
                               std::filesystem::copy_options::overwrite_existing, ec);
    {
        auto w = Package::open(inplace, true);
        CHECK(w.has_value());
        if (w) {
            CHECK(w->save().has_value());
            auto body = w->uncompressed(0);
            CHECK(body.has_value() && as_text(*body) == "Hello SXPE\n");
        }
    }
    auto reread = Package::open(inplace, false);
    CHECK(reread.has_value());
    if (reread) {
        auto body = reread->uncompressed(0);
        CHECK(body.has_value() && as_text(*body) == "Hello SXPE\n");
    }

    auto hole = tmp / "hole.bin";
    std::filesystem::copy_file(out, hole, std::filesystem::copy_options::overwrite_existing, ec);
    {
        auto w = Package::open(hole, true);
        CHECK(w.has_value());
        if (w) {
            const char hi[] = "Hi";
            auto payload = std::as_bytes(std::span{hi, sizeof(hi) - 1});
            CHECK(w->patch_in_place(0, payload, false).has_value());
            CHECK(w->dirty() == false);
            CHECK(w->count() == 1);
        }
    }
    auto nhd = tmp / "layout.nhd";
    std::filesystem::copy_file(out, nhd, std::filesystem::copy_options::overwrite_existing, ec);
    const auto nhd_sz = std::filesystem::file_size(nhd);
    {
        auto w = Package::open(nhd, true);
        CHECK(w.has_value());
        if (w) {
            const char hi[] = "Hi";
            auto payload = std::as_bytes(std::span{hi, sizeof(hi) - 1});
            CHECK(w->set_uncompressed(0, payload, false).has_value());
            CHECK(w->save().has_value());
            CHECK(w->dirty() == false);
            CHECK(std::filesystem::file_size(nhd) == nhd_sz);
        }
    }
    auto nhd_r = Package::open(nhd, false);
    CHECK(nhd_r.has_value());
    if (nhd_r) {
        CHECK(nhd_r->count() == 1);
        auto body = nhd_r->uncompressed(0);
        CHECK(body.has_value() && as_text(*body) == "Hi");
    }

    auto hole_r = Package::open(hole, false);
    CHECK(hole_r.has_value());
    if (hole_r) {
        CHECK(hole_r->count() == 1);
        auto body = hole_r->uncompressed(0);
        CHECK(body.has_value());
        if (body && body->size() >= 2) {
            CHECK((*body)[0] == std::byte{'H'} && (*body)[1] == std::byte{'i'});
        }
        const char too[] = "Hello SXPE!! extra";
        auto big = std::as_bytes(std::span{too, sizeof(too) - 1});
        auto w2 = Package::open(hole, true);
        CHECK(w2.has_value());
        if (w2) {
            auto refused_big = w2->patch_in_place(0, big, false);
            CHECK(!refused_big);
        }
    }

    auto cpath = tmp / "compressed.bin";
    std::vector<std::byte> copied;
    {
        auto cpkg = Package::create_new();
        const char msg[] = "compress me please!!";
        auto cadd = cpkg.add(Tgi{1, 2, 3}, std::as_bytes(std::span{msg, sizeof(msg) - 1}), true);
        CHECK(cadd.has_value());
        CHECK(cpkg.save_as(cpath).has_value());
        CHECK(cpkg.entry(0).compressed == 0xFFFF);
        auto raw1 = cpkg.raw(0);
        CHECK(raw1.has_value());
        if (raw1) {
            copied.assign(raw1->begin(), raw1->end());
        }
        auto plain = cpkg.uncompressed(0);
        CHECK(plain.has_value() && as_text(*plain) == msg);
        CHECK(cpkg.save().has_value());
        auto raw_after = cpkg.raw(0);
        CHECK(raw_after.has_value());
        if (raw_after) {
            CHECK(std::vector<std::byte>(raw_after->begin(), raw_after->end()) == copied);
        }
    }
    auto c2 = Package::open(cpath, false);
    CHECK(c2.has_value());
    if (c2) {
        CHECK(c2->entry(0).compressed == 0xFFFF);
        auto raw2 = c2->raw(0);
        CHECK(raw2.has_value());
        if (raw2) {
            CHECK(std::vector<std::byte>(raw2->begin(), raw2->end()) == copied);
        }
        auto plain = c2->uncompressed(0);
        CHECK(plain.has_value() && as_text(*plain) == "compress me please!!");
    }

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
