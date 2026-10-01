#pragma once

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/scene_graph.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/lua4.hpp"
#include "dw4/pad.hpp"
#include "dw4/resources.hpp"

#include <array>
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

// One original frame is 16,666,666 ns (the messenger's delay unit, 802ECA74 /
// 803061BC). Simulation always advances by exactly this step.
constexpr std::int64_t kFrameNs = 16666666;

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

class Engine {
public:
    explicit Engine(const std::filesystem::path& data_root);
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
    const std::string& stage_name() const { return stage_name_; }
    std::int64_t time() const { return time_; }
    std::uint64_t frame() const { return frame_; }

    Messenger& messenger() { return messenger_; }
    DemoManager& demo() { return demo_; }
    SceneCache& scenes() { return scenes_; }
    const MountedResources& resources() const { return resources_; }
    // Memory card folders (Dolphin's GCI layout: <root>/Card A, <root>/Card B).
    void set_card_root(std::filesystem::path root) { card_root_ = std::move(root); }
    const std::filesystem::path& card_root() const { return card_root_; }
    const std::array<PadState, 4>& pads() const { return pads_.states(); }

    // Script execution (beLua::call 80301FA4): first matching name per info.
    bool run_script(std::string_view name);
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
    const std::vector<LoadedFile>& loaded_files() const { return files_; }
    std::vector<std::string>& common_file_queue() { return common_queue_; }
    std::string folder_name() const { return stage_folder_; }

private:
    void process_stage_loader();
    void begin_stage_load();
    void load_file(const std::string& logical_path, bool member, std::vector<ObjectView>& deferred_infos);
    void register_info(const ObjectView& info);
    void install_lua();
    std::vector<Lua4Value> lua_call(const Lua4HostCall& call);

    MountedResources resources_;
    std::filesystem::path card_root_ = "saves/GC/USA";
    Messenger messenger_;
    DemoManager demo_;
    SceneCache scenes_;
    std::vector<std::unique_ptr<Manager>> managers_;
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
    bool soft_reset_allowed_{}; // stage data +63, set after first.lua outside the title stages
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
    virtual bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) {
        (void)engine; (void)ram; (void)command; (void)packet;
        return false;
    }
    virtual void fallback(Engine& engine, Ram& ram, std::string_view command, Packet& packet);

protected:
    std::string type_name_;
};

} // namespace dw4::engine
