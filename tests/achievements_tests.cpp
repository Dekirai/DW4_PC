// Local achievements: the rule list, the profile file, and the tracker's
// rules and gates (cheats, rebalancing, save states, tests and bots, the
// randomizer's own category) on synthetic steps and engine events; the
// online catalogue against data/online/achievements.json (argument 1) and
// the rules of online rooms (own slot, owner, "was there").
// `dw4_achievements_tests --export FILE` writes the catalogue file.
#include "achievements.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::game;
using dw4::engine::PortEvent;
using Kind = PortEvent::Kind;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

AchievementStep live_step(std::uint64_t serial = 1) {
    AchievementStep step;
    step.stage = "stag0101";
    step.live = true;
    step.stage_serial = serial;
    auto& p = step.players[0];
    p.joined = true;
    p.local = true;
    p.unique = 0x1234;
    p.level = 1;
    p.hp = p.max_hp = 100;
    p.forms[0] = true;
    return step;
}

bool has(const std::vector<AchievementUnlock>& unlocks, const char* id) {
    for (const auto& u : unlocks)
        if (u.def && std::string(u.def->id) == id) return true;
    return false;
}

PortEvent boss(const char* key, int difficulty = 0) { return {Kind::boss_defeated, -1, difficulty, 0, key}; }

void rule_list() {
    const auto list = achievement_list();
    check(list.size() >= 40 && list.size() <= 80, "40 to 80 achievements: " + std::to_string(list.size()));
    std::set<std::string> ids;
    for (const auto& def : list) {
        check(ids.insert(def.id).second, std::string("unique id ") + def.id);
        check(*def.title && *def.description, "title and description");
        for (const char* c = def.id; *c; ++c) check((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_', "id characters");
        check((def.stat == nullptr) == (def.goal == 0), std::string("a counter has a goal: ") + def.id);
        check(find_achievement(def.id) == &def, "lookup");
    }
    check(!find_achievement("nope"), "unknown id");
    check(std::string(achievement_category_name(AchievementCategory::multiplayer)) == "Together", "category names");
}

void profile_file() {
    AchievementProfile profile;
    profile.unlocked["boss_first"] = 1761700000;
    profile.unlocked_randomizer["kills_100"] = 1761700001;
    profile.stats["kills"] = 123;
    profile.characters[0xDEADBEEF] = {5000, 2, true, false, true, true};
    const auto text = profile.serialize();
    const auto back = AchievementProfile::parse(text);
    check(back.has_value(), "parses its own text");
    check(back->unlocked == profile.unlocked && back->unlocked_randomizer == profile.unlocked_randomizer && back->stats == profile.stats,
          "unlocks and stats round trip");
    const auto& c = back->characters.at(0xDEADBEEF);
    check(c.play_frames == 5000 && c.defeats == 2 && c.from_start && !c.assisted && c.coop_very_hard && c.beyond_rookie, "characters round trip");
    check(!AchievementProfile::parse("garbage\nunlock boss_first 1\n"), "a foreign file is refused");
    const auto lenient = AchievementProfile::parse("DW4PROFILE 1\nunlock no_such_id 5\nunlock boss_first 9\nwhatever\nstat x\nchar 1 2\n");
    check(lenient && lenient->unlocked.size() == 1 && lenient->unlocked.at("boss_first") == 9 && lenient->characters.empty(),
          "unknown and broken lines are skipped");
    const auto folder = std::filesystem::temp_directory_path() / ("dw4_profile_test_" + std::to_string(std::rand()));
    const auto file = folder / "saves" / "profile.dat";
    check(profile.save(file), "saved");
    auto loaded = AchievementProfile::load(file);
    check(loaded && loaded->stats.at("kills") == 123, "loaded");
    loaded->stats["kills"] = 124;
    check(loaded->save(file), "saved again");
    auto backup = file;
    backup += ".bak";
    check(std::filesystem::exists(backup) && AchievementProfile::load(backup)->stats.at("kills") == 123, "the previous file kept as .bak");
    check(AchievementProfile::load(file)->stats.at("kills") == 124, "the new values");
    // A profile cut short never replaces the last good .bak.
    {
        auto cut = loaded->serialize();
        cut.resize(cut.size() - 6);
        std::ofstream(file, std::ios::binary | std::ios::trunc) << cut;
    }
    loaded->stats["kills"] = 125;
    check(loaded->save(file), "saved over a cut file");
    check(AchievementProfile::load(backup)->stats.at("kills") == 123 && AchievementProfile::load(file)->stats.at("kills") == 125,
          "the good .bak stays");
    check(!AchievementProfile::load(folder / "missing.dat"), "a missing file");
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}

void bosses_and_gates() {
    AchievementTracker tracker;
    const auto step = live_step();
    auto unlocks = tracker.observe(step, {}, AchievementGate::open, false, 100);
    check(unlocks.empty(), "nothing yet");
    const PortEvent apocalymon[] = {boss("apocaly")};
    unlocks = tracker.observe(step, apocalymon, AchievementGate::cheats, false, 101);
    check(unlocks.empty() && !tracker.unlocked("story_apocalymon"), "cheats: no unlock");
    unlocks = tracker.observe(step, apocalymon, AchievementGate::open, true, 102);
    check(has(unlocks, "story_apocalymon") && unlocks.front().randomizer, "the randomizer's own category");
    check(tracker.unlocked("story_apocalymon", true) && !tracker.unlocked("story_apocalymon"), "kept apart");
    unlocks = tracker.observe(step, apocalymon, AchievementGate::open, false, 103);
    check(has(unlocks, "story_apocalymon") && has(unlocks, "boss_first") && has(unlocks, "nodmg_boss") && has(unlocks, "lowlevel_apocalymon"),
          "the boss, the first boss, no damage, the low level");
    check(tracker.profile().unlocked.at("story_apocalymon") == 103, "the unlock time");
    unlocks = tracker.observe(step, apocalymon, AchievementGate::open, false, 104);
    check(unlocks.empty(), "unlocks only once");
    for (const auto gate : {AchievementGate::disabled, AchievementGate::test_run, AchievementGate::bot, AchievementGate::replay,
                            AchievementGate::save_state, AchievementGate::rebalancing}) {
        AchievementTracker gated;
        const PortEvent kill[] = {{Kind::enemy_defeated, 0, 3, 0, "e_goburi"}};
        check(gated.observe(step, kill, gate, false, 1).empty() && gated.profile().stat("kills") == 0, "a closed gate counts nothing");
        check(*achievement_gate_text(gate), "gate text");
    }
}

void damage_and_stages() {
    AchievementTracker tracker;
    auto step = live_step(1);
    tracker.observe(step, {}, AchievementGate::open, false, 1);
    step.players[0].hp = 80; // hit in this stage
    tracker.observe(step, {}, AchievementGate::open, false, 2);
    const PortEvent blossomon[] = {boss("blosso")};
    auto unlocks = tracker.observe(step, blossomon, AchievementGate::open, false, 3);
    check(has(unlocks, "boss_blossomon") && !has(unlocks, "nodmg_boss"), "damage taken: no flawless boss");
    // A new stage: its record starts clean (healing is no damage).
    auto next = live_step(2);
    next.players[0].hp = 80;
    tracker.observe(next, {}, AchievementGate::open, false, 4);
    next.players[0].hp = 100;
    tracker.observe(next, {}, AchievementGate::open, false, 5);
    const PortEvent skull[] = {boss("skulgrey")};
    unlocks = tracker.observe(next, skull, AchievementGate::open, false, 6);
    check(has(unlocks, "boss_skullgreymon") && has(unlocks, "nodmg_boss"), "a clean stage");
    check(tracker.profile().stat("dungeon_bosses") == 2, "two dungeon bosses");
    for (const char* key : {"mammos", "anomalo", "tonogeko", "mecha4", "diabolo", "mecha5"}) {
        const PortEvent e[] = {boss(key)};
        tracker.observe(next, e, AchievementGate::open, false, 7);
    }
    check(!tracker.unlocked("boss_dungeon_all"), "eight of nine");
    next.stage = "dng412b4";
    const PortEvent knight[] = {boss("ldknight")};
    unlocks = tracker.observe(next, knight, AchievementGate::open, false, 8);
    check(has(unlocks, "boss_dungeon_all") && has(unlocks, "story_lordknightmon"), "all nine; LordKnightmon in the Electro Mine is story");
    // Together.
    auto couch = live_step(3);
    couch.players[1] = couch.players[0];
    couch.players[1].unique = 99;
    unlocks = tracker.observe(couch, blossomon, AchievementGate::open, false, 9);
    check(has(unlocks, "mp_couch") && !has(unlocks, "mp_fullhouse"), "two players");
}

void story_runs() {
    // A new character (level 1, no EXP) through a clean run.
    {
        AchievementTracker tracker;
        auto step = live_step();
        tracker.observe(step, {}, AchievementGate::open, false, 1);
        check(tracker.profile().characters.at(0x1234).from_start, "tracked from the start");
        step.players[0].level = 80;
        step.players[0].exp = 999999;
        step.stage = "stagBS06";
        const PortEvent daemon[] = {boss("devl3", 0), {Kind::story_flag, -1, 406, 1, {}}};
        const auto unlocks = tracker.observe(step, daemon, AchievementGate::open, false, 2);
        for (const char* id : {"story_daemon", "clear_normal", "deathless_clear", "rookie_clear", "speed_normal", "speed_fast"})
            check(has(unlocks, id), std::string("clean run: ") + id);
        check(!has(unlocks, "solo_veryhard") && !has(unlocks, "nodmg_daemon_vh"), "not Very Hard");
    }
    // Defeated once, a mega form, a cheat once: the run achievements stay locked.
    {
        AchievementTracker tracker;
        auto step = live_step();
        tracker.observe(step, {}, AchievementGate::open, false, 1);
        const PortEvent down[] = {{Kind::player_defeated, 0, 0, 0, {}}};
        auto unlocks = tracker.observe(step, down, AchievementGate::open, false, 2);
        check(has(unlocks, "first_defeat") && tracker.profile().characters.at(0x1234).defeats == 1, "a defeat counts");
        step.players[0].species = 6;
        tracker.observe(step, {}, AchievementGate::open, false, 3);
        const PortEvent clear[] = {boss("devl3", 0)};
        unlocks = tracker.observe(step, clear, AchievementGate::open, false, 4);
        check(has(unlocks, "clear_normal") && !has(unlocks, "deathless_clear") && !has(unlocks, "rookie_clear") && has(unlocks, "speed_fast"),
              "a defeat and a mega form");
    }
    {
        AchievementTracker tracker;
        auto step = live_step();
        tracker.observe(step, {}, AchievementGate::open, false, 1);
        tracker.observe(step, {}, AchievementGate::save_state, false, 2);
        check(tracker.profile().characters.at(0x1234).assisted, "a save state marks the character");
        step.difficulty = 2;
        step.stage_serial = 9;
        const PortEvent clear[] = {boss("devl3", 2)};
        const auto unlocks = tracker.observe(step, clear, AchievementGate::open, false, 3);
        check(has(unlocks, "clear_veryhard") && has(unlocks, "nodmg_daemon_vh") && !has(unlocks, "speed_fast") && !has(unlocks, "solo_veryhard"),
              "assisted: the clear, not the run achievements");
    }
    // A character first seen in the middle of the game is not a new run.
    {
        AchievementTracker tracker;
        auto step = live_step();
        step.players[0].level = 40;
        step.players[0].exp = 5000;
        tracker.observe(step, {}, AchievementGate::open, false, 1);
        const PortEvent clear[] = {{Kind::story_flag, -1, 407, 1, {}}};
        const auto unlocks = tracker.observe(step, clear, AchievementGate::open, false, 2);
        check(has(unlocks, "clear_hard") && !has(unlocks, "deathless_clear"), "mid-game character");
    }
}

void counters_and_state() {
    AchievementTracker tracker;
    auto step = live_step();
    std::vector<PortEvent> kills(100, PortEvent{Kind::enemy_defeated, 0, 1, 0, "e_numemon"});
    kills.push_back({Kind::enemy_defeated, 2, 1, 0, "e_goburi"}); // slot 3 is not this machine's
    auto unlocks = tracker.observe(step, kills, AchievementGate::open, false, 1);
    check(has(unlocks, "kills_100") && tracker.profile().stat("kills") == 100 && tracker.profile().stat("numemon_kills") == 100, "kills");
    const PortEvent quests[] = {{Kind::quest_done, -1, 1, 0, {}}, {Kind::quest_done, -1, 2, 0, {}}};
    unlocks = tracker.observe(step, quests, AchievementGate::open, false, 2);
    check(has(unlocks, "quest_first") && tracker.profile().stat("quests") == 2, "quests");
    for (int q = 3; q <= 8; ++q) {
        const PortEvent e[] = {{Kind::quest_done, -1, q, 0, {}}};
        unlocks = tracker.observe(step, e, AchievementGate::open, false, 3);
    }
    check(tracker.unlocked("quest_all"), "all eight quests");
    step.stage = "dngq8b3v";
    const PortEvent vh[] = {{Kind::quest_done, -1, 8, 2, {}}};
    check(has(tracker.observe(step, vh, AchievementGate::open, false, 4), "quest_veryhard"), "a Very Hard quest dungeon");
    const PortEvent misc[] = {{Kind::technique, 0, 44, 4, {}}, {Kind::card, 0, 9, 2, {}}, {Kind::form_reached, 0, 15, 0, {}},
                              {Kind::critical_hit, 0, 0, 0, {}}};
    unlocks = tracker.observe(step, misc, AchievementGate::open, false, 5);
    for (const char* id : {"tech_first", "tech_rank5", "card_first", "card_rare", "form_evolve", "form_susanoomon"})
        check(has(unlocks, id), std::string("events: ") + id);
    check(tracker.profile().stat("criticals") == 1, "a critical hit");
    auto& p = step.players[0];
    p.level = 99;
    p.bits = 9999999;
    p.devices = 36;
    p.best_rank = 5;
    p.best_value = 1200;
    p.techniques = 6;
    for (int f = 0; f < 16; ++f) {
        p.forms[std::size_t(f)] = true;
        p.form_levels[std::size_t(f)] = 99;
    }
    step.cards = 50;
    step.numemon_freed = 10;
    unlocks = tracker.observe(step, {}, AchievementGate::open, false, 6);
    for (const char* id : {"lv_10", "lv_50", "lv_99", "bits_100k", "bits_1m", "bits_max", "hoarder", "item_rank5", "item_pink", "tech_full",
                           "form_rookies", "form_eight", "form_all", "form_master", "card_25", "card_50", "numemon_free"})
        check(has(unlocks, id), std::string("state: ") + id);
    check(!tracker.unlocked("lv_200"), "not level 200");
    // In-game time: only live steps count.
    AchievementTracker clock;
    auto idle = live_step();
    idle.live = false;
    clock.observe(idle, {}, AchievementGate::open, false, 1);
    check(clock.profile().stat("play_fields") == 0, "a load is no play time");
    clock.profile().stats["play_fields"] = 215784 - 1; // an hour less one field
    clock.observe(live_step(), {}, AchievementGate::open, false, 2);
    check(clock.unlocked("time_1h") && clock.profile().stat("play_minutes") == 60, "one hour");
    // Online.
    AchievementTracker online;
    auto session = live_step();
    session.online = true;
    session.hosting = true;
    session.online_players = 3;
    session.players[1] = session.players[0];
    session.players[1].local = false;
    session.players[1].unique = 77;
    unlocks = online.observe(session, {}, AchievementGate::open, false, 1);
    check(has(unlocks, "online_join") && has(unlocks, "online_host"), "online session");
    {
        // Round I1a (19.8.9): the world's client gives "Connected" only as a
        // guest in someone else's instance, never at home.
        AchievementTracker home;
        auto at_home = live_step();
        at_home.online = true;
        at_home.joined_other = false;
        check(!has(home.observe(at_home, {}, AchievementGate::open, false, 1), "online_join"), "at home: no online_join");
        at_home.joined_other = true;
        check(has(home.observe(at_home, {}, AchievementGate::open, false, 2), "online_join"), "a guest in another's instance: online_join");
    }
    const PortEvent remote_kill[] = {{Kind::enemy_defeated, 1, 1, 0, "e_goburi"}};
    online.observe(session, remote_kill, AchievementGate::open, false, 2);
    check(online.profile().stat("kills") == 0, "another machine's kill");
    check(!online.profile().characters.count(77), "another machine's character is not tracked here");
    // A partner back on their feet.
    AchievementTracker raise;
    auto pair = live_step();
    pair.players[1] = pair.players[0];
    pair.players[1].unique = 5;
    pair.players[1].hp = 0;
    raise.observe(pair, {}, AchievementGate::open, false, 1);
    pair.players[1].hp = 40;
    check(has(raise.observe(pair, {}, AchievementGate::open, false, 2), "raise_ally"), "raised");
    check(tracker.dirty(), "changes mark the profile for saving");
}

// The online catalogue (docs/SEAMLESS.md 11): one online form per
// achievement, its goals in the server's units agree with the local goals,
// and the checked-in JSON the server's tests read is this table.
void online_catalogue(const std::string& json_file) {
    const auto list = achievement_list();
    const auto online = achievement_online_list();
    check(online.size() == list.size() && list.size() == 72, "72 achievements, each with its online form");
    std::set<std::string> server_stats;
    for (const auto& c : achievement_online_counters()) server_stats.insert(c.server_stat);
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& def = list[i];
        const auto& o = online[i];
        const std::string id = def.id;
        check(id == o.id, "the same order: " + id);
        check(find_achievement_online(id) == &o, "online lookup " + id);
        check((o.kind == AchievementOnlineKind::stat) == (o.server_stat != nullptr), "a server counter exactly for stats: " + id);
        if (o.kind == AchievementOnlineKind::stat) {
            check(o.goal > 0 && server_stats.count(o.server_stat), "a stat's counter is batched: " + id);
            // The local goal in the server's units.
            const std::string local = def.stat ? def.stat : "";
            const std::int64_t expected = local == "play_minutes" ? def.goal * 60 : def.goal;
            check(o.goal == expected, "the server's goal is the local one: " + id);
        }
        if (o.kind == AchievementOnlineKind::state && def.stat)
            check(o.goal == def.goal, "a state's goal is the local one: " + id);
        if (o.owner_only) check(o.kind == AchievementOnlineKind::event && def.category == AchievementCategory::challenge, "owner only: story challenges " + id);
        check(*achievement_category_key(def.category), "a server category: " + id);
    }
    check(find_achievement_online("speed_normal")->goal == 8 * 3600 && find_achievement_online("speed_fast")->goal == 4 * 3600,
          "the speed limits of the client: 8 h and 4 h");
    check(find_achievement_online("lowlevel_apocalymon")->goal == 12, "Underdog at level 12");
    const auto text = achievement_catalogue_json();
    check(text.find('\r') == std::string::npos && text.ends_with("]\n}\n"), "LF, complete");
    if (json_file.empty()) return;
    std::ifstream in(json_file, std::ios::binary);
    check(bool(in), "the checked-in catalogue " + json_file);
    std::ostringstream file;
    file << in.rdbuf();
    std::string checked_in = file.str();
    std::erase(checked_in, '\r'); // (a checkout with CRLF line ends)
    check(checked_in == text, "data/online/achievements.json is the client's catalogue (regenerate: dw4_achievements_tests --export FILE)");
}

// Online rooms: this machine's slot is the story character, the owner alone
// gets the story challenges, a story collection counts only when it rises here.
void online_rooms() {
    auto room_step = [](int own, bool owner) {
        AchievementStep step;
        step.stage = "stagBS06";
        step.live = true;
        step.stage_serial = 7;
        step.online = true;
        step.online_players = 2;
        step.shared_story = true;
        step.own_slot = own;
        step.story_owner = owner;
        for (int s = 0; s < 2; ++s) {
            auto& p = step.players[std::size_t(s)];
            p.joined = true;
            p.local = s == own;
            p.unique = 100 + std::uint32_t(s);
            p.level = 1;
            p.hp = p.max_hp = 100;
        }
        return step;
    };
    // The guest in slot 1 (slot 0 is the owner): the clear, not the challenges.
    {
        AchievementTracker guest;
        auto step = room_step(1, false);
        guest.observe(step, {}, AchievementGate::open, false, 1);
        check(guest.profile().characters.count(101) && !guest.profile().characters.count(100), "only the own slot's character");
        step.players[1].level = 10;
        step.players[0].level = 90;
        const PortEvent daemon[] = {boss("devl3", 0), {Kind::story_flag, -1, 406, 1, {}}};
        const auto unlocks = guest.observe(step, daemon, AchievementGate::open, false, 2);
        check(has(unlocks, "story_daemon") && has(unlocks, "clear_normal"), "was there");
        check(!has(unlocks, "speed_fast") && !has(unlocks, "deathless_clear") && !has(unlocks, "rookie_clear"), "a guest: no story challenges");
        const PortEvent apocalymon[] = {boss("apocaly")};
        check(has(guest.observe(step, apocalymon, AchievementGate::open, false, 3), "lowlevel_apocalymon"), "the own slot's level (10), not slot 0's (90)");
    }
    // The owner in slot 1: its own character's run.
    {
        AchievementTracker owner;
        auto step = room_step(1, true);
        owner.observe(step, {}, AchievementGate::open, false, 1);
        const PortEvent clear[] = {{Kind::story_flag, -1, 406, 1, {}}};
        const auto unlocks = owner.observe(step, clear, AchievementGate::open, false, 2);
        check(has(unlocks, "clear_normal") && has(unlocks, "speed_fast") && has(unlocks, "deathless_clear"), "the owner's challenges");
        const PortEvent card[] = {{Kind::card, 0, 9, 2, {}}};
        check(!has(owner.observe(step, card, AchievementGate::open, false, 3), "card_rare"), "slot 0's card is not this machine's");
    }
    // Numemon: online only the Numemon freed in this account's own live steps
    // count (round G-core, docs/SEAMLESS.md 8.8: a mask in the profile); the
    // room story's flags never do.
    {
        AchievementTracker member;
        auto step = room_step(0, false);
        step.numemon_freed = 10;
        check(!has(member.observe(step, {}, AchievementGate::open, false, 1), "numemon_free"), "flags from the room story");
        const PortEvent freed[] = {{Kind::story_flag, -1, 95, 1, {}}};
        check(!has(member.observe(step, freed, AchievementGate::open, false, 2), "numemon_free"),
              "the tenth freed here where the owner freed nine is one");
        check(member.profile().stat("numemon_freed") == 1 && member.profile().stat("numemon_mask") == (1 << 6), "one Numemon in the mask");
        // The same Numemon again (another room's story) adds nothing; the other nine freed here complete it.
        member.observe(step, freed, AchievementGate::open, false, 3);
        check(member.profile().stat("numemon_freed") == 1, "a Numemon counts once");
        std::vector<PortEvent> rest;
        for (int f = 89; f <= 98; ++f)
            if (f != 95) rest.push_back({Kind::story_flag, -1, f, 1, {}});
        check(has(member.observe(step, rest, AchievementGate::open, false, 4), "numemon_free"), "all ten freed in own live steps");
        AchievementTracker offline;
        auto local = live_step();
        local.numemon_freed = 10;
        check(has(offline.observe(local, {}, AchievementGate::open, false, 1), "numemon_free"), "offline: the flags count as they are");
    }
    // Bosses are counted for the online counters.
    {
        AchievementTracker t;
        const PortEvent e[] = {boss("blosso")};
        t.observe(live_step(), e, AchievementGate::open, false, 1);
        check(t.profile().stat("bosses") == 1, "a boss counted");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        // dw4_achievements_tests --export FILE: writes data/online/achievements.json.
        if (argc >= 3 && std::string(argv[1]) == "--export") {
            std::ofstream(argv[2], std::ios::binary | std::ios::trunc) << achievement_catalogue_json();
            std::cout << "wrote " << argv[2] << '\n';
            return 0;
        }
        rule_list();
        profile_file();
        bosses_and_gates();
        damage_and_stages();
        story_runs();
        counters_and_state();
        online_catalogue(argc >= 2 ? argv[1] : "");
        online_rooms();
        std::cout << checks << " achievement checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Achievement test failure: " << error.what() << '\n';
        return 1;
    }
}
