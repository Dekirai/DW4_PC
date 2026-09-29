#pragma once

#include "dw4/engine/collision.hpp"
#include "dw4/engine/engine.hpp"
#include "dw4/motion.hpp"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4::engine {

void register_default_managers(Engine& engine);

// Game text: bytes with the high bit set pair into Shift JIS codes that the
// 802FB59C table maps to the fonts' UTF-16 codes; other bytes are ASCII.
std::u16string decode_game_text(std::string_view text);
std::string game_text_utf8(std::string_view text);

// DEVICEFOLDER entry (pack 803B0D4C / unpack 803B0D94): kind (0 weapon,
// 1 armor1, 2 armor2, 3 abChip), record, a flag, rank 0..5 and value 0..2047.
struct ItemDevice {
    int type{}, record{};
    bool flag{};
    int rank{}, value{};
    std::uint32_t pack() const {
        return std::uint32_t(record & 0x3FF) | (flag ? 0x400u : 0u) | (std::uint32_t(type & 0xF) << 12) |
               (std::uint32_t(rank & 0xF) << 16) | (std::uint32_t(value & 0x7FF) << 20);
    }
    static ItemDevice unpack(std::uint32_t word) {
        return {int((word >> 12) & 0xF), int(word & 0x3FF), (word & 0x400u) != 0, int((word >> 16) & 0xF),
                int((word >> 20) & 0x7FF)};
    }
};
// Drop table id (GameSystem::item_tables order) + record -> rolled equipment.
ItemDevice roll_item_device(int table, int record, int level);
// 802EDC54: a floating number (0 damage, 1 EXP, 2 heal, 3 player damage,
// 4 bits) rising from a world position.
void show_number(Engine& engine, int kind, int value, const Vec3f& position);

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
        // beCameraDemoData ([7]): a demo camera info's own path and length.
        std::optional<CameraPath> demo_data;
        std::int64_t demo_data_duration{};
        int demo_path = -1; // index into paths, or -2 for demo_data
        std::int64_t demo_start{}, demo_duration{};
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
    std::int64_t jingle_end_{}; // PLAYatPAUSE stand-in length
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
    bool accepts(const ObjectView& info) const override {
        return info.type() == "beHitLandModelInfo" || info.type() == "beHitLandDelivInfo";
    }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    CollisionWorld& world() { return world_; }
    // beHitLandDelivInfoRam [13]: a model's collision radius (0 when unknown).
    float radius(std::string_view model) const {
        const auto found = radii_.find(model);
        return found != radii_.end() ? found->second : 0.0f;
    }
    // beHitLandDelivInfoRam [12]: the model's HitLand query mask (0 when unknown).
    std::uint32_t mask(std::string_view model) const {
        const auto found = masks_.find(model);
        return found != masks_.end() ? found->second : 0u;
    }

private:
    struct HitRam : Ram {
        std::uint32_t group{};
    };
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<std::unique_ptr<HitRam>> children_;
    std::map<std::string, float, std::less<>> radii_;
    std::map<std::string, std::uint32_t, std::less<>> masks_;
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

// TEXTCTL (beFontInfo, commands 802FA900): text lines built from pack unicode
// strings. Every beFontInfoData entry is one line: layer name [2], line node
// [4] (named fontNNN), category [5] in the FONT layer switch, line index [6]
// in that category's first child switch, max width [7], line height [8],
// alignment [9] and style [10]. Commands address lines by layer and name.
class TextCtlManager final : public Manager {
public:
    TextCtlManager() : Manager("beFont") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beFontInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    struct Line {
        const IgbDocument* document{};
        std::uint32_t info{};
        std::string layer, name;
        int category{}, index{};
        Node* node{};
        float max_width{};
        float spacing{};
        int align{};              // 0 left, 1 center, 2 right
        std::string style;
        std::vector<ObjectView>* strings{};
    };
    void load_fonts(Engine& engine);
    void set_text(Engine& engine, Line& line, const std::u16string& value);
    void switch_line(Engine& engine, const Line& line, bool on);
    void refresh_categories(Engine& engine);
    Node* root_switch(Engine& engine);
    std::vector<std::unique_ptr<Ram>> rams_;
    std::vector<Line> lines_;
    std::map<std::pair<const IgbDocument*, std::uint32_t>, std::vector<ObjectView>> strings_;
    std::map<std::string, std::unique_ptr<FontImage>> fonts_;
    std::shared_ptr<const IgbDocument> font_document_;
    std::vector<std::shared_ptr<GeometryData>> geometry_;
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
    void open_at(Engine& engine, int index, const std::string& name, const std::string& seq, const Vec3f& position,
                 bool track);                                                                            // 8035C0D8
    bool in_sequence(Engine& engine, int index, const std::string& name, const std::string& seq);        // 8035C490
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
        bool entered{};    // +0C: panel taken by this controller
        bool joined{};     // +0D: selection confirmed
        bool closed{};     // +0E: panel closed after the hand-off
        bool active = true; // +0F: still accepting input
        int state{};       // +24 [9]: 2 = LOADCTRL2 closes the panel
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

// MESWIN helper (80362B44 / 80363064..803631A8): the panel message window.
// Messages are TEXTORDER ids; prompts drive j_chrselmesswin's sequences.
class MessageWindow {
public:
    void open();                          // 80363064
    void close();                         // 8036308C
    void show(int id);                    // 803630C0
    void question(int id, int cursor);    // 803630D0: two-choice prompt
    void next(int id);                    // 803630F4: wait for A
    void clear();                         // 8036317C
    bool busy() const { return prompt_; } // 8036316C
    int answer() const { return cursor_; }
    void update(Engine& engine, int player);

private:
    bool open_{}, closing_{}, answered_{}, prompt_{}, pending_{}, clear_request_{};
    int mode_{}, step_{}, cursor_{}, id_{};
    std::string sequence_;
};

// DEGISTATECTRL helper (8035299C / 803529EC / 80353394): face and status windows.
class StatusWindow {
public:
    void show() { mode_ = 1; step_ = 0; done_ = false; }                          // 8035299C
    void hide(int style) { mode_ = 2; style_ = style; step_ = 0; done_ = false; } // 803529EC
    bool update(Engine& engine, CommonModelControl& models, int player, const Vec3f& position, int digimon);
    bool done() const { return done_; }

private:
    int mode_{}, style_{}, step_{};
    bool done_ = true;
};

// DATABASE (beDBManagerInfo): named beDataBase tables with typed columns;
// string columns are names [12] with row-major values [13].
class DatabaseManager final : public Manager {
public:
    DatabaseManager() : Manager("beDBManager") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beDBManagerInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); tables_.clear(); }
    std::string text(std::string_view table, int row, std::string_view column) const;
    // 8031F3CC / 8031F608: integer cell of a char, short, int or bool column.
    std::optional<std::int32_t> number(std::string_view table, int row, std::string_view column) const;
    // First row whose string column equals value, or -1.
    int find_row(std::string_view table, std::string_view column, std::string_view value) const;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    std::map<std::string, ObjectView, std::less<>> tables_;
};

// NUMBERCTRL (beNumberCtrl): HUD digits drawn by texture columns.
class NumberCtrlManager final : public Manager {
public:
    NumberCtrlManager() : Manager("beNumberCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beNumberCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct NumberRam : Ram {
        Node* geometry{};
        float step{};
        int maximum = 9999;
        bool blank_leading{};
        std::vector<std::vector<std::pair<int, int>>> places;
        int shown{}, target{}, rate{};
        bool drawn{};
    };
    void draw(Engine& engine, NumberRam& ram, int value);
    std::vector<std::unique_ptr<NumberRam>> rams_;
};

// METERCTRL (beMeterCtrl): HUD meters whose clips play at the meter's time.
class MeterCtrlManager final : public Manager {
public:
    MeterCtrlManager() : Manager("beMeterCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beMeterCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct MeterRam : Ram {
        Node* node{};
        float maximum = 100, target{}, shown{};
        std::int64_t low_time{}, high_time{};
        bool continuous = true, dirty = true;
        std::string model_name;
        int model_index{};
    };
    std::vector<std::unique_ptr<MeterRam>> rams_;
};

// GENERATER (beGenerater, update 802FCF68, commands 802FD5C4, spawn 802FC9FC):
// one RAM per beGeneraterData. START creates the work (enemy name and max HP
// from DATABASE enemy[_geneEnemyId]); the start mode (direct / timer / range)
// releases the first wave, the fill mode (wipeout / always / timer) spawns the
// rest; when every enemy of the last wave is gone the generator runs _callLua.
class GeneraterManager final : public Manager {
public:
    GeneraterManager() : Manager("beGenerater") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beGeneraterInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;

private:
    struct GeneRam : Ram {
        ObjectView data;           // beGeneraterData
        bool started{};            // RAM +14
        bool done{};               // RAM +18: finished, START is ignored
        int state{};               // RAM +1C
        bool work{};               // RAM +2C allocated
        bool freeze{}, system_freeze{}; // work +8 / +9
        int player_count = 1;      // work +C
        int remaining{};           // work +10 (_nokoriKazu)
        std::vector<int> enemies;  // work +14 (_enemyRamList): generated RAM numbers
        int point{};               // work +18 (_genePoint)
        std::string enemy_name;    // work +1C
        int enemy_record{};        // work +20
        int enemy_max_hp{};        // work +24
        std::int64_t timer_end = -1; // BOOT_TIMER / GENE_TIMER
    };
    int spawn(Engine& engine, GeneRam& ram, int count);
    void refresh(Engine& engine, GeneRam& ram, bool last_pass = false);
    void drop(Engine& engine, GeneRam& ram, const Vec3f& position, bool last);
    std::vector<std::unique_ptr<GeneRam>> rams_;
};

// LOADINTF2 (beNDMWLoadIntf2, update 8035C958, commands 803612DC): one panel per
// controller slot: join prompt, new game / load choice, partner selection with
// the 3D model, status window and the host's START. The name entry keyboard
// (NAMESEL), memory card slots and the difficulty dialog are not reconstructed
// yet; a new game skips straight from the partner to the status window.
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
        int state{}, sub{}, detail{}, detail2{}; // CTRL +24..+30
        int cursor{};                            // CTRL +18
        bool joined_flag{};                      // CTRL +12
        bool cancel{};                           // CTRL +14
        bool message_open{};                     // CTRL +15
        bool no_model{};                         // CTRL +17
        bool new_game{};                         // CTRL +1C
        bool fresh{};                            // CTRL +1D: no saved player
        int digimon = 3;                         // MAKECHR +30
        Vec3f position{};
        CommonModelControl models;
        MessageWindow message;
        StatusWindow status;
    };
    void update_panel(Engine& engine, PanelRam& ram, CommonIntf& common);
    std::vector<std::unique_ptr<PanelRam>> rams_;
};

// MATCTRL (beMatCtrl, update 803030A8, commands 80303B14): floor mats. Each
// beMatCtrlData [8] is a named igAABox; whenever the set of players standing on
// a mat changes, "<name>.lua" runs with that mat as the script context.
class MatCtrlManager final : public Manager {
public:
    MatCtrlManager() : Manager("beMatCtrl") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beMatCtrlInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override;
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;
    void update(Engine& engine) override;
    // MATisAllPlayer / MATgetPlayerNo / MATisRideOffPlayer for the running mat script.
    bool script_call(Engine& engine, const std::string& method, std::span<const Lua4Value> args, std::vector<Lua4Value>& out);

private:
    struct Mat {
        std::string name;
        Vec3f min{}, max{};
        bool enabled = true;
        std::uint32_t current{}, previous{};
    };
    struct MatRam : Ram {
        bool running = true;  // RAM +20: PAUSE / RESTART
        Node* visuals{};      // info [7] switch
        std::vector<Mat> mats;
    };
    std::vector<std::unique_ptr<MatRam>> rams_;
    const Mat* context_{};
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
    // Authored HIT record (first "HIT" data): target categories and local boxes.
    struct HitBody {
        bool player_bullet{}, enemy_bullet{}, player_talk{};
        std::vector<std::pair<Vec3f, Vec3f>> boxes;
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
        // KIND_ITEM work (beNDMWMdlItem): drop table id and record (+EC/+10),
        // the rolled equipment (+F0), the nearest player (+FC), the name line.
        int item_table = -1, item_record{};
        ItemDevice item_device;
        int item_player = -1, item_label = -1;
        bool item_dim{};
        std::int64_t item_blink{};
        Vec3f last_ground{};         // PROVISIONAL fall-out recovery
        bool has_ground{};
        float fall_speed{};
        std::string label;
        std::map<std::string, std::string, std::less<>> labels; // SETLABEL per sequence
        const HitBody* hit{};
        // beModelCtrlSCHitBox queue (MODEL_HITBOX): kind, delay, absolute end.
        struct Hitbox {
            std::int32_t kind{};
            float delay{}, end{}, elapsed{};
            Vec3f offset{}, size{};
            bool started{};
        };
        std::vector<Hitbox> hitboxes;
        // Effect subcommands (SETEFFECT, 80310B50 / start 80310340).
        struct Effect {
            std::string model, sequence;
            Vec3f offset{}, rotation{};
            float delay{}, elapsed{};
            bool started{}, sequence_dead{}, lock{};
            int ram = -1;
        };
        std::vector<Effect> effects;
        // An effect placed on an owner model (GENERATE with attachment strings).
        std::string attach_name;
        int attach_index = -1;
        Vec3f attach_offset{}, attach_rotation{};
        bool attach_lock{};
        std::uint32_t hitbox_generation{}; // bumped when the queue is cleared
        float muteki{};              // MUTEKI invulnerability frames
        std::int32_t record = -1;    // GENERATE record number (enemy DATABASE row)
        std::int64_t attack_ready{}; // player ATDELAY timer end
        int combo_phase{};           // player work +128
        bool combo_released{};       // player work +12C
        bool previous_live{};        // enemy AI: generated last frame
        int ai_timer{};              // PROVISIONAL enemy AI cooldown
        std::string lua_bhit;        // LUACALL BHit script prefix
        std::array<int, 8> spin{};   // player work +114: direction history (Spiral Attack)
        int slam_state{};            // player work +118..+120: Slam Attack double tap
        std::uint32_t slam_direction{};
        float slam_timer{};
        bool capsule_on{};           // KIND_PLAYER2 work +EC: a player is close
        bool capsule_ended{};        // end_g_000bcaps.lua already started
        // AIM map AI (803084B8): maps from the info's "AIM" data, AI work +50.
        const std::vector<ObjectView>* ai_maps{};
        int ai_map{};
        std::int64_t ai_timer_end = -1; // AITIMER
        std::string ai_request;         // AI work +58
    };

    const ModelRam* player(int index) const;
    ModelRam* find(std::string_view name, int index);
    const std::vector<std::unique_ptr<ModelRam>>& rams() const { return rams_; }

private:
    void update_player(Engine& engine, ModelRam& ram);
    void advance_motion(Engine& engine, ModelRam& ram);
    void update_hitboxes(Engine& engine, ModelRam& ram);
    void fire_hitbox(Engine& engine, ModelRam& ram, const ModelRam::Hitbox& box);
    void update_enemy(Engine& engine, ModelRam& ram);
    void update_ai_map(Engine& engine, ModelRam& ram);
    void apply_control(Engine& engine, ModelRam& ram);
    void update_capsule(Engine& engine, ModelRam& ram);
    void update_item(Engine& engine, ModelRam& ram);
    void item_label(Engine& engine, ModelRam& ram, bool show);
    bool pick_item(Engine& engine, ModelRam& ram, int player);
    void update_effects(Engine& engine, ModelRam& ram);
    void place_attached(ModelRam& ram);
    bool player_wait(Engine& engine, ModelRam& ram, std::string_view wait);
    void run_model_script(Engine& engine, ModelRam& ram, std::string_view prefix);
    bool hit_check(Engine& engine, ModelRam& ram, Packet& packet);
    void select_sequence(Engine& engine, ModelRam& ram, std::string_view name);
    void run_commands(Engine& engine, ModelRam& ram);
    std::vector<std::unique_ptr<ModelRam>> rams_;
    std::map<const void*, std::unique_ptr<std::vector<Sequence>>> sequences_;
    std::map<const void*, std::unique_ptr<HitBody>> hit_bodies_;
    std::map<const void*, std::unique_ptr<std::vector<ObjectView>>> ai_maps_;
    std::array<bool, 4> item_labels_{}; // ITNAMEFONT font00..03 in use
};

// PLAYERARMS (beWeapon, commands 80318474): weapon infos. Their sequence rows
// become SETLABEL overrides of the carrying player (WEAPON_RUNTIME.md); the
// weapon geometry attachment to the hand bone is not reconstructed yet.
class WeaponManager final : public Manager {
public:
    WeaponManager() : Manager("beWeapon") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beWeaponInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); labels_.clear(); }
    const std::vector<std::pair<std::string, std::string>>* labels(std::string_view weapon) const;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
    std::map<std::string, std::vector<std::pair<std::string, std::string>>, std::less<>> labels_;
};

// beActionStarter (registration 802F2244, handler 802F228C): on the
// ACTION_BROADCAST of a target model's action, SETEFFECT this effect model on
// that model (hit sparks, defeat smoke, ...).
class ActionStarterManager final : public Manager {
public:
    ActionStarterManager() : Manager("beActionStarter") {}
    bool accepts(const ObjectView& info) const override { return info.type() == "beActionStarterInfo"; }
    void add_info(Engine& engine, const ObjectView& info) override;
    void clear_stage(Engine& engine) override { (void)engine; rams_.clear(); }
    bool command(Engine& engine, Ram& ram, std::string_view command, Packet& packet) override;

private:
    std::vector<std::unique_ptr<Ram>> rams_;
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

    // Player game RAM subset used by FIGHTCALC (GAME_DATA / GAME_STATS).
    struct Stats {
        bool ready{};
        int level = 1;
        int species{};                        // shinka row
        int tsuyosa{}, mamori{}, kashikosa{}, seishin{}, subayasa{};
        int meichuritsu{}, chimeiritsu{}, mahiritsu{}, attack_delay = 30;
        int attack_zokusei = -1;
        std::array<int, 4> resist{}; // HONOOTEIKO, KOORITEIKO, KAMINARITEIKO, YAMITEIKO
        int kaihiritsu{}, bougyoritsu{}, mahiteikou{}, dokuteikou{};
        int exp{}, next_exp{}, exp_rate{}; // EXP, NEXTEXP, EXPRITSU
        int bit_rate{}, luck_rate{};       // BITRITSU, LUCKRITSU
        int hp_drain{}, mp_drain{};        // HPDRAIN, MPDRAIN
        int weapon_record = -1, weapon_skill_no{};
        std::array<int, 9> skill{1, 1, 1, 1, 1, 1, 1, 1, 1};
        std::string weapon_model;
    };
    struct PlayerSlot {
        bool joined{};
        std::string digimon = "p_agumon";
        std::array<std::string, 3> weapons{};
        int hp = 100, max_hp = 100, mp = 50, max_mp = 50;
        Stats stats;
        // DEVICEFOLDER: equipment, at most 36.
        std::vector<ItemDevice> devices;
        // DISKFOLDER: consumable disks, record -> count (at most 9 each).
        std::map<int, int> disks;
        int bits{};                    // player BIT (0..9999999)
        int xdata{};                   // XDATA
        std::array<int, 11> upcnt{};   // UPCNT: MHP, MMP, STR, DEF, WIS, SPI, SPD, fire, ice, thunder, dark
        // UPDATA / DOWNDATA status buffs: 0 SUBAYASA, 1 CHIMEIRITSU, 2 MAHITEIKOU,
        // 3 KAIHIRITSU, 4 TSUYOSA, 5 MAMORI, 6 MHP (up) / DELAY (down), 7.. resists.
        std::array<int, 11> updata{}, downdata{};
        // Equipment: POWERSOCKET (up to 3 weapons, [0] in use) with POWERPOCKET
        // chips, MAINSLOT (armor1) with MAINPOCKET chips, SUBSLOT (armor2).
        std::vector<ItemDevice> power_socket, power_pocket, main_pocket;
        std::optional<ItemDevice> main_slot, sub_slot;
        std::vector<int> wazalist;     // WAZALIST: pcTech rows
        std::map<int, std::uint8_t> base_isuse; // BASE_ISUSE: species reached by this player
        std::map<int, std::uint8_t> cards;      // CARDLIST (kept in player 1's RAM)
    };
    // Drop tables in generator order (804DE098): conItem, insItem, impItem,
    // weapon, armor1, armor2, abChip.
    static constexpr const char* item_tables[7] = {"conItem", "insItem", "impItem", "weapon", "armor1", "armor2", "abChip"};
    std::map<int, std::uint8_t> flag_folder; // common FLAGFOLDER, set by impItem pickups
    // CTRLITEM (GAMERAM 80372054): SYSctlDeviceFolder/DiskFolder/FlagFolder/Bit.
    int control_item(std::string_view folder, std::string_view command, int player, std::string_view table,
                     int a, int b, int c);
    // Creates the player on first use (8036EB0C), otherwise RECALCPLAYER.
    void prepare_player(Engine& engine, int slot);
    // 8036EB0C: species, BASE_LEVEL/SKILL 1, INI_WEAPON1..3 in DEVICEFOLDER and
    // POWERSOCKET, then RECALCPLAYER with full HP/MP.
    void create_player(Engine& engine, int slot);
    // RECALCPLAYER (8036FDE8): status from level, UPCNT, buffs and equipment,
    // the technique list and the socket weapon.
    void recalc_player(Engine& engine, int slot);
    // P2EEXPUP (80373B48): every live player gains the enemy's EXP (the
    // defeating player the full share), levels up past NEXTEXP with full HP/MP.
    // Returns the defeating player's gain.
    int experience_up(Engine& engine, int killer, int enemy_record);
    std::array<PlayerSlot, 4> players{};
    GameSystem() : Manager("beSystem") { players[0].joined = true; }
    std::int32_t difficulty{};
    std::int32_t difficulty_max{};
    std::int32_t camera_mode{};
    std::int32_t light_mode{};
    std::int32_t ninzu = 1;
    bool game_started{}; // beSystem byte 4840: set by TITLE2 GAMESTART
    std::int32_t text_call{}; // TEXTORDER call number read by TXTgetCallNo (80535950)
    std::string gate_stage_name;
    std::int32_t gate_stage_no{};
    std::int32_t player_pos{}; // PLAYERgetPos: how the players entered this stage
};

// FIGHTCALC (GAMERAM dispatcher 803717A4, subcommand names 804E3F38): the
// player-to-enemy weapon path. Magic, status effects and E2P* are not
// reconstructed yet.
struct FightResult {
    std::int32_t damage{};
    std::int32_t attribute = -1; // 0 fire .. 3 dark; -1 common
    std::int32_t flag{};         // largest variance step
};
namespace fight {
std::uint32_t random();
const char* bullet_name(int kind);
bool player_hits(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind, int speed_down);
bool player_critical(const GameSystem::Stats& stats, int kind);
FightResult player_damage(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind, bool critical,
                          int defense_down);
int damage_rank(int hp, int max_hp, const FightResult& fight, bool critical, int kind);
// E2P* (803750B8, 8037526C, 80375C60): enemy attacks; the attack id is kind % 10000
// in the DATABASE eneatk table.
bool enemy_hits(Engine& engine, const GameSystem::Stats& stats, int enemy_record, int kind);
FightResult enemy_damage(Engine& engine, const GameSystem::Stats& stats, int max_hp, int enemy_record, int kind);
int enemy_damage_rank(Engine& engine, int kind, int hp, int damage, int max_hp, const FightResult& fight);
} // namespace fight

} // namespace dw4::engine
