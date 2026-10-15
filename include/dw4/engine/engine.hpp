#pragma once

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/scene_graph.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/lua4.hpp"
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

struct DeferredMessage {
    std::int64_t time{};
    std::string category;
    std::optional<std::string> name;
    std::int32_t index{};
    std::string command;
    Packet arguments;
};

class Messenger {
public:
    void add_row(std::string category, std::optional<std::string> name, std::int32_t index, Ram* recipient);
    void remove_rows(Manager* manager);
    void clear();
    // Select + send. Results are left in the packet.
    std::size_t send(Engine& engine, std::string_view category, std::optional<std::string_view> name,
                     std::int32_t index, std::string_view command, Packet& packet);
    void post(std::int64_t time, std::string category, std::optional<std::string> name, std::int32_t index,
              std::string command, Packet arguments);
    void update(Engine& engine);
    std::size_t count(std::string_view category, std::optional<std::string_view> name, std::int32_t index) const;
    const std::vector<MessengerRow>& rows() const { return rows_; }
    void clear_deferred() { deferred_.clear(); }

private:
    std::vector<MessengerRow> rows_;
    std::vector<DeferredMessage> deferred_;
};

// beDemoManager record types (802F7580 / 802F7EC0).
struct DemoRecord {
    int type{};                 // 0 set, 1 wait-while-false, 2 wait-while-true, 3 switch, 4 case, 5 end, 7 lua
    std::string category;
    std::optional<std::string> name;
    std::int32_t index{};
    std::string command;
    std::optional<std::string> argument;
    std::int64_t delay_frames{};
    std::int32_t case_value{};
};
struct DemoWork {
    std::vector<DemoRecord> records;
    std::size_t cursor{};
};

class DemoManager {
public:
    void add(DemoRecord record);
    void begin_switch(DemoRecord record);
    void add_case(std::int32_t value);
    void end_switch();
    void run();
    void update(Engine& engine);
    void clear();
    std::size_t running() const { return running_.size(); }
    std::vector<std::string> describe() const;

private:
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

// What is in a memory card slot (Engine::card_setting). The card is the
// slot's GCI folder (<card root>/Card A|B) as Dolphin's GCI folder cards:
// its free blocks are the card's blocks minus the block counts of the *.gci
// files in the folder (GCI header +0x38), its free files 127 minus their
// number.
struct CardSetting {
    enum class Mode : std::uint8_t {
        automatic,   // slot A holds a card, slot B when its folder exists
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
    bool original() const {
        return !god && !infinite_mp && !one_hit && exp_rate == 100 && damage_dealt == 100 && damage_taken == 100 && !drop_seed;
    }
};

// The state that survives a stage load, taken when one starts (everything
// else is rebuilt by the load). With the controller input of every step since
// then it reproduces the session exactly: save states restore it into a new
// engine and replay the steps.
struct StageSnapshot {
    std::string stage;
    std::int64_t time{};
    std::uint64_t frame{};
    std::map<std::int32_t, std::uint8_t> flags;
    std::map<std::int32_t, std::int32_t> counters;
    bool pad_input_enabled{};
    Cheats cheats;
    OriginalPads pads;
    std::string random;                  // system generator state (fight::generator)
    std::int32_t point_no{};             // PLAYERPOINT's generate point (it survives stage loads)
    std::shared_ptr<const void> system;  // GameSystem copy
    std::shared_ptr<const void> sound;        // SoundManager streams
    std::uint64_t serial{};                   // counts the snapshots of an engine
};

// Save state files: a snapshot as bytes and back (throws on damaged or
// foreign data).
std::vector<std::uint8_t> encode_snapshot(const StageSnapshot& snapshot);
StageSnapshot decode_snapshot(std::span<const std::uint8_t> bytes);

class Engine {
public:
    explicit Engine(const std::filesystem::path& data_root);
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

    // One fixed 60 Hz simulation step.
    void step(const std::array<PadSample, 4>& samples);
    // Collect the draw list for the current state.
    // `shadow` (optional) receives the projected-shadow light and casters.
    void render(std::vector<DrawItem>& out, FrameCamera& camera, Vec4f& clear_color, ShadowFrame* shadow = nullptr);
    // Optional host inspection camera replacing the game camera (debug only).
    std::optional<FrameCamera> debug_camera;
    // Per-scene draw statistics for diagnostics.
    std::vector<std::string> scene_report();

    void request_stage(std::string name);
    // False after the step that took a stage change (NEXTSTAGE, 80347F2C):
    // the original presents no new picture from then on, the screen keeps
    // the last one until the loader draws.
    bool presenting() const { return pending_stage_.empty(); }
    const std::string& stage_name() const { return stage_name_; }
    std::int64_t time() const { return time_; }
    std::uint64_t frame() const { return frame_; }
    // World time added by the last step (one NTSC field).
    std::int64_t frame_delta() const { return field_time(frame_) - field_time(frame_ ? frame_ - 1 : 0); }

    Messenger& messenger() { return messenger_; }
    DemoManager& demo() { return demo_; }
    SceneCache& scenes() { return scenes_; }
    const MountedResources& resources() const { return resources_; }
    // Mods (folders replacing logical paths, first wins); before the first stage.
    void set_resource_overrides(std::vector<std::filesystem::path> roots) { resources_.set_overrides(std::move(roots)); }
    // Memory card folders (Dolphin's GCI layout: <root>/Card A, <root>/Card B).
    void set_card_root(std::filesystem::path root) { card_root_ = std::move(root); }
    const std::filesystem::path& card_root() const { return card_root_; }
    // What the slots hold (0 = A, 1 = B).
    void set_card_setting(int slot, const CardSetting& setting) { card_settings_[std::size_t(slot & 1)] = setting; }
    const CardSetting& card_setting(int slot) const { return card_settings_[std::size_t(slot & 1)]; }
    const std::array<PadState, 4>& pads() const { return pads_.states(); }

    // Script execution (beLua::call 80301FA4): first matching name per info.
    bool run_script(std::string_view name);
    // The stage packages hold a script of that name.
    bool has_script(std::string_view name) const;
    // That script's compiled chunk contains `text` (a string constant; test bot).
    bool script_mentions(std::string_view name, std::string_view text) const;
    // Scene graph root management.
    Node* global_root() { return &global_root_; }
    bool soft_reset_allowed() const { return soft_reset_allowed_; }
    void set_soft_reset_allowed(bool allowed) { soft_reset_allowed_ = allowed; }
    void attach_scene_root(Node* root);
    Node* find_named_node(Node* from, std::string_view name);

    // Manager access.
    Manager* manager(std::string_view type_name) const;
    template <class T> T* get() const {
        for (const auto& m : managers_) if (auto* p = dynamic_cast<T*>(m.get())) return p;
        return nullptr;
    }
    void add_manager(std::unique_ptr<Manager> manager);

    // Diagnostics for unimplemented original behavior (shown in the overlay).
    void note(std::string text);
    const std::vector<DiagnosticCounter>& notes() const { return notes_; }
    void log(std::string text);
    const std::deque<std::string>& log_lines() const { return log_; }

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
    const std::vector<LoadedFile>& loaded_files() const { return files_; }
    std::vector<std::string>& common_file_queue() { return common_queue_; }
    // Loads a common package now and registers its infos (a direct stage
    // start's weapon packages); false if it cannot be read.
    bool load_common_now(const std::string& logical_path);
    std::string folder_name() const { return stage_folder_; }

private:
    void process_stage_loader();
    void begin_stage_load();
    void load_file(const std::string& logical_path, bool member, std::vector<ObjectView>& deferred_infos);
    void register_info(const ObjectView& info);
    void update_managers();
    void install_lua();
    std::vector<Lua4Value> lua_call(const Lua4HostCall& call);

    MountedResources resources_;
    std::filesystem::path card_root_ = "saves/GC/USA";
    std::array<CardSetting, 2> card_settings_{};
    Messenger messenger_;
    DemoManager demo_;
    SceneCache scenes_;
    std::vector<std::unique_ptr<Manager>> managers_;
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
    std::unique_ptr<Lua4Vm> lua_;
    std::map<std::pair<const void*, std::uint32_t>, Lua4Program> lua_programs_;
    int script_depth_{};
    dw4::OriginalPads pads_;
    Node global_root_;
    std::vector<Node*> attached_roots_;

    std::string stage_name_;
    std::string stage_folder_;
    std::string pending_stage_;
    int loader_state_ = -1;
    std::string loader_last_file_; // the last file of state 4's queue: its final chunk is read on the simulated drive
    bool loader_read_{};           // that read was requested
    std::int64_t loader_booked_{-1}; // the frame that first saw it booked
    bool stage_active_{}; // beSystem +0x2C, see stage_active()
    bool soft_reset_allowed_{}; // stage data +63, set after first.lua outside the title stages
    Cheats cheats_;
    std::optional<StageSnapshot> snapshot_;
    std::uint64_t snapshot_serial_{};
    Node* loading_node_{}; // the loading screen's beLoadingNode
    std::int64_t time_{};
    std::uint64_t frame_{};
    bool pad_input_enabled_{};
    std::map<std::int32_t, std::uint8_t> flags_;
    std::map<std::int32_t, std::int32_t> counters_;
    std::vector<DiagnosticCounter> notes_;
    std::vector<ModelScriptContext> model_contexts_;
    std::deque<std::string> log_;
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
    virtual void clear_stage(Engine& engine) { (void)engine; }
    virtual void update(Engine& engine) { (void)engine; }
    // The frame's update while the stage is not active (Engine::stage_active):
    // the original skips every manager's RAM updates then (802F28F8); only
    // what it runs outside them goes on (the sound driver).
    virtual void update_inactive(Engine& engine) { (void)engine; }
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
