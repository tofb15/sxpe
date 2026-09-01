#include "check.hpp"
#include "sxpe/commands/bus.hpp"
#include "sxpe/resources/nmap.hpp"
#include "sxpe/resources/stbl.hpp"
#include "sxpe/resources/types.hpp"

#include <filesystem>
#include <nlohmann/json.hpp>

int main() {
    using nlohmann::json;
    sxpe::commands::Bus bus;

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

    auto tmp = std::filesystem::temp_directory_path() / "sxpe-m3";
    std::filesystem::create_directories(tmp);
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

    if (g_failed != 0) {
        std::cerr << g_failed << " check(s) failed\n";
        return 1;
    }
    return 0;
}
