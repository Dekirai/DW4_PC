// Seamless online co-op, the engine side (docs/SEAMLESS.md 3.7, 6.5-6.9, 7):
// the online rules, input masking, transition pads, membership (join,
// remove, carry), forks, the barrier's application in the load step, the
// predicates, the session context, the pad redirect, the net fingerprint and
// the process-wide guards. Nothing here runs while Engine::online().enabled
// is off (the MXCSR guard is a no-op with the default MXCSR).
#include "online_detail.hpp"

#include "dw4/engine/managers.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/engine/online_membership.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <xmmintrin.h>

namespace dw4::engine {

// ---------------------------------------------------------------------------
// Process-wide guards
// ---------------------------------------------------------------------------
namespace detail {
namespace {
int g_live_engines = 0;
int g_online_engines = 0;
std::atomic<std::uint64_t> g_mxcsr_repairs{0};
} // namespace

void engine_constructed() {
    if (g_online_engines > 0)
        throw std::logic_error("a second engine while an online engine runs (one deterministic engine per process)");
    ++g_live_engines;
}
void engine_destroyed(bool online) {
    --g_live_engines;
    if (online) --g_online_engines;
}

// Round to nearest, no flush to zero, no denormals are zero, every exception
// masked: the MXCSR of a fresh Windows x64 thread. Only the control bits
// (6-15) count; bits 0-5 are the sticky exception flags any arithmetic sets.
constexpr unsigned kMxcsr = 0x1F80u;
constexpr unsigned kMxcsrControl = 0xFFC0u;

MxcsrGuard::MxcsrGuard() : saved_(_mm_getcsr()) {
    if ((saved_ & kMxcsrControl) != kMxcsr) {
        g_mxcsr_repairs.fetch_add(1, std::memory_order_relaxed);
        _mm_setcsr(kMxcsr | (saved_ & 0x3Fu));
    }
}
MxcsrGuard::~MxcsrGuard() {
    if ((saved_ & kMxcsrControl) != kMxcsr) _mm_setcsr(saved_);
}
} // namespace detail

int Engine::live_engines() { return detail::g_live_engines; }
std::uint64_t Engine::mxcsr_repairs() { return detail::g_mxcsr_repairs.load(std::memory_order_relaxed); }

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------
// data/online/session_values.txt (tools/online/session_census.py); the
// online tests compare both.
const SessionValueTables& session_value_tables() {
    static const SessionValueTables tables = [] {
        SessionValueTables t;
        t.context_flags = {63, 64, 71, 72, 376, 377, 378, 379, 380, 381, 384, 385, 386, 387, 388, 389, 409, 424, 508, 571};
        t.context_counters = {2};
        t.context_folders = {11};
        t.personal_flags = {24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  85,  328, 336, 352, 364,
                            374, 375, 415, 416, 417, 418, 419, 420, 475, 477, 478, 480, 572, 573, 574, 575,
                            594, 611, 908, 909, 910, 912, 913, 914, 915, 916, 917, 918, 919, 920, 921, 922,
                            923, 924, 925, 926, 927, 928, 929, 930, 931, 932, 933, 934, 935, 936, 937};
        return t;
    }();
    return tables;
}

const std::vector<std::string_view>& online_forbidden_env() {
    static const std::vector<std::string_view> names = {
        // cheats and rebalancing (the engine constructor, the host's test switches)
        "DW4_GOD", "DW4_ONE_HIT", "DW4_INFINITE_MP", "DW4_CHEATS_FROM_ENV", "DW4_GAMEPLAY",
        // the world clock, the disc and the AX phase
        "DW4_FIXED_RANDOM", "DW4_CLOCK_FILE", "DW4_DISC_CLOCK", "DW4_AX_PHASE",
        // test characters and their values
        "DW4_LEVEL", "DW4_BITS", "DW4_DEVICES", "DW4_DISKS", "DW4_FORMS", "DW4_XDATA", "DW4_FLAG_FOLDER",
        "DW4_CARD_RECORD", "DW4_LEVEL_ON_LOAD", "DW4_DISKS_ON_LOAD",
        // test run starts
        "DW4_COUNTERS", "DW4_DIFFICULTY", "DW4_ENTRY", "DW4_PLAYERS",
        // the randomizer, load timing, data and cards (the room's settings decide them online)
        "DW4_RANDOMIZER", "DW4_RANDOMIZER_SWEEP", "DW4_FAST_LOADING", "DW4_MODS", "DW4_CARD_DIR", "DW4_CARD_A",
        "DW4_CARD_B",
        // the cameras-exits test switches (game_main.cpp: rules, forks and intents of offline test runs)
        "DW4_CAMERAS", "DW4_CAM_LOCAL", "DW4_LEASH_OFF", "DW4_ONLINE", "DW4_TEST_RULES", "DW4_TEST_GATEDISC",
        "DW4_TEST_GIVEUP", "DW4_TEST_TITLE_DRIVE",
        // state tests, the randomizer's save bond of test runs and the direct netplay menus (the client lane's list)
        "DW4_STATE_FILE", "DW4_STATE_TEST", "DW4_RANDOMIZER_BIND", "DW4_MENU_HOST", "DW4_MENU_IMPORT", "DW4_MENU_JOIN",
        // the cloud backup panel's test login and action (F1 Saves, outside rooms only)
        "DW4_MENU_CLOUD",
        // the pad replay of the test tools replaces the sealed pads before the step (game_main.cpp)
        "DW4_PAD_REPLAY",
        // the title menu's third entry in a test run without the online screens (game_main.cpp; it lets
        // TITLE2's cursor reach it)
        "DW4_TITLE_ONLINE",
        // a Data Overflow run keeps the test characters' variables and cheats (src/game/overflow_run.hpp)
        "DW4_OVERFLOW_TEST_CHEATS",
        // round 63's analysis switch: parts of the status window's pause left out (subwnd.cpp)
        "DW4_TEST_SUBWND_SKIP"};
    return names;
}

const std::vector<std::string_view>& online_presentation_env() {
    // An entry ending in '*' is a prefix.
    static const std::vector<std::string_view> names = {
        "DW4_TRACE_*", "DW4_DEV_*", "DW4_AUDIO_*", "DW4_CAMPAIGN_*", "DW4_MENU_TAB", "DW4_FARM_*",
        "DW4_ACHIEVEMENTS", "DW4_AX_COMPRESSOR", "DW4_AVOID_MATS", "DW4_DISCORD_PIPE", "DW4_DRAW_DUMP", "DW4_DUMP_SEQ",
        "DW4_DUMP_TEXTURES", "DW4_LABELS", "DW4_MAT_SEQUENCE", "DW4_MAT_SWITCH", "DW4_MIGRATE_FROM", "DW4_MSAA",
        "DW4_NO_ERROR_BOX", "DW4_OPEN_MENU", "DW4_PRESENCE_ART_ALL", "DW4_PROFILE", "DW4_RENDER",
        "DW4_SCENE_REPORT", "DW4_SEEK_DIRECT", "DW4_SMOKE_STAGES", "DW4_STAGE_STATES",
        "DW4_TEXTURE_PACK", "DW4_WIDESCREEN", "DW4_WINDOW",
        // the online client's own switches (src/game/online: development server, pin and password, the
        // calibration, the rate override, its trace, the test driver's position interval)
        "DW4_ONLINE_CALIBRATION", "DW4_ONLINE_PASSWORD", "DW4_ONLINE_PIN", "DW4_ONLINE_RATE", "DW4_ONLINE_SERVER",
        "DW4_ONLINE_TRACE", "DW4_ONLINE_SCRIPT_EVERY",
        // the online account's achievement profile of a test run (online_achievements.hpp, round S2)
        "DW4_ONLINE_PROFILE",
        // the resync scenarios' injected desync (DW4_ONLINE_DEV builds only, game_main.cpp: it is the point of the test)
        "DW4_ONLINE_TEST_DESYNC",
        // round J's damaged checkpoint and the tick state that misses a field (DW4_ONLINE_DEV builds only, session.cpp,
        // game_main.cpp: scenarios JB and JS)
        "DW4_ONLINE_TEST_BAD_CHECKPOINT", "DW4_ONLINE_TEST_TICKSTATE_SKIP",
        // the ghost figures (round 60, presentation only): off, and the form and old-client tests of the scenarios
        // (DW4_ONLINE_DEV builds only, session.cpp: the form PRESENCE2 names cycles, the engine's stays; PRESENCE
        // instead of PRESENCE2)
        "DW4_GHOSTS", "DW4_ONLINE_TEST_FORM", "DW4_ONLINE_TEST_PRESENCE1",
        // round 62: the online log's file, the latency scenarios' proxy port and their host-loop throttle (the
        // relay seals by its clock: a throttled member's own pads come late, the simulation is the same)
        "DW4_ONLINE_LOG", "DW4_ONLINE_RELAY_PORT", "DW4_ONLINE_TEST_STALL",
        // the round 62 review's vsync grid of the latency scenarios (a pass ends on a grid of HZ, like a display's vsync)
        "DW4_ONLINE_TEST_PASS_HZ",
        // round G-core's G7: the room story ignored for N seconds after the first Game Start (DW4_ONLINE_DEV builds only, online_run.cpp:
        // the Game Start's hold until the confirmed story is the point of the test)
        "DW4_ONLINE_TEST_STORY_DELAY",
        // round I1a: the automatic choice of the scenarios (DW4_ONLINE_DEV builds only, online_run.cpp: first = the
        // relay's automatic join, new = a new instance; the relay decides, the simulation is the same)
        "DW4_ONLINE_AUTOCHOICE",
        // round I1a's counter-proof of W1 and W30: the replace switched off (DW4_ONLINE_DEV builds only, engine_adapter.cpp:
        // it is the point of the test)
        "DW4_ONLINE_TEST_NO_REPLACE",
        // round I1a's integration: W7e's link cut at the barrier (DW4_ONLINE_DEV builds only, online_run.cpp: the
        // relay decides the barrier without the lost member, the simulation is the same)
        "DW4_ONLINE_TEST_CUT_AT_PARK",
        // round S1 test tools (game_main.cpp)
        "DW4_SNAPSHOT_DUMP", "DW4_PAD_LOG", "DW4_EXTRA_RENDERS", "DW4_EXTRA_WIDEN", "DW4_EXTRA_PREVIOUS",
        "DW4_CAM_CHECK",
        // round I1a's ghost figure measurement (DW4_GHOST_PROBE test builds only, game_main.cpp, ghost_figures.cpp)
        "DW4_PROBE_GHOSTS*"};
    return names;
}

std::vector<std::string> online_forbidden_env_set() {
    std::vector<std::string> set;
    for (const auto name : online_forbidden_env())
        if (std::getenv(std::string(name).c_str())) set.emplace_back(name);
    return set;
}

Cheats online_cheats(const Cheats& room) {
    Cheats cheats = room;
    cheats.god = cheats.infinite_mp = cheats.one_hit = false;
    cheats.exp_rate = cheats.damage_dealt = cheats.damage_taken = 100;
    return cheats;
}

std::uint64_t online_hash(std::span<const std::uint8_t> bytes) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const auto b : bytes) {
        h ^= b;
        h *= 0x100000001b3ull;
    }
    return h;
}

// Every slot's PlayerBox state (6.9; the shared camera's state is in the
// scene nodes Engine::fingerprint already covers).
std::uint64_t net_fingerprint_cameras(const Engine& engine) {
    const auto* cameras = engine.get<CameraManager>();
    return cameras ? cameras->player_box_hash() : 0;
}

namespace {
constexpr std::uint8_t bit(int slot) { return std::uint8_t(1u << slot); }
int lowest(std::uint8_t mask) { return mask ? std::countr_zero(unsigned(mask)) : -1; }

struct Fnv {
    std::uint64_t h = 0xcbf29ce484222325ull;
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            h ^= p[i];
            h *= 0x100000001b3ull;
        }
    }
    template <class T> void value(const T& v) { bytes(&v, sizeof v); }
    void text(std::string_view s) {
        value(std::uint32_t(s.size()));
        bytes(s.data(), s.size());
    }
};

// The HUD panel positions of funcPlayerStatusGenerate.
constexpr const char* kPanelAt[4] = {"V<-260,-160,0>", "V<-110,-160,0>", "V<40,-160,0>", "V<190,-160,0>"};
} // namespace

// ---------------------------------------------------------------------------
// Rules and predicates
// ---------------------------------------------------------------------------
void Engine::set_online(const OnlineRules& rules) {
    if (rules.enabled && !counted_online_) {
        if (detail::g_live_engines > 1)
            throw std::logic_error("an online engine must be the only engine of the process");
        counted_online_ = true;
        ++detail::g_online_engines;
    } else if (!rules.enabled && counted_online_) {
        counted_online_ = false;
        --detail::g_online_engines;
    }
    const OnlineRules before = online_;
    online_ = rules;
    // (Round J's review: the Lua collector counts strings per content online
    // only; offline it stays as before, the rule "offline bit-identical".)
    if (lua_) lua_->set_strings_by_content(online_.enabled);
    // (The sound output plays the own windows' sound effects of this
    // machine's member alone, SEAMLESS.md 5.3.)
    if (auto* sound = get<SoundManager>()) sound->set_listener_slot(online_.enabled ? view_slot_ : -1);
    if (!online_.enabled) {
        personal_ = {};
        barrier_pads_.reset();
        barrier_.reset();
        pending_fork_.reset();
        online_rules_changed(before);
        return;
    }
    cheats_ = online_cheats(cheats_);
    console_reset_.set_enabled(false);
    if (std::popcount(unsigned(online_.present)) >= 2)
        for (int s = 0; s < 4; ++s)
            if ((online_.present & bit(s)) && !personal_[std::size_t(s)]) personal_[std::size_t(s)] = personal_session(s);
    online_rules_changed(before);
}

// The camera mask's box hand-over (4.1), the leash ring (4.6) and the
// funcStageChange port, which exists while two or more members are present
// (5.2): after set_online and after every membership step.
void Engine::online_rules_changed(const OnlineRules& before) {
    apply_online_cameras(before);
    apply_leash_rule();
    const bool shared_before = before.enabled && std::popcount(unsigned(before.present)) >= 2;
    if (before.enabled != online_.enabled || shared_before != online_shared()) install_online_lua();
}

bool Engine::hub_stage(std::string_view stage) {
    std::string s(stage);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s == "stagml02" || s == "stagml11" || s == "stagml12" || s == "stagml13" || s == "stagml14" || s.rfind("stagis", 0) == 0;
}

bool Engine::membership_safe() const {
    if (rebooting_ || !presenting() || loading() || !pending_stage_.empty() || !stage_active_) return false;
    if (!pad_input_enabled_) return false;
    // (A member's own window or scene pauses nothing and holds up nothing:
    // a member joins or leaves while another has its menu open, 5.3.)
    if (demo_.running_unscoped() != 0) return false;
    if (const auto* windows = get<SubWindowManager>(); windows && windows->shared_window_open()) return false;
    if (pending_fork_) return false;
    // (Round I1b's busy test hook, development scenarios only: 0 otherwise.)
    if (online_test_busy_ > 0) return false;
    return true;
}

bool Engine::joinable() const { return membership_safe() && !hub_stage(stage_name_) && !online_.boss_lock; }

int Engine::leader() const {
    if (!online_.enabled) return 0;
    if (const int l = lowest(online_.present & std::uint8_t(~online_.leaving)); l >= 0) return l;
    if (const int l = lowest(online_.present); l >= 0) return l;
    return 0;
}

int Engine::pad_reader_slot(int index, int trigger) const {
    const int i = std::clamp(index, 0, 3);
    if (!online_.enabled) return i;
    const std::uint8_t usable = online_.present & std::uint8_t(~online_.leaving);
    if (i == 0) return trigger >= 0 && trigger < 4 && (usable & bit(trigger)) ? trigger : leader();
    return (usable & bit(i)) ? i : leader();
}

std::uint64_t Engine::session_context_hash() const {
    const auto& tables = session_value_tables();
    Fnv h;
    for (const auto flag : tables.context_flags) {
        const auto found = flags_.find(flag);
        h.value(flag);
        h.value(std::uint8_t(found != flags_.end() ? found->second : 0));
    }
    for (const auto counter : tables.context_counters) {
        const auto found = counters_.find(counter);
        h.value(counter);
        h.value(std::int32_t(found != counters_.end() ? found->second : 0));
    }
    const auto* sys = get<GameSystem>();
    for (const auto entry : tables.context_folders) {
        std::uint8_t value = 0;
        if (sys)
            if (const auto found = sys->flag_folder.find(entry); found != sys->flag_folder.end()) value = found->second;
        h.value(entry);
        h.value(value);
    }
    return h.h;
}

PersonalSession Engine::personal_session(int slot) const {
    if (online_.enabled && slot >= 0 && slot < 4 && personal_[std::size_t(slot)]) return *personal_[std::size_t(slot)];
    PersonalSession personal;
    if (const auto* sys = get<GameSystem>()) {
        personal.gate_stage_name = sys->gate_stage_name;
        personal.gate_stage_no = sys->gate_stage_no;
    }
    for (const auto flag : session_value_tables().personal_flags)
        if (const auto found = flags_.find(flag); found != flags_.end()) personal.flags[flag] = found->second;
    return personal;
}

void Engine::online_gate_stage_set() {
    if (!online_.enabled) return;
    const auto* sys = get<GameSystem>();
    if (!sys) return;
    for (int s = 0; s < 4; ++s)
        if ((online_.present & bit(s)) && personal_[std::size_t(s)]) {
            personal_[std::size_t(s)]->gate_stage_name = sys->gate_stage_name;
            personal_[std::size_t(s)]->gate_stage_no = sys->gate_stage_no;
        }
}

// ---------------------------------------------------------------------------
// Steps
// ---------------------------------------------------------------------------
std::array<PadSample, 4> Engine::online_samples(const std::array<PadSample, 4>& samples) const {
    std::array<PadSample, 4> out = samples;
    // 6.7: the transition steps (the module reload, its held fields, the
    // load step) take the pads of the barrier tick; without one (the
    // session's first stage) the neutral pads.
    if (!pending_stage_.empty()) {
        if (barrier_pads_) out = *barrier_pads_;
        else out = {};
    }
    // 6.5: absent slots neutral and disconnected, leaving ones neutral and
    // connected, whatever the tick holds.
    for (int s = 0; s < 4; ++s) {
        if (!(online_.present & bit(s))) out[std::size_t(s)] = PadSample{};
        else if (online_.leaving & bit(s)) {
            out[std::size_t(s)] = PadSample{};
            out[std::size_t(s)].connected = true;
        } else if (!pending_stage_.empty() && !barrier_pads_) {
            out[std::size_t(s)].connected = true;
        }
    }
    return out;
}

void Engine::apply_pending_fork() {
    if (!pending_fork_) return;
    const auto fork = *pending_fork_;
    pending_fork_.reset();
    // online_split narrowed `present` to this copy's side at once.
    const std::uint8_t other = fork.other;
    OnlineRules before = online_;
    before.present = std::uint8_t(online_.present | other);
    for (int s = 0; s < 4; ++s)
        if (other & bit(s)) despawn_player(s);
    split_ = SplitResult{frame_, fork.leavers, fork.kept, fork.in_stage, fork.take_leavers, fork.child};
    membership_changed(before);
}

void Engine::finish_online_step(const std::array<PadSample, 4>& samples) {
    apply_pending_fork();
    if (!pending_stage_.empty()) {
        if (!barrier_pads_) barrier_pads_ = samples;
    } else {
        barrier_pads_.reset();
    }
    // (The leash ring that loads with the stage goes in every step's
    // apply_leash_rule.)
    if (online_test_busy_ > 0) --online_test_busy_;
}

std::optional<SplitResult> Engine::take_split() {
    auto split = split_;
    split_.reset();
    return split;
}

// ---------------------------------------------------------------------------
// Membership
// ---------------------------------------------------------------------------
void Engine::membership_changed(const OnlineRules& before) {
    if (auto* sys = get<GameSystem>()) {
        int ninzu = 0;
        for (const auto& p : sys->players) ninzu += p.joined ? 1 : 0;
        sys->ninzu = ninzu;
    }
    const int count = std::popcount(unsigned(online_.present));
    if (count >= 2) {
        // 4.1 / 4.6: every present slot has its own box camera, the leash
        // ring is off until the next stage load.
        online_.player_cameras = online_.present;
        online_.leash_off = true;
        // Personal values for every present slot (the base member's from the
        // engine-wide ones, before another member's line can touch them).
        for (int s = 0; s < 4; ++s)
            if ((online_.present & bit(s)) && !personal_[std::size_t(s)]) personal_[std::size_t(s)] = personal_session(s);
    } else if (online_.player_cameras != 0) {
        // 4.4: back to one member keeps the mask {its slot} until the next load.
        online_.player_cameras = online_.present;
    }
    online_rules_changed(before);
}

PlayerCarry Engine::carry_player(int slot) const {
    PlayerCarry carry;
    const int s = std::clamp(slot, 0, 3);
    if (const auto* sys = get<GameSystem>()) {
        carry.slot = sys->players[std::size_t(s)];
        carry.slot.joined = true;
        carry.chip = sys->bit_chips[std::size_t(s)];
    }
    carry.personal = personal_session(s);
    return carry;
}

void Engine::online_join_player(int slot, const PlayerCarry& carry, int entrance_point) {
    const detail::MxcsrGuard mxcsr; // (spawns between steps: 6.12)
    if (!online_.enabled || slot < 0 || slot > 3 || (online_.present & bit(slot))) return;
    auto* sys = get<GameSystem>();
    if (!sys) return;
    const OnlineRules before = online_;
    auto& player = sys->players[std::size_t(slot)];
    player = carry.slot;
    player.joined = true;
    sys->bit_chips[std::size_t(slot)] = carry.chip;
    personal_[std::size_t(slot)] = carry.personal;
    online_.present |= bit(slot);
    online_.leaving &= std::uint8_t(~bit(slot));
    membership_changed(before);
    if (player.digimon.empty()) player.digimon = "p_agumon";
    // The species package (boot.lua: _common/p_<x>.igb for up to two
    // players, the q_ variants for more), loaded now unless the stage has
    // it; then the weapon packages, as for a direct start's players.
    bool quarter = false;
    for (const auto& file : files_)
        if (file.logical_path.rfind("_common/q_", 0) == 0) quarter = true;
    const std::string plain = "_common/" + player.digimon + ".igb";
    std::string reduced = player.digimon;
    if (reduced.rfind("p_", 0) == 0) reduced.replace(0, 2, "q_");
    reduced = "_common/" + reduced + ".igb";
    if (!quarter || !load_common_now(reduced)) load_common_now(plain);
    if (auto* weapons = get<WeaponManager>())
        for (const auto& weapon : player.weapons)
            if (!weapon.empty() && !weapons->labels(weapon)) load_common_now("_common/" + weapon + ".igb");
    // The spawn has no counterpart in the original: the message objects keep
    // their state.
    const Messenger::PortOnly port_only(messenger_);
    Packet generate;
    if (auto* points = get<PlayerPointManager>())
        if (const auto m = points->point_matrix(entrance_point, slot)) generate.matrices.push_back(*m);
    messenger_.send(*this, "PLAYER", player.digimon, slot, "GENERATE", generate);
    Packet capsule;
    messenger_.send(*this, "PLAYER2", std::nullopt, slot, "GENESTAGE", capsule);
    Packet entry;
    messenger_.send(*this, "PLAYER", player.digimon, slot, "SQ_ENTR", entry);
    // funcPlayerStatusGenerate and funcPlayerStatusSet for this slot.
    for (const char* panel : {"i_stpnl01", "i_stpnl02", "i_stpnl03", "i_stpnl04", "i_stpnl05"}) {
        Packet at;
        parse_packet_arguments(kPanelAt[slot], at);
        messenger_.send(*this, "MODELCTRL", std::string_view(panel), slot, "GENERATE", at);
    }
    const std::pair<const char*, std::string> open[] = {
        {"i_stpnl01", "SQ_OPEN"}, {"i_stpnl02", player.digimon}, {"i_stpnl03", "SQ_OPENWN"}, {"i_stpnl04", "SQ_OPENWN"}, {"i_stpnl05", "SQ_WAIT"}};
    for (const auto& [panel, command] : open) {
        Packet none;
        messenger_.send(*this, "MODELCTRL", std::string_view(panel), slot, command, none);
    }
}

void Engine::despawn_player(int slot) {
    auto* sys = get<GameSystem>();
    auto* models = get<ModelCtrlManager>();
    const Messenger::PortOnly port_only(messenger_);
    // Its own window and scenes go with it (5.3).
    end_own_scenes(slot);
    // As the pits' PLAYER REMOVE (the player class hook closes an open quick
    // panel, the model releases its action, subcommands and timers).
    if (models)
        if (const auto* player = models->player(slot); player && player->generated) {
            Packet remove;
            messenger_.send(*this, "PLAYER", player->info_name, slot, "REMOVE", remove);
        }
    Packet arms;
    messenger_.send(*this, "PLAYERARMS", std::nullopt, slot, "REMOVE", arms);
    Packet panel;
    messenger_.send(*this, "NDMWPANEL", std::nullopt, slot, "REMOVE", panel);
    for (const char* hud : {"i_stpnl01", "i_stpnl02", "i_stpnl03", "i_stpnl04", "i_stpnl05"}) {
        Packet remove;
        messenger_.send(*this, "MODELCTRL", std::string_view(hud), slot, "REMOVE", remove);
    }
    Packet capsule;
    messenger_.send(*this, "MODELCTRL", "g_000bcaps", slot, "REMOVE", capsule);
    // (An item label of this player goes by itself: its owner no longer
    // answers, ModelCtrlManager::update_item_label.)
    if (sys) {
        sys->players[std::size_t(slot)].joined = false;
        sys->bit_chips[std::size_t(slot)] = {};
    }
    personal_[std::size_t(slot)].reset();
    online_.present &= std::uint8_t(~bit(slot));
    online_.leaving &= std::uint8_t(~bit(slot));
}

void Engine::online_remove_player(int slot) {
    if (!online_.enabled || slot < 0 || slot > 3 || !(online_.present & bit(slot))) return;
    const detail::MxcsrGuard mxcsr;
    const OnlineRules before = online_;
    despawn_player(slot);
    membership_changed(before);
}

void Engine::online_mark_leaving(int slot) {
    // 6.5: from the LEAVE's tick until it applies, the slot plays the
    // neutral, connected pad, and pad_reader_slot hands its readers (an exit
    // prompt it owns, a window) the leader's pad, so the instance reaches the
    // membership-safe step the LEAVE waits for.
    if (!online_.enabled || slot < 0 || slot > 3 || !(online_.present & bit(slot)) || (online_.leaving & bit(slot))) return;
    online_.leaving |= bit(slot);
}

void Engine::online_intent(int slot, IntentKind kind, int argument) {
    (void)argument;
    if (!online_.enabled || slot < 0 || slot > 3 || !(online_.present & bit(slot))) return;
    const detail::MxcsrGuard mxcsr;
    if (kind == IntentKind::give_up) {
        // The defeated member forks off alone; in that copy nobody lives and
        // the stage's game over (end_g_000bcaps.lua) runs as in the original.
        online_split(bit(slot), ForkKind::give_up);
        apply_pending_fork();
        return;
    }
    // go_without: round S3 (boss and story mats).
}

bool Engine::online_split(std::uint8_t mask, ForkKind kind) {
    if (!online_.enabled) return true;
    const std::uint8_t present = online_.present;
    std::uint8_t leavers = mask & present;
    std::uint8_t stayers = present & std::uint8_t(~leavers);
    std::uint8_t alive = 0;
    if (const auto* models = get<ModelCtrlManager>())
        for (int s = 0; s < 4; ++s)
            if (const auto* player = models->player(s); player && player->generated && player->hp > 0) alive |= bit(s);
    // 6.7, the side rule: the dead travel with the living (not for HOME,
    // a server decision about accounts: exactly the mask leaves, 19.6.5).
    if (kind == ForkKind::give_up) {
        leavers &= std::uint8_t(~alive);
        stayers = present & std::uint8_t(~leavers);
    } else if (kind == ForkKind::home) {
    } else if (!(stayers & alive)) {
        leavers |= stayers;
        stayers = 0;
    } else if (!(leavers & alive)) {
        stayers |= leavers;
        leavers = 0;
    } else {
        stayers |= leavers & std::uint8_t(~alive);
        leavers &= alive;
    }
    if (!leavers) return false; // nobody continues the run
    if (!stayers) return true;  // everyone does: the original group move, no fork
    const std::uint8_t kept = kind == ForkKind::in_stage ? leavers : stayers;
    bool take = kept == leavers;
    bool decided = false;
    // The recorded side: named by the continuing side or by the side that
    // leaves the instance id (the relay's fork lists).
    const std::uint8_t child = kept == leavers ? stayers : leavers;
    // ForkSide::frame is the frame after the fork tick's step; a give-up, a
    // HOME and a GO fork between two steps (online_intent / online_home /
    // online_go before the step), one frame earlier than a fork inside the
    // step.
    const std::uint64_t at = kind == ForkKind::give_up || kind == ForkKind::home || kind == ForkKind::go ? frame_ + 1 : frame_;
    for (const auto& side : fork_sides_)
        if (side.frame == at && (side.leavers == leavers || side.leavers == child)) {
            take = side.leavers == leavers ? side.take_leavers : !side.take_leavers;
            decided = true;
            break;
        }
    if (!decided && split_identity_ >= 0 && split_identity_ < 4 && (present & bit(split_identity_)))
        take = (leavers & bit(split_identity_)) != 0;
    // This copy's side from now on; the other side's players are despawned
    // at the end of the step (finish_online_step).
    // (A second fork in the same step forks the side this copy took; the
    // despawns of both happen at the end of the step.)
    const std::uint8_t earlier = pending_fork_ ? pending_fork_->other : std::uint8_t(0);
    pending_fork_ = PendingFork{leavers, kept, std::uint8_t((take ? stayers : leavers) | earlier), child, kind == ForkKind::in_stage, take};
    online_.present = take ? leavers : stayers;
    return take;
}

void Engine::set_barrier(const BarrierResult& barrier) { barrier_ = std::make_unique<BarrierResult>(barrier); }

bool Engine::redirect_pending_stage(std::string stage, int point) {
    // (M2, measured r71: anywhere from BARRIER_OK to the top of apply_barrier
    // this equals a NEXTSTAGE to that stage and point, bit for bit, solo and
    // for a parked guest of a group; the point is part of it. The target is
    // taken as named: the zone remap belongs to NEXTSTAGE.)
    if (!online_.enabled || pending_stage_.empty() || !barrier_ || barrier_->decision != Decision::dissolve || stage.empty()) return false;
    request_stage(std::move(stage));
    if (auto* points = get<PlayerPointManager>()) points->set_point_no(point);
    return true;
}

void Engine::apply_barrier() {
    if (!barrier_) return;
    const BarrierResult b = std::move(*barrier_);
    barrier_.reset();
    auto* sys = get<GameSystem>();
    if (!sys) return;
    // The room story first (8.5, 8.6): its merge into the engine's story and
    // the removal of the values the server dropped while the room owner was
    // away. Every member of the group applies the same bytes in this load
    // step, before the stage's load scripts read the flags and before the
    // snapshot, so a joiner gets the same state. (JOIN and CO_ARRIVE merge it
    // into the member's own fallback load.)
    // (pending_stage_ holds the barrier's target: a barrier into the training
    // field leaves flag 503 alone, 8.8.)
    // Round I1a (19.8.3, 19.30.4): the return home and a guest's NEW replace
    // the story subset instead (the own story, the host's), here at the top,
    // before the stage's scripts and the snapshot read it.
    if (b.story) {
        if (b.story_mode == StoryMode::replace) story::replace(flags_, sys->flag_folder, counters_, *b.story);
        else story::apply(flags_, sys->flag_folder, counters_, *b.story, b.dropped, pending_stage_);
    }
    auto rules = b.rules;
    rules.enabled = true;
    rules.leaving = 0;
    rules.boss_lock = false;
    switch (b.decision) {
    case Decision::dissolve: {
        const int own = std::clamp(b.own_slot, 0, 3);
        const PersonalSession personal = personal_session(own);
        for (int s = 0; s < 4; ++s)
            if (s != own) {
                sys->players[std::size_t(s)] = GameSystem::PlayerSlot{};
                sys->bit_chips[std::size_t(s)] = {};
            }
        if (own != 0) {
            sys->players[0] = std::move(sys->players[std::size_t(own)]);
            sys->players[std::size_t(own)] = GameSystem::PlayerSlot{};
            sys->bit_chips[0] = std::move(sys->bit_chips[std::size_t(own)]);
            sys->bit_chips[std::size_t(own)] = {};
        }
        sys->players[0].joined = true;
        // The member's own personal values come back (3.7).
        sys->gate_stage_name = personal.gate_stage_name;
        sys->gate_stage_no = personal.gate_stage_no;
        for (const auto flag : session_value_tables().personal_flags) {
            if (const auto found = personal.flags.find(flag); found != personal.flags.end()) flags_[flag] = found->second;
            else flags_.erase(flag);
        }
        personal_ = {};
        split_identity_ = 0;
        rules.present = 1;
        break;
    }
    case Decision::new_instance: {
        std::array<GameSystem::PlayerSlot, 4> players{};
        std::array<GameSystem::BitChip, 4> chips{};
        std::array<std::optional<PersonalSession>, 4> personal{};
        for (int s = 0; s < 4; ++s) {
            const int to = b.slot_map[std::size_t(s)];
            if (to < 0 || to > 3 || !sys->players[std::size_t(s)].joined) continue;
            players[std::size_t(to)] = std::move(sys->players[std::size_t(s)]);
            chips[std::size_t(to)] = std::move(sys->bit_chips[std::size_t(s)]);
            personal[std::size_t(to)] = personal_[std::size_t(s)];
        }
        for (const auto& [slot, carry] : b.co_arrivals) {
            if (slot < 0 || slot > 3) continue;
            players[std::size_t(slot)] = carry.slot;
            players[std::size_t(slot)].joined = true;
            chips[std::size_t(slot)] = carry.chip;
            personal[std::size_t(slot)] = carry.personal;
        }
        // 3.6: slot 0 is occupied at a stage load.
        if (!players[0].joined)
            for (int s = 1; s < 4; ++s)
                if (players[std::size_t(s)].joined) {
                    std::swap(players[0], players[std::size_t(s)]);
                    std::swap(chips[0], chips[std::size_t(s)]);
                    std::swap(personal[0], personal[std::size_t(s)]);
                    break;
                }
        sys->players = std::move(players);
        sys->bit_chips = std::move(chips);
        personal_ = std::move(personal);
        split_identity_ = std::clamp(b.own_slot, 0, 3);
        rules.present = 0;
        for (int s = 0; s < 4; ++s)
            if (sys->players[std::size_t(s)].joined) rules.present |= bit(s);
        break;
    }
    case Decision::join:
    case Decision::co_arrive:
        // The client drops this engine after the load step.
        return;
    }
    // 4.4 / 4.6: a new visit starts with the shared camera and the leash
    // when it has one member, with its own cameras and no leash otherwise.
    rules.player_cameras = 0;
    rules.leash_off = false;
    online_ = rules;
    // (The stage's boxes were cleared with the module reset: every member's
    // box camera starts with SETBOXCAMERA's first run.)
    membership_changed(OnlineRules{});
    if (std::popcount(unsigned(online_.present)) < 2) personal_ = {};
}

void Engine::apply_room_story(const RoomStory& story) {
    flags_.clear();
    for (std::int32_t i = 0; i < std::int32_t(story.flags.size()); ++i)
        if (story.flags[std::size_t(i)]) flags_[i] = story.flags[std::size_t(i)];
    counters_.clear();
    for (std::int32_t i = 0; i < std::int32_t(story.counters.size()); ++i)
        if (story.counters[std::size_t(i)]) counters_[i] = story.counters[std::size_t(i)];
    if (auto* sys = get<GameSystem>()) {
        sys->flag_folder.clear();
        for (int i = 0; i < int(story.folder.size()); ++i)
            if (story.folder[std::size_t(i)]) sys->flag_folder[i] = story.folder[std::size_t(i)];
    }
}

// ---------------------------------------------------------------------------
// Bytes, replays, fingerprints
// ---------------------------------------------------------------------------
std::vector<std::uint8_t> Engine::snapshot_bytes(SnapshotForm form) const {
    if (!snapshot_) return {};
    return encode_snapshot(*snapshot_, form);
}

void Engine::set_replay(bool on) {
    replay_ = on;
    if (on) set_port_events(false);
}

std::uint64_t Engine::net_fingerprint() {
    Fnv h;
    h.value(fingerprint());
    {
        std::ostringstream random;
        random << fight::generator();
        h.text(random.str());
    }
    if (const auto* models = get<ModelCtrlManager>())
        for (const auto& ram : models->rams()) {
            if (!ram->generated || !ram->node) continue;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) h.value(ram->node->matrix(r, c));
        }
    if (const auto* sound = get<SoundManager>()) h.value(sound->random_state());
    h.value(online_.enabled);
    h.value(online_.player_cameras);
    h.value(online_.leash_off);
    h.value(online_.scoped_scenes);
    h.value(online_.near_share);
    h.value(online_.present);
    h.value(online_.leaving);
    h.value(online_.boss_lock);
    for (const auto& personal : personal_) {
        h.value(personal.has_value());
        if (!personal) continue;
        h.value(personal->gate_stage_name.has_value());
        if (personal->gate_stage_name) h.text(*personal->gate_stage_name);
        h.value(personal->gate_stage_no);
        for (const auto& [flag, value] : personal->flags) {
            h.value(flag);
            h.value(value);
        }
    }
    h.value(net_fingerprint_cameras(*this));
    h.value(session_context_hash());
    // The members' own windows and scenes (5.3): their programs, pad and
    // layer switches, the copies made and the scoped demo works.
    if (const auto* windows = get<SubWindowManager>()) h.value(windows->own_windows_hash());
    h.value(scoped_pads_off_);
    h.value(own_window_hold_);
    h.value(own_scene_hold_);
    for (const auto& layers : scoped_layers_off_) {
        h.value(std::uint32_t(layers.size()));
        for (const auto& layer : layers) h.text(layer);
    }
    h.value(std::uint32_t(copies_made_.size()));
    h.value(std::uint32_t(demo_.running_unscoped()));
    h.value(std::uint32_t(demo_.running()));
    return h.h;
}

} // namespace dw4::engine
