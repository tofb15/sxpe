#include "check.hpp"
#include "sxpe/commands/bus.hpp"
#include "sxpe/resources/dds.hpp"
#include "sxpe/resources/dir.hpp"
#include "sxpe/resources/nmap.hpp"
#include "sxpe/resources/stbl.hpp"
#include "sxpe/resources/types.hpp"
#include "sxpe/resources/xml.hpp"
#include "sxpe/core/caps.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <tuple>
#include <vector>

int main() {
    using nlohmann::json;
    sxpe::commands::Bus bus;

    CHECK(sxpe::resources::tag_for(0x00AE6C67) == "BONE");
    CHECK(sxpe::resources::tag_for(0x0580A2B4) == "THUM");
    CHECK(sxpe::resources::tag_for(0x626F60CE) == "THUM");
    CHECK(sxpe::resources::is_png_image(0x626F60CE));
    CHECK(sxpe::resources::tag_for(sxpe::resources::kImag) == "IMAG");
    CHECK(sxpe::resources::is_png_image(sxpe::resources::kImag));
    CHECK(sxpe::resources::tag_for(sxpe::resources::kImagJpeg) == "IMAG");
    CHECK(!sxpe::resources::is_png_image(sxpe::resources::kImagJpeg));
    CHECK(sxpe::resources::name_for(sxpe::resources::kImagJpeg) == "JPEG image");
    CHECK(sxpe::resources::tag_for(0x00B2D882) == "_IMG");
    CHECK(sxpe::resources::tag_for(0xFFFFFFFFu).empty());

    auto unknown = bus.execute("bogus.thing", json::object());
    CHECK(unknown["ok"] == false);
    CHECK(unknown["error"]["message"].get<std::string>().find("unknown command") != std::string::npos);

    auto man = bus.execute("manifest", json::object());
    CHECK(man["ok"] == true);
    CHECK(man["data"]["tools"].is_array());
    CHECK(man["data"]["tools"].size() > 10);
    bool saw_list = false;
    for (const auto& t : man["data"]["tools"]) {
        CHECK(t.contains("inputSchema"));
        CHECK(t.contains("annotations"));
        if (t["name"] == "resource.list") {
            saw_list = true;
            CHECK(t["annotations"]["readOnlyHint"] == true);
        }
    }
    CHECK(saw_list);

    auto h = bus.execute("hash.fnv", json{{"text", "a"}, {"width", 32}});
    CHECK(h["ok"] == true);

    auto np = bus.execute("package.new", json::object());
    CHECK(np["ok"] == true);
    const auto sid = np["data"]["sessionId"].get<std::string>();

    auto info = bus.execute("package.info", json{{"sessionId", sid}});
    CHECK(info["ok"] == true);
    CHECK(info["data"]["game"] == "sims3");
    CHECK(info["data"]["indexCount"] == 0);

    json rid{{"type", sxpe::resources::kStbl}, {"group", 0}, {"instance", 1}, {"ordinal", 0}};
    sxpe::resources::Stbl st;
    st.entries.push_back({42, "Hi"});
    auto raw = sxpe::resources::write_stbl(st);
    CHECK(raw.has_value());
    auto add = bus.execute("resource.add", json{{"sessionId", sid},
                                                {"resourceId", rid},
                                                {"payloadB64",
                                                 nlohmann::json(json{{"x", 1}}).dump()}});
    // payload must be real b64 — redo with a tiny encoder in-test via stbl.set after add of empty
    (void)add;

    auto nmap_rid = json{{"type", sxpe::resources::kNmap}, {"group", 0}, {"instance", 2}};
    // Build STBL via add with uncompressed bytes from write_stbl using a local b64
    auto b64 = [](const std::vector<std::byte>& in) {
        static constexpr char k[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string o;
        std::size_t i = 0;
        while (i + 2 < in.size()) {
            unsigned n = (unsigned)in[i] << 16 | (unsigned)in[i + 1] << 8 | (unsigned)in[i + 2];
            o.push_back(k[(n >> 18) & 63]);
            o.push_back(k[(n >> 12) & 63]);
            o.push_back(k[(n >> 6) & 63]);
            o.push_back(k[n & 63]);
            i += 3;
        }
        if (i < in.size()) {
            unsigned n = (unsigned)in[i] << 16;
            o.push_back(k[(n >> 18) & 63]);
            if (i + 1 < in.size()) {
                n |= (unsigned)in[i + 1] << 8;
                o.push_back(k[(n >> 12) & 63]);
                o.push_back(k[(n >> 6) & 63]);
                o.push_back('=');
            } else {
                o.push_back(k[(n >> 12) & 63]);
                o.append("==");
            }
        }
        return o;
    };
    auto added = bus.execute(
        "resource.add",
        json{{"sessionId", sid}, {"resourceId", rid}, {"payloadB64", b64(*raw)}});
    CHECK(added["ok"] == true);

    auto listed = bus.execute("resource.list", json{{"sessionId", sid}, {"limit", 10}});
    CHECK(listed["ok"] == true);
    CHECK(listed["data"]["items"].size() == 1);
    CHECK(listed["data"]["items"][0].contains("instanceHex"));
    CHECK(listed["data"]["items"][0]["instanceHex"].get<std::string>().rfind("0x", 0) == 0);
    CHECK(listed["data"]["items"][0].contains("chunkOffset"));

    auto tmp = std::filesystem::temp_directory_path() / "sxpe-m3";
    std::filesystem::create_directories(tmp);

    std::vector<std::byte> px(4 * 4 * 4, std::byte{128});
    auto dds = sxpe::resources::encode_dds_bgra(4, 4, px);
    CHECK(dds.has_value());
    auto img_rid = json{{"type", sxpe::resources::kImg}, {"group", 0}, {"instance", 7}};
    auto img_add = bus.execute(
        "resource.add", json{{"sessionId", sid}, {"resourceId", img_rid}, {"payloadB64", b64(*dds)}});
    CHECK(img_add["ok"] == true);
    auto dinfo = bus.execute("dds.info", json{{"sessionId", sid}, {"resourceId", img_rid}});
    CHECK(dinfo["ok"] == true);
    CHECK(dinfo["data"].value("width", 0) == 4);
    auto ddec = bus.execute("dds.decode", json{{"sessionId", sid}, {"resourceId", img_rid}});
    CHECK(ddec["ok"] == true);
    auto dds_out = (tmp / "out.dds").string();
    auto dexp = bus.execute("dds.export", json{{"sessionId", sid},
                                               {"resourceId", img_rid},
                                               {"path", dds_out},
                                               {"force", true}});
    CHECK(dexp["ok"] == true);
    std::ifstream df(dds_out, std::ios::binary);
    char mag[4]{};
    df.read(mag, 4);
    CHECK(df.gcount() == 4 && mag[0] == 'D' && mag[1] == 'D' && mag[2] == 'S');

    auto got = bus.execute("stbl.get", json{{"sessionId", sid}, {"resourceId", rid}});
    CHECK(got["ok"] == true);
    CHECK(got["data"]["entries"][0]["text"] == "Hi");

    auto set = bus.execute(
        "stbl.set", json{{"sessionId", sid}, {"resourceId", rid}, {"id", 42}, {"text", "Bye"}});
    CHECK(set["ok"] == true);
    auto got2 = bus.execute("stbl.get", json{{"sessionId", sid}, {"resourceId", rid}});
    CHECK(got2["data"]["entries"][0]["text"] == "Bye");

    auto dry = bus.execute("stbl.set", json{{"sessionId", sid},
                                            {"resourceId", rid},
                                            {"id", 42},
                                            {"text", "Nope"},
                                            {"dryRun", true}});
    CHECK(dry["ok"] == true);
    CHECK(dry["data"]["dryRun"] == true);
    auto got3 = bus.execute("stbl.get", json{{"sessionId", sid}, {"resourceId", rid}});
    CHECK(got3["data"]["entries"][0]["text"] == "Bye");

    auto und_set = bus.execute("undo", json{{"sessionId", sid}});
    CHECK(und_set["ok"] == true);
    auto after_und = bus.execute("stbl.get", json{{"sessionId", sid}, {"resourceId", rid}});
    CHECK(after_und["data"]["entries"][0]["text"] == "Hi");
    auto red_set = bus.execute("redo", json{{"sessionId", sid}});
    CHECK(red_set["ok"] == true);
    CHECK(red_set["data"]["redone"] == true);
    auto after_red = bus.execute("stbl.get", json{{"sessionId", sid}, {"resourceId", rid}});
    CHECK(after_red["data"]["entries"][0]["text"] == "Bye");
    CHECK(bus.execute("redo", json{{"sessionId", sid}})["ok"] == false);

    auto outp = (tmp / "hi.bin").string();
    auto exp = bus.execute("resource.export",
                           json{{"sessionId", sid}, {"resourceId", rid}, {"path", outp}, {"force", true}});
    CHECK(exp["ok"] == true);

    sxpe::resources::Nmap nm;
    nm.version = 1;
    nm.entries.push_back({1, "HelloName"});
    auto nmap_body = sxpe::resources::write_nmap(nm);
    CHECK(nmap_body.has_value());
    auto nadd = bus.execute(
        "resource.add",
        json{{"sessionId", sid}, {"resourceId", nmap_rid}, {"payloadB64", b64(*nmap_body)}});
    CHECK(nadd["ok"] == true);
    auto ng = bus.execute("nmap.get", json{{"sessionId", sid}});
    CHECK(ng["ok"] == true);
    CHECK(ng["data"]["entries"].size() == 1);

    auto fresh = bus.execute("package.new", json::object());
    CHECK(fresh["ok"] == true);
    const auto fsid = fresh["data"]["sessionId"].get<std::string>();
    auto named = bus.execute("nmap.set", json{{"sessionId", fsid}, {"instance", 99}, {"name", "Door"}});
    CHECK(named["ok"] == true);
    auto xml_rid = json{{"type", sxpe::resources::kXml}, {"group", 0}, {"instance", 99}};
    auto xml_add = bus.execute("resource.add",
                               json{{"sessionId", fsid}, {"resourceId", xml_rid}, {"payloadB64", b64(*raw)}});
    CHECK(xml_add["ok"] == true);
    auto renamed = bus.execute(
        "resource.rename", json{{"sessionId", fsid}, {"resourceId", xml_rid}, {"name", "NRaas.NoCD"}});
    CHECK(renamed["ok"] == true);
    auto listed_names = bus.execute("resource.list", json{{"sessionId", fsid}, {"limit", 20}});
    CHECK(listed_names["ok"] == true);
    bool saw_door = false;
    for (const auto& it : listed_names["data"]["items"]) {
        if (it.value("instance", 0ull) == 99 && it.value("name", "") == "NRaas.NoCD") {
            saw_door = true;
        }
    }
    CHECK(saw_door);
    bus.execute("package.close", json{{"sessionId", fsid}});

    auto fl = bus.execute("resource.setFlags",
                          json{{"sessionId", sid}, {"resourceId", rid}, {"deleted", true}});
    CHECK(fl["ok"] == true);

    auto filtered = bus.execute(
        "resource.list",
        json{{"sessionId", sid}, {"filter", json{{"tag", "STBL"}}}});
    CHECK(filtered["ok"] == true);

    auto save_dry = bus.execute("package.save", json{{"sessionId", sid}, {"dryRun", true}});
    CHECK(save_dry["ok"] == true);
    CHECK(save_dry["data"]["dryRun"] == true);

    auto dest = (tmp / "new.bin").string();
    auto sa = bus.execute("package.saveAs",
                          json{{"sessionId", sid}, {"path", dest}, {"force", true}});
    CHECK(sa["ok"] == true);

    auto a_path = (tmp / "iso-a.bin").string();
    auto b_path = (tmp / "iso-b.bin").string();
    auto na = bus.execute("package.new", json::object());
    auto nb = bus.execute("package.new", json::object());
    CHECK(na["ok"] == true && nb["ok"] == true);
    const auto sa_id = na["data"]["sessionId"].get<std::string>();
    const auto sb_id = nb["data"]["sessionId"].get<std::string>();
    CHECK(sa_id != sb_id);
    auto add_a = bus.execute("resource.add",
                             json{{"sessionId", sa_id},
                                  {"resourceId", json{{"type", 1}, {"group", 0}, {"instance", 1}}},
                                  {"payloadB64", b64(*raw)}});
    auto add_b = bus.execute("resource.add",
                             json{{"sessionId", sb_id},
                                  {"resourceId", json{{"type", 2}, {"group", 0}, {"instance", 2}}},
                                  {"payloadB64", b64(*raw)}});
    CHECK(add_a["ok"] == true && add_b["ok"] == true);
    CHECK(bus.execute("package.saveAs",
                      json{{"sessionId", sa_id}, {"path", a_path}, {"force", true}})["ok"] == true);
    CHECK(bus.execute("package.saveAs",
                      json{{"sessionId", sb_id}, {"path", b_path}, {"force", true}})["ok"] == true);
    bus.execute("package.close", json{{"sessionId", sa_id}});
    bus.execute("package.close", json{{"sessionId", sb_id}});

    auto merge = bus.execute("package.new", json::object());
    CHECK(merge["ok"] == true);
    const auto mid = merge["data"]["sessionId"].get<std::string>();
    auto imp = bus.execute("resource.importPackage",
                           json{{"sessionId", mid},
                                {"paths", json::array({a_path, b_path})},
                                {"force", true}});
    CHECK(imp["ok"] == true);
    CHECK(imp["data"].value("packages", 0) == 2);
    auto lm = bus.execute("resource.list", json{{"sessionId", mid}, {"limit", 10}});
    CHECK(lm["ok"] == true);
    CHECK(lm["data"]["items"].size() == 2);
    bus.execute("package.close", json{{"sessionId", mid}});

    auto oa = bus.execute("package.open", json{{"path", a_path}, {"writable", true}});
    auto ob = bus.execute("package.open", json{{"path", b_path}, {"writable", true}});
    CHECK(oa["ok"] == true && ob["ok"] == true);
    const auto oa_id = oa["data"]["sessionId"].get<std::string>();
    const auto ob_id = ob["data"]["sessionId"].get<std::string>();
    auto again = bus.execute("package.open", json{{"path", a_path}, {"writable", true}});
    CHECK(again["ok"] == true);
    CHECK(again["data"].value("alreadyOpen", false) == true);
    CHECK(again["data"]["sessionId"] == oa_id);
    CHECK(bus.execute("resource.setFlags",
                      json{{"sessionId", oa_id},
                           {"resourceId", json{{"type", 1}, {"group", 0}, {"instance", 1}}},
                           {"deleted", true}})["ok"] == true);
    CHECK(bus.execute("package.save", json{{"sessionId", oa_id}})["ok"] == true);
    auto lb = bus.execute("resource.list", json{{"sessionId", ob_id}, {"limit", 10}});
    CHECK(lb["ok"] == true);
    CHECK(lb["data"]["items"].size() == 1);
    CHECK(lb["data"]["items"][0]["type"] == 2);
    CHECK(lb["data"]["items"][0].value("deleted", false) == false);

    auto bad = bus.execute("package.open", json{{"path", dest + "-missing"}});
    CHECK(bad["ok"] == false);

    auto ma = bus.execute("package.new", json::object());
    auto mb = bus.execute("package.new", json::object());
    const auto ma_id = ma["data"]["sessionId"].get<std::string>();
    const auto mb_id = mb["data"]["sessionId"].get<std::string>();
    CHECK(bus.execute("resource.add",
                      json{{"sessionId", ma_id},
                           {"resourceId", json{{"type", 11}, {"group", 0}, {"instance", 11}}},
                           {"payloadB64", b64(*raw)}})["ok"] == true);
    CHECK(bus.execute("resource.add",
                      json{{"sessionId", mb_id},
                           {"resourceId", json{{"type", 12}, {"group", 0}, {"instance", 12}}},
                           {"payloadB64", b64(*raw)}})["ok"] == true);
    auto ma_path = (tmp / "merge-a.package").string();
    auto mb_path = (tmp / "merge-b.package").string();
    CHECK(bus.execute("package.saveAs",
                      json{{"sessionId", ma_id}, {"path", ma_path}, {"force", true}})["ok"] == true);
    CHECK(bus.execute("package.saveAs",
                      json{{"sessionId", mb_id}, {"path", mb_path}, {"force", true}})["ok"] == true);
    bus.execute("package.close", json{{"sessionId", ma_id}});
    bus.execute("package.close", json{{"sessionId", mb_id}});
    auto merge_sess = bus.execute("package.new", json::object());
    const auto merge_id = merge_sess["data"]["sessionId"].get<std::string>();
    auto mimp = bus.execute("resource.importPackage",
                            json{{"sessionId", merge_id},
                                 {"paths", json::array({ma_path, mb_path})},
                                 {"force", true},
                                 {"writeMergeManifest", true}});
    CHECK(mimp["ok"] == true);
    CHECK(mimp["data"].value("mergeManifest", false) == true);
    auto merged_path = (tmp / "merged.package").string();
    CHECK(bus.execute("package.saveAs", json{{"sessionId", merge_id},
                                             {"path", merged_path},
                                             {"force", true}})["ok"] == true);
    bus.execute("package.close", json{{"sessionId", merge_id}});
    auto outdir = (tmp / "unmerged").string();
    std::filesystem::create_directories(outdir);
    auto um = bus.execute("package.unmerge",
                          json{{"path", merged_path}, {"outDir", outdir}, {"force", true}});
    CHECK(um["ok"] == true);
    CHECK(um["data"].value("packagesWritten", 0) == 2);
    auto refuse = bus.execute("package.unmerge",
                              json{{"path", ma_path}, {"outDir", outdir}, {"force", true}});
    CHECK(refuse["ok"] == false);

    // Traversal rejection: craft a merged package with a malicious SXMM name.
    {
        auto evil = bus.execute("package.new", json::object());
        CHECK(evil["ok"] == true);
        const auto eid = evil["data"]["sessionId"].get<std::string>();
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", eid},
                               {"resourceId", json{{"type", 21}, {"group", 0}, {"instance", 21}}},
                               {"payloadB64", b64(*raw)},
                               {"compress", true}})["ok"] == true);
        json evil_man{{"format", "sxpe.mergeManifest"},
                      {"version", 1},
                      {"sources",
                       json::array({json{{"id", "src-1"},
                                         {"originalFileName", "../escape.package"},
                                         {"resources",
                                          json::array({json{{"type", 21},
                                                            {"group", 0},
                                                            {"instance", 21},
                                                            {"ordinal", 0}}})}}})}};
        const auto dumped = evil_man.dump();
        std::vector<std::byte> mb(dumped.size());
        for (std::size_t i = 0; i < dumped.size(); ++i) {
            mb[i] = static_cast<std::byte>(static_cast<unsigned char>(dumped[i]));
        }
        auto b64evil = b64(mb);
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", eid},
                               {"resourceId",
                                json{{"type", static_cast<int>(sxpe::resources::kSxmm)},
                                     {"group", 0},
                                     {"instance", 1}}},
                               {"payloadB64", b64evil}})["ok"] == true);
        auto evil_path = (tmp / "evil-merged.package").string();
        CHECK(bus.execute("package.saveAs",
                          json{{"sessionId", eid}, {"path", evil_path}, {"force", true}})["ok"] ==
              true);
        bus.execute("package.close", json{{"sessionId", eid}});
        auto trav = bus.execute("package.unmerge",
                                json{{"path", evil_path},
                                     {"outDir", (tmp / "unmerged-evil").string()},
                                     {"force", true}});
        CHECK(trav["ok"] == false);
        CHECK(trav["error"]["message"].get<std::string>().find("basename") != std::string::npos ||
              trav["error"]["message"].get<std::string>().find("..") != std::string::npos);
    }

    // Compressed merge → unmerge preserves on-disk sizes (copy-through).
    {
        auto ca = bus.execute("package.new", json::object());
        auto cb = bus.execute("package.new", json::object());
        const auto ca_id = ca["data"]["sessionId"].get<std::string>();
        const auto cb_id = cb["data"]["sessionId"].get<std::string>();
        std::vector<std::byte> bulky;
        const char pat[] = "merge-compress-pattern-AAAA-BBBB-CCCC-DDDD";
        for (int i = 0; i < 80; ++i) {
            for (std::size_t j = 0; j + 1 < sizeof(pat); ++j) {
                bulky.push_back(static_cast<std::byte>(static_cast<unsigned char>(pat[j])));
            }
        }
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", ca_id},
                               {"resourceId", json{{"type", 31}, {"group", 0}, {"instance", 31}}},
                               {"payloadB64", b64(bulky)},
                               {"compress", true}})["ok"] == true);
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", cb_id},
                               {"resourceId", json{{"type", 32}, {"group", 0}, {"instance", 32}}},
                               {"payloadB64", b64(bulky)},
                               {"compress", true}})["ok"] == true);
        auto ca_path = (tmp / "cmerge-a.package").string();
        auto cb_path = (tmp / "cmerge-b.package").string();
        CHECK(bus.execute("package.saveAs",
                          json{{"sessionId", ca_id}, {"path", ca_path}, {"force", true}})["ok"] ==
              true);
        CHECK(bus.execute("package.saveAs",
                          json{{"sessionId", cb_id}, {"path", cb_path}, {"force", true}})["ok"] ==
              true);
        auto la = bus.execute("resource.list", json{{"sessionId", ca_id}, {"limit", 5}});
        auto lb = bus.execute("resource.list", json{{"sessionId", cb_id}, {"limit", 5}});
        CHECK(la["ok"] == true && lb["ok"] == true);
        const auto a_fs = la["data"]["items"][0]["fileSize"].get<int>();
        const auto b_fs = lb["data"]["items"][0]["fileSize"].get<int>();
        CHECK(a_fs * 4 < static_cast<int>(bulky.size()));
        bus.execute("package.close", json{{"sessionId", ca_id}});
        bus.execute("package.close", json{{"sessionId", cb_id}});

        auto cm = bus.execute("package.new", json::object());
        const auto cm_id = cm["data"]["sessionId"].get<std::string>();
        CHECK(bus.execute("resource.importPackage",
                          json{{"sessionId", cm_id},
                               {"paths", json::array({ca_path, cb_path})},
                               {"force", true},
                               {"writeMergeManifest", true}})["ok"] == true);
        auto lm = bus.execute("resource.list", json{{"sessionId", cm_id}, {"limit", 20}});
        CHECK(lm["ok"] == true);
        int saw_a = -1;
        int saw_b = -1;
        for (const auto& it : lm["data"]["items"]) {
            if (it["type"] == 31) {
                saw_a = it["fileSize"].get<int>();
                CHECK(it.value("compressed", false) == true);
            }
            if (it["type"] == 32) {
                saw_b = it["fileSize"].get<int>();
                CHECK(it.value("compressed", false) == true);
            }
        }
        CHECK(saw_a == a_fs);
        CHECK(saw_b == b_fs);
        auto cm_path = (tmp / "cmerged.package").string();
        CHECK(bus.execute("package.saveAs",
                          json{{"sessionId", cm_id}, {"path", cm_path}, {"force", true}})["ok"] ==
              true);
        bus.execute("package.close", json{{"sessionId", cm_id}});

        auto cout = (tmp / "cunmerged").string();
        std::filesystem::create_directories(cout);
        auto cum = bus.execute("package.unmerge",
                               json{{"path", cm_path}, {"outDir", cout}, {"force", true}});
        CHECK(cum["ok"] == true);
        CHECK(cum["data"].value("packagesWritten", 0) == 2);
        auto oa = bus.execute("package.open",
                              json{{"path", (std::filesystem::path(cout) / "cmerge-a.package").string()}});
        CHECK(oa["ok"] == true);
        auto oaid = oa["data"]["sessionId"].get<std::string>();
        auto ola = bus.execute("resource.list", json{{"sessionId", oaid}, {"limit", 5}});
        CHECK(ola["ok"] == true);
        CHECK(ola["data"]["items"][0]["fileSize"].get<int>() == a_fs);
        CHECK(ola["data"]["items"][0].value("compressed", false) == true);
        bus.execute("package.close", json{{"sessionId", oaid}});
    }

    auto us = bus.execute("package.new", json::object());
    CHECK(us["ok"] == true);
    const auto uid = us["data"]["sessionId"].get<std::string>();
    auto uadd = bus.execute(
        "resource.add",
        json{{"sessionId", uid},
             {"resourceId", json{{"type", 9}, {"group", 0}, {"instance", 9}}},
             {"payloadB64", b64(*raw)}});
    CHECK(uadd["ok"] == true);
    CHECK(bus.execute("resource.list", json{{"sessionId", uid}, {"limit", 10}})["data"]["items"].size() ==
          1);
    CHECK(bus.execute("undo", json{{"sessionId", uid}})["ok"] == true);
    CHECK(bus.execute("resource.list", json{{"sessionId", uid}, {"limit", 10}})["data"]["items"].size() ==
          0);
    CHECK(bus.execute("redo", json{{"sessionId", uid}})["ok"] == true);
    CHECK(bus.execute("resource.list", json{{"sessionId", uid}, {"limit", 10}})["data"]["items"].size() ==
          1);
    bus.execute("package.close", json{{"sessionId", uid}});

    auto wu32 = [](std::vector<std::byte>& o, std::uint32_t v) {
        const auto* p = reinterpret_cast<const std::byte*>(&v);
        o.insert(o.end(), p, p + 4);
    };
    std::vector<std::byte> objk_body;
    objk_body.push_back(std::byte{1});
    wu32(objk_body, 0x23177498u);
    objk_body.push_back(std::byte{1});
    wu32(objk_body, 11);
    const char* k = "scriptClass";
    objk_body.insert(objk_body.end(), reinterpret_cast<const std::byte*>(k),
                     reinterpret_cast<const std::byte*>(k) + 11);
    objk_body.push_back(std::byte{0});
    wu32(objk_body, 8);
    const char* cls = "My.Class";
    objk_body.insert(objk_body.end(), reinterpret_cast<const std::byte*>(cls),
                     reinterpret_cast<const std::byte*>(cls) + 8);
    objk_body.push_back(std::byte{1});
    const auto tgi_off = static_cast<std::uint32_t>(objk_body.size());
    objk_body.push_back(std::byte{0});
    std::vector<std::byte> objk_bytes;
    wu32(objk_bytes, 7);
    wu32(objk_bytes, tgi_off);
    wu32(objk_bytes, 1);
    objk_bytes.insert(objk_bytes.end(), objk_body.begin(), objk_body.end());
    auto osess = bus.execute("package.new", json::object());
    const auto oid = osess["data"]["sessionId"].get<std::string>();
    auto objk_rid = json{{"type", sxpe::resources::kObjk}, {"group", 0}, {"instance", 1}};
    CHECK(bus.execute("resource.add",
                      json{{"sessionId", oid},
                           {"resourceId", objk_rid},
                           {"payloadB64", b64(objk_bytes)}})["ok"] == true);
    auto og = bus.execute("objk.get", json{{"sessionId", oid}, {"resourceId", objk_rid}});
    CHECK(og["ok"] == true);
    CHECK(og["data"]["version"] == 7);
    CHECK(og["data"]["data"][0]["key"] == "scriptClass");
    auto gg = bus.execute("graph.get", json{{"sessionId", oid}, {"resourceId", objk_rid}});
    CHECK(gg["ok"] == true);
    CHECK(gg["data"]["type"] == "OBJK");
    bus.execute("package.close", json{{"sessionId", oid}});

    auto s3sess = bus.execute("package.new", json::object());
    const auto s3id = s3sess["data"]["sessionId"].get<std::string>();
    auto dllp = (tmp / "mod.dll").string();
    {
        std::ofstream df(dllp, std::ios::binary);
        df.write("MZ\x00\x00", 4);
    }
    auto impd = bus.execute("s3sa.importDll", json{{"sessionId", s3id}, {"path", dllp}});
    CHECK(impd["ok"] == true);
    CHECK(impd["data"].value("loadLibrary", true) == false);
    CHECK(impd["data"].value("nmapName", "") == "mod.dll");
    auto s3rid = impd["data"]["resourceId"];
    auto nmap_after = bus.execute("nmap.get", json{{"sessionId", s3id}});
    CHECK(nmap_after["ok"] == true);
    bool saw_mod = false;
    for (const auto& e : nmap_after["data"]["entries"]) {
        if (e.value("name", "") == "mod.dll") {
            saw_mod = true;
        }
    }
    CHECK(saw_mod);
    auto s3info = bus.execute("s3sa.info", json{{"sessionId", s3id}, {"resourceId", s3rid}});
    CHECK(s3info["ok"] == true);
    CHECK(s3info["data"].value("parsed", false) == true);
    CHECK(s3info["data"].value("version", 0) == 1);
    auto out_dll = (tmp / "mod-out.dll").string();
    auto expd = bus.execute("s3sa.exportDll", json{{"sessionId", s3id},
                                                   {"resourceId", s3rid},
                                                   {"path", out_dll},
                                                   {"force", true}});
    CHECK(expd["ok"] == true);
    std::ifstream in_dll(out_dll, std::ios::binary);
    char mz2[2]{};
    in_dll.read(mz2, 2);
    CHECK(in_dll.gcount() == 2 && mz2[0] == 'M' && mz2[1] == 'Z');
    bus.execute("package.close", json{{"sessionId", s3id}});

    {
        auto bad = bus.execute("package.new", json::object());
        CHECK(bad["ok"] == true);
        const auto bid = bad["data"]["sessionId"].get<std::string>();
        auto bad_nmap = bus.execute(
            "resource.add",
            json{{"sessionId", bid},
                 {"resourceId", json{{"type", sxpe::resources::kNmap}, {"group", 0}, {"instance", 1}}},
                 {"payloadB64", b64(std::vector<std::byte>{std::byte{'x'}})}});
        CHECK(bad_nmap["ok"] == true);
        auto dll_bad = (tmp / "roll.dll").string();
        {
            std::ofstream df(dll_bad, std::ios::binary);
            df.write("MZ\x00\x00", 4);
        }
        auto before = bus.execute("resource.list", json{{"sessionId", bid}, {"limit", 50}});
        CHECK(before["ok"] == true);
        const auto before_n = before["data"]["items"].size();
        auto fail_imp = bus.execute("s3sa.importDll", json{{"sessionId", bid}, {"path", dll_bad}});
        CHECK(fail_imp["ok"] == false);
        auto after = bus.execute("resource.list", json{{"sessionId", bid}, {"limit", 50}});
        CHECK(after["ok"] == true);
        CHECK(after["data"]["items"].size() == before_n);
        bool saw_s3sa = false;
        for (const auto& it : after["data"]["items"]) {
            if (it.value("type", 0) == sxpe::resources::kS3sa) {
                saw_s3sa = true;
            }
        }
        CHECK(!saw_s3sa);
        bus.execute("package.close", json{{"sessionId", bid}});
    }

    {
        auto cs = bus.execute("package.new", json::object());
        CHECK(cs["ok"] == true);
        const auto cid = cs["data"]["sessionId"].get<std::string>();
        const auto clip_rid =
            json{{"type", sxpe::resources::kClip}, {"group", 0}, {"instance", 1}};
        std::vector<std::byte> clip_body{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", cid},
                               {"resourceId", clip_rid},
                               {"payloadB64", b64(clip_body)}})["ok"] == true);
        auto exp = bus.execute(
            "clip.exportAs",
            json{{"sessionId", cid}, {"resourceId", clip_rid}, {"name", "a_walk"}});
        CHECK(exp["ok"] == true);
        constexpr std::uint64_t kFrozenAWalk = 0x11a06ab91bca6bdeull;
        CHECK(exp["data"]["resourceId"]["instance"].get<std::uint64_t>() == kFrozenAWalk);
        CHECK(exp["data"]["resourceId"]["type"].get<std::uint32_t>() == sxpe::resources::kClip);
        bus.execute("package.close", json{{"sessionId", cid}});
    }

    // Duplicate NMAP TGI must concatenate names, not last-wins replace.
    {
        auto nmap_tgi = json{{"type", sxpe::resources::kNmap}, {"group", 0}, {"instance", 0}};
        auto make_named = [&](const std::string& sid, std::uint32_t type, std::uint64_t inst,
                              const std::string& name, const std::string& path) {
            json rid{{"type", type}, {"group", 0}, {"instance", inst}};
            CHECK(bus.execute("resource.add",
                              json{{"sessionId", sid},
                                   {"resourceId", rid},
                                   {"payloadB64", b64(*raw)}})["ok"] == true);
            sxpe::resources::Nmap nm;
            nm.version = 1;
            nm.entries.push_back({inst, name});
            auto body = sxpe::resources::write_nmap(nm);
            CHECK(body.has_value());
            CHECK(bus.execute("resource.add",
                              json{{"sessionId", sid},
                                   {"resourceId", nmap_tgi},
                                   {"payloadB64", b64(*body)}})["ok"] == true);
            CHECK(bus.execute("package.saveAs",
                              json{{"sessionId", sid}, {"path", path}, {"force", true}})["ok"] ==
                  true);
        };
        auto pa = bus.execute("package.new", json::object());
        auto pb = bus.execute("package.new", json::object());
        CHECK(pa["ok"] == true && pb["ok"] == true);
        const auto paid = pa["data"]["sessionId"].get<std::string>();
        const auto pbid = pb["data"]["sessionId"].get<std::string>();
        auto na_path = (tmp / "nmap-a.package").string();
        auto nb_path = (tmp / "nmap-b.package").string();
        make_named(paid, 1, 0x111, "AlphaMesh", na_path);
        make_named(pbid, 2, 0x222, "BetaMesh", nb_path);
        bus.execute("package.close", json{{"sessionId", paid}});
        bus.execute("package.close", json{{"sessionId", pbid}});

        auto mg = bus.execute("package.new", json::object());
        CHECK(mg["ok"] == true);
        const auto mgid = mg["data"]["sessionId"].get<std::string>();
        auto nimp = bus.execute("resource.importPackage",
                                json{{"sessionId", mgid},
                                     {"paths", json::array({na_path, nb_path})},
                                     {"writeMergeManifest", true}});
        CHECK(nimp["ok"] == true);
        auto nlist = bus.execute("resource.list", json{{"sessionId", mgid}, {"limit", 20}});
        CHECK(nlist["ok"] == true);
        bool saw_alpha = false;
        bool saw_beta = false;
        std::uint32_t nmap_rows = 0;
        for (const auto& it : nlist["data"]["items"]) {
            if (it.value("type", 0u) == sxpe::resources::kNmap) {
                ++nmap_rows;
            }
            if (it.value("instance", 0ull) == 0x111 && it.value("name", "") == "AlphaMesh") {
                saw_alpha = true;
            }
            if (it.value("instance", 0ull) == 0x222 && it.value("name", "") == "BetaMesh") {
                saw_beta = true;
            }
        }
        CHECK(nmap_rows == 1);
        CHECK(saw_alpha);
        CHECK(saw_beta);
        CHECK(nlist["data"]["items"][0].value("type", 0u) == sxpe::resources::kNmap);
        auto ng2 = bus.execute("nmap.get", json{{"sessionId", mgid}});
        CHECK(ng2["ok"] == true);
        CHECK(ng2["data"]["entries"].size() == 2);
        auto nmap_merged = (tmp / "nmap-merged.package").string();
        CHECK(bus.execute("package.saveAs",
                          json{{"sessionId", mgid}, {"path", nmap_merged}, {"force", true}})["ok"] ==
              true);
        bus.execute("package.close", json{{"sessionId", mgid}});
        auto nout = (tmp / "nmap-unmerged").string();
        std::filesystem::create_directories(nout);
        auto umn = bus.execute(
            "package.unmerge", json{{"path", nmap_merged}, {"outDir", nout}, {"force", true}});
        CHECK(umn["ok"] == true);
        CHECK(umn["data"].value("packagesWritten", 0) == 2);
        auto check_child = [&](const std::string& fname, std::uint64_t inst, const std::string& name,
                               std::uint64_t other) {
            auto op = bus.execute("package.open", json{{"path", (tmp / "nmap-unmerged" / fname).string()}});
            CHECK(op["ok"] == true);
            const auto cid = op["data"]["sessionId"].get<std::string>();
            auto ng = bus.execute("nmap.get", json{{"sessionId", cid}});
            CHECK(ng["ok"] == true);
            CHECK(ng["data"]["entries"].size() == 1);
            CHECK(ng["data"]["entries"][0].value("instance", 0ull) == inst);
            CHECK(ng["data"]["entries"][0].value("name", "") == name);
            auto lst = bus.execute("resource.list", json{{"sessionId", cid}, {"limit", 20}});
            CHECK(lst["ok"] == true);
            bool saw = false;
            bool leaked = false;
            for (const auto& it : lst["data"]["items"]) {
                if (it.value("instance", 0ull) == inst && it.value("name", "") == name) {
                    saw = true;
                }
                if (it.value("instance", 0ull) == other) {
                    leaked = true;
                }
            }
            CHECK(saw);
            CHECK(!leaked);
            bus.execute("package.close", json{{"sessionId", cid}});
        };
        check_child("nmap-a.package", 0x111, "AlphaMesh", 0x222);
        check_child("nmap-b.package", 0x222, "BetaMesh", 0x111);
    }

    // Issue #21: validate summary + dirPolicy strip / copy-through (no DIR invent on new).
    {
        auto empty = bus.execute("package.new", json::object());
        CHECK(empty["ok"] == true);
        const auto eid = empty["data"]["sessionId"].get<std::string>();
        auto einfo = bus.execute("package.info", json{{"sessionId", eid}});
        CHECK(einfo["ok"] == true);
        CHECK(einfo["data"].value("dirPresent", true) == false);
        auto eval = bus.execute("package.validate", json{{"sessionId", eid}});
        CHECK(eval["ok"] == true);
        CHECK(eval["data"].value("ok", false) == true);
        CHECK(eval["data"].contains("summary"));
        CHECK(eval["data"]["summary"].is_array());
        CHECK(!eval["data"]["summary"].empty());
        CHECK(eval["data"]["summary"][0].get<std::string>().find("OK") != std::string::npos);
        CHECK(eval["data"]["dir"].value("present", true) == false);
        auto empty_path = (tmp / "no-dir-new.package").string();
        CHECK(bus.execute("package.saveAs",
                          json{{"sessionId", eid}, {"path", empty_path}, {"force", true}})["ok"] ==
              true);
        bus.execute("package.close", json{{"sessionId", eid}});
        auto reopen = bus.execute("package.open", json{{"path", empty_path}});
        CHECK(reopen["ok"] == true);
        auto rinfo = bus.execute("package.info",
                                 json{{"sessionId", reopen["data"]["sessionId"].get<std::string>()}});
        CHECK(rinfo["data"].value("dirPresent", true) == false);
        bus.execute("package.close",
                    json{{"sessionId", reopen["data"]["sessionId"].get<std::string>()}});

        auto with_dir = bus.execute("package.new", json::object());
        CHECK(with_dir["ok"] == true);
        const auto did = with_dir["data"]["sessionId"].get<std::string>();
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", did},
                               {"resourceId", json{{"type", 21}, {"group", 0}, {"instance", 21}}},
                               {"payloadB64", b64(*raw)}})["ok"] == true);
        sxpe::resources::DirEntry de{{21u, 0u, 21ull}, static_cast<std::uint32_t>(raw->size())};
        auto dir_bytes = sxpe::resources::write_dir(std::span<const sxpe::resources::DirEntry>(&de, 1));
        CHECK(dir_bytes.has_value());
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", did},
                               {"resourceId",
                                json{{"type", static_cast<int>(sxpe::resources::kDir)},
                                     {"group", 0},
                                     {"instance", 0}}},
                               {"payloadB64", b64(*dir_bytes)}})["ok"] == true);
        auto dval = bus.execute("package.validate", json{{"sessionId", did}});
        CHECK(dval["ok"] == true);
        CHECK(dval["data"].value("ok", false) == true);
        CHECK(dval["data"]["dir"].value("present", false) == true);
        CHECK(dval["data"]["dir"].value("records", 0) == 1);
        CHECK(dval["data"]["dir"].value("unmatched", 1) == 0);
        auto with_dir_path = (tmp / "with-dir.package").string();
        CHECK(bus.execute("package.saveAs", json{{"sessionId", did},
                                                 {"path", with_dir_path},
                                                 {"force", true}})["ok"] == true);
        bus.execute("package.close", json{{"sessionId", did}});

        auto strip_sess = bus.execute("package.new", json::object());
        const auto strip_id = strip_sess["data"]["sessionId"].get<std::string>();
        auto strip_imp = bus.execute("resource.importPackage",
                                     json{{"sessionId", strip_id},
                                          {"path", with_dir_path},
                                          {"force", true},
                                          {"writeMergeManifest", true}});
        CHECK(strip_imp["ok"] == true);
        CHECK(strip_imp["data"].value("dirPolicy", "") == "strip");
        auto strip_info = bus.execute("package.info", json{{"sessionId", strip_id}});
        CHECK(strip_info["data"].value("dirPresent", true) == false);
        bool saw_sxmm = false;
        auto strip_list = bus.execute("resource.list", json{{"sessionId", strip_id}, {"limit", 20}});
        for (const auto& it : strip_list["data"]["items"]) {
            if (it.value("type", 0u) == sxpe::resources::kSxmm) {
                saw_sxmm = true;
            }
            CHECK(it.value("type", 0u) != sxpe::resources::kDir);
        }
        CHECK(saw_sxmm);
        auto sxmm_read =
            bus.execute("resource.read",
                        json{{"sessionId", strip_id},
                             {"resourceId",
                              json{{"type", static_cast<int>(sxpe::resources::kSxmm)},
                                   {"group", 0},
                                   {"instance", 1}}},
                             {"includePayload", true},
                             {"maxBytes", 65536}});
        CHECK(sxmm_read["ok"] == true);
        // payloadB64 present — decode notes via listing notes from import response only;
        // SXMM notes.dirPolicy is strip by default with writeMergeManifest.
        bus.execute("package.close", json{{"sessionId", strip_id}});

        auto copy_sess = bus.execute("package.new", json::object());
        const auto copy_id = copy_sess["data"]["sessionId"].get<std::string>();
        auto copy_imp = bus.execute("resource.importPackage",
                                    json{{"sessionId", copy_id},
                                         {"path", with_dir_path},
                                         {"force", true},
                                         {"writeMergeManifest", true},
                                         {"dirPolicy", "copy-through"}});
        CHECK(copy_imp["ok"] == true);
        CHECK(copy_imp["data"].value("dirPolicy", "") == "copy-through");
        auto copy_info = bus.execute("package.info", json{{"sessionId", copy_id}});
        CHECK(copy_info["data"].value("dirPresent", false) == true);
        auto copy_val = bus.execute("package.validate", json{{"sessionId", copy_id}});
        CHECK(copy_val["ok"] == true);
        CHECK(copy_val["data"]["dir"].value("present", false) == true);
        CHECK(copy_val["data"]["summary"].is_array());
        bus.execute("package.close", json{{"sessionId", copy_id}});

        auto rebuild_sess = bus.execute("package.new", json::object());
        const auto rebuild_id = rebuild_sess["data"]["sessionId"].get<std::string>();
        auto rebuild_imp = bus.execute("resource.importPackage",
                                       json{{"sessionId", rebuild_id},
                                            {"path", with_dir_path},
                                            {"force", true},
                                            {"dirPolicy", "rebuild"}});
        CHECK(rebuild_imp["ok"] == false);
        CHECK(rebuild_imp["error"].value("message", std::string{}).find("rebuild") !=
              std::string::npos);
        bus.execute("package.close", json{{"sessionId", rebuild_id}});

        auto bad_pol = bus.execute("package.new", json::object());
        const auto bad_id = bad_pol["data"]["sessionId"].get<std::string>();
        auto bad_imp = bus.execute("resource.importPackage",
                                   json{{"sessionId", bad_id},
                                        {"path", with_dir_path},
                                        {"dirPolicy", "explode"}});
        CHECK(bad_imp["ok"] == false);
        bus.execute("package.close", json{{"sessionId", bad_id}});
    }

    // Issue #22: package.diff — only-in-A / only-in-B / same-TGI different payload.
    {
        auto make_pkg = [&](const std::string& name, const std::vector<std::tuple<int, int, std::uint64_t, std::string>>& rows) {
            auto created = bus.execute("package.new", json::object());
            CHECK(created["ok"] == true);
            const auto sid = created["data"]["sessionId"].get<std::string>();
            for (const auto& [type, group, inst, payload] : rows) {
                std::vector<std::byte> bytes(payload.size());
                for (std::size_t i = 0; i < payload.size(); ++i) {
                    bytes[i] = static_cast<std::byte>(payload[i]);
                }
                CHECK(bus.execute("resource.add",
                                  json{{"sessionId", sid},
                                       {"resourceId",
                                        json{{"type", type}, {"group", group}, {"instance", inst}}},
                                       {"payloadB64", b64(bytes)}})["ok"] == true);
            }
            const auto path = (tmp / name).string();
            CHECK(bus.execute("package.saveAs",
                              json{{"sessionId", sid}, {"path", path}, {"force", true}})["ok"] ==
                  true);
            bus.execute("package.close", json{{"sessionId", sid}});
            return path;
        };
        const auto path_a =
            make_pkg("diff-a.package", {{1, 0, 10, "alpha"}, {2, 0, 20, "shared-old"}, {3, 0, 30, "only-a"}});
        const auto path_b =
            make_pkg("diff-b.package", {{1, 0, 10, "alpha"}, {2, 0, 20, "shared-new"}, {4, 0, 40, "only-b"}});

        auto man = bus.execute("manifest", json::object());
        bool saw_diff = false;
        for (const auto& t : man["data"]["tools"]) {
            if (t["name"] == "package.diff") {
                saw_diff = true;
                CHECK(t["annotations"]["readOnlyHint"] == true);
                CHECK(t.contains("mcpName"));
                CHECK(t["mcpName"] == "package_diff");
            }
        }
        CHECK(saw_diff);

        auto diff = bus.execute("package.diff", json{{"pathA", path_a}, {"pathB", path_b}});
        CHECK(diff["ok"] == true);
        const auto& d = diff["data"];
        CHECK(d.value("hashAlgorithm", "") == "sha256-uncompressed");
        CHECK(d.value("sameCount", 0u) == 1);
        CHECK(d["onlyInA"].is_array());
        CHECK(d["onlyInA"].size() == 1);
        CHECK(d["onlyInA"][0].value("instance", 0ull) == 30);
        CHECK(d["onlyInB"].is_array());
        CHECK(d["onlyInB"].size() == 1);
        CHECK(d["onlyInB"][0].value("instance", 0ull) == 40);
        CHECK(d["different"].is_array());
        CHECK(d["different"].size() == 1);
        CHECK(d["different"][0].value("instance", 0ull) == 20);
        CHECK(d["different"][0].contains("hashA"));
        CHECK(d["different"][0].contains("hashB"));
        CHECK(d["different"][0]["hashA"] != d["different"][0]["hashB"]);
        CHECK(d.contains("summary"));
        CHECK(d["summary"].is_array());
        CHECK(!d["summary"].empty());
        CHECK(d["summary"][0].get<std::string>().find("differences") != std::string::npos);

        auto same = bus.execute("package.diff", json{{"pathA", path_a}, {"pathB", path_a}});
        CHECK(same["ok"] == true);
        CHECK(same["data"].value("sameCount", 0u) == 3);
        CHECK(same["data"]["onlyInA"].empty());
        CHECK(same["data"]["onlyInB"].empty());
        CHECK(same["data"]["different"].empty());
        CHECK(same["data"]["summary"][0].get<std::string>().find("match") != std::string::npos);
    }


    // Issue #23: xml.get / xml.set — edit `_XML`, encoding round-trip, dryRun/undo, cap.
    {
        const std::string xml_body = "<?xml version=\"1.0\"?>\n<root attr=\"a\">hi</root>\n";
        std::vector<std::byte> xml_bytes;
        xml_bytes.reserve(xml_body.size());
        for (unsigned char c : xml_body) {
            xml_bytes.push_back(static_cast<std::byte>(c));
        }
        auto xml_sid_env = bus.execute("package.new", json::object());
        CHECK(xml_sid_env["ok"] == true);
        const auto xsid = xml_sid_env["data"]["sessionId"].get<std::string>();
        json xrid{{"type", sxpe::resources::kXml}, {"group", 0}, {"instance", 99}, {"ordinal", 0}};
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", xsid},
                               {"resourceId", xrid},
                               {"payloadB64", b64(xml_bytes)}})["ok"] == true);

        auto man = bus.execute("manifest", json::object());
        bool saw_xml_get = false, saw_xml_set = false;
        for (const auto& tool : man["data"]["tools"]) {
            if (tool["name"] == "xml.get") {
                saw_xml_get = true;
                CHECK(tool["annotations"]["readOnlyHint"] == true);
                CHECK(tool["mcpName"] == "xml_get");
            }
            if (tool["name"] == "xml.set") {
                saw_xml_set = true;
                CHECK(tool["annotations"]["readOnlyHint"] == false);
                CHECK(tool["mcpName"] == "xml_set");
            }
        }
        CHECK(saw_xml_get);
        CHECK(saw_xml_set);

        auto got = bus.execute("xml.get", json{{"sessionId", xsid}, {"resourceId", xrid}});
        CHECK(got["ok"] == true);
        CHECK(got["data"]["text"].get<std::string>().find("<root") != std::string::npos);
        CHECK(got["data"]["encoding"] == "utf-8");
        CHECK(got["data"]["tag"] == "_XML");

        auto dry = bus.execute("xml.set", json{{"sessionId", xsid},
                                               {"resourceId", xrid},
                                               {"text", "<?xml version=\"1.0\"?><root>changed</root>"},
                                               {"dryRun", true}});
        CHECK(dry["ok"] == true);
        CHECK(dry["data"]["dryRun"] == true);
        auto still = bus.execute("xml.get", json{{"sessionId", xsid}, {"resourceId", xrid}});
        CHECK(still["data"]["text"].get<std::string>().find("hi") != std::string::npos);

        auto set = bus.execute("xml.set", json{{"sessionId", xsid},
                                               {"resourceId", xrid},
                                               {"text", "<?xml version=\"1.0\"?><root>changed</root>"}});
        CHECK(set["ok"] == true);
        auto after = bus.execute("xml.get", json{{"sessionId", xsid}, {"resourceId", xrid}});
        CHECK(after["ok"] == true);
        CHECK(after["data"]["text"].get<std::string>().find("changed") != std::string::npos);

        CHECK(bus.execute("undo", json{{"sessionId", xsid}})["ok"] == true);
        auto und = bus.execute("xml.get", json{{"sessionId", xsid}, {"resourceId", xrid}});
        CHECK(und["data"]["text"].get<std::string>().find("hi") != std::string::npos);
        CHECK(bus.execute("redo", json{{"sessionId", xsid}})["ok"] == true);
        auto red = bus.execute("xml.get", json{{"sessionId", xsid}, {"resourceId", xrid}});
        CHECK(red["data"]["text"].get<std::string>().find("changed") != std::string::npos);

        // UTF-16LE round-trip
        auto enc = sxpe::resources::encode_xml_text("<?xml version=\"1.0\"?><u16>le</u16>",
                                                    sxpe::resources::XmlEncoding::Utf16Le);
        CHECK(enc.has_value());
        json irid{{"type", sxpe::resources::kItun}, {"group", 0}, {"instance", 100}};
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", xsid},
                               {"resourceId", irid},
                               {"payloadB64", b64(*enc)}})["ok"] == true);
        auto ig = bus.execute("xml.get", json{{"sessionId", xsid}, {"resourceId", irid}});
        CHECK(ig["ok"] == true);
        CHECK(ig["data"]["encoding"] == "utf-16le");
        CHECK(ig["data"]["tag"] == "ITUN");
        CHECK(ig["data"]["text"].get<std::string>().find("<u16>le</u16>") != std::string::npos);
        CHECK(bus.execute("xml.set",
                          json{{"sessionId", xsid},
                               {"resourceId", irid},
                               {"text", "<?xml version=\"1.0\"?><u16>ok</u16>"}})["ok"] == true);
        auto ig2 = bus.execute("xml.get", json{{"sessionId", xsid}, {"resourceId", irid}});
        CHECK(ig2["data"]["encoding"] == "utf-16le");
        CHECK(ig2["data"]["text"].get<std::string>().find("<u16>ok</u16>") != std::string::npos);

        // Cap refuse
        std::string huge(static_cast<std::size_t>(sxpe::core::caps::kMaxXmlEditorBytes) + 8, 'x');
        huge = "<?xml version=\"1.0\"?><huge>" + huge + "</huge>";
        auto over = bus.execute("xml.set", json{{"sessionId", xsid},
                                                {"resourceId", xrid},
                                                {"text", huge}});
        CHECK(over["ok"] == false);
        CHECK(over["error"]["code"] == "cap_exceeded");
        CHECK(over["error"]["message"].get<std::string>().find("XML editor cap") != std::string::npos);

        // Also refuse get when existing payload is over cap
        std::vector<std::byte> big;
        big.reserve(sxpe::core::caps::kMaxXmlEditorBytes + 16);
        const std::string head = "<?xml version=\"1.0\"?><b>";
        for (unsigned char c : head) {
            big.push_back(static_cast<std::byte>(c));
        }
        big.resize(sxpe::core::caps::kMaxXmlEditorBytes + 8, std::byte{'Z'});
        const std::string tail = "</b>";
        for (unsigned char c : tail) {
            big.push_back(static_cast<std::byte>(c));
        }
        json brid{{"type", sxpe::resources::kXml}, {"group", 0}, {"instance", 101}};
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", xsid},
                               {"resourceId", brid},
                               {"payloadB64", b64(big)}})["ok"] == true);
        auto bigget = bus.execute("xml.get", json{{"sessionId", xsid}, {"resourceId", brid}});
        CHECK(bigget["ok"] == false);
        CHECK(bigget["error"]["code"] == "cap_exceeded");

        bus.execute("package.close", json{{"sessionId", xsid}});
    }


    // Issue #24: NMAP table editor — batch replace once + duplicate last-wins policy.
    {
        auto fresh = bus.execute("package.new", json::object());
        CHECK(fresh["ok"] == true);
        const auto nsid = fresh["data"]["sessionId"].get<std::string>();
        sxpe::resources::Nmap nm;
        nm.version = 1;
        nm.entries.push_back({0x10, "Alpha"});
        nm.entries.push_back({0x20, "Beta"});
        nm.entries.push_back({0x10, "AlphaLast"});  // duplicate → Name column last-wins
        auto body = sxpe::resources::write_nmap(nm);
        CHECK(body.has_value());
        json nmap_rid{{"type", sxpe::resources::kNmap}, {"group", 0}, {"instance", 0}};
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", nsid},
                               {"resourceId", nmap_rid},
                               {"payloadB64", b64(*body)}})["ok"] == true);

        auto listed = bus.execute("nmap.list", json{{"sessionId", nsid}});
        CHECK(listed["ok"] == true);
        CHECK(listed["data"]["entries"].size() == 3);
        CHECK(listed["data"]["duplicatePolicy"] == "last-wins");
        CHECK(listed["data"]["duplicates"].size() == 1);
        CHECK(listed["data"]["duplicates"][0].value("instance", 0ull) == 0x10);
        CHECK(listed["data"]["duplicates"][0].value("count", 0u) == 2);
        CHECK(listed["data"]["duplicates"][0].value("effectiveName", "") == "AlphaLast");

        // Name column effective name is last-wins.
        json xml_rid{{"type", sxpe::resources::kXml}, {"group", 0}, {"instance", 0x10}};
        const std::string xml = "<?xml version=\"1.0\"?><r/>";
        std::vector<std::byte> raw(xml.size());
        for (std::size_t i = 0; i < xml.size(); ++i) {
            raw[i] = static_cast<std::byte>(static_cast<unsigned char>(xml[i]));
        }
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", nsid},
                               {"resourceId", xml_rid},
                               {"payloadB64", b64(raw)}})["ok"] == true);
        auto rlist = bus.execute("resource.list", json{{"sessionId", nsid}, {"limit", 20}});
        CHECK(rlist["ok"] == true);
        bool saw_last = false;
        for (const auto& it : rlist["data"]["items"]) {
            if (it.value("instance", 0ull) == 0x10 && it.value("type", 0u) == sxpe::resources::kXml) {
                CHECK(it.value("name", "") == "AlphaLast");
                saw_last = true;
            }
        }
        CHECK(saw_last);

        // Edit multiple names in one dialog save (nmap.replace) — single undo.
        auto rep = bus.execute(
            "nmap.replace",
            json{{"sessionId", nsid},
                 {"resourceId", nmap_rid},
                 {"entries",
                  json::array({{{"instance", 0x10}, {"name", "Door"}},
                               {{"instance", 0x20}, {"name", "Window"}},
                               {{"instance", 0x30}, {"name", "Roof"}}})}});
        CHECK(rep["ok"] == true);
        CHECK(rep["data"].value("count", 0u) == 3);
        CHECK(rep["data"]["duplicates"].empty());

        auto after = bus.execute("nmap.get", json{{"sessionId", nsid}, {"resourceId", nmap_rid}});
        CHECK(after["ok"] == true);
        CHECK(after["data"]["entries"].size() == 3);
        CHECK(after["data"]["entries"][0].value("name", "") == "Door");
        CHECK(after["data"]["entries"][1].value("name", "") == "Window");
        CHECK(after["data"]["entries"][2].value("name", "") == "Roof");

        CHECK(bus.execute("undo", json{{"sessionId", nsid}})["ok"] == true);
        auto und = bus.execute("nmap.get", json{{"sessionId", nsid}, {"resourceId", nmap_rid}});
        CHECK(und["ok"] == true);
        CHECK(und["data"]["entries"].size() == 3);
        CHECK(und["data"]["duplicates"].size() == 1);
        CHECK(und["data"]["duplicates"][0].value("effectiveName", "") == "AlphaLast");

        CHECK(bus.execute("redo", json{{"sessionId", nsid}})["ok"] == true);
        auto red = bus.execute("nmap.get", json{{"sessionId", nsid}, {"resourceId", nmap_rid}});
        CHECK(red["data"]["entries"].size() == 3);
        CHECK(red["data"]["entries"][2].value("name", "") == "Roof");

        auto del = bus.execute(
            "nmap.delete", json{{"sessionId", nsid}, {"resourceId", nmap_rid}, {"instance", 0x20}});
        CHECK(del["ok"] == true);
        CHECK(del["data"].value("removed", 0u) == 1);
        auto after_del = bus.execute("nmap.get", json{{"sessionId", nsid}, {"resourceId", nmap_rid}});
        CHECK(after_del["data"]["entries"].size() == 2);

        // nmap.set updates last duplicate row when duplicates remain.
        auto with_dups = bus.execute(
            "nmap.replace",
            json{{"sessionId", nsid},
                 {"entries",
                  json::array({{{"instance", 1}, {"name", "A"}},
                               {{"instance", 1}, {"name", "B"}}})}});
        CHECK(with_dups["ok"] == true);
        CHECK(bus.execute("nmap.set",
                          json{{"sessionId", nsid}, {"instance", 1}, {"name", "C"}})["ok"] == true);
        auto gs = bus.execute("nmap.get", json{{"sessionId", nsid}});
        CHECK(gs["data"]["entries"].size() == 2);
        CHECK(gs["data"]["entries"][0].value("name", "") == "A");
        CHECK(gs["data"]["entries"][1].value("name", "") == "C");
        CHECK(gs["data"]["duplicates"][0].value("effectiveName", "") == "C");

        bus.execute("package.close", json{{"sessionId", nsid}});
    }


    // Issue #25: resource.findRefs — REFS + OBJK structured hits; optional byteScan.
    {
        auto wu16 = [](std::vector<std::byte>& o, std::uint16_t v) {
            const auto* p = reinterpret_cast<const std::byte*>(&v);
            o.insert(o.end(), p, p + 2);
        };
        auto wu32 = [](std::vector<std::byte>& o, std::uint32_t v) {
            const auto* p = reinterpret_cast<const std::byte*>(&v);
            o.insert(o.end(), p, p + 4);
        };
        auto wu64 = [](std::vector<std::byte>& o, std::uint64_t v) {
            const auto* p = reinterpret_cast<const std::byte*>(&v);
            o.insert(o.end(), p, p + 8);
        };
        auto wu8 = [](std::vector<std::byte>& o, std::uint8_t v) { o.push_back(std::byte{v}); };
        auto wtgi = [&](std::vector<std::byte>& o, std::uint32_t type, std::uint32_t group,
                        std::uint64_t inst) {
            wu32(o, type);
            wu32(o, group);
            wu64(o, inst);
        };

        const std::uint32_t target_type = 0x0333406Cu;
        const std::uint32_t target_group = 0;
        const std::uint64_t target_inst = 0xABCDull;

        std::vector<std::byte> refs_body;
        wu16(refs_body, 1);
        wu32(refs_body, 1);
        wtgi(refs_body, target_type, target_group, target_inst);
        wu16(refs_body, 0);
        wu32(refs_body, 0);

        std::vector<std::byte> objk_inner;
        wu8(objk_inner, 0);  // components
        wu8(objk_inner, 0);  // data
        wu8(objk_inner, 1);  // visibility
        const auto tgi_off = static_cast<std::uint32_t>(objk_inner.size());
        wu8(objk_inner, 1);
        wtgi(objk_inner, target_type, target_group, target_inst);
        std::vector<std::byte> objk_body;
        wu32(objk_body, 7);
        wu32(objk_body, tgi_off);
        wu32(objk_body, 17);
        objk_body.insert(objk_body.end(), objk_inner.begin(), objk_inner.end());

        // Unrelated resource embedding the TGI bytes for byteScan.
        std::vector<std::byte> blob;
        blob.push_back(std::byte{'X'});
        wtgi(blob, target_type, target_group, target_inst);
        blob.push_back(std::byte{'Y'});

        auto created = bus.execute("package.new", json::object());
        CHECK(created["ok"] == true);
        const auto fsid = created["data"]["sessionId"].get<std::string>();
        json target_rid{{"type", target_type}, {"group", target_group}, {"instance", target_inst}};
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", fsid},
                               {"resourceId", target_rid},
                               {"payloadB64", b64(std::vector<std::byte>{std::byte{'t'}})}})["ok"] ==
              true);
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", fsid},
                               {"resourceId",
                                json{{"type", sxpe::resources::kRefs},
                                     {"group", 0},
                                     {"instance", 1}}},
                               {"payloadB64", b64(refs_body)}})["ok"] == true);
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", fsid},
                               {"resourceId",
                                json{{"type", sxpe::resources::kObjk},
                                     {"group", 0},
                                     {"instance", 2}}},
                               {"payloadB64", b64(objk_body)}})["ok"] == true);
        CHECK(bus.execute("resource.add",
                          json{{"sessionId", fsid},
                               {"resourceId",
                                json{{"type", 0x12345678u}, {"group", 0}, {"instance", 3}}},
                               {"payloadB64", b64(blob)}})["ok"] == true);

        auto man = bus.execute("manifest", json::object());
        bool saw = false;
        for (const auto& tool : man["data"]["tools"]) {
            if (tool["name"] == "resource.findRefs") {
                saw = true;
                CHECK(tool["annotations"]["readOnlyHint"] == true);
                CHECK(tool["mcpName"] == "resource_findRefs");
            }
        }
        CHECK(saw);

        auto found = bus.execute("resource.findRefs",
                                 json{{"sessionId", fsid}, {"resourceId", target_rid}});
        CHECK(found["ok"] == true);
        CHECK(found["data"]["hits"].is_array());
        CHECK(found["data"]["hits"].size() == 2);
        CHECK(found["data"].contains("summary"));
        CHECK(found["data"]["summary"].is_array());
        bool saw_refs = false;
        bool saw_objk = false;
        for (const auto& h : found["data"]["hits"]) {
            const auto reason = h.value("reason", "");
            if (reason == "refs.entry") {
                saw_refs = true;
                CHECK(h["source"].value("type", 0u) == sxpe::resources::kRefs);
            }
            if (reason == "objk.tgi") {
                saw_objk = true;
                CHECK(h["source"].value("type", 0u) == sxpe::resources::kObjk);
            }
        }
        CHECK(saw_refs);
        CHECK(saw_objk);
        CHECK(found["data"]["byteScan"].value("enabled", true) == false);

        auto with_bs = bus.execute(
            "resource.findRefs",
            json{{"sessionId", fsid},
                 {"resourceId", target_rid},
                 {"byteScan", true},
                 {"byteScanMaxBytes", 1024},
                 {"byteScanMaxResources", 50}});
        CHECK(with_bs["ok"] == true);
        CHECK(with_bs["data"]["hits"].size() >= 3);
        bool saw_bs = false;
        for (const auto& h : with_bs["data"]["hits"]) {
            const auto reason = h.value("reason", "");
            if (reason == "byteScan" || reason == "byteScan.hiLo") {
                saw_bs = true;
                CHECK(h["source"].value("instance", 0ull) == 3);
            }
        }
        CHECK(saw_bs);
        CHECK(with_bs["data"]["byteScan"].value("enabled", false) == true);

        bus.execute("package.close", json{{"sessionId", fsid}});
    }


    // Issue #26: folder.scan — empty / wrong-game / corrupt / duplicate TGI (fixture folder).
    {
        const auto scan_root = tmp / "folder-scan-fixture";
        std::filesystem::create_directories(scan_root);
        const auto sub = scan_root / "sub";
        std::filesystem::create_directories(sub);

        // Empty / zero-byte.
        {
            std::ofstream((scan_root / "empty.package").string(), std::ios::binary);
        }
        // Wrong-game sniff: DBPF major 1 (Sims 2-ish).
        {
            std::ofstream out((scan_root / "sims2ish.package").string(), std::ios::binary);
            const unsigned char hdr[8] = {'D', 'B', 'P', 'F', 1, 0, 0, 0};
            out.write(reinterpret_cast<const char*>(hdr), 8);
        }
        // Corrupt: DBPF major 2 but truncated (not a valid TS3 index).
        {
            std::ofstream out((scan_root / "corrupt.package").string(), std::ios::binary);
            std::vector<unsigned char> buf(96, 0);
            buf[0] = 'D';
            buf[1] = 'B';
            buf[2] = 'P';
            buf[3] = 'F';
            buf[4] = 2;  // major
            // claim resources / index that do not exist
            buf[0x24] = 1;
            buf[0x3C] = 3;  // index version
            buf[0x40] = 96; // index pos past EOF for tiny file — actually file is 96 bytes
            out.write(reinterpret_cast<const char*>(buf.data()),
                      static_cast<std::streamsize>(buf.size()));
        }
        // Two OK packages sharing a TGI (duplicate across files).
        auto make_pkg = [&](const std::filesystem::path& dest, int type, int group,
                            std::uint64_t inst, const std::string& payload) {
            auto created = bus.execute("package.new", json::object());
            CHECK(created["ok"] == true);
            const auto sid = created["data"]["sessionId"].get<std::string>();
            std::vector<std::byte> bytes(payload.size());
            for (std::size_t i = 0; i < payload.size(); ++i) {
                bytes[i] = static_cast<std::byte>(payload[i]);
            }
            CHECK(bus.execute("resource.add",
                              json{{"sessionId", sid},
                                   {"resourceId",
                                    json{{"type", type}, {"group", group}, {"instance", inst}}},
                                   {"payloadB64", b64(bytes)}})["ok"] == true);
            CHECK(bus.execute("package.saveAs",
                              json{{"sessionId", sid},
                                   {"path", dest.string()},
                                   {"force", true}})["ok"] == true);
            bus.execute("package.close", json{{"sessionId", sid}});
        };
        make_pkg(scan_root / "a.package", 0x11, 0, 0xABC, "alpha");
        make_pkg(sub / "b.package", 0x11, 0, 0xABC, "beta");  // same TGI, different payload
        make_pkg(scan_root / "unique.package", 0x22, 0, 0x1, "solo");

        auto man = bus.execute("manifest", json::object());
        bool saw_scan = false;
        for (const auto& t : man["data"]["tools"]) {
            if (t["name"] == "folder.scan") {
                saw_scan = true;
                CHECK(t["annotations"]["readOnlyHint"] == true);
                CHECK(t["annotations"]["destructiveHint"] == false);
                CHECK(t["mcpName"] == "folder_scan");
            }
        }
        CHECK(saw_scan);

        // Relative ".." survives lexically_normal; absolute ".../fixture/.." does not.
        auto refused = bus.execute("folder.scan", json{{"path", "../sxpe-folder-scan-refuse"}});
        CHECK(refused["ok"] == false);
        CHECK(refused["error"]["code"] == "refused");

        auto scanned = bus.execute("folder.scan", json{{"path", scan_root.string()}});
        CHECK(scanned["ok"] == true);
        const auto& d = scanned["data"];
        CHECK(d.value("readOnly", false) == true);
        CHECK(d.value("filesScanned", 0u) >= 5);
        CHECK(d["issues"].is_array());
        CHECK(d["duplicates"].is_array());
        CHECK(d.contains("summary"));
        CHECK(d["summary"].is_array());
        CHECK(!d["summary"].empty());

        bool saw_empty = false, saw_wrong = false, saw_corrupt = false;
        for (const auto& issue : d["issues"]) {
            const auto kind = issue.value("kind", "");
            if (kind == "empty") {
                saw_empty = true;
            }
            if (kind == "wrong_game") {
                saw_wrong = true;
            }
            if (kind == "corrupt" || kind == "unreadable") {
                // truncated major-2 may surface as corrupt or unreadable/wrong_game
                if (issue.value("path", std::string{}).find("corrupt.package") != std::string::npos) {
                    saw_corrupt = true;
                }
            }
            if (issue.value("path", std::string{}).find("corrupt.package") != std::string::npos) {
                saw_corrupt = true;
            }
        }
        CHECK(saw_empty);
        CHECK(saw_wrong);
        CHECK(saw_corrupt);

        CHECK(d.value("duplicateTgiCount", 0u) >= 1);
        bool saw_dup = false;
        for (const auto& dup : d["duplicates"]) {
            if (dup.value("type", 0u) == 0x11u && dup.value("instance", 0ull) == 0xABCull) {
                saw_dup = true;
                CHECK(dup.value("fileCount", 0u) >= 2);
                CHECK(dup["paths"].is_array());
                CHECK(dup["paths"].size() >= 2);
            }
        }
        CHECK(saw_dup);

        // Caps: maxFiles=1 stops early.
        auto capped = bus.execute(
            "folder.scan", json{{"path", scan_root.string()}, {"maxFiles", 1}});
        CHECK(capped["ok"] == true);
        CHECK(capped["data"].value("capped", false) == true);
        CHECK(capped["data"].value("filesScanned", 0u) == 1);
        CHECK(capped["data"].value("capReason", "") == "maxFiles");
    }


    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }

    return 0;
}
