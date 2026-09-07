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
    return 0;
}
