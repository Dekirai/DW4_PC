// Seamless online co-op, the engine side (docs/SEAMLESS.md 3.7, 6.5-6.9, 7):
// snapshot 43 bytes, the canonical form, carries and barriers on the wire,
// the session value tables against data/online/session_values.txt, the
// forbidden environment list against every getenv("DW4_..") in src, the
// rules (input masking, pad redirect, cheats, resets, guards) on an engine
// without data, and - with the game data (argument 1, the CMake cache value
// DW4_TEST_DATA) - membership in real stages: sequential engines fed the same
// pads and membership steps stay net-fingerprint-equal, and a joiner restored
// from snapshot bytes plus the log equals the instance it joins.
#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/engine/online_membership.hpp"
#include "online/engine_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <xmmintrin.h>

#ifndef DW4_SOURCE_DIR
#define DW4_SOURCE_DIR "."
#endif

namespace {
using namespace dw4::engine;
using dw4::PadSample;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
const std::filesystem::path kSource = DW4_SOURCE_DIR;

// ---------------------------------------------------------------------------
// Bytes
// ---------------------------------------------------------------------------
GameSystem::PlayerSlot sample_player(const char* digimon, int level) {
    GameSystem::PlayerSlot p;
    p.joined = true;
    p.unique = 0x1234u + std::uint32_t(level);
    p.name = u"ＫＩＭ";
    p.digimon = digimon;
    p.weapons = {"w_common01", "", ""};
    p.hp = 77;
    p.max_hp = 120;
    p.stats.level = level;
    p.stats.ready = true;
    p.bits = 4321;
    p.base_flags[960] = 1;
    return p;
}

void snapshot_bytes() {
    check(snapshot_version() == 43, "snapshot version 43");
    StageSnapshot s;
    s.stage = "dng101b1";
    s.frame = 4711;
    s.flags = {{1, 1}, {409, 1}};
    s.counters = {{2, 902}};
    auto sys = std::make_shared<GameSystem>();
    sys->direct_start = true;
    sys->text_call = 67;
    sys->players[1] = sample_player("p_dorumon", 12);
    sys->gate_stage_name = "stag0101";
    sys->gate_stage_no = 3;
    s.system = sys;
    s.online.enabled = true;
    s.online.present = 0b0101;
    s.online.leaving = 0b0100;
    s.online.player_cameras = 0b0101;
    s.online.leash_off = true;
    s.online.boss_lock = true;
    PersonalSession personal;
    personal.gate_stage_name = "Player";
    personal.gate_stage_no = 2;
    personal.flags = {{572, 1}, {908, 0}};
    s.personal[2] = personal;
    const auto bytes = encode_snapshot(s);
    const auto back = decode_snapshot(bytes);
    const auto* system = static_cast<const GameSystem*>(back.system.get());
    check(system && system->direct_start, "direct_start survives the bytes");
    check(system->text_call == 67, "text_call survives the bytes");
    check(system->players[1].digimon == "p_dorumon" && system->players[1].stats.level == 12, "the player slots survive");
    check(back.online == s.online, "the online rules survive the bytes");
    check(back.personal[2] == personal && !back.personal[0], "the personal values survive the bytes");
    check(encode_snapshot(back) == bytes, "decode + encode gives the same bytes");
    const std::uint8_t abc[] = {'a', 'b', 'c'};
    check(online_hash(abc) == 0xe71fa2190541574bull, "online_hash is FNV-1a 64");
    check(online_hash(bytes) == online_hash(encode_snapshot(back)), "equal bytes, equal hash");
    // Without sound streams the canonical form is the raw form.
    check(encode_snapshot(s, SnapshotForm::canonical) == bytes, "no streams: canonical == raw");
    // A version 42 state no longer loads.
    auto old = bytes;
    old[0] = 42;
    bool refused = false;
    try {
        (void)decode_snapshot(old);
    } catch (const std::exception&) {
        refused = true;
    }
    check(refused, "a version 42 snapshot is refused");
}

void carries_and_barriers() {
    PlayerCarry carry;
    carry.slot = sample_player("p_vmon", 20);
    carry.chip.stage = "stag0102";
    carry.chip.bits = 99;
    carry.personal.gate_stage_name = "stag0203";
    carry.personal.gate_stage_no = 1;
    carry.personal.flags = {{24, 1}};
    carry.record = {1, 2, 3};
    const auto bytes = encode_carry(carry);
    const auto back = decode_carry(bytes);
    check(back.slot.digimon == "p_vmon" && back.slot.stats.level == 20 && back.slot.name == carry.slot.name, "the carry's slot");
    check(back.chip.stage == "stag0102" && back.chip.bits == 99, "the carry's capsule");
    check(back.personal == carry.personal && back.record == carry.record, "the carry's personal values and record");
    check(encode_carry(back) == bytes, "carry bytes are stable");
    bool truncated = false;
    try {
        (void)decode_carry(std::span<const std::uint8_t>(bytes.data(), bytes.size() - 1));
    } catch (const std::exception&) {
        truncated = true;
    }
    check(truncated, "a truncated carry throws");

    BarrierResult barrier;
    barrier.decision = Decision::new_instance;
    barrier.slot_map = {1, 0, -1, -1};
    barrier.co_arrivals.push_back({2, carry});
    barrier.own_slot = 1;
    barrier.story.emplace();
    barrier.story->flags[5] = 1;
    barrier.story->revision = 7;
    barrier.dropped.flags = {300};
    barrier.rules.enabled = true;
    barrier.rules.scoped_scenes = true;
    const auto b = encode_barrier(barrier);
    const auto bb = decode_barrier(b);
    check(bb.decision == Decision::new_instance && bb.slot_map == barrier.slot_map && bb.own_slot == 1, "the barrier's decision and slots");
    check(bb.co_arrivals.size() == 1 && bb.co_arrivals[0].first == 2 && bb.co_arrivals[0].second.slot.digimon == "p_vmon", "co-arrivals");
    check(bb.story && bb.story->flags[5] == 1 && bb.story->revision == 7 && bb.dropped.flags == barrier.dropped.flags, "story fields");
    check(bb.rules == barrier.rules, "rules");
    check(encode_barrier(bb) == b, "barrier bytes are stable");
    auto broken = b;
    broken[8] = 9; // the decision byte after magic and version
    bool unknown = false;
    try {
        (void)decode_barrier(broken);
    } catch (const std::exception&) {
        unknown = true;
    }
    check(unknown, "an unknown decision throws");
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------
void session_tables() {
    std::ifstream file(kSource / "data/online/session_values.txt");
    check(bool(file), "data/online/session_values.txt is readable");
    std::set<int> context_flags, context_counters, context_folders, personal_flags;
    bool gatestage_personal = false;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string cls, kind;
        int index = -1;
        in >> cls >> kind;
        if (kind != "gatestage") in >> index;
        if (cls == "context" && kind == "flag") context_flags.insert(index);
        if (cls == "context" && kind == "counter") context_counters.insert(index);
        if (cls == "context" && kind == "folder") context_folders.insert(index);
        if (cls == "personal" && kind == "flag") personal_flags.insert(index);
        if (cls == "personal" && kind == "gatestage") gatestage_personal = true;
    }
    const auto& t = session_value_tables();
    auto as_set = [](const std::vector<std::int32_t>& v) { return std::set<int>(v.begin(), v.end()); };
    check(as_set(t.context_flags) == context_flags, "context flags match the census file");
    check(as_set(t.context_counters) == context_counters, "context counters match the census file");
    check(as_set(t.context_folders) == context_folders, "context folder entries match the census file");
    check(as_set(t.personal_flags) == personal_flags, "personal flags match the census file");
    check(gatestage_personal, "GATESTAGE is personal");
}

bool env_listed(const std::string& name, const std::vector<std::string_view>& list) {
    for (const auto entry : list) {
        if (!entry.empty() && entry.back() == '*') {
            if (name.rfind(std::string(entry.substr(0, entry.size() - 1)), 0) == 0) return true;
        } else if (name == entry) {
            return true;
        }
    }
    return false;
}

void forbidden_environment() {
    std::set<std::string> found;
    const std::regex pattern(R"re(getenv\("(DW4_[A-Z0-9_]+)"\))re");
    for (const auto& entry : std::filesystem::recursive_directory_iterator(kSource / "src")) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".cpp" && ext != ".hpp") continue;
        std::ifstream file(entry.path());
        const std::string text((std::istreambuf_iterator<char>(file)), {});
        for (std::sregex_iterator it(text.begin(), text.end(), pattern), end; it != end; ++it) found.insert((*it)[1].str());
    }
    check(found.size() > 50, "the grep found the engine's variables");
    for (const auto& name : found)
        check(env_listed(name, online_forbidden_env()) || env_listed(name, online_presentation_env()),
              name + " is neither forbidden online nor marked presentation-only (online.cpp)");
    for (const auto name : online_forbidden_env())
        check(!env_listed(std::string(name), online_presentation_env()), std::string(name) + " is in both lists");
    check(env_listed("DW4_GOD", online_forbidden_env()) && env_listed("DW4_FIXED_RANDOM", online_forbidden_env()), "cheats and the clock are forbidden");
}

// ---------------------------------------------------------------------------
// Rules without game data
// ---------------------------------------------------------------------------
void rules_offline_defaults() {
    Engine engine{dw4::MountedResources{}};
    check(!engine.online().enabled && engine.online() == OnlineRules{}, "online is off by default");
    check(engine.pad_reader_slot(0, 2) == 0 && engine.pad_reader_slot(3, -1) == 3, "offline the pad reader is the index");
    check(engine.leader() == 0, "offline the leader is slot 0");
    check(!engine.pending_stage(), "no pending stage");
    check(engine.snapshot_bytes(SnapshotForm::raw).empty(), "no snapshot before the first load");
}

void rules_cheats_and_resets() {
    Engine engine{dw4::MountedResources{}};
    engine.cheats().god = true;
    engine.cheats().exp_rate = 300;
    engine.cheats().rand_seed = 77;
    engine.cheats().rand_enemies = 1;
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 1;
    engine.set_online(rules);
    check(!engine.cheats().god && engine.cheats().exp_rate == 100, "online: no cheats, no rebalancing");
    check(engine.cheats().rand_seed == 77 && engine.cheats().rand_enemies == 1, "online: the room's randomizer stays");
    check(!engine.console_reset().enabled(), "online: no console reset");
    engine.set_reset_enabled(true);
    std::array<PadSample, 4> pads{};
    engine.step(pads);
    check(!engine.console_reset().enabled(), "an online step switches the console reset off again");
    engine.set_online(OnlineRules{});
}

void rules_input_masking() {
    Engine engine{dw4::MountedResources{}};
    engine.set_pad_input_enabled(true);
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b0011;
    rules.leaving = 0b0010;
    engine.set_online(rules);
    std::array<PadSample, 4> pads{};
    for (auto& p : pads) {
        p.connected = true;
        p.buttons = dw4::pad_button::a;
    }
    engine.step(pads);
    const auto& states = engine.pads();
    check(states[0].connected && (states[0].buttons.held & dw4::pad_button::a), "a present slot gets its pad");
    check(states[1].connected && states[1].buttons.held == 0, "a leaving slot is neutral and connected");
    check(!states[2].connected && states[2].buttons.held == 0 && !states[3].connected, "absent slots are neutral and disconnected");
    engine.set_online(OnlineRules{});
}

void rules_pad_redirect() {
    Engine engine{dw4::MountedResources{}};
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b0110; // slot 0 absent after a fork
    engine.set_online(rules);
    check(engine.leader() == 1, "the leader is the lowest present slot");
    check(engine.pad_reader_slot(0, 2) == 2, "index 0 with a present trigger reads the trigger");
    check(engine.pad_reader_slot(0, -1) == 1, "index 0 without a trigger reads the leader");
    check(engine.pad_reader_slot(0, 3) == 1, "index 0 with an absent trigger reads the leader");
    check(engine.pad_reader_slot(2, -1) == 2, "a present index reads itself");
    check(engine.pad_reader_slot(3, 2) == 1, "an absent index reads the leader");
    rules.leaving = 0b0010;
    engine.set_online(rules);
    check(engine.leader() == 2 && engine.pad_reader_slot(1, -1) == 2, "a leaving slot is not the leader and reads the leader");
    engine.set_online(OnlineRules{});
}

void rules_context_hash() {
    Engine engine{dw4::MountedResources{}};
    const auto base = engine.session_context_hash();
    engine.flags()[63] = 0; // FLGisOn made an entry
    check(engine.session_context_hash() == base, "an absent context flag hashes like 0");
    engine.flags()[150] = 1; // a dungeon flag: instance class
    check(engine.session_context_hash() == base, "instance values are not in the context");
    engine.flags()[24] = 1; // a lobby flag: personal
    check(engine.session_context_hash() == base, "personal values are not in the context");
    engine.flags()[376] = 1; // quest 1
    const auto quest = engine.session_context_hash();
    check(quest != base, "a quest flag changes the context");
    engine.counters()[2] = 902;
    check(engine.session_context_hash() != quest, "COUNTER 2 is in the context");
    engine.counters()[2] = 0;
    check(engine.session_context_hash() == quest, "COUNTER 2 = 0 hashes like absent");
    engine.get<GameSystem>()->flag_folder[11] = 1;
    check(engine.session_context_hash() != quest, "folder 11 is in the context");
}

void rules_guards() {
    // One online engine per process.
    {
        Engine a{dw4::MountedResources{}};
        OnlineRules rules;
        rules.enabled = true;
        rules.present = 1;
        a.set_online(rules);
        bool refused = false;
        try {
            Engine b{dw4::MountedResources{}};
        } catch (const std::logic_error&) {
            refused = true;
        }
        check(refused, "no second engine while an online engine runs");
        a.set_online(OnlineRules{});
        Engine b{dw4::MountedResources{}};
        bool online_refused = false;
        try {
            a.set_online(rules);
        } catch (const std::logic_error&) {
            online_refused = true;
        }
        check(online_refused, "an engine does not go online while another lives");
    }
    check(Engine::live_engines() == 0, "every engine counted out");
    // MXCSR: an entry with flush to zero repairs it for its duration.
    Engine engine{dw4::MountedResources{}};
    const unsigned before = _mm_getcsr();
    const auto repairs = Engine::mxcsr_repairs();
    std::array<PadSample, 4> pads{};
    engine.step(pads);
    check(Engine::mxcsr_repairs() == repairs, "the default MXCSR needs no repair");
    _mm_setcsr(before | 0x8040u); // FTZ + DAZ
    engine.step(pads);
    check(Engine::mxcsr_repairs() == repairs + 1, "a changed MXCSR is repaired");
    check((_mm_getcsr() & 0xFFC0u) == ((before | 0x8040u) & 0xFFC0u), "and put back afterwards");
    _mm_setcsr(before);
}

void rules_replay_and_split_without_players() {
    Engine engine{dw4::MountedResources{}};
    engine.set_port_events(true);
    engine.set_replay(true);
    check(!engine.port_events_enabled(), "a replay collects no port events");
    engine.set_port_events(true);
    check(!engine.port_events_enabled(), "not even when asked");
    engine.set_replay(false);
    engine.set_port_events(true);
    check(engine.port_events_enabled(), "live again");
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b0011;
    engine.set_online(rules);
    // Nobody lives (no stage): the stayers have no living member and go
    // with the leavers - the original group move, no fork.
    check(engine.online_split(0b0010, ForkKind::exit), "everyone goes on");
    std::array<PadSample, 4> pads{};
    engine.step(pads);
    check(!engine.take_split(), "no fork without a living stayer");
    check(engine.online().present == 0b0011, "nobody despawned");
    engine.set_online(OnlineRules{});
}

void barrier_without_data() {
    // DISSOLVE in the load step: the own player to slot 0, the others gone,
    // the personal values written back (the loader finds no data, which
    // does not matter for GameSystem).
    Engine engine{dw4::MountedResources{}};
    auto* sys = engine.get<GameSystem>();
    sys->players[0] = sample_player("p_agumon", 5);
    sys->players[2] = sample_player("p_dorumon", 9);
    sys->gate_stage_name = "stag0101";
    sys->gate_stage_no = 1;
    engine.flags()[24] = 1;
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b0101;
    engine.set_online(rules);
    check(engine.personal_session(0).gate_stage_name == std::optional<std::string>("stag0101"), "slot 0 keeps the engine-wide values");
    PersonalSession mine;
    mine.gate_stage_name = "stag0302";
    mine.gate_stage_no = 2;
    mine.flags = {{25, 1}};
    // As a joiner's carry: the personal values of slot 2.
    PlayerCarry carry = engine.carry_player(2);
    carry.personal = mine;
    engine.set_online(OnlineRules{});
    rules.present = 0b0001;
    engine.set_online(rules);
    // (online_join_player needs a stage; set the values the way it does.)
    BarrierResult barrier;
    barrier.decision = Decision::new_instance;
    barrier.slot_map = {0, -1, -1, -1};
    barrier.co_arrivals.push_back({2, carry});
    barrier.own_slot = 2;
    barrier.rules.enabled = true;
    engine.request_stage("stagxx01");
    engine.set_barrier(barrier);
    std::array<PadSample, 4> pads{};
    for (int i = 0; i < 40 && engine.barrier_set(); ++i) engine.step(pads);
    check(!engine.barrier_set(), "the barrier applied in the load step");
    check(sys->players[2].joined && sys->players[2].digimon == "p_dorumon", "the co-arrival is a joined player");
    check(engine.online().present == 0b0101, "present follows the joined slots");
    check(engine.online().player_cameras == 0b0101 && engine.online().leash_off, "two members: own cameras, no leash");
    check(engine.stage_snapshot() && engine.stage_snapshot()->personal[2] == mine, "the snapshot holds the co-arrival's personal values");
    check(engine.personal_session(2) == mine, "the co-arrival's personal values");
    check(engine.personal_session(0).gate_stage_name == std::optional<std::string>("stag0101"), "slot 0 keeps its own");
    check(engine.split_identity() == 2, "the own slot is the split identity");
    // Back to the lobby: DISSOLVE for this machine (slot 2).
    BarrierResult dissolve;
    dissolve.decision = Decision::dissolve;
    dissolve.own_slot = 2;
    dissolve.rules.enabled = true;
    engine.request_stage("stagml11");
    engine.set_barrier(dissolve);
    for (int i = 0; i < 40 && engine.barrier_set(); ++i) engine.step(pads);
    check(sys->players[0].joined && sys->players[0].digimon == "p_dorumon", "the own player moved to slot 0");
    check(!sys->players[1].joined && !sys->players[2].joined && !sys->players[3].joined, "the others are gone");
    check(sys->gate_stage_name == std::optional<std::string>("stag0302") && sys->gate_stage_no == 2, "the own GATESTAGE is back");
    check(engine.flags().count(25) && !engine.flags().count(24), "the own lobby flags are back");
    check(engine.online().present == 1 && engine.online().player_cameras == 0 && !engine.online().leash_off, "a solo visit");
    engine.set_online(OnlineRules{});
}

// DISSOLVE as the relay and the fake hub send it (round S1 review): own_slot
// names the NEW slot 0, the slot map's source of 0 is this machine's old
// slot. A member in slot 1 keeps its own character and its own personal
// values, not slot 0's (S19a on real clients showed a k10 member in the
// lobby with its k2 partner's level 7). A damaged carry from the network
// drops its co-arrival or join instead of throwing.
void dissolve_through_the_link() {
    namespace w = dw4::game::online::wire;
    Engine engine{dw4::MountedResources{}};
    auto* sys = engine.get<GameSystem>();
    sys->players[0] = sample_player("p_agumon", 7);
    sys->gate_stage_name = "stag0101";
    sys->gate_stage_no = 1;
    engine.flags()[24] = 1;
    dw4::game::online::EngineHooks hooks;
    hooks.engine = [&]() -> Engine& { return engine; };
    dw4::game::online::GameEngineLink link(hooks, {});
    link.start_online(0, 0b0001);
    // A NEW with the partner as a co-arrival in slot 1 (its own character and
    // personal values) and a co-arrival whose carry is damaged in slot 2.
    PersonalSession mine;
    mine.gate_stage_name = "stag0302";
    mine.gate_stage_no = 2;
    mine.flags = {{25, 1}};
    PlayerCarry carry;
    carry.slot = sample_player("p_dorumon", 21);
    carry.personal = mine;
    w::BarrierOk arrive;
    arrive.decision = w::Decision::new_instance;
    arrive.slot_map = {0, -1, -1, -1};
    arrive.own_slot = 0;
    arrive.co_arrivals.push_back({1, 5, encode_carry(carry)});
    arrive.co_arrivals.push_back({2, 6, {0x44, 0x57, 0x34, 0x00, 0x01, 0x02}});
    arrive.mode = w::Mode::sealed;
    engine.request_stage("stagxx01");
    link.set_barrier(arrive);
    std::array<PadSample, 4> pads{};
    for (int i = 0; i < 40 && engine.barrier_set(); ++i) engine.step(pads);
    check(!engine.barrier_set(), "the NEW applied");
    check(sys->players[1].joined && sys->players[1].digimon == "p_dorumon" && !sys->players[2].joined,
          "the readable co-arrival joined, the damaged one was dropped");
    check(engine.online().present == 0b0011, "two members");
    check(!link.join_player(3, std::vector<std::uint8_t>{1, 2, 3}, 0) && !sys->players[3].joined, "a damaged JOIN carry is dropped");
    // This machine is the member in slot 1 (a co-arrival restores the base's
    // snapshot: the same engine): back to the lobby.
    link.set_split_identity(1);
    w::BarrierOk dissolve;
    dissolve.decision = w::Decision::dissolve;
    dissolve.own_slot = 0;                 // the new slot (RoomHub.Dissolve, the fake hub)
    dissolve.slot_map = {-1, 0, -1, -1};   // old slot 1 -> 0
    dissolve.mode = w::Mode::hub;
    engine.request_stage("stagml11");
    link.set_barrier(dissolve);
    for (int i = 0; i < 40 && engine.barrier_set(); ++i) engine.step(pads);
    check(!engine.barrier_set(), "the DISSOLVE applied");
    check(sys->players[0].joined && sys->players[0].digimon == "p_dorumon" && sys->players[0].stats.level == 21,
          "the member keeps its own character (level 21), not slot 0's");
    check(!sys->players[1].joined && !sys->players[2].joined && !sys->players[3].joined, "the others are gone");
    check(sys->gate_stage_name == std::optional<std::string>("stag0302") && sys->gate_stage_no == 2, "its own GATESTAGE");
    check(engine.flags().count(25) && !engine.flags().count(24), "its own lobby flags");
    engine.set_online(OnlineRules{});
}

// 8.5 / 8.6: the barrier's room story reaches the engine in the load step,
// before the membership: the phase rises one-hot, persistent flags, keys and
// counters come in, temporaries stay, and the values the server dropped while
// the room owner was away go back to the room's. Through the link the same
// bytes give the same engine; a damaged story blob merges nothing.
void story_at_the_barrier() {
    namespace w = dw4::game::online::wire;
    RoomStory room;
    room.flags[3] = 1;
    room.flags[19] = 1;
    room.folder[1] = 1;
    room.counters[1] = 500;
    room.counters[7] = 1;
    room.revision = 12;
    const StoryKeys dropped{{392}, {5}, {7}};
    const auto prepare = [](Engine& engine) {
        auto* sys = engine.get<GameSystem>();
        sys->players[0] = sample_player("p_agumon", 5);
        engine.flags()[1] = 0;   // an absent-or-zero phase entry stays as it is
        engine.flags()[2] = 1;   // phase 2
        engine.flags()[392] = 1; // made while the owner was away (dropped)
        engine.flags()[393] = 1; // pending, not dropped: stays
        engine.flags()[23] = 1;  // a temporary: never touched
        sys->flag_folder[5] = 1; // dropped
        engine.counters()[7] = 3;
        engine.counters()[2] = 4; // a session counter
        OnlineRules rules;
        rules.enabled = true;
        rules.present = 1;
        engine.set_online(rules);
    };
    const auto verify = [](Engine& engine, const std::string& how) {
        auto* sys = engine.get<GameSystem>();
        auto& f = engine.flags();
        check(!engine.barrier_set(), how + ": the barrier applied");
        check(f.at(1) == 0 && f.at(2) == 0 && f.at(3) == 1 && !f.count(4), how + ": the phase rose one-hot to the room's");
        check(f.at(19) == 1 && f.at(393) == 1 && f.at(23) == 1, how + ": room flags came in, pending and temporary ones stay");
        check(f.at(392) == 0, how + ": the dropped flag is gone");
        check(sys->flag_folder.at(1) == 1 && sys->flag_folder.at(5) == 0, how + ": keys");
        check(engine.counters().at(1) == 500 && engine.counters().at(7) == 1 && engine.counters().at(2) == 4, how + ": counters");
        check(sys->players[0].joined && engine.online().present == 1, how + ": the membership applied after the story");
    };
    std::array<PadSample, 4> pads{};
    {
        Engine engine{dw4::MountedResources{}};
        prepare(engine);
        BarrierResult b;
        b.decision = Decision::new_instance;
        b.slot_map = {0, -1, -1, -1};
        b.own_slot = 0;
        b.rules.enabled = true;
        b.story = room;
        b.dropped = dropped;
        engine.request_stage("stagxx01");
        engine.set_barrier(b);
        for (int i = 0; i < 40 && engine.barrier_set(); ++i) engine.step(pads);
        verify(engine, "engine");
        engine.set_online(OnlineRules{});
    }
    {
        Engine engine{dw4::MountedResources{}};
        prepare(engine);
        dw4::game::online::EngineHooks hooks;
        hooks.engine = [&]() -> Engine& { return engine; };
        dw4::game::online::GameEngineLink link(hooks, {});
        link.start_online(0, 0b0001);
        check(story::pending(room, link.story_values()).flags == std::vector<std::int32_t>{392, 393}, "the link's story values");
        w::BarrierOk ok;
        ok.decision = w::Decision::dissolve;
        ok.slot_map = {0, -1, -1, -1};
        ok.mode = w::Mode::hub;
        ok.story_revision = room.revision;
        ok.story = story::encode_story(room);
        ok.dropped = story::encode_keys(dropped);
        engine.request_stage("stagml11");
        link.set_barrier(ok);
        for (int i = 0; i < 40 && engine.barrier_set(); ++i) engine.step(pads);
        verify(engine, "link");
        // A damaged story blob: nothing merges (the next barrier brings it).
        ok.story.pop_back();
        engine.flags()[22] = 0;
        room.flags[22] = 1;
        ok.story = story::encode_story(room);
        ok.story.pop_back();
        engine.request_stage("stagml12");
        link.set_barrier(ok);
        for (int i = 0; i < 40 && engine.barrier_set(); ++i) engine.step(pads);
        check(!engine.barrier_set() && engine.flags().at(22) == 0, "a damaged story blob merges nothing");
        engine.set_online(OnlineRules{});
    }
}

// 5.3 (the round 63 review): inside a member's own scene the instance-wide
// control commands reach that member alone - stag0302's Gekomon demo
// (demo00.lua) sends PLAYER -1 SQ_WAIT and MODELCTRL -1 ONCTRL, which in the
// original reach every player and hand every model its control back, also a
// member frozen in its exit prompt.
void scoped_commands() {
    Engine engine{dw4::MountedResources{}};
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b0011;
    engine.set_online(rules);
    std::string storage;
    Packet packet;
    std::optional<std::string_view> name;
    std::int32_t index = -1;
    {
        const Engine::SceneScope scope(engine, 1);
        check(!engine.scope_message("PLAYER", name, index, "OFCTRL", packet, storage) && engine.own_input_held(1) && !engine.own_input_held(0),
              "scope: PLAYER -1 OFCTRL holds the member's own input");
        index = -1;
        check(engine.scope_message("PLAYER", name, index, "SQ_WAIT", packet, storage) && index == 1,
              "scope: PLAYER -1 SQ_WAIT reaches the member's own player alone");
        index = -1;
        check(!engine.scope_message("MODELCTRL", name, index, "ONCTRL", packet, storage) && !engine.own_input_held(1),
              "scope: MODELCTRL -1 ONCTRL gives the member its input back and no other model its control");
        index = -1;
        check(!engine.scope_message("MODELCTRL", name, index, "OFCTRL", packet, storage) && engine.own_input_held(1),
              "scope: MODELCTRL -1 OFCTRL holds the member's own input");
        index = 0;
        std::optional<std::string_view> npc = std::string_view("n_gekoa");
        check(engine.scope_message("MODELCTRL", npc, index, "SQ_DEMO0302", packet, storage) && npc && *npc == "n_gekoa" && index == 0,
              "scope: a named model's command reaches it unchanged");
        engine.hold_own_input(1, false, false);
    }
    index = -1;
    check(engine.scene_scope() < 0 && engine.scope_message("PLAYER", name, index, "SQ_WAIT", packet, storage) && index == -1,
          "no scope: every player as the original");
    engine.set_online(OnlineRules{});
}

// 6.5: a LEAVE that waits for a membership-safe step marks the slot leaving:
// its pad is neutral and connected, and its readers (a prompt it owns, a
// status window: SUBWND reads window.player through pad_reader_slot) get the
// leader's pad until the LEAVE applies.
void leaving_redirect() {
    Engine engine{dw4::MountedResources{}};
    auto* sys = engine.get<GameSystem>();
    sys->players[0] = sample_player("p_agumon", 7);
    sys->players[1] = sample_player("p_dorumon", 21);
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b0011;
    engine.set_online(rules);
    check(engine.pad_reader_slot(1, 1) == 1, "slot 1 reads its own pad");
    engine.online_mark_leaving(1);
    check(engine.online().leaving == 0b0010, "slot 1 is leaving");
    check(engine.pad_reader_slot(1, 1) == 0 && engine.leader() == 0, "its readers take the leader's pad");
    engine.set_pad_input_enabled(true);
    std::array<PadSample, 4> in{};
    in[0] = PadSample{true, {}, {}, dw4::pad_button::a};
    in[1] = PadSample{false, {0.5f, 0.0f}, {}, dw4::pad_button::a}; // (the relay no longer carries it: disconnected)
    engine.step(in);
    check(engine.pads()[1].connected && engine.pads()[1].buttons.held == 0, "its pad is neutral and connected");
    check(engine.pads()[0].buttons.held & dw4::pad_button::a, "the leader plays on");
    engine.online_mark_leaving(3);
    check(engine.online().leaving == 0b0010, "an absent slot is not marked");
    engine.online_remove_player(1);
    check(engine.online().leaving == 0 && engine.online().present == 0b0001, "the LEAVE clears the mark");
    engine.set_online(OnlineRules{});
    Engine offline{dw4::MountedResources{}};
    offline.online_mark_leaving(1);
    check(offline.online().leaving == 0 && offline.pad_reader_slot(1, 1) == 1, "nothing offline");
}

// ---------------------------------------------------------------------------
// With game data
// ---------------------------------------------------------------------------
struct Planned {
    enum Kind { join, remove, split, give_up, kill, barrier } kind{};
    std::uint64_t earliest{}; // the first pre-step frame it may apply at
    int slot{};
    PlayerCarry carry;
    int point{};
    std::uint8_t mask{};
    ForkKind fork{};
    std::string stage;     // barrier: the NEXTSTAGE target
    BarrierResult result;  // barrier: BARRIER_OK
};

struct Applied {
    std::uint64_t frame{};
    std::size_t planned{};
};

struct Scenario {
    std::string name;
    std::string stage;
    int players = 1;          // joined at the start (slots 0..n-1)
    std::vector<std::string> species; // per starting slot
    std::uint64_t steps{};    // total steps from the start
    int identity = 0;         // the split identity of the recorded copy
    std::vector<Planned> plan;
    std::uint64_t join_at{};  // the joiner restores the instance's bytes at this pre-step frame (0: none)
    // B+X+START on slot 1 from this pre-step frame for 240 frames (0: none).
    std::uint64_t reset_combo_at{};
    std::array<PadSample, 4> pads(std::uint64_t frame) const;
    // The pads of chosen slots replaced (a menu's presses), and a look at the
    // engine after every step of the recorded run (A).
    std::function<void(std::uint64_t frame, std::array<PadSample, 4>& pads)> shape;
    std::function<void(Engine& engine, std::uint64_t frame)> observe;
};

PadSample pad_for(std::uint64_t frame, int slot) {
    PadSample p;
    p.connected = true;
    // Walk in a slot-specific direction that turns every few seconds, attack
    // and jump now and then; sticks on the GameCube's 1/74 steps.
    const std::uint64_t phase = (frame / std::uint64_t(150 + 40 * slot) + std::uint64_t(slot)) % 8;
    static const int dirs[8][2] = {{0, 60}, {42, 42}, {60, 0}, {42, -42}, {0, -60}, {-42, -42}, {-60, 0}, {-42, 42}};
    if ((frame / 90) % 5 != 4) {
        p.left.x = float(dirs[phase][0]) / 74.0f;
        p.left.y = float(dirs[phase][1]) / 74.0f;
    }
    if ((frame + std::uint64_t(slot) * 11) % 37 < 2) p.buttons |= dw4::pad_button::a;
    if ((frame + std::uint64_t(slot) * 7) % 211 < 2) p.buttons |= dw4::pad_button::x;
    return p;
}

struct Recording {
    std::map<std::uint64_t, std::uint64_t> fingerprints; // post-step frame -> net fingerprint
    std::vector<Applied> applied;
    std::vector<std::uint8_t> join_bytes;               // the visit's snapshot at join_at
    std::vector<std::array<PadSample, 4>> join_log;     // the steps of the visit before join_at
    std::uint64_t join_serial{};
    std::vector<SplitResult> splits;
    std::uint8_t final_present{};
    std::string final_stage;
    int generated_players{};
    int max_generated{};
    bool rebooted{};
    std::size_t carry_bytes{}; // encode_carry of slot 0 at the end
    double seconds{};
};

std::array<PadSample, 4> pads_at(std::uint64_t frame) {
    std::array<PadSample, 4> pads{};
    for (int s = 0; s < 4; ++s) pads[std::size_t(s)] = pad_for(frame, s);
    return pads;
}

std::array<PadSample, 4> Scenario::pads(std::uint64_t frame) const {
    auto out = pads_at(frame);
    if (reset_combo_at && frame >= reset_combo_at && frame < reset_combo_at + 240)
        out[1].buttons = dw4::pad_button::b | dw4::pad_button::x | dw4::pad_button::start;
    if (shape) shape(frame, out);
    return out;
}

bool predicate_for(const Engine& engine, Planned::Kind kind) {
    return kind == Planned::join ? engine.joinable() : engine.membership_safe();
}

void apply_planned(Engine& engine, const Planned& p) {
    switch (p.kind) {
    case Planned::join: engine.online_join_player(p.slot, p.carry, p.point); break;
    case Planned::remove: engine.online_remove_player(p.slot); break;
    case Planned::split: break; // applied inside the step (below)
    case Planned::give_up: engine.online_intent(p.slot, IntentKind::give_up, 0); break;
    case Planned::kill: {
        // A defeat as in a fight: no HP left (GAMERAM ADDHP, absolute 0),
        // then the death sequence, whose end (SQ_ERAS) leaves the capsule.
        const Messenger::PortOnly port_only(engine.messenger());
        Packet packet;
        parse_packet_arguments("I<" + std::to_string(p.slot) + ">/I<0>/I<0>/I<0>", packet);
        engine.messenger().send(engine, "GAMERAM", std::nullopt, 0, "ADDHP", packet);
        const auto& digimon = engine.get<GameSystem>()->players[std::size_t(p.slot)].digimon;
        Packet dead;
        engine.messenger().send(engine, "PLAYER", std::string_view(digimon), p.slot, "SQ_DEAD", dead);
        break;
    }
    case Planned::barrier:
        // A NEXTSTAGE and, while parked, BARRIER_OK.
        engine.request_stage(p.stage);
        engine.set_barrier(p.result);
        break;
    }
}

// One step as the hosts run it (the traversal after every step that
// updated), with the split of a planned fork applied inside the step through
// the same call the online overrides make.
void host_step(Engine& engine, const std::array<PadSample, 4>& pads, const Planned* split) {
    if (split) (void)engine.online_split(split->mask, split->fork);
    engine.step(pads);
    if (!engine.held()) engine.traverse_scene();
}

// `replayed`: the applied events of an earlier run to repeat at their frames
// (B and the joiner), else the plan decides by the predicates (A).
Recording run_scenario(const std::filesystem::path& data, const Scenario& sc, const std::vector<Applied>* replayed) {
    Recording rec;
    const auto t0 = std::chrono::steady_clock::now();
    Engine engine(data);
    engine.set_card_root(std::filesystem::temp_directory_path() / "dw4_online_tests_cards");
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    for (int s = 0; s < sc.players; ++s) {
        sys->players[std::size_t(s)].joined = true;
        if (s < int(sc.species.size())) sys->players[std::size_t(s)].digimon = sc.species[std::size_t(s)];
    }
    OnlineRules rules;
    rules.enabled = true;
    rules.present = std::uint8_t((1u << sc.players) - 1u);
    engine.set_online(rules);
    engine.set_split_identity(sc.identity);
    engine.request_stage(sc.stage);
    std::size_t next_plan = 0, next_replay = 0;
    std::uint64_t log_serial = 0;
    std::vector<std::array<PadSample, 4>> log;
    for (std::uint64_t i = 0; i < sc.steps; ++i) {
        const std::uint64_t frame = engine.frame();
        if (sc.join_at && frame == sc.join_at) {
            rec.join_bytes = engine.snapshot_bytes(SnapshotForm::raw);
            rec.join_log = log;
            rec.join_serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
        }
        const Planned* split = nullptr;
        if (replayed) {
            while (next_replay < replayed->size() && (*replayed)[next_replay].frame == frame) {
                const auto& p = sc.plan[(*replayed)[next_replay].planned];
                check(predicate_for(engine, p.kind), sc.name + ": a replayed event's predicate holds at its frame");
                if (p.kind == Planned::split) split = &p;
                else apply_planned(engine, p);
                ++next_replay;
            }
        } else if (next_plan < sc.plan.size() && frame >= sc.plan[next_plan].earliest && predicate_for(engine, sc.plan[next_plan].kind)) {
            const auto& p = sc.plan[next_plan];
            rec.applied.push_back({frame, next_plan});
            if (p.kind == Planned::split) split = &p;
            else apply_planned(engine, p);
            ++next_plan;
        }
        const auto pads = sc.pads(frame);
        host_step(engine, pads, split);
        if (auto s = engine.take_split()) rec.splits.push_back(*s);
        rec.fingerprints[engine.frame()] = engine.net_fingerprint();
        if (!replayed && sc.observe) sc.observe(engine, engine.frame());
        rec.rebooted = rec.rebooted || engine.rebooting();
        if (const auto* models = engine.get<ModelCtrlManager>()) {
            int generated = 0;
            for (int s = 0; s < 4; ++s)
                if (const auto* p = models->player(s); p && p->generated) ++generated;
            rec.max_generated = std::max(rec.max_generated, generated);
        }
        if (const auto* snapshot = engine.stage_snapshot(); snapshot && snapshot->serial != log_serial) {
            log_serial = snapshot->serial;
            log.clear();
        } else {
            log.push_back(pads);
        }
    }
    if (!replayed) check(next_plan == sc.plan.size(), sc.name + ": every planned event applied (" + std::to_string(next_plan) + "/" +
                                                       std::to_string(sc.plan.size()) + ")");
    rec.final_present = engine.online().present;
    rec.final_stage = engine.stage_name();
    if (const auto* models = engine.get<ModelCtrlManager>())
        for (int s = 0; s < 4; ++s)
            if (const auto* p = models->player(s); p && p->generated) ++rec.generated_players;
    rec.carry_bytes = encode_carry(engine.carry_player(0)).size();
    rec.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    engine.set_online(OnlineRules{});
    return rec;
}

// The joiner: a fresh engine from the instance's snapshot bytes, the visit's
// log replayed (catch-up, replay mode), then live with the same pads and
// events as the instance.
Recording run_joiner(const std::filesystem::path& data, const Scenario& sc, const Recording& a) {
    Recording rec;
    Engine engine(data);
    engine.set_card_root(std::filesystem::temp_directory_path() / "dw4_online_tests_cards");
    const auto snapshot = decode_snapshot(a.join_bytes);
    engine.restore(snapshot);
    check(engine.online().enabled, sc.name + ": the restored engine is online");
    // An observer while it catches up: no split identity, the instance's
    // fork sides instead (6.7).
    engine.set_split_identity(-1);
    std::vector<ForkSide> sides;
    for (const auto& split : a.splits) sides.push_back({split.frame, split.leavers, split.local_leaves});
    engine.set_fork_sides(sides);
    if (!engine.held()) engine.traverse_scene();
    engine.set_replay(true);
    for (const auto& pads : a.join_log) {
        const std::uint64_t frame = engine.frame();
        for (const auto& applied : a.applied)
            if (applied.frame == frame && sc.plan[applied.planned].kind != Planned::split) apply_planned(engine, sc.plan[applied.planned]);
        const Planned* split = nullptr;
        for (const auto& applied : a.applied)
            if (applied.frame == frame && sc.plan[applied.planned].kind == Planned::split) split = &sc.plan[applied.planned];
        host_step(engine, pads, split);
        (void)engine.take_split();
    }
    engine.set_replay(false);
    engine.set_split_identity(sc.identity);
    check(engine.frame() == sc.join_at, sc.name + ": the catch-up ends at the join frame");
    const auto at = a.fingerprints.find(engine.frame());
    check(at != a.fingerprints.end() && at->second == engine.net_fingerprint(), sc.name + ": the joiner equals the instance after the catch-up");
    std::size_t next = 0;
    while (next < a.applied.size() && a.applied[next].frame < engine.frame()) ++next;
    for (std::uint64_t frame = engine.frame(); frame < a.fingerprints.rbegin()->first;) {
        const Planned* split = nullptr;
        while (next < a.applied.size() && a.applied[next].frame == frame) {
            const auto& p = sc.plan[a.applied[next].planned];
            if (p.kind == Planned::split) split = &p;
            else apply_planned(engine, p);
            ++next;
        }
        host_step(engine, sc.pads(frame), split);
        (void)engine.take_split();
        rec.fingerprints[engine.frame()] = engine.net_fingerprint();
        frame = engine.frame();
    }
    engine.set_online(OnlineRules{});
    return rec;
}

std::size_t compare(const Recording& a, const Recording& b, const std::string& what) {
    std::size_t compared = 0;
    for (const auto& [frame, fp] : b.fingerprints) {
        const auto it = a.fingerprints.find(frame);
        if (it == a.fingerprints.end()) continue;
        check(it->second == fp, what + ": net fingerprints differ from frame " + std::to_string(frame));
        ++compared;
    }
    return compared;
}

PlayerCarry carry_of(const char* digimon, int level, const char* weapon) {
    PlayerCarry c;
    c.slot = GameSystem::PlayerSlot{};
    c.slot.digimon = digimon;
    c.slot.weapons = {weapon, "", ""};
    c.slot.stats.level = level;
    c.personal.gate_stage_name = "GateOff";
    c.personal.gate_stage_no = -1;
    return c;
}

// 6.7: the transition steps take the pads of the barrier tick. Two machines
// that execute the same NEXTSTAGE but sample different pads while they run
// the module reload, its held fields and the load step still take the same
// load snapshot (the load step's pads are in it) and stay equal.
struct TransitionRun {
    std::vector<std::uint8_t> canonical; // the new visit's snapshot
    std::map<std::uint64_t, std::uint64_t> fingerprints;
    std::uint64_t barrier_frame{}, load_frame{};
};

TransitionRun run_transition(const std::filesystem::path& data, bool noise) {
    TransitionRun run;
    Engine engine(data);
    engine.set_card_root(std::filesystem::temp_directory_path() / "dw4_online_tests_cards");
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    sys->players[1].joined = true;
    sys->players[1].digimon = "p_dorumon";
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b0011;
    engine.set_online(rules);
    engine.set_split_identity(0);
    engine.request_stage("dng101b1");
    std::uint64_t serial = 0;
    bool posted = false;
    for (int i = 0; i < 3200; ++i) {
        const std::uint64_t frame = engine.frame();
        if (!posted && frame >= 1100 && engine.membership_safe()) {
            // STAGECTL NEXTSTAGE delivered inside the next step, as a script's.
            Packet next;
            next.strings.emplace_back("dng101b2");
            engine.messenger().post(engine.time() + kFrameNs, "STAGECTL", std::nullopt, -1, "NEXTSTAGE", std::move(next));
            posted = true;
        }
        auto pads = pads_at(frame);
        // While the transition runs, this machine's controllers do anything.
        if (noise && engine.pending_stage())
            for (auto& p : pads) {
                p.buttons = dw4::pad_button::b | dw4::pad_button::x | std::uint32_t(frame % 7 == 0 ? dw4::pad_button::start : 0);
                p.left = {float(frame % 74) / 74.0f, -0.5f};
            }
        const bool before = engine.pending_stage().has_value();
        host_step(engine, pads, nullptr);
        if (!before && engine.pending_stage() && !run.barrier_frame) run.barrier_frame = engine.frame();
        if (const auto* snapshot = engine.stage_snapshot(); snapshot && snapshot->serial != serial) {
            serial = snapshot->serial;
            if (snapshot->stage == "dng101b2") {
                run.canonical = engine.snapshot_bytes(SnapshotForm::canonical);
                run.load_frame = engine.frame();
            }
        }
        run.fingerprints[engine.frame()] = engine.net_fingerprint();
    }
    engine.set_online(OnlineRules{});
    return run;
}

void transition_pads(const std::filesystem::path& data) {
    const auto quiet = run_transition(data, false);
    const auto noisy = run_transition(data, true);
    std::cout << "  transition: barrier tick at frame " << quiet.barrier_frame << ", dng101b2 load step at " << quiet.load_frame << '\n';
    check(quiet.barrier_frame && quiet.load_frame > quiet.barrier_frame, "the NEXTSTAGE ran and dng101b2 loaded");
    check(!quiet.canonical.empty() && quiet.canonical == noisy.canonical, "the load snapshots are equal whatever the pads of the transition steps");
    check(quiet.fingerprints == noisy.fingerprints, "and the machines stay equal");
}

// The point a barrier names (BARRIER, the entrance a JOIN puts the joiner in
// at): the one the exit leads to, which its SETGENERATEPOS set before its
// NEXTSTAGE, not the one the departing stage was entered at (the stage
// snapshot's; that put a quest member who came through stag0401 point 3 in at
// point 0, across the field from its instance: S16 with real clients).
void barrier_point(const std::filesystem::path& data) {
    Engine engine(data);
    engine.set_card_root(std::filesystem::temp_directory_path() / "dw4_online_tests_cards");
    engine.get<GameSystem>()->direct_start = true;
    OnlineRules rules;
    rules.enabled = true;
    rules.present = 0b0001;
    engine.set_online(rules);
    engine.set_split_identity(0);
    engine.request_stage("dng101b1");
    dw4::game::online::EngineHooks hooks;
    hooks.engine = [&]() -> Engine& { return engine; };
    const dw4::game::online::GameEngineLink link(hooks, {});
    bool posted = false;
    // (Until the exit's stage is pending: dng101b1's own request is pending at first.)
    for (int i = 0; i < 3000 && !(posted && engine.pending_stage()); ++i) {
        if (!posted && engine.frame() >= 1100 && engine.membership_safe()) {
            // An exit's pair of messages, as the lobby gate's (mate00.lua: point 3 of stag0401).
            Packet point;
            point.ints.push_back(3);
            engine.messenger().post(engine.time() + kFrameNs, "PLAYERPOINT", std::nullopt, -1, "SETGENERATEPOS", std::move(point));
            Packet next;
            next.strings.emplace_back("stag0401");
            engine.messenger().post(engine.time() + kFrameNs, "STAGECTL", std::nullopt, -1, "NEXTSTAGE", std::move(next));
            posted = true;
        }
        host_step(engine, pads_at(engine.frame()), nullptr);
    }
    check(engine.pending_stage() && *engine.pending_stage() == "stag0401", "the exit's NEXTSTAGE is pending (the barrier)");
    check(engine.stage_snapshot() && engine.stage_snapshot()->point_no == 0, "dng101b1 was entered at point 0");
    check(link.pending_point() == 3, "the barrier names the point the exit leads to (3), not the departing stage's entrance (" +
                                         std::to_string(link.pending_point()) + ")");
    engine.set_online(OnlineRules{});
}

// ---------------------------------------------------------------------------
// Own talks and gifts (SEAMLESS.md 5.3, the round 63 review): members of one
// online instance in a stage, driven by `act` before every step (their pads,
// port-only sends); the net fingerprints of every step.
// ---------------------------------------------------------------------------
using MembersAct = std::function<void(Engine& engine, std::uint64_t frame, std::array<PadSample, 4>& pads)>;

std::map<std::uint64_t, std::uint64_t> run_members(const std::filesystem::path& data, const std::string& stage, int players,
                                                   std::uint64_t steps, const std::function<void(Engine&)>& setup,
                                                   const MembersAct& act) {
    std::map<std::uint64_t, std::uint64_t> fingerprints;
    Engine engine(data);
    engine.set_card_root(std::filesystem::temp_directory_path() / "dw4_online_tests_cards");
    auto* sys = engine.get<GameSystem>();
    sys->direct_start = true;
    for (int s = 0; s < players; ++s) sys->players[std::size_t(s)].joined = true;
    if (setup) setup(engine);
    OnlineRules rules;
    rules.enabled = true;
    rules.present = std::uint8_t((1u << players) - 1u);
    engine.set_online(rules);
    engine.set_split_identity(0);
    engine.request_stage(stage);
    for (std::uint64_t i = 0; i < steps; ++i) {
        std::array<PadSample, 4> pads{};
        for (int s = 0; s < players; ++s) pads[std::size_t(s)].connected = true;
        act(engine, engine.frame(), pads);
        host_step(engine, pads, nullptr);
        fingerprints[engine.frame()] = engine.net_fingerprint();
    }
    engine.set_online(OnlineRules{});
    return fingerprints;
}

// The input scripts' SEND PLAYER MOVETRANS: a member placed before an NPC.
void place_member(Engine& engine, int slot, const char* where) {
    Packet packet;
    parse_packet_arguments(where, packet);
    const Messenger::PortOnly port(engine.messenger());
    engine.messenger().send(engine, "PLAYER", std::nullopt, slot, "MOVETRANS", packet);
}

std::string seq_of(Engine& engine, std::string_view name, int index) {
    auto* models = engine.get<ModelCtrlManager>();
    const auto* ram = models ? models->find(name, index) : nullptr;
    return ram ? ram->seq_name : std::string();
}

// Each member presses A every 60 frames from `from` until its first own talk
// has run and ended (started / ended: the frames, 0 none yet); `both`: two
// own talks ran at once.
struct TalkTracker {
    std::array<std::uint64_t, 4> started{}, ended{};
    bool both = false;
    void press(Engine& engine, std::uint64_t frame, std::array<PadSample, 4>& pads, std::uint8_t members, std::uint64_t from) {
        int talking = 0;
        for (int s = 0; s < 4; ++s) {
            if (!(members & (1u << s))) continue;
            const bool running = engine.own_scene_running(s);
            talking += running ? 1 : 0;
            if (running && !started[std::size_t(s)]) started[std::size_t(s)] = frame;
            if (!running && started[std::size_t(s)] && !ended[std::size_t(s)]) ended[std::size_t(s)] = frame;
            if (!ended[std::size_t(s)] && frame >= from && (frame - from) % 60 < 3) pads[std::size_t(s)].buttons = dw4::pad_button::a;
        }
        both = both || talking > 1;
    }
    bool done(std::uint8_t members) const {
        for (int s = 0; s < 4; ++s)
            if ((members & (1u << s)) && !ended[std::size_t(s)]) return false;
        return true;
    }
};

void own_talks(const std::filesystem::path& data) {
    // a) stag0101: slot 0 before the field recorder n_recorder#1, slot 1
    // before n_recorder#0 across the field; both strike theirs in the same
    // frame. The review's harness: both talks ran at once and shared the
    // talk globals (g_npcRamNo, g_messPlayerNo), slot 0's close set
    // n_recorder#0 back and slot 0's scene never ended. Now talks wait for
    // each other (the original's one talk at a time): one starts, the other
    // member's strike only hits, and its talk runs after the first ended.
    {
        TalkTracker talks;
        std::uint64_t idle_at = 0;
        std::string seq0, seq1;
        bool idle = false;
        const auto act = [&](Engine& engine, std::uint64_t frame, std::array<PadSample, 4>& pads) {
            if (frame == 900) {
                place_member(engine, 0, "V<482,50,136>/V<0,180,0>");
                place_member(engine, 1, "V<-380,57,-209>/V<0,180,0>");
            }
            talks.press(engine, frame, pads, 0b0011, 960);
            if (!idle_at && talks.done(0b0011)) idle_at = frame + 60;
            if (idle_at && frame == idle_at) {
                seq0 = seq_of(engine, "n_recorder", 0);
                seq1 = seq_of(engine, "n_recorder", 1);
                idle = engine.busy_slots() == 0 && !engine.own_input_held(0) && !engine.own_input_held(1);
            }
        };
        const auto a = run_members(data, "stag0101", 2, 3200, nullptr, act);
        std::cout << "  stag0101 two recorders: talks " << talks.started[0] << "-" << talks.ended[0] << " / " << talks.started[1] << "-"
                  << talks.ended[1] << ", at once " << talks.both << ", recorders " << seq0 << " / " << seq1 << ", idle " << idle << '\n';
        check(talks.started[0] && talks.started[1] && talks.ended[0] && talks.ended[1], "own talks: both members read their recorders");
        // (Both struck at frame 960 and every 60 frames after it.)
        const int first = talks.started[0] <= talks.started[1] ? 0 : 1;
        check(!talks.both && talks.started[std::size_t(1 - first)] >= talks.ended[std::size_t(first)] &&
                  talks.started[std::size_t(first)] < 1100,
              "own talks: the second talk waits for the first (one talk at a time)");
        check(seq0 == "SQ_WAIT" && seq1 == "SQ_WAIT" && idle, "own talks: both recorders back in SQ_WAIT, no scene and no hold left");
        TalkTracker again;
        const auto b = run_members(data, "stag0101", 2, 3200, nullptr, [&](Engine& engine, std::uint64_t frame, std::array<PadSample, 4>& pads) {
            if (frame == 900) {
                place_member(engine, 0, "V<482,50,136>/V<0,180,0>");
                place_member(engine, 1, "V<-380,57,-209>/V<0,180,0>");
            }
            again.press(engine, frame, pads, 0b0011, 960);
        });
        check(a == b, "own talks: a second engine agrees");
    }
    // b) dngq5b1v (Trader Hole, very hard), the common FLAGFOLDER holds the
    // Strange Figurine (11) that Agumon#0 trades for a Mighty Talon (weapon
    // 175, n_agumon_00v.lua / itemchange2.lua) and Guilmon#0 for a Battle
    // Hawk. Slot 1 alone at Agumon: the Mighty Talon goes into slot 1's
    // device folder, not slot 0's (the scripts name player 0: online that is
    // the talking member). Then slot 0 at Agumon and slot 1 at Guilmon at
    // once: one trade, the other talk after it finds no figurine any more.
    const auto trader = [&](std::uint8_t talkers, const char* where1, std::array<std::size_t, 2>& devices, bool& got175_0, bool& got175_1,
                            bool& figurine, bool& both) {
        TalkTracker talks;
        std::array<std::size_t, 2> before{};
        bool measured = false;
        run_members(
            data, "dngq5b1v", 2, 2900,
            [](Engine& engine) {
                auto* sys = engine.get<GameSystem>();
                sys->difficulty = 2;
                sys->flag_folder[11] = 1;
            },
            [&](Engine& engine, std::uint64_t frame, std::array<PadSample, 4>& pads) {
                auto* sys = engine.get<GameSystem>();
                // The quest's opening message (slot 0, the leader's pad).
                if (frame >= 320 && frame <= 640 && (frame - 320) % 80 < 3) pads[0].buttons = dw4::pad_button::a;
                if (frame == 700) {
                    if (talkers & 1) place_member(engine, 0, "V<-80,10,26>/V<0,180,0>");
                    place_member(engine, 1, where1);
                    before = {sys->players[0].devices.size(), sys->players[1].devices.size()};
                }
                if (frame > 700) talks.press(engine, frame, pads, talkers, 760);
                if (frame > 700 && !measured && talks.done(talkers)) {
                    measured = true;
                    devices = {sys->players[0].devices.size() - before[0], sys->players[1].devices.size() - before[1]};
                    auto has175 = [&](int s) {
                        const auto& d = sys->players[std::size_t(s)].devices;
                        return std::any_of(d.begin(), d.end(), [](const ItemDevice& item) { return item.record == 175; });
                    };
                    got175_0 = has175(0);
                    got175_1 = has175(1);
                    const auto found = sys->flag_folder.find(11);
                    figurine = found != sys->flag_folder.end() && found->second;
                }
            });
        both = talks.both;
        return measured;
    };
    {
        std::array<std::size_t, 2> added{};
        bool got0 = false, got1 = false, figurine = true, both = false;
        const bool ran = trader(0b0010, "V<-80,10,26>/V<0,180,0>", added, got0, got1, figurine, both);
        std::cout << "  dngq5b1v slot 1 trades alone: devices +" << added[0] << " / +" << added[1] << ", Mighty Talon " << got0 << " / " << got1
                  << ", figurine left " << figurine << '\n';
        check(ran && added[0] == 0 && added[1] == 1 && !got0 && got1 && !figurine,
              "own talks: an exchange trades with the talking member's own folder");
    }
    {
        std::array<std::size_t, 2> added{};
        bool got0 = false, got1 = false, figurine = true, both = false;
        const bool ran = trader(0b0011, "V<80,10,26>/V<0,180,0>", added, got0, got1, figurine, both);
        std::cout << "  dngq5b1v Agumon and Guilmon at once: devices +" << added[0] << " / +" << added[1] << ", at once " << both
                  << ", figurine left " << figurine << '\n';
        check(ran && !both && added[0] + added[1] == 1 && !figurine, "own talks: one figurine, one exchange (the talks wait for each other)");
    }
}

// Gifts look at the partner's room when they are given (the review: two
// members' own windows gave a device each to a partner with 35 at once, who
// had 37 then, one more than a folder and a save hold). Three members in
// stag0101 with 35 devices each; slot 0 and slot 2 give one to slot 1.
void own_gifts(const std::filesystem::path& data) {
    std::array<std::size_t, 3> counts{};
    // The status window: START, Folder, the device folder, the fourth device,
    // Give, the partner (slot 2: the second line, slot 1), then B to close.
    const auto give = [](std::uint64_t t, bool second_partner) -> std::uint32_t {
        namespace b = dw4::pad_button;
        static constexpr std::pair<std::uint64_t, std::uint32_t> steps[] = {
            {0, b::start}, {100, b::down}, {200, b::a}, {300, b::a},  {400, b::down}, {460, b::down}, {520, b::down}, {600, b::a},
            {700, b::down}, {760, b::down}, {860, b::a}, {1060, b::b}, {1120, b::b}, {1180, b::b}, {1240, b::b},  {1300, b::b}, {1360, b::b}};
        for (const auto& [at, button] : steps)
            if (t >= at && t < at + 3) return button;
        if (t >= 900 && t < 903) return second_partner ? b::down : 0;
        if (t >= 960 && t < 963) return b::a;
        return 0;
    };
    run_members(
        data, "stag0101", 3, 2700, nullptr, [&](Engine& engine, std::uint64_t frame, std::array<PadSample, 4>& pads) {
            auto* sys = engine.get<GameSystem>();
            if (frame == 600)
                for (int s = 0; s < 3; ++s) {
                    auto& devices = sys->players[std::size_t(s)].devices;
                    while (devices.size() < 35) devices.push_back(ItemDevice{0, 50, false, 1, 0});
                }
            if (frame >= 1000) {
                pads[0].buttons = give(frame - 1000, false);
                pads[2].buttons = give(frame - 1000, true);
            }
            if (frame == 2600)
                for (int s = 0; s < 3; ++s) counts[std::size_t(s)] = sys->players[std::size_t(s)].devices.size();
        });
    std::cout << "  stag0101 two gifts to a partner with 35 devices: " << counts[0] << " / " << counts[1] << " / " << counts[2] << '\n';
    check(counts[1] == 36 && counts[0] + counts[1] + counts[2] == 105 && counts[0] + counts[2] == 69,
          "own gifts: the partner's folder takes one, the other gift finds it full (36, nothing lost)");
}

void data_scenarios(const std::filesystem::path& data) {
    // 1. dng101b1: Agumon alone, a Dorumon joins mid-stage (another species:
    // its package loads now), a Veemon joins as the third, the Dorumon leaves
    // through a fork (this copy stays), the Veemon leaves.
    Scenario s1;
    s1.name = "dng101b1 joins and fork";
    s1.stage = "dng101b1";
    s1.players = 1;
    s1.steps = 5200;
    s1.identity = 0;
    Planned j1;
    j1.kind = Planned::join;
    j1.earliest = 1400;
    j1.slot = 1;
    j1.carry = carry_of("p_dorumon", 3, "w_common01");
    j1.point = 0;
    Planned j2 = j1;
    j2.earliest = 2400;
    j2.slot = 2;
    j2.carry = carry_of("p_vmon", 4, "");
    Planned f1;
    f1.kind = Planned::split;
    f1.earliest = 3300;
    f1.mask = 0b0010;
    f1.fork = ForkKind::exit;
    Planned r1;
    r1.kind = Planned::remove;
    r1.earliest = 4200;
    r1.slot = 2;
    Planned r0;
    r0.kind = Planned::remove;
    r0.earliest = 3700;
    r0.slot = 1; // gone on the other side: every copy ignores it
    s1.plan = {j1, j2, f1, r0, r1};
    s1.join_at = 3900;
    const auto a = run_scenario(data, s1, nullptr);
    std::cout << "  " << s1.name << ": " << a.fingerprints.size() << " steps in " << a.seconds << " s, events at";
    for (const auto& e : a.applied) std::cout << ' ' << e.frame;
    std::cout << ", final present " << int(a.final_present) << ", generated " << a.generated_players << ", carry of slot 0 "
              << a.carry_bytes << " B\n";
    check(a.splits.size() == 1 && a.splits[0].leavers == 0b0010 && !a.splits[0].local_leaves, "the fork: slot 1 left, this copy stayed");
    check(a.final_present == 0b0001 && a.generated_players == 1, "one member left at the end");
    const auto b = run_scenario(data, s1, &a.applied);
    const auto n = compare(a, b, s1.name + " (second engine)");
    check(n == a.fingerprints.size(), "the second engine compared every step");
    std::cout << "  second engine: " << n << " steps equal\n";
    check(!a.join_bytes.empty(), "the instance had a snapshot at the join frame");
    const auto c = run_joiner(data, s1, a);
    const auto m = compare(a, c, s1.name + " (joiner)");
    std::cout << "  joiner from " << a.join_bytes.size() << " snapshot bytes + " << a.join_log.size() << " steps: " << m << " steps equal after\n";
    check(m > 1200, "the joiner compared the rest of the run");

    // 2. The other side of the same fork: the copy of the leaver (identity 1).
    Scenario s2 = s1;
    s2.name = "dng101b1 fork, leaver's copy";
    s2.identity = 1;
    s2.plan = {j1, j2, f1};
    s2.steps = 3800;
    s2.join_at = 0;
    const auto a2 = run_scenario(data, s2, nullptr);
    check(a2.splits.size() == 1 && a2.splits[0].local_leaves && a2.final_present == 0b0010, "the leaver's copy keeps slot 1 only");
    const auto b2 = run_scenario(data, s2, &a2.applied);
    check(compare(a2, b2, s2.name) == a2.fingerprints.size(), "the leaver's copies agree");

    // 3. stag0102 (a field with four players at the end: the q_ packages).
    Scenario s3;
    s3.name = "stag0102 three joins";
    s3.stage = "stag0102";
    s3.players = 1;
    s3.steps = 4200;
    Planned k1 = j1;
    k1.earliest = 1500;
    k1.carry = carry_of("p_vmon", 6, "");
    Planned k2 = j1;
    k2.earliest = 2000;
    k2.slot = 2;
    k2.carry = carry_of("p_hekabut", 6, "");
    Planned k3 = j1;
    k3.earliest = 2500;
    k3.slot = 3;
    k3.carry = carry_of("p_dorumon", 6, "");
    // An in-stage fork (a story trigger's participants keep the instance).
    Planned f3;
    f3.kind = Planned::split;
    f3.earliest = 3200;
    f3.mask = 0b0011;
    f3.fork = ForkKind::in_stage;
    s3.plan = {k1, k2, k3, f3};
    s3.join_at = 2200;
    // 7.4: B+X+START held on slot 1 for four seconds reboots nobody online.
    s3.reset_combo_at = 2700;
    const auto a3 = run_scenario(data, s3, nullptr);
    std::cout << "  " << s3.name << ": events at";
    for (const auto& e : a3.applied) std::cout << ' ' << e.frame;
    std::cout << ", generated " << a3.generated_players << '\n';
    check(a3.splits.size() == 1 && a3.splits[0].in_stage && a3.splits[0].kept == 0b0011 && a3.splits[0].local_leaves,
          "the in-stage fork: the participants keep the instance");
    check(a3.final_present == 0b0011 && a3.generated_players == 2, "two players in stag0102 after the fork");
    check(a3.max_generated == 4, "four players of four species in stag0102");
    check(a3.final_stage == "stag0102" && !a3.rebooted, "no console reset online");
    const auto b3 = run_scenario(data, s3, &a3.applied);
    check(compare(a3, b3, s3.name) == a3.fingerprints.size(), "stag0102: the second engine agrees");
    const auto c3 = run_joiner(data, s3, a3);
    check(compare(a3, c3, s3.name + " (joiner)") > 1500, "stag0102: the joiner agrees");

    // 4. Arriving together: two members leave dng101b1 for dng101b2, the
    // second as a co-arrival spawned in the base's load step (before any
    // stage script); a joiner restores the new visit's bytes. Then the group
    // dissolves into the lobby (this machine: slot 1).
    Scenario s4;
    s4.name = "dng101b1 co-arrival and dissolve";
    s4.stage = "dng101b1";
    s4.players = 1;
    s4.steps = 4600;
    s4.identity = 0;
    Planned arrive;
    arrive.kind = Planned::barrier;
    arrive.earliest = 900;
    arrive.stage = "dng101b2";
    arrive.result.decision = Decision::new_instance;
    arrive.result.slot_map = {0, -1, -1, -1};
    arrive.result.co_arrivals.push_back({1, carry_of("p_hekabut", 1, "")});
    arrive.result.own_slot = 0;
    arrive.result.rules.enabled = true;
    Planned leave;
    leave.kind = Planned::barrier;
    leave.earliest = 3400;
    leave.stage = "stagml11";
    leave.result.decision = Decision::dissolve;
    leave.result.own_slot = 1;
    leave.result.rules.enabled = true;
    s4.plan = {arrive, leave};
    s4.join_at = 2600;
    const auto a4 = run_scenario(data, s4, nullptr);
    std::cout << "  " << s4.name << ": events at";
    for (const auto& e : a4.applied) std::cout << ' ' << e.frame;
    std::cout << ", final stage " << a4.final_stage << ", present " << int(a4.final_present) << ", generated " << a4.generated_players << std::endl;
    check(a4.max_generated == 2, "the co-arrival is generated in dng101b2 with the base member");
    check(a4.final_stage == "stagml11" && a4.final_present == 1 && a4.generated_players == 1, "dissolved into a solo lobby");
    const auto b4 = run_scenario(data, s4, &a4.applied);
    check(compare(a4, b4, s4.name) == a4.fingerprints.size(), "co-arrival: the second engine agrees");
    const auto c4 = run_joiner(data, s4, a4);
    check(compare(a4, c4, s4.name + " (joiner)") > 1500, "co-arrival: a joiner of the new visit agrees");

    // 5. Give up: slot 1 is defeated and gives up; in the copy of slot 0
    // the stage goes on, in slot 1's the stage's game over runs.
    for (const int identity : {0, 1}) {
        Scenario s5;
        s5.name = "dng101b1 give up, copy " + std::to_string(identity);
        s5.stage = "dng101b1";
        s5.players = 2;
        s5.species = {"p_agumon", "p_dorumon"};
        s5.steps = 3600;
        s5.identity = identity;
        Planned kill;
        kill.kind = Planned::kill;
        kill.earliest = 1200;
        kill.slot = 1;
        Planned up;
        up.kind = Planned::give_up;
        up.earliest = 1500;
        up.slot = 1;
        s5.plan = {kill, up};
        const auto a5 = run_scenario(data, s5, nullptr);
        std::cout << "  " << s5.name << ": events at";
        for (const auto& e : a5.applied) std::cout << ' ' << e.frame;
        std::cout << ", final stage " << a5.final_stage << ", present " << int(a5.final_present) << std::endl;
        check(a5.splits.size() == 1 && a5.splits[0].leavers == 0b0010, "give up forks the defeated member off");
        if (identity == 0) check(a5.final_stage == "dng101b1" && a5.final_present == 1, "the living member plays on");
        else check(a5.final_present == 0b0010 && a5.final_stage == "stagml11", "the member who gave up gets the game over");
        const auto b5 = run_scenario(data, s5, &a5.applied);
        check(compare(a5, b5, s5.name) == a5.fingerprints.size(), "give up: the second engine agrees");
    }

    // 6. Own windows (SEAMLESS.md 5.3, round 63): two members in dng101b1;
    // slot 0 opens its status window with START and walks its pages while
    // slot 1 plays on, slot 1 opens its own while slot 0's is open, a third
    // member joins while both are open; nothing pauses (slot 1's player moves
    // and the enemies run while slot 0 is in its menu), both close again and
    // their players take their pads again. A second engine and a joiner
    // restored while the windows are open stay equal.
    {
        constexpr std::uint64_t open0 = 1500, open1 = 1700;
        Scenario s6;
        s6.name = "dng101b1 own menus";
        s6.stage = "dng101b1";
        s6.players = 2;
        s6.species = {"p_agumon", "p_dorumon"};
        s6.steps = 3000;
        s6.identity = 0;
        s6.join_at = open1 + 80;
        Planned j;
        j.kind = Planned::join;
        j.earliest = open1 + 40;
        j.slot = 2;
        j.carry = carry_of("p_vmon", 4, "");
        j.point = 0;
        s6.plan = {j};
        // A member's menu: still, START, then A (Equipment) / B / DOWN / A
        // (Folder) / B / B (close), 120 frames apart.
        auto menu_pad = [](std::uint64_t frame, std::uint64_t open, PadSample& pad) {
            if (frame + 30 < open || frame > open + 900) return;
            pad = PadSample{};
            pad.connected = true;
            static constexpr std::uint32_t presses[] = {dw4::pad_button::start, dw4::pad_button::a, dw4::pad_button::b, dw4::pad_button::down,
                                                        dw4::pad_button::a,     dw4::pad_button::b, dw4::pad_button::b, dw4::pad_button::b};
            if (frame < open) return;
            const std::uint64_t t = frame - open;
            if (t % 120 < 3 && t / 120 < std::size(presses)) pad.buttons = presses[t / 120];
        };
        s6.shape = [&](std::uint64_t frame, std::array<PadSample, 4>& pads) {
            menu_pad(frame, open0, pads[0]);
            menu_pad(frame, open1, pads[1]);
        };
        struct Seen {
            bool own0{}, both{}, joined_in_menus{}, closed{}, control_back{}, one_moved{}, enemies_ran{}, copies{};
            dw4::engine::Vec3f one_at{};
            std::uint64_t enemies_at{};
        } seen;
        auto enemies = [](Engine& engine) {
            std::uint64_t h = 0;
            if (const auto* models = engine.get<ModelCtrlManager>())
                for (const auto& ram : models->rams())
                    if (ram->category == "ENEMY" && ram->generated && ram->node)
                        h = h * 1000003u + std::uint64_t(std::int64_t(ram->node->matrix(3, 0) * 16) ^ std::int64_t(ram->node->matrix(3, 2) * 16));
            return h;
        };
        s6.observe = [&](Engine& engine, std::uint64_t frame) {
            auto* models = engine.get<ModelCtrlManager>();
            const auto* one = models ? models->player(1) : nullptr;
            if (frame == open0 + 60) {
                // (Its player stands without its input but keeps its control: it can be hit.)
                const auto* zero = models ? models->player(0) : nullptr;
                seen.own0 = engine.own_window_open(0) && !engine.own_window_open(1) && engine.busy_slots() == 0b0001 &&
                            engine.own_input_held(0) && !engine.own_input_held(1) && zero && zero->control_enabled;
                if (one && one->node) seen.one_at = {one->node->matrix(3, 0), 0, one->node->matrix(3, 2)};
                seen.enemies_at = enemies(engine);
                seen.enemies_ran = models && std::none_of(models->rams().begin(), models->rams().end(), [](const auto& ram) { return ram->frozen; });
                seen.copies = models && models->find("i_stmenu01@0", 0) && models->find("i_stbase@0", 0) && !models->find("i_stmenu01@1", 0);
            }
            if (frame == open0 + 180 && one && one->node) {
                const float dx = one->node->matrix(3, 0) - seen.one_at.x, dz = one->node->matrix(3, 2) - seen.one_at.z;
                seen.one_moved = dx * dx + dz * dz > 1.0f;
            }
            if (frame == open1 + 60) {
                seen.both = engine.own_window_open(0) && engine.own_window_open(1) && models && models->find("i_stmenu01@1", 0);
                seen.enemies_ran = seen.enemies_ran && models &&
                                   std::none_of(models->rams().begin(), models->rams().end(), [](const auto& ram) { return ram->frozen; });
            }
            if (frame == open1 + 120) seen.joined_in_menus = (engine.online().present & 0b0100) && engine.own_window_open(0) && engine.own_window_open(1);
            if (frame == open1 + 1100) {
                seen.closed = !engine.own_window_open(0) && !engine.own_window_open(1) && engine.busy_slots() == 0;
                const auto* zero = models ? models->player(0) : nullptr;
                seen.control_back = zero && zero->control_enabled && one && one->control_enabled && !engine.own_input_held(0) &&
                                    !engine.own_input_held(1);
            }
        };
        const auto a6 = run_scenario(data, s6, nullptr);
        std::cout << "  " << s6.name << ": events at";
        for (const auto& e : a6.applied) std::cout << ' ' << e.frame;
        std::cout << "; own window " << seen.own0 << ", copies " << seen.copies << ", slot 1 moved " << seen.one_moved << ", nothing frozen "
                  << seen.enemies_ran << ", both " << seen.both << ", joined " << seen.joined_in_menus << ", closed " << seen.closed
                  << ", control " << seen.control_back << '\n';
        check(seen.own0 && seen.copies, "own menus: slot 0's window is its own, with its own copies, its player on control without input");
        check(seen.one_moved && seen.enemies_ran, "own menus: slot 0's window pauses nothing");
        check(seen.both, "own menus: two windows at once");
        check(seen.joined_in_menus && !a6.applied.empty() && a6.applied[0].frame < open1 + 120, "own menus: a member joins while both are open");
        check(seen.closed && seen.control_back, "own menus: both close, both players take their pads again");
        const auto b6 = run_scenario(data, s6, &a6.applied);
        check(compare(a6, b6, s6.name) == a6.fingerprints.size(), "own menus: the second engine agrees");
        const auto c6 = run_joiner(data, s6, a6);
        const auto m6 = compare(a6, c6, s6.name + " (joiner)");
        std::cout << "  joiner restored with both windows open: " << m6 << " steps equal after\n";
        check(m6 > 1000, "own menus: the joiner compared the rest of the run");
    }
    // 7. Own talks wait for each other and trade with the talker's folders;
    // gifts look at the partner's room when given (the round 63 review).
    own_talks(data);
    own_gifts(data);

    // 8. The transition steps take the pads of the barrier tick.
    transition_pads(data);
    // 9. The barrier names the point the exit leads to.
    barrier_point(data);
}

} // namespace

// The room's story and modes at the title's Game Start (SEAMLESS.md 8.5): on
// the step of every TITLE2 GAMESTART (Engine::title_game_start), not at
// game_started's rising edge. That flag stays set after "Continue? No" took the
// player back to the title, and the title's option menu sets it without a game
// start: the next Game Start of the session got neither (round 56 review).
void title_application_at_every_game_start() {
    Engine engine{dw4::MountedResources{}};
    dw4::game::online::EngineHooks hooks;
    hooks.engine = [&]() -> Engine& { return engine; };
    dw4::game::online::RoomSettings settings;
    settings.difficulty = 2;
    settings.camera_mode = 1;
    settings.light_mode = 0;
    dw4::game::online::GameEngineLink link(hooks, settings);
    RoomStory room;
    room.flags[19] = 1;
    room.revision = 3;
    link.set_room_story(room);
    auto* sys = engine.get<GameSystem>();
    // The option menu's reload: game_started without a game start.
    sys->game_started = true;
    sys->difficulty = 0;
    sys->camera_mode = 0;
    link.after_live_step();
    check(!link.game_start() && sys->difficulty == 0 && sys->camera_mode == 0 && !engine.flags().count(19), "no application without GAMESTART");
    // GAMESTART (TITLE2): the story and the modes on that step, once.
    engine.title_game_start = true;
    check(link.game_start(), "the host sees the Game Start");
    link.after_live_step();
    check(!engine.title_game_start && sys->difficulty == 2 && sys->camera_mode == 1 && sys->light_mode == 0 && engine.flags().at(19) == 1,
          "the room's story and modes at the Game Start");
    // "Continue? No": the title again; its character select writes its own
    // difficulty and the record's story (game_started stays set).
    sys->difficulty = 0;
    engine.flags()[19] = 0;
    link.after_live_step();
    check(sys->difficulty == 0, "nothing between two Game Starts");
    engine.title_game_start = true;
    link.after_live_step();
    check(sys->difficulty == 2 && engine.flags().at(19) == 1, "the session's second Game Start gets them again");
}

int main(int argc, char** argv) {
    try {
        snapshot_bytes();
        carries_and_barriers();
        session_tables();
        forbidden_environment();
        rules_offline_defaults();
        rules_cheats_and_resets();
        rules_input_masking();
        rules_pad_redirect();
        rules_context_hash();
        rules_guards();
        rules_replay_and_split_without_players();
        barrier_without_data();
        dissolve_through_the_link();
        story_at_the_barrier();
        title_application_at_every_game_start();
        leaving_redirect();
        scoped_commands();
        std::string data = argc > 1 ? argv[1] : "";
        if (data.empty())
            if (const char* env = std::getenv("DW4_TEST_DATA")) data = env;
        if (!data.empty() && std::filesystem::exists(data)) {
            std::cout << "online scenarios with the game data in " << data << '\n';
            data_scenarios(data);
        } else {
            std::cout << "online scenarios skipped (no game data: DW4_TEST_DATA)\n";
        }
        std::cout << checks << " online checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Online test failure: " << error.what() << '\n';
        return 1;
    }
}
