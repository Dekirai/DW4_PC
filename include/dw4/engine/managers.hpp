#pragma once

#include "dw4/engine/collision.hpp"
#include "dw4/engine/engine.hpp"
#include "dw4/motion.hpp"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dw4::engine {

void register_default_managers(Engine& engine);

// Original scaled node/timer clock (8030FA2C / 8031DE44 family).
struct NodeTimer {
    float scale = 1.0f;
    std::int64_t bias{};
    std::int64_t end{};
    std::int64_t saved_world = -1;
    float cached_frames{};
    std::int64_t previous{};
};
std::uint64_t scaled_clock(std::int64_t world, float scale);
void timer_start(NodeTimer& timer, std::int64_t world, std::int64_t end = 0);
std::int64_t timer_time(const NodeTimer& timer, std::int64_t world);
float timer_frames(NodeTimer& timer, std::int64_t world);
bool timer_expired(const NodeTimer& timer, std::int64_t world);
std::int64_t frames_to_time(float frames);

// ---------------------------------------------------------------------------
class SceneManager final : public Manager {
public:
    SceneManager() : Manager("igSceneInfo") {}
    bool accepts(const ObjectView& info) const override { return info.is_a("igSceneInfo"); }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    const std::vector<ObjectView>& scenes() const { return scenes_; }
    // Camera paths (igGraphPath lists) keyed by scene name.
    ObjectView find_scene(std::string_view name) const;

private:
    std::vector<ObjectView> scenes_;
};

class LayerManager final : public Manager {
public:
    LayerManager() : Manager("beLayerCtl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beLayerInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    void order(Engine& engine, Ram& ram);
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<std::string> groups_;
    std::unique_ptr<Node> layer_switch_;
    std::vector<std::unique_ptr<Node>> group_nodes_;
    bool ordered_{};
};

class BkColorManager final : public Manager {
public:
    BkColorManager() : Manager("beBkColor") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beBkColorInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    Vec4f color() const { return color_; }

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    Vec4f color_{0, 0, 0, 1};
};

class CameraManager final : public Manager {
public:
    CameraManager() : Manager("beCameraCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beCameraCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    void frame_camera(Engine& engine, FrameCamera& camera);

    struct CameraPath {
        std::string name;
        std::vector<Node*> nodes; // root .. igCamera
        Node* camera{};
    };

private:
    struct CameraRam : Ram {
        std::vector<CameraPath> paths;
        int demo_path = -1;
        std::int64_t demo_start{};
        bool demo_active{};
        bool box_camera{};
    };
    std::vector<std::unique_ptr<CameraRam>> rams_;
    std::optional<FrameCamera> current_;
};

class StageCtlManager final : public Manager {
public:
    StageCtlManager() : Manager("beNDMWStageCtl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWStageCtlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
};

class SwitchCtrlManager final : public Manager {
public:
    SwitchCtrlManager() : Manager("beSwitchCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beSwitchCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
};

class LogoManager final : public Manager {
public:
    LogoManager() : Manager("beNDMWLogo") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWLogoInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    void update(Engine& engine) override;

private:
    struct LogoRam : Ram { int state{}; };
    std::vector<std::unique_ptr<LogoRam>> rams_;
};

// Sound, sound effects and movies. Audio output is a separate host service;
// until it exists these managers track state and report the requests.
class SoundManager final : public Manager {
public:
    SoundManager() : Manager("beSound") {}
    bool accepts(const ObjectView& info) const override;
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    bool movie_playing_{};
    std::int64_t movie_end_{};
};

// beTextureCtrl (80324B30 / 80324C54): binds named pack textures to the
// placeholder igTextureBindAttr of listed attribute sets.
class TextureCtrlManager final : public Manager {
public:
    TextureCtrlManager() : Manager("beTextureCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beTextureCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    void update(Engine& engine) override;
    void bind_all(Engine& engine);

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<ObjectView> pack_data_;   // beTextureCtrlData
    std::vector<ObjectView> ctrl_attrs_;  // beTextureCtrlAttr
    std::vector<std::unique_ptr<Attribute>> owned_;
    bool pending_{};
};

// bePoint01 (80312A70): PLAYERPOINT generation groups and point matrices.
class PlayerPointManager final : public Manager {
public:
    PlayerPointManager() : Manager("bePoint01") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "bePoint01Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    std::optional<Mat4> point_matrix(int group, int player) const;
    std::int32_t point_no() const { return point_no_; }
    void set_point_no(std::int32_t value) { point_no_ = value; }

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<ObjectView> infos_;
    std::int32_t point_no_{}; // manager-wide _pointNo, survives stage loads
};

// beHitLandModel: static collision geometry (provisional host queries).
class HitLandManager final : public Manager {
public:
    HitLandManager() : Manager("beHitLandModel") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beHitLandModelInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    CollisionWorld& world() { return world_; }

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    CollisionWorld world_;
};

// Global font images from bectext.igb (beFontImage + igCharMetricsList).
struct Glyph {
    float advance{}, x_offset{}, y_offset{}, width{}, height{};
    float u0{}, v0{}, u1{}, v1{};
};
struct FontImage {
    std::string name;
    float line_height{};
    std::unique_ptr<Attribute> binding;
    std::map<std::uint32_t, Glyph> glyphs;
};

// TEXTCTL (beFontInfo): text layers built from pack unicode strings.
class TextCtlManager final : public Manager {
public:
    TextCtlManager() : Manager("beFont") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beFontInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    std::u16string text(std::size_t index) const;

private:
    struct Line {
        std::string layer;
        int line{};
        Node* node{};
        float max_width{};
        float spacing{};
        std::string style;
    };
    void load_fonts(Engine& engine);
    void set_text(Engine& engine, const std::string& layer, int first_line, const std::u16string& value);
    void clear_layer(const std::string& layer);
    Node* layer_node(Engine& engine, const std::string& layer);
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<Line> lines_;
    std::vector<ObjectView> strings_;
    std::map<std::string, std::unique_ptr<FontImage>> fonts_;
    std::shared_ptr<const IgbDocument> font_document_;
    std::vector<std::shared_ptr<GeometryData>> geometry_;
    std::map<std::string, Vec4f> colors_;
    std::map<std::string, std::string> align_;
};

// SELECTCTRL (beSelectCtrl, 80313CD4 / 803132C0): cursor menus and prompts.
class SelectCtrlManager final : public Manager {
public:
    SelectCtrlManager() : Manager("beSelectCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beSelectCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct SelectRam : Ram {
        bool running{};
        int state{}, sub{}, counter{}, busy{};
        int cursor{}, items = 1;
        bool vertical = true, cancel = true;
        Vec3f position{};
        std::vector<std::uint8_t> enabled;
        std::string sequence;
    };
    std::vector<std::unique_ptr<SelectRam>> rams_;
};

// COMMDLCTRL helper (8035BE8C..8035C73C): opens, re-sequences and closes menu
// models and tracks two wait lists. A sequence wait ends once the model left
// the requested sequence; a close wait ends once the model is removed.
class CommonModelControl {
public:
    void open(Engine& engine, int index, const std::string& name, const std::string& seq, bool track);  // 8035BE8C
    void set(Engine& engine, int index, const std::string& name, const std::string& seq, bool track);   // 8035C2DC
    void close(Engine& engine, int index, const std::string& name, const std::string& seq, bool track); // 8035C51C
    bool is_live(Engine& engine, int index, const std::string& name);                                   // 8035C6D0
    bool sequences_done() const { return sequence_waits_.empty(); }                                     // 8035C504
    bool closes_done() const { return close_waits_.empty(); }                                           // 8035C724
    void update(Engine& engine);                                                                         // 8035C73C
    void reset() { sequence_waits_.clear(); close_waits_.clear(); }

private:
    struct Wait {
        int index{};
        std::string name, seq;
    };
    std::vector<Wait> sequence_waits_, close_waits_;
};

// TITLE2 (beNDMWTitle2, update 80369284, commands 8036B1C0): press start,
// Game Start / Option menu, attract timeout and hand-off to LOADCTRL2.
class Title2Manager final : public Manager {
public:
    Title2Manager() : Manager("beNDMWTitle2") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWTitle2Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    int state() const { return rams_.empty() ? -1 : rams_.front()->state; }

private:
    struct Option {
        int kind{};                  // +20: 0 = window B (camera/light only)
        int row{};                   // +24
        bool confirmed{};            // +28
        int camera_mode{}, light_mode = 1; // +32 / +36 committed values
        bool keep{};                 // +40
        int edit_camera{}, edit_light{}, edit_sound{}; // +44 / +48 / +52
    };
    struct TitleRam : Ram {
        bool created{};              // data list at RAM +28
        bool run{};                  // RAM +18, set by RUN
        int cursor{}, choice{};      // CTRL +18 / +1C
        int state{}, sub{}, step{}, detail{}; // CTRL +20..+2C
        CommonModelControl models;
        Option option;
    };
    void update_ram(Engine& engine, TitleRam& ram);
    void update_option(Engine& engine, TitleRam& ram, bool up, bool down, bool left, bool right, std::uint32_t trigger);
    std::vector<std::unique_ptr<TitleRam>> rams_;
};

// State shared by LOADCTRL2 and the four LOADINTF2 panels (COMMONINTF data,
// created by LOADCTRL2 GENERATE / GENERATE_TITLE at 803689A8).
struct CommonIntf {
    struct Player {
        bool joined{};   // +13
        bool closed{};   // +14
        int state{};     // [9]
        int sub{}, step{}, detail{};
        std::string digimon = "p_agumon";
    };
    bool active{};
    bool title{};        // +64: started from the title screen
    int state{};         // +48: 3 once every panel finished
    int difficulty{};    // +60
    std::array<Player, 4> players{};
};

// LOADCTRL2 (beNDMWLoadCtrl2, update 80368050, commands 803689A8).
class LoadCtrl2Manager final : public Manager {
public:
    LoadCtrl2Manager() : Manager("beNDMWLoadCtrl2") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWLoadCtrl2Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); common_ = {}; }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    CommonIntf& common() { return common_; }
    int state() const { return rams_.empty() ? -1 : rams_.front()->state; }

private:
    struct CtrlRam : Ram {
        bool created{};
        bool title{};                    // CTRL +16
        int state{}, sub{}, step{}, counter{}; // CTRL +20..+32
        int camera_mode{}, light_mode{}; // manager +24 / +28
        bool save_utility{};             // helper +12
    };
    std::vector<std::unique_ptr<CtrlRam>> rams_;
    CommonIntf common_;
};

// LOADINTF2 (beNDMWLoadIntf2, update 8035C958, commands 803612DC): one panel per
// controller slot. The original panel runs data-slot, name, character and
// difficulty dialogs; this reconstruction currently confirms the default
// selection so the new-game hand-off to LOADCTRL2 can run.
class LoadIntf2Manager final : public Manager {
public:
    LoadIntf2Manager() : Manager("beNDMWLoadIntf2") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNDMWLoadIntf2Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct PanelRam : Ram {
        bool created{};
    };
    std::vector<std::unique_ptr<PanelRam>> rams_;
};

// Accepts info types whose runtime is not reconstructed yet, reporting them once.
class PassiveManager final : public Manager {
public:
    PassiveManager() : Manager("passive") {}
    bool accepts(const ObjectView& info) const override;
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
};

// ACTION2 (beAction2) and MODELCTRL (beModelCtrl).
class ActionManager;
class ModelCtrlManager final : public Manager {
public:
    ModelCtrlManager() : Manager("beModelCtrl") {}
    bool accepts(const ObjectView& info) const override;
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    void install_actor_hook(Engine& engine, TraversalContext& context);

    struct Sequence {
        std::string name;
        std::vector<ObjectView> commands;
    };
    struct ModelRam : Ram {
        std::string category;
        Node* node{};
        std::vector<Sequence>* sequences{};
        bool generated{};
        std::string seq_name;
        const Sequence* seq{};
        std::size_t cursor{};
        bool label_passed{};
        std::optional<std::string> wait_name;
        std::int32_t hp = 100, max_hp = 100;
        Mat4 base_matrix{};
        // Motion / control work (beModelCtrlInfoWork subset).
        dw4::MotionChannels channels;
        int ctrl_mode{}, ctrl_phase{};
        std::array<float, 4> pdmove{};
        bool pdmove_set{};
        bool fall_check = true;
        std::array<float, 3> applied{}, desired{};
        float turn_rate_y = 7.0f; // 80456F68/8045700C player rates (radians/frame)
        bool is_player{};
        bool control_enabled = true; // ONCTRL / OFCTRL
        bool key_input{};
        bool on_ground = true;
        float fall_speed{};
        std::string label;
    };

    const ModelRam* player(int index) const;

private:
    void update_player(Engine& engine, ModelRam& ram);
    void advance_motion(Engine& engine, ModelRam& ram);
    void select_sequence(Engine& engine, ModelRam& ram, std::string_view name);
    void run_commands(Engine& engine, ModelRam& ram);
    std::vector<std::unique_ptr<ModelRam>> rams_;
    std::map<const void*, std::unique_ptr<std::vector<Sequence>>> sequences_;
};

class ActionManager final : public Manager {
public:
    ActionManager() : Manager("beAction2") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beAction2Info"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

    struct Command {
        std::string type, name, loop, interpolation;
        float first{}, second{};
    };
    struct Action {
        std::string name;
        std::vector<Command> commands;
    };
    struct ActionRam : Ram {
        Node* node{};
        Node* switch_node{};
        std::size_t switch_child{};
        std::vector<Action>* actions{};
        const Action* current{};
        std::size_t cursor{};
        bool action_active{}, visible{}, frozen{};
        int joint_state{}, fade_state{}, wait_state{}, flash_state{};
        std::int64_t joint_start{}, joint_end{};
        bool joint_ended{};
        float fade_limit{}, fade_add{}, fade_value{};
        std::optional<Vec4f> color;
        std::optional<bool> blend, texture, lighting;
        std::map<std::string, NodeTimer> timers;
        NodeTimer node_clock;
        AnimationDatabase* database{};
        std::vector<Node*> actors;
        MotionState motion;
        ActorPose pose;
        std::int64_t pose_time = -1;
    };
    const ActorPose* pose_for(const Node& actor, std::int64_t world_time);

private:
    void dispatch(Engine& engine, ActionRam& ram, std::string_view name);
    void run(Engine& engine, ActionRam& ram);
    bool execute(Engine& engine, ActionRam& ram, const Command& command);
    void apply_visuals(ActionRam& ram);
    std::vector<std::unique_ptr<ActionRam>> rams_;
    std::map<const void*, std::unique_ptr<std::vector<Action>>> actions_;
    std::map<std::pair<const void*, std::uint32_t>, std::unique_ptr<AnimationDatabase>> databases_;
    std::map<const Node*, ActionRam*> actor_owner_;
};

// Game-wide system state (beSystem / beNDMWGameRam script services).
class GameSystem final : public Manager {
public:
    bool accepts(const ObjectView&) const override { return false; }
    void add_info(Engine&, const ObjectView&) override {}
    bool script_call(Engine& engine, const std::string& method, std::span<const Lua4Value> args, std::vector<Lua4Value>& out);

    struct PlayerSlot {
        bool joined{};
        std::string digimon = "p_agumon";
        std::array<std::string, 3> weapons{};
    };
    std::array<PlayerSlot, 4> players{};
    GameSystem() : Manager("beSystem") { players[0].joined = true; }
    std::int32_t difficulty{};
    std::int32_t difficulty_max{};
    std::int32_t camera_mode{};
    std::int32_t light_mode{};
    std::int32_t ninzu = 1;
    bool game_started{}; // beSystem byte 4840: set by TITLE2 GAMESTART
    std::string gate_stage_name;
    std::int32_t gate_stage_no{};
    std::int32_t player_pos{}; // PLAYERgetPos: how the players entered this stage
};

} // namespace dw4::engine
