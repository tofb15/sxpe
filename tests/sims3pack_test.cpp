#include "check.hpp"
#include "sxpe/games/sims3/package.hpp"
#include "sxpe/games/sims3/sims3pack.hpp"

#include <filesystem>
#include <fstream>

#ifndef SXPE_SYNTHETIC_DIR
#error "SXPE_SYNTHETIC_DIR required"
#endif

int main() {
    namespace fs = std::filesystem;
    const fs::path syn = SXPE_SYNTHETIC_DIR;
    const fs::path pack = syn / "minimal.sims3pack";
    CHECK(fs::exists(pack));

    auto opened = sxpe::games::sims3::open_sims3pack(pack);
    CHECK(opened.has_value());
    CHECK(opened->header_version == 0x0101);
    CHECK(opened->display_name == "SXPE Synthetic");
    CHECK(opened->entries.size() == 1);
    CHECK(opened->entries[0].name == "0x0000000000000001.package");
    CHECK(opened->entries[0].looks_like_package);
    CHECK(opened->entries[0].offset == 0);
    CHECK(opened->entries[0].length > 96);

    // Refuse path with ..
    auto bad = sxpe::games::sims3::open_sims3pack("../nope.sims3pack");
    // open itself does not check_path — bus does; still may fail not_found
    (void)bad;

    // DBPF-at-start is not a Sims3Pack
    auto dbpf = sxpe::games::sims3::open_sims3pack(syn / "single-blob.bin");
    CHECK(!dbpf.has_value());
    CHECK(dbpf.error().code == sxpe::ErrorCode::unsupported_game_or_format);

    const auto out = fs::temp_directory_path() / "sxpe-sims3pack-test-out";
    fs::remove_all(out);
    auto written = sxpe::games::sims3::extract_sims3pack_entry(*opened, 0, out, true);
    CHECK(written.has_value());
    CHECK(fs::exists(*written));
    CHECK(written->filename() == "0x0000000000000001.package");

    auto pkg = sxpe::games::sims3::Package::open(*written, false);
    CHECK(pkg.has_value());
    CHECK(pkg->count() == 1);
    auto payload = pkg->uncompressed(0);
    CHECK(payload.has_value());
    const std::string hello(reinterpret_cast<const char*>(payload->data()), payload->size());
    CHECK(hello == "Hello SXPE\n");

    // no overwrite without force
    auto again = sxpe::games::sims3::extract_sims3pack_entry(*opened, 0, out, false);
    CHECK(!again.has_value());
    CHECK(again.error().code == sxpe::ErrorCode::refused);

    fs::remove_all(out);

    // --- pack → list → extract round-trip (issue #58) ---
    const fs::path src_dir = fs::temp_directory_path() / "sxpe-sims3pack-pack-src";
    const fs::path packed = fs::temp_directory_path() / "sxpe-sims3pack-pack-out.sims3pack";
    const fs::path pack_extract = fs::temp_directory_path() / "sxpe-sims3pack-pack-extract";
    fs::remove_all(src_dir);
    fs::remove_all(pack_extract);
    fs::remove(packed);
    fs::create_directories(src_dir);
    // Reuse the extracted synthetic .package as pack input.
    auto extracted = sxpe::games::sims3::extract_sims3pack_entry(*opened, 0, src_dir, true);
    CHECK(extracted.has_value());

    auto items = sxpe::games::sims3::collect_sims3pack_packages(src_dir);
    CHECK(items.has_value());
    CHECK(items->size() == 1);

    sxpe::games::sims3::Sims3PackCreateOptions opts;
    opts.display_name = "SXPE Pack Roundtrip";
    opts.description = "pack→list→extract";
    opts.package_id = "sxpe-pack-rt-0001";
    opts.package_type = "Object";
    opts.package_subtype = "0x00000000";

    auto made = sxpe::games::sims3::pack_sims3pack(packed, *items, opts, true);
    CHECK(made.has_value());
    CHECK(made->display_name == "SXPE Pack Roundtrip");
    CHECK(made->entries.size() == 1);
    CHECK(made->entries[0].name == extracted->filename().string());
    CHECK(made->entries[0].looks_like_package);

    // no overwrite without force
    auto again_pack = sxpe::games::sims3::pack_sims3pack(packed, *items, opts, false);
    CHECK(!again_pack.has_value());
    CHECK(again_pack.error().code == sxpe::ErrorCode::refused);

    auto listed = sxpe::games::sims3::open_sims3pack(packed);
    CHECK(listed.has_value());
    CHECK(listed->package_id == "sxpe-pack-rt-0001");
    CHECK(listed->entries.size() == 1);

    auto round = sxpe::games::sims3::extract_sims3pack_entry(*listed, 0, pack_extract, true);
    CHECK(round.has_value());
    auto pkg2 = sxpe::games::sims3::Package::open(*round, false);
    CHECK(pkg2.has_value());
    auto payload2 = pkg2->uncompressed(0);
    CHECK(payload2.has_value());
    const std::string hello2(reinterpret_cast<const char*>(payload2->data()), payload2->size());
    CHECK(hello2 == "Hello SXPE\n");

    // metaXml subset
    const fs::path meta_xml = src_dir / "meta.xml";
    {
        std::ofstream m(meta_xml);
        m << "<?xml version=\"1.0\"?>"
             "<Sims3Package Type=\"CasPart\" SubType=\"0x00000004\">"
             "<ArchiveVersion>1.4</ArchiveVersion>"
             "<DisplayName>From Meta</DisplayName>"
             "<Description>meta subset</Description>"
             "<PackageId>meta-id</PackageId>"
             "</Sims3Package>";
    }
    auto meta_opts = sxpe::games::sims3::load_sims3pack_meta_subset(meta_xml);
    CHECK(meta_opts.has_value());
    CHECK(meta_opts->display_name == "From Meta");
    CHECK(meta_opts->package_type == "CasPart");
    CHECK(meta_opts->package_subtype == "0x00000004");
    const fs::path packed2 = fs::temp_directory_path() / "sxpe-sims3pack-pack-meta.sims3pack";
    fs::remove(packed2);
    auto made2 = sxpe::games::sims3::pack_sims3pack(packed2, *items, *meta_opts, true);
    CHECK(made2.has_value());
    CHECK(made2->display_name == "From Meta");
    CHECK(made2->package_type == "CasPart");

    fs::remove_all(src_dir);
    fs::remove_all(pack_extract);
    fs::remove(packed);
    fs::remove(packed2);
    return 0;
}
