// Data Overflow's meta file (docs/SURVIVAL.md 8, 14.1 T7 "overflow_profile",
// round O1b lane F): the format (read, write, unknown lines kept, versions),
// the .bak and the corruption cases (cut, garbage, empty, a damaged file with
// and without a backup, a failed write), the migration from no file, the
// Plug Lab, the loadout rules, Sim Bits and the Data Records, the form
// unlocks, the run request's options and the path rule. No game data.
#include "../src/game/overflow_profile.hpp"
#include "../src/game/overflow_run.hpp"
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace ov = dw4::game::overflow;
namespace fs = std::filesystem;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}
bool complete_file(const fs::path& path) {
    const std::string text = read(path);
    return dw4::game::overflow::complete_profile(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}
void write(const fs::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// A folder of its own below the temporary folder (removed at the end).
struct Folder {
    fs::path root;
    Folder() {
        root = fs::temp_directory_path() / ("dw4_overflow_profile_tests_" + std::to_string(std::rand()) + "_" +
                                            std::to_string(reinterpret_cast<std::uintptr_t>(this) & 0xFFFF));
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Folder() {
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

// The example of SURVIVAL.md 8.5 (with lane E's gear keys in the loadout).
const char* kExample = "DW4OVERFLOW 1\n"
                       "seen_intro 1\n"
                       "sim_bits 3420\n"
                       "earned_total 18950\n"
                       "plug p_agumon str 4 def 2 mhp 3\n"
                       "loadout p_agumon armor brave_core chip drain_ram\n"
                       "form p_agumon runs 14 clears_normal 1 best_time 21:12.4 best_level 41 best_defeats 3511\n"
                       "form p_vmon runs 3 best_time 11:42.3 best_level 27 best_defeats 1290\n"
                       "record gate_keeper 1\n"
                       "record blossom_data 1\n"
                       "recorded_tech p_agumon\n"
                       "total defeats 21877\n"
                       "total runs 17\n"
                       "total casts 6120\n"
                       "total crates 133\n"
                       "end\n";

ov::OverflowProfile example() {
    bool complete = false;
    auto profile = ov::OverflowProfile::parse(kExample, &complete);
    check(profile && complete, "the 8.5 example parses as a complete profile");
    return *profile;
}

void format() {
    const auto p = example();
    check(p.version == 1 && p.seen_intro && p.sim_bits == 3420 && p.earned_total == 18950, "the example's head");
    check(p.plug_steps("p_agumon", 2) == 4 && p.plug_steps("p_agumon", 3) == 2 && p.plug_steps("p_agumon", 0) == 3 &&
              p.plug_steps("p_agumon", 1) == 0 && p.plug_steps("p_vmon", 2) == 0,
          "plug steps by UPCNT index");
    check(p.loadout("p_agumon") == ov::Loadout{"brave_core", "drain_ram"} && p.loadout("p_vmon") == ov::Loadout{}, "loadouts");
    const auto* agumon = p.form("p_agumon");
    check(agumon && agumon->runs == 14 && agumon->clears[0] == 1 && agumon->best_level == 41 && agumon->best_defeats == 3511 &&
              std::abs(agumon->best_seconds - 1272.4) < 1e-6,
          "Agumon's line");
    check(p.form("p_vmon") && p.form("p_vmon")->clears[0] == 0 && !p.form("p_girumon"), "Veemon's line, no Guilmon");
    check(p.record("gate_keeper") && p.record("blossom_data") && !p.record("first_clear"), "records");
    check(p.total("defeats") == 21877 && p.total("runs") == 17 && p.total("casts") == 6120 && p.total("crates") == 133 && p.total("nothing") == 0,
          "totals");
    // The line this version does not know is kept and written back.
    check(p.unknown.size() == 1 && p.unknown[0] == "recorded_tech p_agumon", "the unknown line is kept");
    const auto text = p.serialize();
    check(text.find("\nrecorded_tech p_agumon\n") != std::string::npos && text.ends_with("\nend\n"), "... and written back before end");
    // Round trip: what is written reads back the same and writes the same.
    bool complete = false;
    const auto again = ov::OverflowProfile::parse(text, &complete);
    check(again && complete && *again == p && again->serialize() == text, "the round trip is exact");
    // CRLF line ends and a BOM (an editor) read the same.
    std::string crlf;
    for (const char c : text) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    const auto from_crlf = ov::OverflowProfile::parse("\xEF\xBB\xBF" + crlf, &complete);
    check(from_crlf && complete && *from_crlf == p, "CRLF and a BOM");
    // A later version: read as far as this one knows; its lines and keys stay.
    const auto later = ov::OverflowProfile::parse("DW4OVERFLOW 7\nsim_bits 5\nform p_vmon runs 2 best_time 1:00.0 best_level 3 best_defeats 4 "
                                                  "best_score 99\nsigil p_vmon 3 2\nplug p_vmon str 1 luck 4\nend\n",
                                                  &complete);
    check(later && complete && later->version == 7 && later->sim_bits == 5 && later->plug_steps("p_vmon", 2) == 1, "a later version reads");
    check(later->form("p_vmon")->extra.size() == 1 && later->form("p_vmon")->extra[0].first == "best_score" &&
              later->serialize().find("best_score 99") != std::string::npos && later->serialize().find("\nsigil p_vmon 3 2\n") != std::string::npos,
          "a later version's form key and line are kept");
    check(later->serialize().starts_with("DW4OVERFLOW 1\n"), "this version writes its own header");
    // Times.
    check(ov::time_text(0) == "0:00.0" && ov::time_text(1272.44) == "21:12.4" && ov::time_text(3600) == "60:00.0", "time text");
    check(ov::parse_time("21:12.4") && std::abs(*ov::parse_time("21:12.4") - 1272.4) < 1e-9 && ov::parse_time("1:05") == 65.0 &&
              !ov::parse_time("1:75.0") && !ov::parse_time(":12.0") && !ov::parse_time("1:12.45") && !ov::parse_time("x"),
          "time parse");
    // A new profile writes the minimum and reads back.
    const ov::OverflowProfile fresh;
    const auto fresh_back = ov::OverflowProfile::parse(fresh.serialize(), &complete);
    check(fresh_back && complete && *fresh_back == fresh && !fresh.seen_intro && fresh.sim_bits == 0, "a new profile");
}

void damaged_text() {
    bool complete = true;
    // No header: no profile at all.
    check(!ov::OverflowProfile::parse("", &complete) && !complete, "empty text");
    check(!ov::OverflowProfile::parse("DW4PROFILE 1\nend\n"), "another file's header");
    check(!ov::OverflowProfile::parse(std::string("\0\0\0\0garbage", 11)), "binary garbage");
    check(!ov::OverflowProfile::parse("DW4OVERFLOW x\nend\n") && !ov::OverflowProfile::parse("DW4OVERFLOW 0\nend\n"), "a bad version");
    // A cut file: what it holds, but not complete.
    const std::string text = example().serialize();
    for (const std::size_t cut : {text.size() - 1, text.size() - 4, text.size() / 2, std::size_t(14)}) {
        const auto cut_profile = ov::OverflowProfile::parse(text.substr(0, cut), &complete);
        check(cut_profile && !complete, "a file cut at " + std::to_string(cut) + " is not complete");
    }
    check(!ov::complete_profile(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size() - 1)), "complete_profile: cut");
    check(ov::complete_profile(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size())), "complete_profile: whole");
    // Text after end: not what was written.
    check(ov::OverflowProfile::parse(text + "sim_bits 9\n", &complete) && !complete, "text after end");
    // Damaged lines in a complete file are dropped, values are clamped.
    const auto odd = ov::OverflowProfile::parse("DW4OVERFLOW 1\nsim_bits -50\nearned_total 12abc\nplug p_agumon str 99 def -3\n"
                                                "form p_agumon runs 2 best_time 99:99.9 best_level 5\nform p_vmon runs 1 best_level 4\n"
                                                "record first_clear 0\nrecord ten_minutes 20261106\ntotal defeats x\n"
                                                "loadout p_agumon armor brave_core\nsim\xFF\xFE bits 7\nseen_intro\n"
                                                "Weird Line\nend\n",
                                                &complete);
    check(odd && complete, "a complete file with damaged lines");
    check(odd->sim_bits == 0 && odd->earned_total == 0, "negative bits clamp, a bad number is dropped");
    check(odd->plug_steps("p_agumon", 2) == 10 && odd->plug_steps("p_agumon", 3) == 0, "plug steps clamp to 0-10");
    check(!odd->form("p_agumon") && odd->form("p_vmon") && odd->form("p_vmon")->best_level == 4, "a form line with a bad time is dropped");
    check(!odd->record("first_clear") && odd->record("ten_minutes") && odd->records.at("ten_minutes") == 20261106, "records need a date");
    check(odd->total("defeats") == 0 && odd->loadouts.empty() && !odd->seen_intro, "bad totals, loadouts and flags are dropped");
    check(odd->unknown.empty(), "damaged or odd lines are not kept as unknown lines");
}

void store() {
    Folder folder;
    const fs::path file = folder.root / "saves" / "overflow.dat";
    auto bak = file;
    bak += ".bak";
    auto damaged = file;
    damaged += ".damaged";
    // Migration from no file: a new profile, nothing written until a write.
    ov::MetaStore s;
    s.open(file);
    check(s.source() == ov::LoadSource::none && s.load_note().empty() && s.profile() == ov::OverflowProfile{}, "no file: a new profile");
    check(!fs::exists(file), "opening writes nothing");
    // The first meeting writes (8.5), the folder is made.
    check(s.mark_intro_seen() && fs::exists(file) && !fs::exists(bak), "the first meeting writes the file (no .bak yet)");
    check(!s.mark_intro_seen(), "seen once");
    {
        ov::MetaStore again;
        again.open(file);
        check(again.source() == ov::LoadSource::file && again.profile().seen_intro, "the written file reads back");
    }
    // A run's results write; the previous file becomes the .bak.
    ov::RunReport report;
    report.form = "p_agumon";
    report.seconds = 615.0;
    report.level = 18;
    report.defeats = 900;
    report.bits = 640;
    report.outcome = "defeated";
    const auto reward = s.record_run(report, 20261106);
    check(reward.sim_bits == 640 && s.profile().sim_bits == 640 && fs::exists(bak), "a run writes, the old file becomes the .bak");
    check(reward.new_records == std::vector<std::string>{"ten_minutes"}, "Ten Minutes is new");
    check(s.last_report() && s.last_report()->defeats == 900 && s.last_reward().sim_bits == 640, "the last run is kept for the results");
    check(complete_file(bak), "the .bak is complete");
    const std::string good = read(file);
    // A Plug Lab purchase and a loadout change write.
    check(s.buy_plug("p_agumon", 2) == ov::BuyResult::bought && s.profile().sim_bits == 540, "a purchase writes");
    check(s.set_loadout("p_agumon", {"brave_core", "wisdom_chip"}) && s.profile().loadout("p_agumon").armor == "brave_core", "a loadout writes");
    check(!s.set_loadout("p_agumon", {"brave_core", "wisdom_chip"}), "the same loadout: no write");
    check(!s.set_loadout("p_agumon", {"cipher_core", ""}), "an unopened core is refused");
    {
        ov::MetaStore again;
        again.open(file);
        check(again.profile() == s.profile(), "every write reads back");
    }
    const std::string latest = read(file);

    // Corruption 1: the file is cut (a crash in another program, a full disk): the .bak.
    write(file, latest.substr(0, latest.size() / 2));
    {
        ov::MetaStore again;
        again.open(file);
        check(again.source() == ov::LoadSource::backup && !again.load_note().empty(), "a cut file: the .bak is read");
        check(again.profile().seen_intro, "... with the backup's data");
        check(fs::exists(damaged) && read(damaged) == latest.substr(0, latest.size() / 2), "... and the damaged file is kept");
        const std::string backup_before = read(bak);
        check(again.mark_intro_seen() == false, "(nothing to do)");
        check(again.buy_plug("p_agumon", 3) == ov::BuyResult::bought, "a write after a restore");
        check(read(bak) == backup_before, "a damaged file never replaces the good .bak");
        check(complete_file(file), "the new file is complete");
    }
    // Corruption 2: garbage bytes over the file.
    write(file, std::string("\x00\x01\x02 garbage \xFF", 14));
    {
        ov::MetaStore again;
        again.open(file);
        check(again.source() == ov::LoadSource::backup, "garbage: the .bak");
    }
    // Corruption 3: an empty file.
    write(file, "");
    {
        ov::MetaStore again;
        again.open(file);
        check(again.source() == ov::LoadSource::backup, "an empty file: the .bak");
    }
    // Corruption 4: the file is gone, the .bak is there.
    fs::remove(file);
    {
        ov::MetaStore again;
        again.open(file);
        check(again.source() == ov::LoadSource::backup && again.load_note().find("missing") != std::string::npos, "no file: the .bak");
    }
    // Corruption 5: both damaged: a cut file keeps what it holds.
    write(file, latest.substr(0, latest.find("\nform ")));
    write(bak, "DW4OVERF");
    {
        ov::MetaStore again;
        again.open(file);
        check(again.source() == ov::LoadSource::salvaged && again.profile().seen_intro && again.profile().sim_bits == 540,
              "both damaged: the cut file's lines are kept");
        // Its next write is complete again.
        check(again.mark_intro_seen() == false && again.save(), "a save after salvage");
        ov::MetaStore third;
        third.open(file);
        check(third.source() == ov::LoadSource::file && third.profile().sim_bits == 540, "the salvaged profile is whole again");
    }
    // Corruption 6: both unreadable: a new profile, the damaged file kept.
    write(file, "garbage");
    write(bak, "garbage");
    {
        ov::MetaStore again;
        again.open(file);
        check(again.source() == ov::LoadSource::fresh && again.profile() == ov::OverflowProfile{} && read(damaged) == "garbage",
              "nothing readable: a new profile, the file kept as .damaged");
    }
    // A failed write (the target is a folder): the change stays in memory, the error is told.
    {
        const fs::path blocked = folder.root / "blocked.dat";
        fs::create_directories(blocked);
        ov::MetaStore again;
        again.open(blocked);
        check(!again.mark_intro_seen() && again.profile().seen_intro && !again.last_error().empty(), "a failed write keeps the change and says why");
    }
    // A test run's store (no path): memory only.
    {
        ov::MetaStore memory;
        memory.open({});
        check(memory.mark_intro_seen() && memory.profile().seen_intro && memory.path().empty(), "a memory-only store");
    }
    (void)good;
}

void plug_lab() {
    ov::OverflowProfile p;
    p.sim_bits = 5500;
    const auto rows = ov::plug_lab_rows(p, "p_agumon");
    check(rows[0].upcnt == 2 && std::string(rows[0].name) == "Strength" && rows[1].upcnt == 3 && rows[2].upcnt == 0 && rows[6].upcnt == 6,
          "the rows: Strength, Def., MaxHP, ..., Speed");
    check(rows[0].steps == 0 && rows[0].points == 0 && rows[0].next_price == 100, "a new row");
    // Step n costs 100 x n: ten steps cost 5,500.
    for (int n = 1; n <= 10; ++n) check(ov::buy_plug(p, "p_agumon", 2) == ov::BuyResult::bought, "step " + std::to_string(n));
    check(p.sim_bits == 0 && p.plug_steps("p_agumon", 2) == 10, "ten steps cost 5,500");
    check(ov::buy_plug(p, "p_agumon", 2) == ov::BuyResult::maxed, "the 11th step: maxed");
    check(ov::buy_plug(p, "p_agumon", 3) == ov::BuyResult::not_enough && p.plug_steps("p_agumon", 3) == 0, "no bits: nothing bought");
    check(ov::buy_plug(p, "p_agumon", 7) == ov::BuyResult::bad_stat && ov::buy_plug(p, "p_nobody", 2) == ov::BuyResult::bad_stat, "bad stat / form");
    const auto rows2 = ov::plug_lab_rows(p, "p_agumon");
    check(rows2[0].steps == 10 && rows2[0].points == 50 && rows2[0].next_price == 0, "a full row");
    // Plug Lab points per form, by UPCNT index (the run's RunConfig::plug_lab).
    const auto points = ov::plug_lab_points(p, "p_agumon");
    check(points[2] == 50 && points[0] == 0 && points[7] == 0, "points by UPCNT index");
    check(ov::plug_lab_points(p, "p_vmon") == std::array<int, 11>{}, "per form");
    check(std::string(ov::buy_result_text(ov::BuyResult::not_enough)) == "Not enough Sim Bits.", "texts");
}

void defeat_bits() {
    // 4.7: 5 % of the defeats, the original's band for the level, x (100 + BITRITSU) / 100.
    std::uint64_t random = ov::bits_seed(1);
    int drops = 0, low = 1 << 30, high = 0;
    long long sum = 0;
    for (int i = 0; i < 20000; ++i) {
        const int bits = ov::defeat_bits(10, 0, random);
        if (!bits) continue;
        ++drops;
        sum += bits;
        low = std::min(low, bits);
        high = std::max(high, bits);
    }
    check(drops > 800 && drops < 1200, "about 5 % of the defeats leave bits (" + std::to_string(drops) + ")");
    check(low >= 10 && high < 55, "level 10: the band 10-55");
    std::uint64_t a = ov::bits_seed(7), b = ov::bits_seed(7);
    for (int i = 0; i < 500; ++i) check(ov::defeat_bits(40, 10, a) == ov::defeat_bits(40, 10, b), "deterministic");
    check(ov::bits_seed(1) != ov::bits_seed(2), "seeds differ");
    // The band at level 60 and the BIT Board's +10 %.
    std::uint64_t c = ov::bits_seed(3);
    int seen = 0;
    for (int i = 0; i < 5000 && seen < 50; ++i) {
        const int bits = ov::defeat_bits(60, 10, c);
        if (!bits) continue;
        ++seen;
        check(bits >= 162 * 110 / 100 && bits < 350 * 110 / 100, "level 60, BITRITSU 10");
    }
    check(seen == 50, "drops at level 60");
    (void)sum;
}

void records_and_bits() {
    // Sim Bits (4.9): bits x the tier's factor, +500 x the factor on a clear.
    ov::RunReport r;
    r.bits = 1000;
    check(ov::run_sim_bits(r) == 1000, "Normal: x 1");
    r.tier = 1;
    check(ov::run_sim_bits(r) == 1500, "Hard: x 1.5");
    r.tier = 2;
    r.outcome = "cleared";
    check(ov::run_sim_bits(r) == 3000, "Very Hard clear: (1000 + 500) x 2");
    r.bits = -5;
    r.tier = 0;
    check(ov::run_sim_bits(r) == 500, "no negative bits");

    ov::OverflowProfile p;
    // A run that meets nothing.
    ov::RunReport a;
    a.form = "p_vmon";
    a.seconds = 120;
    a.level = 6;
    a.defeats = 150;
    a.bits = 90;
    a.outcome = "defeated";
    auto reward = ov::add_run(p, a, 20261106);
    check(reward.sim_bits == 90 && reward.new_records.empty() && p.sim_bits == 90 && p.earned_total == 90, "a short run");
    check(p.form("p_vmon")->runs == 1 && p.form("p_vmon")->best_level == 6 && p.total("runs") == 1 && p.total("defeats") == 150, "its stats");
    // A cleared Normal run with two Guardians, techniques and an awakening.
    ov::RunReport b;
    b.form = "p_agumon";
    b.seconds = 1500;
    b.level = 41;
    b.defeats = 900;
    b.bits = 4600;
    b.outcome = "cleared";
    b.guardians = {"e_blosso", "e_skulgrey", "e_ldknight", "e_goburi"};
    b.awakened = true;
    b.casts = 520;
    b.tech_defeats = {310, 0, 12, 0};
    reward = ov::add_run(p, b, 20261107);
    const std::vector<std::string> expected = {"gate_keeper", "blossom_data", "skull_data", "ten_minutes", "fifteen_minutes", "first_clear",
                                               "run_level_25", "thousand", "fire_adept", "caster", "first_awakening", "banker"};
    check(reward.new_records == expected, "the cleared run's records in table order");
    check(reward.sim_bits == 5100 && p.earned_total == 5190, "the clear bonus");
    check(p.form("p_agumon")->clears[0] == 1 && p.total("guardians") == 3 && p.total("defeat_e_blosso") == 1 && p.total("defeat_e_goburi") == 0,
          "clears and Guardians (a regular family is no Guardian)");
    check(p.records.at("gate_keeper") == 20261107, "the record's date");
    // What the records opened: the start set first, then the records' gear.
    const auto open = ov::gear_open(p);
    check(open == std::vector<std::string>{"cipher_core", "drain_ram", "assault_ram", "atlas_board", "exp_board", "achilles_board",
                                           "pwr_medal_fire", "mana_board", "beast_board", "bit_board"},
          "gear_open from the records");
    const auto opened = ov::opened_gear(p);
    check(opened.size() == ov::start_gear().size() + open.size() && opened[0] == "brave_core" && opened[ov::start_gear().size()] == "cipher_core",
          "opened_gear: the start set, then the records'");
    // A record a later version adds is checked against the totals the file has.
    ov::OverflowProfile old;
    old.totals["crates"] = 140;
    old.totals["defeats"] = 5000;
    const auto added = ov::check_records(old, 20261108);
    check(added == std::vector<std::string>{"thousand", "crate_breaker"}, "records from the file's totals");
    // Every record has a text and every gear key a slot.
    for (const auto& record : ov::record_table())
        check(std::string(record.title).size() > 0 && std::string(record.condition).ends_with(".") && ov::find_record(record.key) == &record,
              std::string("record ") + record.key);
    for (const auto& record : ov::record_table())
        for (const auto& gear : ov::record_gear(record.key))
            check(ov::gear_is_core(gear) + ov::gear_is_board(gear) + ov::gear_is_chip(gear) == 1, "gear " + gear);
    for (const char* gear : ov::start_gear()) check(ov::gear_is_core(gear) + ov::gear_is_board(gear) + ov::gear_is_chip(gear) == 1, gear);
}

void loadouts() {
    ov::OverflowProfile p;
    check(ov::loadout_allowed(p, {}), "no loadout");
    check(ov::loadout_allowed(p, {"brave_core", "wisdom_chip"}) && ov::loadout_allowed(p, {"fire_board", "pwr_medal"}), "start gear");
    check(!ov::loadout_allowed(p, {"wisdom_chip", ""}) && !ov::loadout_allowed(p, {"", "brave_core"}), "the wrong slots");
    check(!ov::loadout_allowed(p, {"", "drain_ram"}) && !ov::loadout_allowed(p, {"cipher_core", ""}), "unopened gear");
    p.records["shogun_data"] = 1;
    p.records["scorpio_data"] = 1;
    check(ov::loadout_allowed(p, {"brave_core", "resist_rom_fire"}) && ov::loadout_allowed(p, {"brave_core", "defence_rom"}), "a core-pocket chip with a Core");
    check(!ov::loadout_allowed(p, {"armor_board", "defence_rom"}) && !ov::loadout_allowed(p, {"", "resist_rom_ice"}),
          "a core-pocket chip needs a Core (8.3)");
    check(ov::loadout_allowed(p, {"resist_board", ""}), "the Resist Board from Shogun Data");
}

void forms() {
    ov::OverflowProfile p;
    check(ov::overflow_forms().size() == 16 && std::string(ov::overflow_forms()[0]) == "p_agumon" && std::string(ov::overflow_forms()[15]) == "p_bwargrey",
          "the face sheet's order");
    for (const char* rookie : {"p_agumon", "p_vmon", "p_girumon", "p_dorumon"}) check(ov::form_unlocked(p, rookie) && ov::rookie_form(rookie), rookie);
    for (std::size_t i = 4; i < 16; ++i) {
        const std::string model = ov::overflow_forms()[i];
        check(!ov::form_unlocked(p, model), model + " is locked at first");
        check(ov::form_unlock_text(model).ends_with("."), model + " has a condition text");
    }
    check(!ov::form_unlocked(p, "p_nobody"), "an unknown form");
    // The story save's forms (read only): bit n = shinka row n (WarGreymon 6).
    check(ov::form_unlocked(p, "p_wargrey", 1u << 6) && !ov::form_unlocked(p, "p_hekabut", 1u << 6), "a form the story reached");
    // The mode's own conditions (5.3).
    p.forms["p_agumon"].best_seconds = 601;
    check(ov::form_unlocked(p, "p_hekabut") && ov::form_unlocked(p, "p_wargrey") && !ov::form_unlocked(p, "p_weregaru"), "survive 10:00");
    p.forms["p_dorumon"].clears[0] = 1;
    check(ov::form_unlocked(p, "p_alpha") && !ov::form_unlocked(p, "p_susanoo"), "Dorumon's clear");
    for (const char* rookie : {"p_agumon", "p_vmon", "p_girumon"}) p.forms[rookie].clears[0] = 1;
    check(ov::form_unlocked(p, "p_susanoo") && ov::form_unlocked(p, "p_impdrapm") && ov::form_unlocked(p, "p_dukecrim"), "all four Rookies cleared");
    check(!ov::form_unlocked(p, "p_impdrafm"), "no Hard clear yet");
    p.forms["p_vmon"].clears[1] = 1;
    check(ov::form_unlocked(p, "p_impdrafm"), "a Hard clear");
    p.forms["p_vmon"].best_defeats = 1500;
    p.forms["p_girumon"].best_level = 40;
    check(ov::form_unlocked(p, "p_beelzeb") && ov::form_unlocked(p, "p_metalgaru"), "defeats in one run, run level 40");
    // The story's forms from a story engine (only read).
    {
        dw4::engine::Engine story{dw4::MountedResources{}};
        auto* sys = story.get<dw4::engine::GameSystem>();
        check(sys != nullptr, "a story engine");
        sys->players[0].base_isuse.clear();
        sys->players[0].base_isuse[0] = 1;
        sys->players[0].base_isuse[13] = 1; // MetalGarurumon
        sys->players[0].base_isuse[9] = 0;
        sys->players[1].base_isuse[10] = 1; // another player's: not the story character's
        const auto forms = ov::story_forms(story);
        check(forms == ((1u << 0) | (1u << 13)), "player 1's reached forms");
        ov::OverflowProfile empty;
        check(ov::form_unlocked(empty, "p_metalgaru", forms) && !ov::form_unlocked(empty, "p_bwargrey", forms) &&
                  !ov::form_unlocked(empty, "p_alpha", forms),
              "the story's forms open their faces");
        check(sys->players[0].base_isuse.size() == 3, "nothing written");
    }
}

void request_and_path() {
    ov::OverflowProfile p;
    p.sim_bits = 1000;
    ov::buy_plug(p, "p_agumon", 2);
    ov::buy_plug(p, "p_agumon", 0);
    p.loadouts["p_agumon"] = {"brave_core", "wisdom_chip"};
    p.records["blossom_data"] = 1;
    ov::RunRequest request;
    request.form = "p_agumon";
    ov::apply_meta(p, request);
    check(request.options.at("armor") == "brave_core" && request.options.at("chip") == "wisdom_chip", "the loadout's keys");
    check(request.options.at("plug_lab") == "5,0,5,0,0,0,0,0,0,0,0", "the Plug Lab points by UPCNT index");
    check(request.options.at("gear_open") == "drain_ram", "the records' gear");
    check(ov::parse_plug_lab(request.options.at("plug_lab"))[0] == 5 && ov::parse_plug_lab(request.options.at("plug_lab"))[2] == 5,
          "plug_lab parses back");
    check(ov::parse_plug_lab("1,2,3") == std::array<int, 11>{} && ov::parse_plug_lab("x") == std::array<int, 11>{} && ov::parse_plug_lab("") == std::array<int, 11>{},
          "a malformed plug_lab is none");
    // A test run's own options win.
    ov::RunRequest own;
    own.form = "p_agumon";
    own.options["chip"] = "speed_chip";
    own.options["plug_lab"] = "0,0,0,0,0,0,0,0,0,0,0";
    ov::apply_meta(p, own);
    check(own.options.at("chip") == "speed_chip" && own.options.at("armor") == "brave_core" && own.options.at("plug_lab") == "0,0,0,0,0,0,0,0,0,0,0",
          "the request's own options win");
    // A form without meta: nothing added.
    ov::RunRequest plain;
    plain.form = "p_vmon";
    ov::OverflowProfile empty;
    ov::apply_meta(empty, plain);
    check(plain.options.empty(), "no meta, no options");
    // A loadout that is not allowed (an edited file) is not used.
    p.loadouts["p_vmon"] = {"cipher_core", ""};
    ov::RunRequest edited;
    edited.form = "p_vmon";
    ov::apply_meta(p, edited);
    check(!edited.options.count("armor"), "an unopened armour from an edited file is not used");
    // The path rule.
    set_env("DW4_OVERFLOW_PROFILE", nullptr);
    check(ov::profile_path("C:/game", false) == fs::path("C:/game") / "saves" / "overflow.dat", "the program's saves folder");
    check(ov::profile_path("C:/game", true).empty(), "a test run writes nothing");
    set_env("DW4_OVERFLOW_PROFILE", "D:/tmp/test_overflow.dat");
    check(ov::profile_path("C:/game", true) == fs::path("D:/tmp/test_overflow.dat") && ov::profile_path("C:/game", false) == fs::path("D:/tmp/test_overflow.dat"),
          "DW4_OVERFLOW_PROFILE wins");
    set_env("DW4_OVERFLOW_PROFILE", nullptr);
    const auto day = ov::today();
    check(day > 20200101 && day < 21000101, "today");
}

// Lane D's LobbyMeta as overflow_lobby.hpp has it (feat-o1b-screens), for
// fill_lobby_meta.
struct FormRecord {
    int runs = 0;
    double best_seconds = 0;
    int best_level = 0;
    int clears = 0;
};
struct LobbyMeta {
    bool seen_intro = false;
    int sim_bits = 0;
    int earned_total = 0;
    std::map<std::string, FormRecord, std::less<>> forms;
    std::map<std::string, std::array<int, 7>, std::less<>> plugs;
    std::vector<std::string> armors{"", "brave_core", "armor_board", "fire_board", "ice_board", "bolt_board", "dark_board"};
    std::vector<std::string> chips{"", "wisdom_chip", "spirit_chip", "speed_chip", "pwr_medal"};
    std::vector<std::string> records;
    std::vector<std::string> locked_forms;
};

// ... and the parts of D's OverflowLobby and LobbyAction the glue uses.
struct StandLobby {
    LobbyMeta meta_;
    LobbyMeta& meta() { return meta_; }
    std::function<void()> on_intro_seen;
};
struct StandAction {
    enum class Kind { none, start_run, intro_seen, buy_plug, closed } kind = Kind::none;
    ov::RunRequest request;
    std::string form;
    int stat = 0;
};

void lobby() {
    ov::OverflowProfile p;
    LobbyMeta fresh;
    ov::fill_lobby_meta(p, fresh);
    const LobbyMeta stand_in;
    check(fresh.armors == stand_in.armors && fresh.chips == stand_in.chips, "a new profile: D's stand-in lists");
    check(fresh.locked_forms.size() == 12 && fresh.records.empty() && fresh.forms.empty() && fresh.plugs.empty(), "a new profile: 12 locked faces");
    p.seen_intro = true;
    p.sim_bits = 3000;
    p.earned_total = 7000;
    p.plugs["p_vmon"] = {1, 2, 3, 4, 5, 6, 7}; // by UPCNT index: MHP 1, MMP 2, STR 3, DEF 4, WIS 5, SPI 6, SPD 7
    p.forms["p_vmon"].runs = 4;
    p.forms["p_vmon"].best_seconds = 700;
    p.forms["p_vmon"].clears = {1, 1, 0};
    p.records["gate_keeper"] = 1;
    p.records["shogun_data"] = 1;
    LobbyMeta meta;
    ov::fill_lobby_meta(p, meta);
    check(meta.seen_intro && meta.sim_bits == 3000 && meta.earned_total == 7000, "the head");
    check(meta.plugs.at("p_vmon") == std::array<int, 7>{3, 4, 1, 2, 5, 6, 7}, "the Plug Lab rows: Strength, Def., MaxHP, MaxMP, Wisdom, Spirit, Speed");
    check(meta.forms.at("p_vmon").runs == 4 && meta.forms.at("p_vmon").clears == 2 && meta.forms.at("p_vmon").best_seconds == 700, "a form");
    check(meta.records == std::vector<std::string>{"gate_keeper", "shogun_data"}, "done records");
    check(std::find(meta.armors.begin(), meta.armors.end(), "cipher_core") != meta.armors.end() &&
              std::find(meta.armors.begin(), meta.armors.end(), "resist_board") != meta.armors.end() &&
              std::find(meta.chips.begin(), meta.chips.end(), "resist_rom_dark") != meta.chips.end() && meta.chips[0].empty(),
          "the records' gear in the lists");
    check(std::find(meta.locked_forms.begin(), meta.locked_forms.end(), "p_weregaru") == meta.locked_forms.end() &&
              std::find(meta.locked_forms.begin(), meta.locked_forms.end(), "p_hekabut") != meta.locked_forms.end(),
          "Veemon's 10:00 opens WereGarurumon");
    // The process-wide store: the screen's buy row and a started run (memory only).
    ov::meta().open({});
    auto profile = ov::meta().profile();
    profile.sim_bits = 1000;
    ov::meta().replace(profile);
    check(ov::buy_plug_row("p_agumon", 0) == ov::BuyResult::bought && ov::meta().profile().plug_steps("p_agumon", 2) == 1, "row 0 is Strength");
    check(ov::buy_plug_row("p_agumon", 2) == ov::BuyResult::bought && ov::meta().profile().plug_steps("p_agumon", 0) == 1, "row 2 is MaxHP");
    check(ov::buy_plug_row("p_agumon", 7) == ov::BuyResult::bad_stat, "no row 7");
    ov::RunRequest start;
    start.form = "p_girumon";
    start.options["armor"] = "fire_board";
    start.options["chip"] = "pwr_medal";
    start.options["tier"] = "normal";
    ov::remember_start(start);
    check(ov::meta().profile().loadout("p_girumon") == ov::Loadout{"fire_board", "pwr_medal"} && ov::meta().profile().last_form == "p_girumon",
          "a started run keeps its loadout and choice");
    start.options["chip"] = "drain_ram";
    ov::remember_start(start);
    check(ov::meta().profile().loadout("p_girumon").chip == "pwr_medal", "an unopened chip is not kept");

    // The merge's glue on stand-ins of D's OverflowLobby and LobbyAction (a file this time).
    Folder folder;
    const fs::path file = folder.root / "overflow.dat";
    ov::meta().open(file);
    StandLobby lobby;
    ov::connect_lobby(lobby);
    check(!lobby.meta().seen_intro && lobby.meta().sim_bits == 0 && lobby.on_intro_seen, "connected");
    lobby.on_intro_seen();
    check(lobby.meta().seen_intro && ov::meta().profile().seen_intro && fs::exists(file), "the first meeting is written");
    auto with_bits = ov::meta().profile();
    with_bits.sim_bits = 250;
    ov::meta().replace(with_bits);
    StandAction buy;
    buy.kind = StandAction::Kind::buy_plug;
    buy.form = "p_dorumon";
    buy.stat = 1; // Def.
    check(ov::lobby_action(lobby, buy) == ov::BuyResult::bought && lobby.meta().sim_bits == 150 && lobby.meta().plugs.at("p_dorumon")[1] == 1,
          "buy_plug: the row bought, the lobby's meta filled again");
    check(ov::lobby_action(lobby, buy) == ov::BuyResult::not_enough && lobby.meta().sim_bits == 150, "not enough for step 2 (200)");
    StandAction run;
    run.kind = StandAction::Kind::start_run;
    run.request.form = "p_dorumon";
    run.request.options["armor"] = "ice_board";
    run.request.options["chip"] = "speed_chip";
    ov::lobby_action(lobby, run);
    ov::MetaStore back;
    back.open(file);
    check(back.profile().loadout("p_dorumon") == ov::Loadout{"ice_board", "speed_chip"} && back.profile().plug_steps("p_dorumon", 3) == 1 &&
              back.profile().last_form == "p_dorumon",
          "start_run: the loadout and choice are in the file");
    ov::meta().open({});
}

} // namespace

int main() {
    try {
        format();
        damaged_text();
        store();
        plug_lab();
        defeat_bits();
        records_and_bits();
        loadouts();
        forms();
        request_and_path();
        lobby();
    } catch (const std::exception& error) {
        std::cerr << "overflow_profile_tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "overflow_profile_tests: " << checks << " checks passed\n";
    return 0;
}
