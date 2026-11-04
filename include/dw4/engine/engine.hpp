#pragma once

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/online.hpp"
#include "dw4/engine/port_events.hpp"
#include "dw4/engine/scene_graph.hpp"
#include "dw4/card_store.hpp"
#include "dw4/console_reset.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/lua4.hpp"
#include "dw4/packages.hpp"
#include "dw4/pad.hpp"
#include "dw4/resources.hpp"

#include <array>
#include <span>
#include <random>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dw4::engine {

class Engine;
class Manager;
// online_membership.hpp
struct PlayerCarry;
struct BarrierResult;
struct RoomStory;

// The messenger's delay unit: one nominal frame is 16,666,666 ns (802ECA74 /
// 803061BC). Delays and frame counts in the data use this constant.
constexpr std::int64_t kFrameNs = 16666666;

// The world clock is not a frame counter: igInsightCore 8028AF84 adds the
// sampled OSGetTime delta (timer 8007734C, ticks * 8000 / 324) every frame,
// and the game runs one frame per NTSC field. A field is 675,675 timebase
// ticks = 1001/60000 s, so the clock advances 16,683,333 or 16,683,334 ns
// (50,050,000 ns every three fields), and an n-frame WAIT ends after exactly
// n fields.
constexpr std::int64_t kFieldNsPer3 = 50050000;
constexpr double kFieldSeconds = 1001.0 / 60000.0;
// World time after `frame` fields.
constexpr std::int64_t field_time(std::uint64_t frame) {
    return static_cast<std::int64_t>(frame / 3) * kFieldNsPer3 + static_cast<std::int64_t>(frame % 3) * kFieldNsPer3 / 3;
}

// Shared messenger argument/result buffer (typed lists, as 80306xxx).
struct Packet {
    std::vector<std::uint8_t> bools;
    std::vector<std::int32_t> ints;
    std::vector<float> floats;
    std::vector<std::int64_t> longs;
    std::vector<Vec3f> vectors;
    std::vector<std::optional<std::string>> strings;
    std::vector<Mat4> matrices;
    std::vector<void*> objects;
    void clear() { *this = Packet{}; }
    std::string_view string(std::size_t index) const {
        return index < strings.size() && strings[index] ? std::string_view(*strings[index]) : std::string_view{};
    }
    bool has_string(std::size_t index) const { return index < strings.size() && strings[index].has_value(); }
    std::int32_t integer(std::size_t index, std::int32_t fallback = 0) const { return index < ints.size() ? ints[index] : fallback; }
    float real(std::size_t index, float fallback = 0) const { return index < floats.size() ? floats[index] : fallback; }
    bool boolean(std::size_t index) const { return index < bools.size() && bools[index] != 0; }
    bool has_results() const { return !bools.empty() || !ints.empty() || !floats.empty() || !longs.empty(); }
};

// Parse the original compact string argument form: '/' separates entries;
// "F<x>", "I<n>", "L<n>", "V< x , y , z >" are typed, everything else a string.
void parse_packet_arguments(std::string_view text, Packet& packet);

// Base runtime record for one serialized beBaseInfoRam occurrence.
struct Ram {
    Manager* manager{};
    ObjectView info;          // owning be*Info
    ObjectView ram;           // serialized RAM record
    std::string info_name;
    std::int32_t index{};
    bool active{};
    virtual ~Ram() = default;
};

struct MessengerRow {
    std::string category;
    std::optional<std::string> name;
    std::int32_t index{};
    Ram* recipient{};
};

// beMessenger's message objects: +0x10 the current one, which every select
// (80305344) and post preparation (80305F9C) resets and fills and every
// dispatch (80305A28) answers into, and one in each deferred record (+0x28).
// 80303C0C makes them at the first select or post of a stage (made flag
// +0x30): the current object and 32 records with their objects. A post's
// commit (803061BC, 0x803062A0..0x8030632C) swaps the current object with the
// record's: the record takes the arguments, the record's old object becomes
// current. The delivery pass (80304C58) selects (resetting the current
// object's counts), swaps the delivered object in for the dispatch
// (0x80304DCC..0x80304E58) and never swaps back: the record (80304A04 resets
// its counts) goes to the end of the record list with the previous current
// object, and the next post takes the last record. Each pass ends with
// 80304618 on the current object; a stage change releases them all (803040C0).
// A select only resets the counts and the shrink to four entries keeps the
// data (8004155C), so the first bool entry an unchecked read sees (80306A1C
// without 8030690C: the soft reset 0x80347DA4 / 0x80347E54) is the one the
// last bool written at index 0 of the current object left - possibly by
// another message. Only the bool counts and that entry are modelled.
class MessageObjects {
public:
    void clear();                        // 803040C0
    void select();                       // make (80303C0C), else 80304618 on the current object
    void append(std::uint8_t value);     // 80306964: data[count] = value, count + 1
    // 80305F9C + 803061BC after select() and the arguments: the record's
    // object (a new record's when none is idle) becomes current; returns the
    // object now holding the arguments.
    std::size_t commit();
    // 80304C58 for a due record holding `object`: select, swap; returns the
    // previous current object, which the record now holds.
    std::size_t deliver(std::size_t object);
    // 80304A04 and 0x80304EC8..0x80304EF0 after the dispatch: the record's
    // object counts reset, the record to the end of the list.
    void retire(std::size_t previous);
    void end_pass();                     // 0x80304F8C: 80304618 on the current object
    bool made() const { return made_; }
    bool holds(std::size_t object) const { return made_ && object < objects_.size(); }
    std::uint8_t first_bool() const { return made_ ? objects_[current_].first_bool : 0; }
    struct State {
        std::size_t current = 0;
        std::int32_t bools = 0;
        std::uint8_t first_bool = 0;
    };
    State state() const { return made_ ? State{current_, objects_[current_].bools, objects_[current_].first_bool} : State{}; }
    State state(std::size_t object) const { return holds(object) ? State{object, objects_[object].bools, objects_[object].first_bool} : State{}; }
    std::size_t objects() const { return objects_.size(); }

    static constexpr std::size_t kRecords = 32;

private:
    struct Object {
        std::int32_t bools = 0;          // the bool list's count (+0x10 +8)
        std::uint8_t first_bool = 0;     // its data[0]
    };
    std::vector<Object> objects_;
    std::size_t current_ = 0;
    std::vector<std::size_t> idle_;      // the idle records' objects in record list order
    bool made_ = false;
};

struct DeferredMessage {
    std::int64_t time{};
    std::string category;
    std::optional<std::string> name;
    std::int32_t index{};
    std::string command;
    Packet arguments;
    // The message object the record holds (a MessageObjects index: the one
    // the post filled, swapped into the record at the commit); kPortOnly for
    // a message posted inside a Messenger::PortOnly scope, which has no
    // record of the original.
    static constexpr std::size_t kPortOnly = static_cast<std::size_t>(-1);
    std::size_t object = kPortOnly;
    // Online co-op: the trigger slot of the script run that posted it
    // (Engine::trigger_slot; read only online).
    int trigger = -1;
    // Online co-op: the member whose own window or scene posted it
    // (Engine::scene_scope, docs/SEAMLESS.md 5.3; -1 none, always offline).
    int scope = -1;
};

class Messenger {
public:
    void add_row(std::string category, std::optional<std::string> name, std::int32_t index, Ram* recipient);
    void remove_rows(Manager* manager);
    void clear();
    // Select + send. Results are left in the packet.
    std::size_t send(Engine& engine, std::string_view category, std::optional<std::string_view> name,
                     std::int32_t index, std::string_view command, Packet& packet);
    // Files a message for delivery at `time` (803061BC: behind the messages
    // of the same or an earlier time). A time that is already due still
    // waits for the next delivery pass (update).
    void post(std::int64_t time, std::string category, std::optional<std::string> name, std::int32_t index,
              std::string command, Packet arguments);
    // The delivery pass of the frame (80304C58), after the camera viewer and
    // before the demo and the managers.
    void update(Engine& engine);
    // 80304FA8: holds the posted messages while on (SUBWND programs, the soft
    // reset); their times move on with the world clock until it is off.
    void pause(const Engine& engine, bool on);
    std::size_t count(std::string_view category, std::optional<std::string_view> name, std::int32_t index) const;
    const std::vector<MessengerRow>& rows() const { return rows_; }
    void clear_deferred() { deferred_.clear(); }
    // The first entry of the current message object's bool list as 80306A1C
    // reads it, without the count check 8030690C (the soft reset 0x80347DA4 /
    // 0x80347E54): a select only resets the counts (80304618) and the shrink
    // to four entries keeps the data (8004155C), so after a send or a
    // delivery without a bool the entry is the one an earlier message left in
    // that object - see MessageObjects. 0 before the first bool of a stage.
    std::uint8_t stale_bool() const { return objects_.first_bool(); }
    // A send of the port alone (the campaign bot's queries, input-script
    // SEND lines) has no counterpart in the original: while a scope is open
    // the message objects keep their state.
    class PortOnly {
    public:
        explicit PortOnly(Messenger& messenger) : messenger_(messenger) { ++messenger_.port_only_; }
        ~PortOnly() { --messenger_.port_only_; }
        PortOnly(const PortOnly&) = delete;
        PortOnly& operator=(const PortOnly&) = delete;

    private:
        Messenger& messenger_;
    };
    const MessageObjects& message_objects() const { return objects_; }
    // The trigger slot posts take (Engine::TriggerScope).
    void set_trigger(int slot) { trigger_ = slot; }
    // The scene scope posts take (Engine::SceneScope).
    void set_scope(int slot) { scope_ = slot; }

private:
    int trigger_ = -1;
    int scope_ = -1;
    bool modelling() const { return port_only_ == 0; }
    std::size_t dispatch(Engine& engine, std::string_view category, std::optional<std::string_view> name,
                         std::int32_t index, std::string_view command, Packet& packet, bool model);
    void end_pass(const Engine& engine);
    MessageObjects objects_;
    int port_only_ = 0;
    // 80303C0C makes 32 deferred records; preparing a post grows the pool by
    // one record while all are in use. A delivery pass covers at most the
    // records the pool had when it began. Port-only messages (no record)
    // neither count as records in use nor spend a pass's deliveries.
    static constexpr std::size_t kDeferredRecords = 32;
    std::vector<MessengerRow> rows_;
    std::vector<DeferredMessage> deferred_;
    std::size_t deferred_records_ = kDeferredRecords;
    // beMessenger +0x28: the world time of the pause or of the last paused
    // pass; 0 = running (also when paused at time 0, as the original).
    std::int64_t paused_at_ = 0;
    // The rows of each category, all and by name, as row numbers in row
    // order (selection reads only these; rebuilt after the rows change).
    struct TextHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
    };
    struct CategoryRows {
        std::vector<std::uint32_t> all, unnamed;
        std::unordered_map<std::string, std::vector<std::uint32_t>, TextHash, std::equal_to<>> named;
    };
    mutable std::unordered_map<std::string, CategoryRows, TextHash, std::equal_to<>> by_category_;
    mutable bool by_category_stale_ = true;
    // DW4_TRACE_NO_RECIPIENT: the (category, command, name) of the stage's
    // messages that found no row, each logged once per stage (diagnostics
    // only, not part of the simulated state).
    std::set<std::string> traced_no_recipient_;
    // Calls `visit(row)` for the rows of `category` that a selection by
    // `name` (any when empty) can take, in row order, until it returns false.
    template <class Visit>
    void candidates(std::string_view category, std::optional<std::string_view> name, Visit&& visit) const;
};

// beDemoManager record types (802F7580 / 802F7EC0).
struct DemoRecord {
    int type{};                 // 0 set, 1 wait-while-false, 2 wait-while-true, 3 switch, 4 case, 5 end, 7 lua
                                // port only (seamless co-op, never in an offline game): 8 wait while `port`
                                // returns nonzero, 9 switch on `port`'s value
    std::string category;
    std::optional<std::string> name;
    std::int32_t index{};
    std::string command;
    std::optional<std::string> argument;
    std::int64_t delay_frames{};
    std::int32_t case_value{};
    std::function<int(Engine&)> port;
};
struct DemoWork {
    std::vector<DemoRecord> records;
    std::size_t cursor{};
    int trigger = -1; // the trigger slot of the script run that started it (Engine::trigger_slot)
    int scope = -1;   // online: the member whose own scene started it (Engine::scene_scope, -1 none)
};

class DemoManager {
public:
    void add(DemoRecord record);
    void begin_switch(DemoRecord record);
    void add_case(std::int32_t value);
    void end_switch();
    void run();
    // A work built elsewhere starts at once (the seamless co-op exit port;
    // a script's unfinished work stays as it is). It runs from the next
    // update on.
    void start(std::unique_ptr<DemoWork> work) { running_.push_back(std::move(work)); }
    void update(Engine& engine);
    void clear();
    std::size_t running() const { return running_.size(); }
    // Online (docs/SEAMLESS.md 5.3): the works that are no member's own
    // scene, and those plus the own scenes of `slot` (what DMgetCount counts
    // for a script run of that trigger; -1: the unscoped works alone).
    // Offline every work is unscoped: both equal running().
    std::size_t running_unscoped() const;
    std::size_t running_for(int slot) const;
    // The own scenes of `slot` end at once (the member left the instance).
    void drop_scope(int slot);
    std::vector<std::string> describe() const;
    // The trigger slot new works take (Engine::TriggerScope).
    void set_trigger(int slot) { trigger_ = slot; }
    // The scene scope new works take (Engine::SceneScope).
    void set_scope(int slot) { scope_ = slot; }

private:
    int trigger_ = -1;
    int scope_ = -1;
    std::unique_ptr<DemoWork> current_;
    bool in_switch_{};
    std::vector<std::unique_ptr<DemoWork>> running_;
};

// Host input for one controller slot: logical GDJEB2 buttons (dw4::pad_button)
// and sticks in the original normalized range. The engine runs the recovered
// bePadData logic (dw4::OriginalPads) once per simulation frame.
using PadSample = dw4::PadSample;
using PadState = dw4::PadState;

struct LoadedFile {
    std::string logical_path;
    std::shared_ptr<const IgbDocument> document;
    bool stage_member{};
};

struct DiagnosticCounter {
    std::string text;
    std::size_t count{};
};

// Model-script context (8030B6EC): the model a hit/LUACALL script runs for.
// Context-free MDL* methods and BLT* bullet queries read it.
struct ModelScriptContext {
    std::string category = "MODELCTRL";
    std::string name;
    std::int32_t index = -1;
    std::int32_t shooter = -1;     // BLTgetShootRamNo
    std::string shooter_name;
    std::int32_t bullet_kind{};
    bool turn = true;              // cleared by MDLsetDMGTurn(0)
    std::string call_name;         // MDLisCallName
    std::int32_t bullet_box{};     // MDLgetBulletBoxNo: the body box a bullet reached (80535934)
    Vec3f* world_target{};         // MDLsetWorldPosTarget's destination (AIMAP WPOS, 8053594C)
    // Last FIGHTCALC results of this hit (80535A54..5C): damage, attribute, flag.
    std::int32_t damage{}, attribute = -1, damage_flag{};
};

// What is in a memory card slot (Engine::card_setting). The card keeps its
// files where Engine::card_store says (save::CardStore): a GCI folder
// (<card root>/Card A|B) as Dolphin's GCI folder cards, or the single save
// file of the slot. Its free blocks are the card's blocks minus the block
// counts of the card's files (GCI header +0x38), its free files 127 minus
// their number.
struct CardSetting {
    enum class Mode : std::uint8_t {
        automatic,   // slot A holds a card, slot B when its folder (or save file) exists
        none,        // an empty slot (CARDProbeEx: no card)
        inserted,    // a card, also without a folder
        unformatted, // a card without a file system (CARDMount: broken)
    };
    Mode mode = Mode::automatic;
    std::uint32_t blocks = 2043; // user blocks (59, 123, 251, 507, 1019 or Dolphin's 2043)
};
// "auto", "none", "inserted", "unformatted" and/or a block count, comma
// separated ("unformatted,251"); DW4_CARD_A / DW4_CARD_B, settings.ini
// [data] card_a / card_b. Nothing for an unknown word.
std::optional<CardSetting> parse_card_setting(std::string_view text);

// Optional PC cheats (F1 menu); all off is the original game.
struct Cheats {
    bool god{};         // players lose no HP (also DW4_GOD for test runs)
    bool infinite_mp{}; // techniques cost no MP
    bool one_hit{};     // a damaging player hit defeats the target
    // Optional rebalancing and randomizer (settings.ini [gameplay]).
    int exp_rate = 100;          // percent of the EXP enemies give
    int damage_dealt = 100;      // percent of the damage player hits do
    int damage_taken = 100;      // percent of the damage enemies do
    std::uint32_t drop_seed = 0; // item drop randomizer, 0 off
    // PC loading (settings.ini [gameplay] loading): off is the original's
    // load timing (the simulated disc drive and loading thread), on takes
    // none of it (Engine::fast_loading). A stage load latches it.
    bool fast_loading{};
    // Randomizer (settings.ini [randomizer], bound to the save; see
    // dw4/engine/randomizer.hpp): the master seed the enemy and loading zone
    // shuffles derive their seeds from (the item drops use it unchanged as
    // drop_seed), 0 off; the enemy shuffle (0 off, 1 every enemy of a wave
    // drawn on its own, 2 one family per wave) and the loading zone shuffle
    // (0 off, 1 dungeon entrances within an area).
    std::uint32_t rand_seed = 0;
    std::uint8_t rand_enemies = 0;
    std::uint8_t rand_zones = 0;
    bool enemy_shuffle() const { return rand_seed != 0 && rand_enemies != 0; }
    bool zone_shuffle() const { return rand_seed != 0 && rand_zones != 0; }
    bool original() const {
        return !god && !infinite_mp && !one_hit && exp_rate == 100 && damage_dealt == 100 && damage_taken == 100 && !drop_seed &&
               !fast_loading && !enemy_shuffle() && !zone_shuffle();
    }
};

// The state that survives a stage load, taken when one starts (everything
// else is rebuilt by the load). With the controller input of every step since
// then it reproduces the session exactly: save states restore it into a new
// engine and replay the steps.
struct StageSnapshot {
    std::string stage;
    std::int64_t time{};
    std::int64_t clock_lateness{};       // the lateness in that update's world clock sample (Engine::set_clock_lateness)
    std::uint64_t frame{};
    std::map<std::int32_t, std::uint8_t> flags;
    std::map<std::int32_t, std::int32_t> counters;
    bool pad_input_enabled{};
    Cheats cheats;
    OriginalPads pads;
    std::string random;                  // system generator state (fight::generator)
    std::int32_t point_no{};             // PLAYERPOINT's generate point (it survives stage loads)
    std::int32_t loader_teardown{};      // milli-fields the module reload before this load took (Engine::step, loader_teardown_cost)
    bool loader_common{};                // this load still reads BecText.igb and _common/sbiallse.igb (the first of a session)
    bool stage_active{};                 // beSystem +0x2C at the change (cleared by the loader's state 0)
    std::shared_ptr<const void> system;  // GameSystem copy
    std::shared_ptr<const void> sound;        // SoundManager streams
    std::uint64_t serial{};                   // counts the snapshots of an engine
    ConsoleReset console_reset;               // the reset button and the controller's combination
    // Online co-op (online.hpp): the rules of the visit and the present
    // slots' personal session values (all off / empty offline).
    OnlineRules online;
    std::array<std::optional<PersonalSession>, 4> personal;
};

// Save state files: a snapshot as bytes and back (throws on damaged or
// foreign data). The canonical form zeroes the presentation-only fields
// (SnapshotForm); it is for hashes and comparisons and is not meant to be
// decoded into a running engine.
std::vector<std::uint8_t> encode_snapshot(const StageSnapshot& snapshot, SnapshotForm form = SnapshotForm::raw);
StageSnapshot decode_snapshot(std::span<const std::uint8_t> bytes);
// The snapshot layout version (kSnapshotVersion; protocols follow it).
std::uint32_t snapshot_version();

class Engine {
public:
    explicit Engine(const std::filesystem::path& data_root);
    // An engine on resources already mounted (the tests: empty ones).
    explicit Engine(MountedResources resources);
    // The snapshot of the running stage's load (null before the first).
    const StageSnapshot* stage_snapshot() const { return snapshot_ ? &*snapshot_ : nullptr; }
    // On a new engine: the state of the snapshot, then the rest of the step
    // that started that stage load.
    void restore(const StageSnapshot& snapshot);
    // A hash of the simulated state (frame, stage, flags, players, models)
    // to check that a replay arrived where the session was.
    std::uint64_t fingerprint();
    Cheats& cheats() { return cheats_; }
    const Cheats& cheats() const { return cheats_; }
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    // One deterministic engine per process (6.12): the system generator is a
    // process-wide static that every construction reseeds, so an engine
    // constructed while an online engine runs would change that engine's
    // stream. Constructing an engine while an online one lives, or turning
    // an engine online while another lives, throws std::logic_error.
    static int live_engines();

    // ---- Seamless online co-op (docs/SEAMLESS.md 7; online.hpp,
    // online_membership.hpp). Nothing below acts while online().enabled is
    // off and the camera mask is 0.
    const OnlineRules& online() const { return online_; }
    // Only between steps, at a tick every member applies. Turning online on
    // fixes the cheats to the room's (online_cheats), switches the console
    // reset off and records the present slots' personal session values.
    void set_online(const OnlineRules& rules);
    // The slots with their own box camera (OnlineRules::player_cameras); 0:
    // the one shared camera and the original single traversal.
    std::uint8_t player_cameras() const { return online_.player_cameras; }
    // A multi-member instance: online with two or more present members (the
    // exit port, the gate disc fork and the draw filters act only then).
    bool online_shared() const;
    // Presentation only: whose camera this machine's picture uses (the local
    // member's slot, 0..3). picture_slot() is the camera render() draws: the
    // view slot, or while its player is defeated the nearest living member's
    // (4.1); the view slot itself without per-slot cameras.
    // (It also tells the sound output whose own windows' sound effects this
    // machine plays, SEAMLESS.md 5.3.)
    void set_view_slot(int slot);
    int view_slot() const { return view_slot_; }
    int picture_slot() const;
    // Replays (catch-up, resync, resume): the side to take at each fork.
    void set_fork_sides(std::span<const ForkSide> sides) { fork_sides_.assign(sides.begin(), sides.end()); }
    // Live play: this machine's slot, which decides its side at a fork
    // (NETsplit); -1 = none (the side that keeps the instance id).
    void set_split_identity(int slot) { split_identity_ = slot; }
    int split_identity() const { return split_identity_; }
    // A NEXTSTAGE waits for its load step (read-only).
    std::optional<std::string_view> pending_stage() const {
        return pending_stage_.empty() ? std::nullopt : std::optional<std::string_view>(pending_stage_);
    }
    // 6.5: LEAVE and INTENT wait for a membership-safe step, JOIN for a
    // joinable one (pad input enabled, no demo record, no SUBWND program,
    // not loading, presenting, no fork pending; joinable adds: not a hub or
    // UI stage, no boss lock).
    bool membership_safe() const;
    bool joinable() const;
    // A hub or UI stage (stagML02, stagML11-14, stagIS*): solo instances.
    static bool hub_stage(std::string_view stage);
    // The session context (3.7): a hash of the context values (absent = 0).
    std::uint64_t session_context_hash() const;
    // The personal session values of a slot (online: the slot's own; the
    // only present member, or offline: the engine-wide values).
    PersonalSession personal_session(int slot) const;
    // A slot's player data for a join, a co-arrival or keeping aside.
    PlayerCarry carry_player(int slot) const;
    // Before a step (at the JOIN's tick, when joinable()): the member enters
    // the running stage at `entrance_point`'s point group (any species: its
    // packages load now), with SQ_ENTR, its HUD panels and its capsule.
    void online_join_player(int slot, const PlayerCarry& carry, int entrance_point);
    // Before a step (LEAVE, when membership_safe()): the slot's player, HUD
    // panels, quick panel, item label, capsule and weapon go; joined off.
    void online_remove_player(int slot);
    // Before a step (a LEAVE that waits for a membership-safe step, 6.5):
    // the slot plays the neutral, connected pad and its prompts and windows
    // take the leader's pad until the LEAVE applies.
    void online_mark_leaving(int slot);
    // Before a step: a port choice of a present slot (round S3 uses
    // go_without; give_up forks the defeated member off alone).
    void online_intent(int slot, IntentKind kind, int argument);
    // Inside a step (the online overrides, Lua NETsplit): a fork of the
    // present members; `mask` is the side that continues the script run
    // (ForkKind). The side rule for defeated members applies. Returns whether
    // this copy is on that side. The other side's players are despawned at
    // the end of the step; take_split() reports it once.
    bool online_split(std::uint8_t mask, ForkKind kind);
    std::optional<SplitResult> take_split();
    // An exit the local copy takes (the funcStageChange port or the gate
    // disc, 5.2), once, for the client's HEADING.
    std::optional<ExitHeading> take_heading();
    // A demo record posted STAGECTL NEXTSTAGE `delay` frames ahead (the
    // lobby gate's menu, a solo instance's funcStageChange): online, in a
    // one-member instance, this announces the departure as a heading (3.4:
    // members who take the same gate within seconds arrive together).
    // Observe-only (nothing of it is in the fingerprint or the snapshot).
    void note_departure(std::string_view stage, int point, std::int64_t delay);
    // The exit port's state for this machine's texts and draw filter.
    const ExitPromptView& exit_prompt() const { return exit_view_; }
    // The subtrees this machine's picture leaves out in a multi-member
    // instance (4.3, 5.2): other members' item name lines and exit prompts.
    // Presentation only; render()'s picture pass skips them.
    void picture_hidden_roots(std::vector<const Node*>& out);
    // While parked after the barrier tick: BARRIER_OK, applied in the load
    // step right before begin_stage_load.
    void set_barrier(const BarrierResult& barrier);
    bool barrier_set() const { return barrier_ != nullptr; }
    // At game_started only (barriers go through set_barrier): FLAG,
    // FLAGFOLDER and COUNTER become the room story's.
    void apply_room_story(const RoomStory& story);
    // The running stage's load snapshot as bytes (empty before the first).
    std::vector<std::uint8_t> snapshot_bytes(SnapshotForm form) const;
    // Catch-up replays: no port events (the host renders no audio).
    void set_replay(bool on);
    bool replay() const { return replay_; }
    // The net fingerprint (6.9): fingerprint() plus the generator state,
    // the models' rotations, the sound manager's random value, the online
    // rules, the personal values, the cameras' state and the session context.
    std::uint64_t net_fingerprint();
    // boss_lock (6.5): set by boss_mat / boss_intro runs, cleared by
    // boss_end runs and at every barrier (round S3 wires the scripts).
    void set_boss_lock(bool on) { online_.boss_lock = on; }
    // The lowest present slot (0 offline).
    int leader() const;
    // SYSsetGateStage ran: the present slots' personal GATESTAGE follows the
    // engine-wide one (online; 3.7).
    void online_gate_stage_set();
    // The trigger slot of the running script (5.1): the member that stepped
    // on a mat, the player that shot a hit, -1 none. Demo works keep the
    // trigger of the script run that started them.
    int trigger_slot() const { return trigger_slot_; }
    class TriggerScope {
    public:
        TriggerScope(Engine& engine, int slot) : engine_(engine), saved_(engine.trigger_slot_) { engine.set_trigger(slot); }
        ~TriggerScope() { engine_.set_trigger(saved_); }
        TriggerScope(const TriggerScope&) = delete;
        TriggerScope& operator=(const TriggerScope&) = delete;

    private:
        Engine& engine_;
        int saved_;
    };
    // 7.4: the pad a reader of RAM index `index` gets (SelectCtrl windows,
    // the film skip): offline and in a one-member instance `index` itself;
    // online, index 0 of a window with a present trigger reads the trigger's
    // pad, an absent or leaving index or one without a trigger the leader's.
    int pad_reader_slot(int index, int trigger) const;

    // ---- Own windows and scenes online (docs/SEAMLESS.md 5.3, round 63;
    // online_scenes.cpp). Online in a stage that is not a hub or UI stage, a
    // member's status window (START) and the scenes of the class local_scene
    // (NPC talks and map recorders, the hit_n_* scripts) belong to that member
    // alone, also while it is alone in its instance. They run in the
    // simulation like everything else (every engine of the instance runs them
    // with that member's sealed pads), but they never pause the instance:
    // inside their scope the instance-wide commands are narrowed to the
    // member's slot (PLAYER -1 OFCTRL / ONCTRL, STAGECTL PADINPUT, LAYERCTL)
    // or left out (ENEMY -1 OFCTRL / ONCTRL, GENERATER and category freezes,
    // the quest timer's stop), and their window models, cursors and text lines
    // are the member's own copies, which only its machine draws and whose
    // sound effects only it hears. Offline, in a hub stage and in the replay
    // of an offline save state nothing of it acts.
    bool scenes_scoped() const;
    // The slot whose own window or scene runs right now (-1: none).
    int scene_scope() const { return scene_scope_; }
    class SceneScope {
    public:
        // `slot` -1 leaves the scope (an unscoped delivery inside a scoped one).
        SceneScope(Engine& engine, int slot);
        ~SceneScope();
        SceneScope(const SceneScope&) = delete;
        SceneScope& operator=(const SceneScope&) = delete;

    private:
        Engine& engine_;
        int saved_scope_;
        int saved_trigger_;
    };
    // The messenger's hook for a message inside a scene scope: rewrites the
    // target (a window model to the member's own copy, made on first use; the
    // index of PLAYER -1 to the member's slot) or keeps the message from the
    // instance (false). `storage` holds a rewritten name.
    bool scope_message(std::string_view category, std::optional<std::string_view>& name, std::int32_t& index,
                       std::string_view command, Packet& packet, std::string& storage);
    // The member's own copy of a window text line: TextCtlManager asks for it
    // inside a scope. `child` is the line switch's child that holds the line
    // node `line`; its copy becomes a new child of `parent` (hidden, `index`
    // its place), the line node's copy is returned (nullptr: none).
    Node* scene_copy_line(const Node* child, const Node* line, Node* parent, std::size_t& index);
    // A slot's own status window or own scene (presentation, the exit port's
    // busy rule, the name tags); busy_slots(): a bit per such slot.
    bool own_window_open(int slot) const;
    bool own_scene_running(int slot) const;
    std::uint8_t busy_slots() const;
    // The slot's own windows and scenes end at once (a LEAVE, the other side
    // of a fork): the window closes without its close sequence, the scene's
    // works stop, its copies hide and its layer filter goes.
    void end_own_scenes(int slot);
    // A talk (a hit_n_* script) in the talking member's own scope. While
    // another member's own talk runs, the script's DMgetCount answers 1 (its
    // busy path): talks wait for each other, as the original's one talk at a
    // time; status windows and exits do not wait for them.
    void run_own_talk(int slot, const std::string& script);
    // STAGECTL PADINPUT inside a slot's scope: that slot alone reads the
    // neutral pad (pads()).
    std::uint8_t scoped_pads_off() const { return scoped_pads_off_; }
    // A member's player in its own window or scene stands without its input
    // (the window's or the talk's cursors read the pad) but keeps its control:
    // it can be hit, which the original's OFCTRL would prevent (hit_check
    // takes no hit while _isOnCtrl is clear). `window`: the status window's
    // hold, else the scene's (its PLAYER OFCTRL / ONCTRL); the player stands
    // while either holds. player_pad: the pad the player model reads.
    void hold_own_input(int slot, bool window, bool on);
    // The scene holds of the members without an own scene left (the demo
    // manager, after its works ran).
    void release_scene_holds();
    bool own_input_held(int slot) const {
        return slot >= 0 && slot < 4 && ((own_window_hold_ | own_scene_hold_) & (1u << slot)) != 0;
    }
    const PadState& player_pad(int slot) const {
        const int s = slot < 0 ? 0 : slot > 3 ? 3 : slot;
        return own_input_held(s) ? kFifthPads[std::size_t(s)] : pads()[std::size_t(s)];
    }
    // Simulation entries that found another MXCSR than 0x1F80 and repaired
    // it for their duration (diagnostics, 6.12).
    static std::uint64_t mxcsr_repairs();

    // One fixed 60 Hz simulation step.
    void step(const std::array<PadSample, 4>& samples);
    // Collect the draw list for the current state.
    // `shadow` (optional) receives the projected-shadow light and casters.
    void render(std::vector<DrawItem>& out, FrameCamera& camera, Vec4f& clear_color, ShadowFrame* shadow = nullptr);
    // The scene traversal of render(..., shadow) without the draw lists, for
    // pictures that are not shown (test runs, catch-up steps, save state
    // replays). The traversal advances animation clocks, motion combiners,
    // LOD choices and recorded matrices, which the simulation reads, so a
    // picture the original draws must be traversed either way.
    void traverse_scene();
    // Optional host inspection camera replacing the game camera (debug only).
    // It moves only the picture: the frustum cull and the view-dependent
    // choices of the traversal stay with the game camera.
    std::optional<FrameCamera> debug_camera;
    // The renderer's widescreen widening of perspective projections (the x
    // scale it applies, 1 for 4:3): render() draws the subtrees the
    // original's cull skips that lie inside the wider picture, without
    // their updates. Presentation only; simulation never reads it.
    float picture_widen = 1.0f;
    // Smooth motion: the previous step's picture camera view while the host
    // blends the picture between two steps (TraversalContext::
    // picture_previous_view). Presentation only, like picture_widen.
    std::optional<Mat4> picture_previous_view;
    // The PC's third title menu entry "Online" below Game Start and Option
    // (TITLE2, title.cpp): while the host sets it, the cursor goes down from
    // Option to it, and A there sets title_online_request for the host (which
    // opens its online screens over the menu; the menu stays). Off in every
    // new engine: test runs, replays and online sessions never turn it on,
    // and neither field is in a snapshot or the fingerprint, so a run without
    // it is the original.
    bool title_online_entry = false;
    bool title_online_request = false;
    // TITLE2 GAMESTART sets it (the title's Game Start, every time: Game
    // System::game_started stays set once a game ran, and the option menu's
    // reload sets that too); an online host applies the room's story and
    // modes on that step and clears it. Like the two above: not in a snapshot
    // or the fingerprint, read by nothing in the simulation.
    bool title_game_start = false;
    // Development measurement (DW4_TRACE_TRAVERSAL_TIME): the traversals'
    // accumulated CPU time in ns and their count: [0] the combined pass
    // (render, the original), [1] traverse_scene (the original), [2] the
    // union simulation pass, [3] the picture-only pass. Never read by the
    // simulation; counted only while that variable is set.
    std::array<std::uint64_t, 4> traversal_ns{}, traversal_count{};
    // Per-scene draw statistics for diagnostics.
    std::vector<std::string> scene_report();

    void request_stage(std::string name);
    // False after the step that took a stage change (NEXTSTAGE, 80347F2C)
    // and in the step that tears the stage down: the original presents no
    // new picture from then on, the screen keeps the last one until the
    // loader draws (Dolphin: the XFB of the teardown's frame still holds the
    // opening's last picture, after its end and after a skip; black from the
    // frame after it). A reboot presents black.
    bool presenting() const { return rebooting_ || (pending_stage_.empty() && !torn_down_); }
    // The console reboots (ConsoleReset): from the step beSystem saw the reset
    // button's release on, the picture is black and nothing runs; after
    // kRebootFields more steps the host starts a new engine on the boot stage
    // (the disc's apploader has loaded the game again).
    bool rebooting() const { return rebooting_; }
    bool reboot_due() const { return rebooting_ && reboot_fields_ >= kRebootFields; }
    // The steps the reboot stays black before the new engine starts: Dolphin
    // shows "Licensed by Nintendo" 258 fields after the latched release
    // (work/dref/r29reset d_reset3s: release at dframe ~5700, license
    // dframes 5958-6115, the memory card window from 6122, the cold boot's
    // pictures 78-235 / 242 shifted by 5880), the port's own start shows it
    // at its frame 77 (d_boot / p_boot): the reset, the apploader loading the
    // game from the disc again and the OS start before the first picture
    // take the other 181. The new engine then boots as at power-on.
    static constexpr int kRebootFields = 258 - 77;
    // beSystem +0x5F (ConsoleReset::set_enabled).
    void set_reset_enabled(bool enabled) { console_reset_.set_enabled(enabled); }
    const ConsoleReset& console_reset() const { return console_reset_; }
    const std::string& stage_name() const { return stage_name_; }
    // The world clock as the last update sampled it (8028AF84 at the start of
    // each frame): it stays while the update covers more fields (held()).
    std::int64_t time() const { return time_; }
    std::uint64_t frame() const { return frame_; }
    // An update that waits for retraces inside it (VIWaitForRetrace: Sofdec's
    // open 802ADC8C reads the movie's header, the movie codec 802AE8B8 waits
    // once in every frame of a movie) covers more fields: the next `fields`
    // steps run no update, the game's picture and input stay, only the
    // per-field work goes on (the CRI servers, Manager::update_held).
    void hold_update(int fields) { hold_fields_ = std::max(hold_fields_, fields); }
    // The last step was such a covered field (no update ran in it).
    bool held() const { return held_; }
    // The frame starts late (ns after its retrace; 0 by default) from the next
    // update on: a higher-priority thread's work on every retrace, Sofdec's
    // decoder while a movie plays (SoundManager), takes the CPU first, so
    // the update samples the world clock later (Dolphin: 0.005..0.022
    // field, 0.011 on average, on time again from the frame after the codec
    // took the last but one picture); the stage change's module reload
    // (Engine::step) makes the update after it late by its fraction of a
    // field.
    void set_clock_lateness(std::int64_t ns) { clock_lateness_ = ns; }
    // The retrace of the last update plus the fields since: the VI clock of
    // the per-field work (time() without the frame's lateness).
    std::int64_t vi_time() const { return time_ - applied_lateness_ + field_time(frame_) - field_time(update_frame_); }
    // World time added by the last step (one NTSC field).
    std::int64_t frame_delta() const { return field_time(frame_) - field_time(frame_ ? frame_ - 1 : 0); }
    // beSystem's frame step (80316258, 0x80316320..0x8031635C): the world
    // time the last step really added (after a DW4_CLOCK_FILE replay, unlike
    // frame_delta) as a float of frames, (float)delta / 16666667.0f
    // (flt_804271BC), unquantised. The model subcommands count their delays
    // with it (80310754) and the player's Slam double-tap timer runs down by
    // it (8034C78C).
    float system_step() const { return float(world_delta_) / 16666667.0f; }

    Messenger& messenger() { return messenger_; }
    DemoManager& demo() { return demo_; }
    SceneCache& scenes() { return scenes_; }
    const MountedResources& resources() const { return resources_; }
    // Mods (folders replacing logical paths, first wins); before the first stage.
    void set_resource_overrides(std::vector<std::filesystem::path> roots) { resources_.set_overrides(std::move(roots)); }
    // Where the memory cards keep their files: GCI folders (Dolphin's layout:
    // <root>/Card A, <root>/Card B) or one save file per slot.
    void set_card_store(save::CardStore store) { card_store_ = std::move(store); }
    const save::CardStore& card_store() const { return card_store_; }
    // GCI folders at `root` (the folder store).
    void set_card_root(std::filesystem::path root) { card_store_ = save::CardStore::folder(std::move(root)); }
    const std::filesystem::path& card_root() const { return card_store_.root(); }
    // What the slots hold (0 = A, 1 = B).
    void set_card_setting(int slot, const CardSetting& setting) { card_settings_[std::size_t(slot & 1)] = setting; }
    const CardSetting& card_setting(int slot) const { return card_settings_[std::size_t(slot & 1)]; }
    // The pads as the game's readers get them (bePadManager's getter
    // 80312138): while STAGECTL PADINPUT DISABLE has set the manager's
    // redirect byte (+0x1D, 0x80348108..0x803481A8) every reader gets the
    // fifth pad, which the update 80311F84 never touches (it serves devices
    // 0..3 only: no input, its connected byte +8 never set). The getter
    // checks the byte at each read, so an ENABLE from this frame's demo
    // already counts for the managers that read after it; the four slots
    // update every frame meanwhile and keep their own history, so a press
    // read in the frame of the ENABLE counts and a button held across it
    // gives no trigger.
    const std::array<PadState, 4>& pads() const {
        if (!pad_input_enabled_) return kFifthPads;
        if (!scoped_pads_off_) return pads_.states();
        // Online: a member's own scene turned its pad input off (SEAMLESS.md 5.3).
        scoped_pads_ = pads_.states();
        for (std::size_t s = 0; s < 4; ++s)
            if (scoped_pads_off_ & (1u << s)) scoped_pads_[s] = kFifthPads[s];
        return scoped_pads_;
    }

    // Script execution (beLua::call 80301FA4): first matching name per info.
    bool run_script(std::string_view name);
    // The stage packages hold a script of that name.
    bool has_script(std::string_view name) const;
    // That script's compiled chunk contains `text` (a string constant; test bot).
    bool script_mentions(std::string_view name, std::string_view text) const;
    // The randomizer's enemy pool of the running stage as the loader made it
    // (its own families and the donor families it loaded), empty without.
    const std::vector<std::string>& randomizer_pool() const { return randomizer_pool_; }
    // A family the loader brought from a donor stage for that pool (not one
    // of the stage's own).
    bool randomizer_donor(std::string_view family) const { return randomizer_donors_.contains(family); }
    // Every stage script (name, compiled chunk bytes), stage scripts first
    // (randomizer: the generators a script starts next to a model name).
    void for_each_script(const std::function<void(std::string_view name, std::string_view chunk)>& visit) const;
    // Scene graph root management.
    Node* global_root() { return &global_root_; }
    bool soft_reset_allowed() const { return soft_reset_allowed_; }
    void set_soft_reset_allowed(bool allowed) { soft_reset_allowed_ = allowed; }
    void attach_scene_root(Node* root);
    Node* find_named_node(Node* from, std::string_view name);

    // Manager access.
    Manager* manager(std::string_view type_name) const;
    // The first manager that is a T (looked up once per type, then kept
    // until managers are added).
    template <class T> T* get() const {
        const std::size_t id = manager_type_id<T>();
        if (id < found_managers_.size() && found_managers_[id].known) return static_cast<T*>(found_managers_[id].manager);
        T* found = nullptr;
        for (const auto& m : managers_)
            if ((found = dynamic_cast<T*>(m.get()))) break;
        if (id >= found_managers_.size()) found_managers_.resize(id + 1);
        found_managers_[id] = {found, true};
        return found;
    }
    void add_manager(std::unique_ptr<Manager> manager);

    // Diagnostics for unimplemented original behavior (shown in the overlay).
    void note(std::string text);
    const std::vector<DiagnosticCounter>& notes() const { return notes_; }
    void log(std::string text);
    const std::deque<std::string>& log_lines() const { return log_; }
    // Observe-only events for the PC host (port_events.hpp): collected only
    // while enabled (off by default, and on a new engine), never read by the
    // simulation, not in fingerprint() and not in a snapshot. At most
    // kMaxPortEvents wait for the host; more are dropped.
    static constexpr std::size_t kMaxPortEvents = 4096;
    void set_port_events(bool enabled) {
        // (Never while replaying: Engine::set_replay.)
        port_events_enabled_ = enabled && !replay_;
        if (!port_events_enabled_) port_events_.clear();
    }
    bool port_events_enabled() const { return port_events_enabled_; }
    void port_event(PortEvent event) {
        if (port_events_enabled_ && port_events_.size() < kMaxPortEvents) port_events_.push_back(std::move(event));
    }
    std::vector<PortEvent> take_port_events() {
        std::vector<PortEvent> taken;
        taken.swap(port_events_);
        return taken;
    }
    // (port_events.cpp) The boss death scripts running now, innermost last;
    // an entry is cleared once its boss was reported.
    std::vector<std::string>& port_boss_scripts() { return port_boss_scripts_; }

    // Global game state used by the script bindings.
    std::map<std::int32_t, std::uint8_t>& flags() { return flags_; }
    std::map<std::int32_t, std::int32_t>& counters() { return counters_; }
    bool pad_input_enabled() const { return pad_input_enabled_; }
    void set_pad_input_enabled(bool value) { pad_input_enabled_ = value; }
    std::int32_t area() const { return 1; }     // USA disc: AUS assets (SYSgetArea)
    std::int32_t country() const { return 1; }  // USA text (SYSgetCountry)

    // Model-script context stack for hit and LUACALL scripts.
    void push_model_context(ModelScriptContext context) { model_contexts_.push_back(std::move(context)); }
    ModelScriptContext pop_model_context() {
        auto context = std::move(model_contexts_.back());
        model_contexts_.pop_back();
        return context;
    }
    ModelScriptContext* model_context() { return model_contexts_.empty() ? nullptr : &model_contexts_.back(); }
    // Main lobby stages (80457128): A checks for talk partners instead of attacking.
    bool main_lobby() const;

    // Stage loader view.
    std::string loader_state() const;
    // beSystem +0x2C (_isActive): cleared by the loader's state 0
    // (0x802F8B54), set by its state 8, the step after start.lua
    // (0x802F9B68). The managers' updates run only while it is set.
    bool stage_active() const { return stage_active_; }
    // A stage load is running (NEXTSTAGE to first.lua).
    bool loading() const { return loader_state_ != -1; }
    // Fast loading (a PC option, Cheats::fast_loading as it was when the
    // running stage's load started, so a change applies from the next load
    // on): the loader runs the same states in the same order without the
    // simulated disc and the original's loading-thread time (no chunk reads,
    // no teardown / routing / registration / start.lua updates that cover
    // retraces, no minimum time of the loading screen), and the sound
    // driver's disc reads end at its next tick (DiscDrive::instant), so ADX
    // streams are ready a few fields after they start.
    bool fast_loading() const { return loader_fast_; }
    const std::vector<LoadedFile>& loaded_files() const { return files_; }
    std::vector<std::string>& common_file_queue() { return common_queue_; }
    // Loads a common package now and registers its infos (a direct stage
    // start's weapon packages); false if it cannot be read.
    bool load_common_now(const std::string& logical_path);
    std::string folder_name() const { return stage_folder_; }

private:
    void process_stage_loader();
    void loader_update();
    // `restoring`: a save state's snapshot (its loader values instead of the
    // outgoing stage's).
    void begin_stage_load(const StageSnapshot* restoring = nullptr);
    // The module resets of the application loop's module reload (8028A87C:
    // every module's +0x70, then every info of the application's list
    // deleted), at the reload's start (Engine::step).
    void reset_modules();
    // What the module reload deletes (loader_teardown_cost): all objects
    // and those the beDBManagerInfos reach, the beFontInfos, the model RAMs,
    // all memory blocks and those outside the DB / font infos.
    struct TeardownLoad {
        std::size_t objects{}, db_objects{}, fonts{}, rams{}, memories{}, rest{};
    };
    TeardownLoad teardown_load() const;
    std::int32_t loader_teardown_cost() const;
    std::int64_t loader_register_cost(const std::vector<ObjectView>& infos) const;
    std::int64_t loader_start_cost() const;
    static constexpr int kLoaderDescriptor = 30; // port-only: beSystem's descriptor request (80316258)
    static constexpr int kLoaderBoot = 31;       // port-only: the boot's reads before the first descriptor
    // The loader's reads (process_stage_loader): one file at a time.
    struct LoaderItem {
        std::string name;        // logical path or member specifier
        int member{-1};          // index into loader_members_, -1 for a logical path
        bool timing_only{};      // read, not loaded (BecText / sbiallse: the port has its own)
    };
    // `start`: nano-fields after the frame's retrace at which the loading
    // thread takes the request up (the main thread's update ends; see
    // Engine::loader_request).
    void loader_request(const LoaderItem& item, std::int64_t start);
    bool loader_ready() const;
    bool loader_queue_step(int kind);
    // Fast loading: several files of the queue per update (loader_queue_step).
    bool loader_queue_fast(int kind);
    std::size_t loader_item_bytes(const LoaderItem& item) const;
    // Fast loading takes further files into an update while those it took
    // stay within this many bytes (the first file is always taken): a
    // frame's parsing stays near that of the largest single file, which the
    // original timing parses in one update as well.
    static constexpr std::size_t kFastLoadBytes = 1u << 20;
    void loader_route(const LoaderItem& item, int kind);
    void load_file(const std::string& logical_path, bool member, std::vector<ObjectView>& deferred_infos);
    void register_info(const ObjectView& info);
    void update_managers();
    // The frame's camera, clear colour and traversal context (render,
    // traverse_scene); `out` null: no draw items.
    TraversalContext frame_context(std::vector<DrawItem>* out, FrameCamera& camera, Vec4f& clear_color);
    // Per-slot cameras (OnlineRules::player_cameras): the simulation pass
    // culled against the union of the member cameras (no draw items), once
    // per step, and the frame and world time it last ran on.
    void traverse_union();
    std::uint64_t union_frame_ = ~0ull;
    std::int64_t union_time_ = -1;
    void install_lua();
    std::vector<Lua4Value> lua_call(const Lua4HostCall& call);

    MountedResources resources_;
    save::CardStore card_store_ = save::CardStore::folder("saves/GC/USA");
    std::array<CardSetting, 2> card_settings_{};
    Messenger messenger_;
    DemoManager demo_;
    SceneCache scenes_;
    std::vector<std::unique_ptr<Manager>> managers_;
    // get<T>() results by manager_type_id<T>() (the T* as void*).
    struct FoundManager {
        void* manager{};
        bool known{};
    };
    mutable std::vector<FoundManager> found_managers_;
    static std::size_t next_manager_type_id() {
        static std::size_t next = 0;
        return next++;
    }
    template <class T> static std::size_t manager_type_id() {
        static const std::size_t id = next_manager_type_id();
        return id;
    }
    // igSerialScheduler's active list (8028B7B0): the managers in the order
    // their first info of the stage was accepted (802F251C -> 8028A398 ->
    // 80041E40 append-unique); every stage clear unschedules them (802F2628).
    // One entry per original manager: a port manager that stands for several
    // (the beNDMWMdl* model classes) is scheduled once per slot.
    struct Scheduled {
        Manager* manager{};
        int slot{};
        bool operator==(const Scheduled&) const = default;
    };
    std::vector<Scheduled> schedule_;
    std::vector<Scheduled> schedule_run_; // the copy one run walks (8028B7B0 -> +0x0C)
    std::vector<LoadedFile> files_;
    std::vector<std::shared_ptr<const IgbDocument>> retired_;
    std::vector<ObjectView> deferred_infos_;
    std::vector<ObjectView> lua_infos_;
    std::vector<std::string> common_queue_;
    // Randomizer enemy pool loads (load_enemy_donors): the stage's pool, the
    // donor families' infos (registered after the stage's in state 5), their
    // action starters with the targets they may serve, and their scripts
    // (consulted after the stage's own).
    std::vector<std::string> randomizer_pool_;
    std::set<std::string, std::less<>> randomizer_donors_;
    std::vector<ObjectView> donor_infos_;
    std::vector<std::pair<ObjectView, std::set<std::string, std::less<>>>> donor_starters_;
    std::vector<ObjectView> donor_scripts_;
    void load_enemy_donors();
    // The targets (families and the effects they start) it registered.
    std::set<std::string, std::less<>> route_donor(const std::shared_ptr<const IgbDocument>& document, const std::vector<std::string>& families);
    std::unique_ptr<Lua4Vm> lua_;
    std::map<std::pair<const void*, std::uint32_t>, Lua4Program> lua_programs_;
    int script_depth_{};
    dw4::OriginalPads pads_;
    static inline const std::array<PadState, 4> kFifthPads{};
    Node global_root_;
    std::vector<Node*> attached_roots_;

    std::string stage_name_;
    std::string stage_folder_;
    std::string pending_stage_;
    int loader_state_ = -1;
    // The file on its way (802F9E98's pending directory): requested, its
    // reads issued on the simulated drive chunk by chunk as the loading
    // thread asks for them (Engine::process_stage_loader, loader_work), the
    // frame whose update sees it done.
    struct LoaderFile {
        LoaderItem item;
        bool active{}, issued{};
        unsigned archive{}, entry{};
        std::uint32_t sectors{};
        std::uint32_t read{};      // sectors read so far (requests of up to 64)
        std::int64_t booked{-1};   // the frame that saw the chunk on its way booked
        int parse{};               // the boot reads: fields from the booking to the next request
        std::vector<std::int64_t> work; // nano-fields the loading thread works after each chunk arrives (the last: until done)
        std::int64_t request_time{};    // nano-fields from frame 0: the loading thread's last request
        std::int64_t issue_frame{-1};   // the frame whose server pass issues the next chunk's read
        std::int64_t ready_frame{-1};   // the frame whose update sees the file done (80070130)
        std::int64_t route_cost{}; // milli-fields of the update that routes the file
        std::shared_ptr<const IgbDocument> document; // parsed at the request, routed when ready
        std::optional<std::uint64_t> raw_offset;      // a read of `sectors` at a disc offset (the boot reads)
    } loader_file_;
    int loader_boot_step_{};                 // the boot reads (kLoaderBoot) done so far
    std::vector<LoaderItem> loader_queue_;   // the queue the current state reads (+0x24 / system +0x34)
    std::size_t loader_index_{};             // its ordinal (+0x38)
    std::vector<PackageMember> loader_members_;
    std::vector<ObjectView> loader_preliminary_; // infos of the /s members, registered when state 100 ends
    std::int64_t loader_cost_{};             // milli-fields the current loader update takes
    int loader_skip_{};                      // retraces the original's current update still covers
    std::int32_t loader_teardown_{};         // see StageSnapshot::loader_teardown
    bool reloading_{};                       // the module reload before the pending stage's teardown has begun (Engine::step)
    bool loader_common_{true};               // see StageSnapshot::loader_common
    bool loader_fast_{};                     // see fast_loading()
    bool stage_active_{}; // beSystem +0x2C, see stage_active()
    bool soft_reset_allowed_{}; // stage data +63, set after first.lua outside the title stages
    ConsoleReset console_reset_;   // beSystem +0x5F..0x62 and the controllers' reset combination
    bool rebooting_{};             // the reset button restarted the console (rebooting())
    bool torn_down_{};             // this step tore the stage down (presenting(); the host's picture only)
    int reboot_fields_{};          // steps since then
    Cheats cheats_;
    std::optional<StageSnapshot> snapshot_;
    std::uint64_t snapshot_serial_{};
    Node* loading_node_{}; // the loading screen's beLoadingNode
    std::int64_t time_{};
    std::uint64_t frame_{};
    std::uint64_t update_frame_{};        // frame_ of the last step that updated (vi_time)
    int hold_fields_{};                   // fields the running update still covers (hold_update)
    bool held_{};                         // the last step was one of them (held())
    std::int64_t clock_lateness_{};       // set_clock_lateness: for the next updates
    std::int64_t applied_lateness_{};     // ... and in time_ now
    std::int64_t world_delta_ = 16683333; // world time added by the last step (system_step)
    bool pad_input_enabled_{};
    bool port_events_enabled_{};          // see set_port_events (host only)
    std::vector<PortEvent> port_events_;
    std::vector<std::string> port_boss_scripts_;
    std::map<std::int32_t, std::uint8_t> flags_;
    std::map<std::int32_t, std::int32_t> counters_;
    std::vector<DiagnosticCounter> notes_;
    std::vector<ModelScriptContext> model_contexts_;
    std::deque<std::string> log_;

    // ---- Online co-op (online.cpp).
    OnlineRules online_;
    std::array<std::optional<PersonalSession>, 4> personal_;
    // The pads of the barrier tick, which every transition step takes (6.7).
    std::optional<std::array<PadSample, 4>> barrier_pads_;
    std::unique_ptr<BarrierResult> barrier_;
    std::vector<ForkSide> fork_sides_;
    int split_identity_ = -1;
    struct PendingFork {
        std::uint8_t leavers{}, kept{}, other{}, child{}; // other: the side this copy despawns; child: the side that leaves the id
        bool in_stage{}, take_leavers{};
    };
    std::optional<PendingFork> pending_fork_;
    std::optional<SplitResult> split_;
    bool replay_{};
    int view_slot_ = 0;
    int trigger_slot_ = -1;
    bool counted_online_{};
    // The online step's input: absent slots neutral and disconnected,
    // leaving ones neutral and connected, the transition steps the pads of
    // the barrier tick.
    std::array<PadSample, 4> online_samples(const std::array<PadSample, 4>& samples) const;
    // The end of an online step: a pending fork's despawns, the barrier
    // tick's pads.
    void finish_online_step(const std::array<PadSample, 4>& samples);
    void apply_pending_fork();
    // The load step: BARRIER_OK's membership and rules (before begin_stage_load).
    void apply_barrier();
    // After a membership step: NINZU, the camera mask, the leash, the
    // personal values (`before`: the rules before the step).
    void membership_changed(const OnlineRules& before);
    void despawn_player(int slot);
    void install_online_lua();
    // The camera, leash and Lua side of a rules change (set_online and every
    // membership step): the camera mask's box hand-over, the leash ring, the
    // funcStageChange port while two or more members are present.
    void online_rules_changed(const OnlineRules& before);
    void apply_online_cameras(const OnlineRules& before);
    void apply_leash_rule();
    // The exit port (online_exits.cpp).
    std::optional<ExitHeading> heading_;
    ExitPromptView exit_view_;
    struct ExitPort;
    std::shared_ptr<ExitPort> exit_port_;     // the running exit prompt (shared with its demo records)
    std::vector<const Node*> picture_hidden_; // render(): picture_hidden_roots
    std::vector<Lua4Value> stage_change_port(std::span<const Lua4Value> args);
    bool gatedisc_fork();
    // 6.5: the exit prompt of a leaving member closes as No (before each online step).
    void close_leaving_prompt();
    friend struct ExitPortAccess;
    void set_trigger(int slot) {
        trigger_slot_ = slot;
        messenger_.set_trigger(slot);
        demo_.set_trigger(slot);
    }
    // Own windows and scenes (online_scenes.cpp, SEAMLESS.md 5.3).
    int scene_scope_ = -1;
    std::uint8_t scoped_pads_off_{};                  // STAGECTL PADINPUT DISABLE inside a slot's scope
    std::uint8_t own_window_hold_{}, own_scene_hold_{}; // hold_own_input
    mutable std::array<PadState, 4> scoped_pads_{};   // pads() with those slots neutral
    // LAYERCTL inside a slot's scope: the layer groups that slot's picture
    // leaves out (its own screen only; the simulation's layers stay).
    std::array<std::vector<std::string>, 4> scoped_layers_off_;
    // The members' own copies of window models and text lines: the cloned
    // subtrees (owned here, hung under the original's parent), their parents,
    // the colour attributes of copied lines, the copies made (name@slot#index).
    struct SceneCopy {
        Node* parent{};
        Node* root{};
        int slot{};
    };
    std::vector<std::unique_ptr<Node>> copy_nodes_;
    std::vector<std::unique_ptr<Attribute>> copy_attributes_;
    std::vector<SceneCopy> copies_;
    std::set<std::string, std::less<>> copies_made_;
    // The talk globals of the own scenes: func.lua's message helpers and the
    // NPC scripts keep a talk's state in stage globals (g_npcRamNo,
    // g_messPlayerNo, g_talk, an exchange's g_SItem* / g_TItem*) from the hit
    // script to the scripts its DMsetLua records load later - the original
    // never runs two talks at once. Online every member's own scenes keep
    // their own values ([slot]) and the rest of the instance its own ([4]);
    // the VM holds the values of talk_globals_side_ (-1: the instance), and
    // run_script switches to the running scope's side and back (never
    // offline: there is no scope).
    static constexpr std::size_t kTalkGlobals = 7;
    std::array<std::array<std::optional<Lua4Value>, kTalkGlobals>, 5> talk_globals_{};
    int talk_globals_side_ = -1;
    void switch_talk_globals(int side);
    // A talk's hit script while another member's own talk runs
    // (run_own_talk: its DMgetCount answers 1).
    bool talk_waits_ = false;
    Node* clone_subtree(const Node* root, std::unordered_map<const Node*, Node*>& map);
    bool make_window_copy(std::string_view name, int index, int slot);
    void clear_scene_copies();
    // The picture filter's part of the own windows and scenes: every slot's
    // copies but the viewed one's, and the layers the viewed slot's own
    // window or scene hides.
    void scoped_picture_hidden(std::vector<const Node*>& out);
    bool scoped_picture_filter() const;
    void step_impl(const std::array<PadSample, 4>& samples);
};

// Base class for original be* managers. Registration follows the original
// virtual +6C info handlers; commands follow structured hook +90 / fallback +8C.
class Manager {
public:
    explicit Manager(std::string type_name) : type_name_(std::move(type_name)) {}
    virtual ~Manager() = default;
    const std::string& type_name() const { return type_name_; }
    virtual bool accepts(const ObjectView& info) const = 0;
    virtual void add_info(Engine& engine, const ObjectView& info) = 0;
    // The module's reset at the start of a stage change's module reload
    // (8028A87C, Engine::reset_modules): the stage's RAMs and infos go.
    virtual void clear_stage(Engine& engine) { (void)engine; }
    virtual void update(Engine& engine) { (void)engine; }
    // The frame's update while the stage is not active (Engine::stage_active):
    // the original skips every manager's RAM updates then (802F28F8); only
    // what it runs outside them goes on (the sound driver).
    virtual void update_inactive(Engine& engine) { (void)engine; }
    // A field the running update covers (Engine::hold_update): only what runs
    // on the retraces themselves goes on (the CRI servers).
    virtual void update_held(Engine& engine) { (void)engine; }
    // Scheduled when the plugin registers it (+5C -> 8028A398), so it runs
    // before every manager an info schedules and stays scheduled across
    // stage clears (beSystem, bePadManager, beMessenger, ...).
    virtual bool scheduled_at_registration() const { return false; }
    // The original manager among those this one stands for that accepts the
    // info (its schedule slot), and the update of that manager alone.
    virtual int schedule_slot(const ObjectView& info) const { (void)info; return 0; }
    virtual void update_slot(Engine& engine, int slot) { (void)slot; update(engine); }
    virtual std::string slot_name(int slot) const { (void)slot; return type_name_; }
    virtual bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) {
        (void)engine; (void)ram; (void)command; (void)packet;
        return false;
    }
    virtual void fallback(Engine& engine, Ram& ram, std::string_view command, Packet& packet);

protected:
    std::string type_name_;
};

} // namespace dw4::engine
