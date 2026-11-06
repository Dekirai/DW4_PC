#pragma once

// Data Overflow in the lobby (docs/SURVIVAL.md 3, 9.2; round O1b lane D):
// the Simulator Keeper Gekomon (n_gekoa) beside the Outside World Gate,
// when it is there (3.2), talking to it through H0's observe-only port event
// (3.3), the dialog (3.4) and the lobby screens (the character select with
// the starting loadout, the Plug Lab and Data Records pages, the help
// pages). Presentation and host input only: the story engine is read, never
// written; while the dialog is open the story steps with neutral pads.
#include "overflow_run.hpp"
#include "presentation_actor.hpp"
#include "sim_ui.hpp"

#include "dw4/engine/port_events.hpp"
#include "dw4/engine/scene_graph.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4::engine {
class Engine;
}

namespace dw4::game::overflow {

// 3.1: the place (on the main floor, 45 units east of the gate's pad) and
// the facing (the model's Z axis towards +z).
inline constexpr engine::Vec3f kNpcPlace{50.0f, 10.0f, -80.0f};
inline constexpr float kNpcYaw = 0.0f;
// The original NPCs' player_talk cube on the model origin (full size).
inline constexpr float kTalkCube = 10.0f;
// The talk hitbox's size (model_ctrl.cpp: kind 60000, size 3).
inline constexpr float kTalkBox = 3.0f;

// DW4_OVERFLOW in test runs: unset (hidden), "1" (shown, talk works),
// "show" (drawn, never talks; T2's second run).
enum class NpcTestMode { off, talk, show };
NpcTestMode npc_test_mode(const char* value);

struct NpcConditions {
    bool lobby = false;       // the story shows stagML11 (loaded, presenting)
    bool offline = true;      // no room, no direct session, no online rules
    bool test_run = false;    // --smoke-test, input scripts, the campaign driver
    NpcTestMode test = NpcTestMode::off;
    bool setting = true;      // settings.ini [overflow] npc
    bool intro_seen = false;  // flag 501
    bool gate_open = false;   // mate00's first destination (gate_open)
    int players = 1;          // local players joined
};
struct NpcState {
    bool shown = false; // drawn
    bool talk = false;  // A next to it opens the dialog
    bool crowd = false; // 2-4 players: it only says they must come alone
};
NpcState npc_state(const NpcConditions& c);
// mate00.lua's branches: flag 409, or one of 2, 4, 5, 6, 86 (the gate's
// first destinations; 409 the original's own special case).
bool gate_open(const std::map<std::int32_t, std::uint8_t>& flags);
// The talk box (centre, kTalkBox) overlaps the NPC's talk cube.
bool talk_reaches(engine::Vec3f centre, engine::Vec3f npc = kNpcPlace);

// The meta file's data the screens show (lane F's overflow_profile fills it
// at the merge; until then a stand-in in memory).
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
    std::map<std::string, FormRecord, std::less<>> forms; // by model (p_agumon)
    std::map<std::string, std::array<int, 7>, std::less<>> plugs; // Plug Lab steps per form (Strength .. Speed)
    std::vector<std::string> armors{"", "brave_core", "armor_board", "fire_board", "ice_board", "bolt_board", "dark_board"};
    std::vector<std::string> chips{"", "wisdom_chip", "spirit_chip", "speed_chip", "pwr_medal"};
    std::vector<std::string> records; // done records (keys)
    std::vector<std::string> locked_forms; // forms not open (none of the four Rookies)
};
// How the last run went (Gekomon's "Welcome back" line, 3.4).
struct RunSummary {
    RunEnd end = RunEnd::retired;
    double seconds = 0;
    std::string defeated_by; // the family whose hit ended it (e_geko, e_tonogeko: the Gekomon joke)
};
std::string welcome_back(const RunSummary& run);

inline constexpr std::array<const char*, 4> kSelectForms{"p_agumon", "p_vmon", "p_girumon", "p_dorumon"};
// The face sheet's order (charaicon, 9.1): 4 x 4.
inline constexpr std::array<const char*, 16> kFaceOrder{"p_agumon", "p_vmon", "p_girumon", "p_dorumon", "p_angelr", "p_alpha",
                                                        "p_beelzeb", "p_hekabut", "p_weregaru", "p_wargrey", "p_impdrafm", "p_impdrapm",
                                                        "p_dukecrim", "p_susanoo", "p_metalgaru", "p_bwargrey"};

enum class LobbyScreen { none, talk, choice, select, plug_lab, records, help };

struct MenuKeys {
    bool up = false, down = false, left = false, right = false, a = false, b = false, l = false, r = false;
};

struct LobbyAction {
    enum class Kind { none, start_run, intro_seen, buy_plug, closed } kind = Kind::none;
    RunRequest request;   // start_run
    std::string form;     // buy_plug
    int stat = 0;         // buy_plug: 0..6
};

// The dialog and the lobby screens as a state machine of menu keys (no
// drawing; unit tested).
class LobbyMenu {
public:
    // Opens the talk: the first meeting's six pages, or "Welcome back" after
    // a run, then the choice list; with 2-4 players the crowd line only.
    void open(bool crowd, const LobbyMeta& meta, const std::optional<RunSummary>& back);
    // A screen directly (tests: DW4_OVERFLOW_SCREEN).
    void open_screen(LobbyScreen screen, const LobbyMeta& meta);
    void close();
    bool is_open() const { return screen_ != LobbyScreen::none; }
    LobbyAction input(const MenuKeys& keys, const LobbyMeta& meta);

    LobbyScreen screen() const { return screen_; }
    const std::vector<std::string>& pages() const { return pages_; }
    int page() const { return page_; }
    int cursor() const { return cursor_; }
    bool closing_page() const { return closing_; }
    // The select: the face, the loadout lines and the confirm window.
    int face() const { return face_; }
    int line() const { return line_; } // 0 faces, 1 armour, 2 chip, 3 level, 4 start
    int armor() const { return armor_; }
    int chip() const { return chip_; }
    bool confirming() const { return confirming_; }
    int confirm_cursor() const { return confirm_; }
    int records_page() const { return records_page_; }
    int plug_form() const { return plug_form_; }
    std::uint64_t opened_frames() const { return frames_; }
    void tick() { ++frames_; }

private:
    void to_choice();
    LobbyScreen screen_ = LobbyScreen::none;
    std::vector<std::string> pages_;
    int page_ = 0, cursor_ = 0;
    bool intro_ = false, closing_ = false, crowd_ = false;
    int face_ = 0, line_ = 0, armor_ = 0, chip_ = 0, confirm_ = 0, records_page_ = 0, plug_form_ = 0;
    bool confirming_ = false;
    std::uint64_t frames_ = 0;
};

// The screens of a LobbyMenu into SimUi.
void draw_lobby_menu(SimUi& ui, const LobbyMenu& menu, const LobbyMeta& meta, std::uint64_t frame);

// The lobby side as the story loop uses it.
class OverflowLobby {
public:
    OverflowLobby();
    void set_resources(const MountedResources& resources);
    LobbyMeta& meta() { return meta_; }
    // Each pass: whether the NPC is there (3.2).
    void update(const NpcConditions& conditions, std::optional<engine::Vec3f> own, double now);
    const NpcState& state() const { return state_; }
    // The host asks for port events while the NPC is shown (3.3).
    bool wants_port_events() const { return state_.shown; }
    // After a story step: takes the talk_box events out of `events` (the
    // achievements and the online client never see them); a box of player 1
    // reaching the NPC asks for the dialog, which opens when player 1 still
    // has the control a step later (no original talk started).
    void take_events(std::vector<engine::PortEvent>& events);
    void stepped(bool player_has_control);
    bool dialog_open() const { return menu_.is_open(); }
    // The live keys while the dialog is open (edges are found here).
    LobbyAction input(std::uint32_t buttons, float stick_x, float stick_y);
    void close();
    // A test: the screen to open as soon as the NPC is shown.
    void open_test_screen(LobbyScreen screen) { test_screen_ = screen; }
    // The run came back: the next talk starts with "Welcome back".
    void returned(const RunSummary& summary);
    // Draws the NPC (3D) and the dialog (2D) into a copy of the frame.
    void present(std::vector<engine::DrawItem>& items, engine::ShadowFrame* shadow, const engine::FrameCamera& camera,
                 std::optional<engine::Vec3f> own, double now, std::uint64_t frame);
    bool needs_present() const;
    const PresentationActor& actor() const { return actor_; }
    std::function<void()> on_intro_seen; // the meta file remembers the first meeting (lane F)

private:
    PresentationActor actor_;
    SimUi ui_;
    std::unique_ptr<MountedResources> resources_;
    LobbyMenu menu_;
    LobbyMeta meta_;
    NpcState state_;
    std::optional<RunSummary> back_;
    std::optional<LobbyScreen> test_screen_;
    int pending_ = 0; // steps until a talk_box reaching the NPC opens the dialog
    std::uint32_t last_buttons_ = 0;
    int last_stick_y_ = 0, last_stick_x_ = 0;
    std::optional<engine::Vec3f> own_;
};

} // namespace dw4::game::overflow
