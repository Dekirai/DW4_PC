#pragma once

// Overhead name tags: name, level and HP / MP bars above the players'
// Digimon, drawn by the host on top of the game picture (ImGui's background
// draw list, after the scene and before the F1 menu). Nothing here enters
// the simulation: the labels read GameSystem::players and the player models
// after a step, the camera the renderer drew with, and the renderer's
// widescreen widening, so lockstep sessions, save states and the Dolphin
// comparisons see no difference. Remote players of the coming seamless
// co-op (more than the four GameSystem slots) are handed in by the netcode
// (set_remote_players) and drawn the same way when they are in this stage.
#include "dw4/engine/math.hpp"
#include "dw4/engine/scene_graph.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::engine {
class Engine;
struct Node;
}

namespace dw4::game {

// Where the 3D picture lies: the renderer's `wide` viewport (GL window
// pixels, origin bottom left), its widescreen x scale of the projection
// (GlRenderer::render's widen), the window's pixel height, and the window
// pixels per ImGui unit (ImGuiIO::DisplayFramebufferScale).
struct LabelView {
    engine::FrameCamera camera;
    float widen = 1.0f;
    float wide_x{}, wide_y{}, wide_width{}, wide_height{};
    float window_height{};
    float scale_x = 1.0f, scale_y = 1.0f;
};

struct ScreenPoint {
    float x{}, y{};    // ImGui units, origin top left
    float distance{};  // from the camera eye, world units
};

// The world point on the screen; nullopt behind the camera (clip w <= 1e-4,
// as the item name labels' projection, items.cpp) or outside the picture by
// more than `margin` (a fraction of its size).
std::optional<ScreenPoint> project_label(const engine::Vec3f& world, const LabelView& view, float margin = 0.1f);
// The camera eye of a view matrix (row-vector layout).
engine::Vec3f camera_eye(const engine::Mat4& view);

// Label opacity by distance: 1 up to `near`, 0 from `far`, linear between.
float label_fade(float distance, float near_distance = 350.0f, float far_distance = 700.0f);

// The head of each of the 16 forms (`species`, the "shinka" row), world
// units above the model's origin, measured in round 54 from the skinned
// figure the renderer draws (hidden 4-player runs in dng101b1 and the
// lobby, the initial weapons). The head's top: the highest point within 2
// units (horizontally) of the head joint, which takes horns, ears, a collar
// or wings close to the head and leaves out weapons, raised arms and wing
// tips further out.
struct FormHead {
    // The head's top the tag rests on: the higher of the field's stance and
    // the lobby's, or the lower one where the head joint's rise (`reach`)
    // covers the other (WereGarurumon stands up in the lobby).
    float height;
    float reach; // how far the head's top stands above the head joint (the more of the two stances)
    float joint; // the head joint's highest standing height (the higher stance)
};
const FormHead* form_head(int species); // nullptr for another species
// The head's top without a pose to follow (remote players): the higher
// stance's, the larger of `height` and `joint + reach`. 30 for another
// species.
float form_label_height(int species);
// How far a form's head rises above its height with the head joint at
// `joint_height` above the model's origin (a jump, the leap of a charged
// attack, a run's bounce, the upright stance of a form whose height is
// the other's): `joint_height + reach - height`, at least 0.
float form_head_rise(int species, float joint_height);
// The form a player's tag measures: the drawn model's first (a Digi-Lab
// digivolution changes the player's form at once, the model only with the
// next stage load), else the stats' species (-1: none), else the player's
// model name's; -1 for none.
int label_species(std::string_view drawn_model, std::string_view player_model, int stats_species);
// The head point: for a form of the table its height, the head's rise as
// followed (`head`'s y) and a gap above the model's origin, moved
// horizontally by `head`'s x and z (the head joint's offset as followed;
// OverheadLabels::follow_head), so the tag keeps still whatever arms and
// weapons do and moves with the head when it rises; without `head` (no
// pose) form_label_height. A model of no known form: the highest joint of
// its last traversed pose (`pose_top`, world y) plus a margin, else part of
// its actors' bounding box (the bind pose's box is about twice the height
// the figure shows), else 30 units.
engine::Vec3f head_point(const engine::Node& node, int species, std::optional<float> pose_top = std::nullopt,
                         std::optional<engine::Vec3f> head = std::nullopt);
// The head point's follow of the head, per label: the head joint's
// horizontal offset from the model's origin and the head's rise, both as
// shown.
struct HeadFollow {
    engine::Vec3f offset{}; // y 0
    float lift{};
    int hold{}; // steps the lift keeps its height before it eases down
};
// One capture of the follow after `steps` simulation steps towards the head
// joint's `offset` from the model's origin (y ignored, cut to 3.5 units;
// each step moves a twentieth of the way) and the head's `rise`
// (form_head_rise): a higher rise is taken at once and held for 30 steps,
// as again by a rise of three quarters of the held one or more (a run's
// stride takes some 20 steps, its tops differ a little), then the lift
// eases down a tenth of the way per step. No `current`: the target itself.
HeadFollow advance_head_follow(std::optional<HeadFollow> current, engine::Vec3f offset, float rise, int steps = 1);
// The world position of a player's head joint in its last traversed pose
// ("Head" in the rookies' skeletons, "Head01" in the others'); nullopt
// without one.
std::optional<engine::Vec3f> player_head_joint(const engine::Engine& engine, std::string_view model, int index, const engine::Node& node);
// Whether the picture hides a label's player: read from the depth buffer the
// renderer just drew (the stage's own collision holds invisible walls, so it
// cannot tell). The probes sit a little above the head point, at its centre
// and to either side: each has its framebuffer pixel (origin top left) and
// its distance along the view axis (the projection's w).
struct OcclusionProbe {
    float x{}, y{};
    float depth{};
};
std::vector<OcclusionProbe> occlusion_probes(const engine::Vec3f& head, const LabelView& view);
// The distance along the view axis of a window depth (0..1) a perspective
// projection (row-vector layout, GL depth range) wrote; nullopt for an
// orthographic one or the far plane's clear value.
std::optional<float> view_distance(float window_depth, const engine::Mat4& projection);
// Hidden when the picture is nearer than every probe by more than `margin`
// (world units) at each probe's pixel; a probe without a depth (off the
// window) counts as seen. `depth_at(x, y)`: the window depth at a
// framebuffer pixel (origin top left).
bool occluded_in_picture(const std::vector<OcclusionProbe>& probes, const std::function<std::optional<float>(float, float)>& depth_at,
                         const engine::Mat4& projection, float margin = 2.0f);
// The highest joint of a player's pose in world y (ActionManager's last
// traversed pose of the model's first actor); nullopt without one.
std::optional<float> player_pose_top(const engine::Engine& engine, std::string_view model, int index, const engine::Node& node);

enum class LabelMode { automatic, all, others, off };
LabelMode parse_label_mode(std::string_view text); // "auto", "all", "others", "off"
const char* label_mode_name(LabelMode mode);
// Whether the slot's label shows: `own` is this machine's player (slot 0
// offline, the session slot online), `players` the joined players in the
// stage (local and remote). auto: everyone once two or more play.
bool label_visible(LabelMode mode, bool own, int players);

struct OverheadLabel {
    std::uint64_t key{};  // local slot 0..3, remote players 0x100 + their id
    int slot = -1;        // GameSystem slot (-1 remote)
    bool own{};           // this machine's player
    // How far the picture hides the player (occluded_in_picture where the
    // labels are drawn, eased over a few frames by
    // OverheadLabels::ease_occlusion): 0 seen .. 1 hidden, the label then
    // faint, so it still shows where the player is.
    float occlusion{};
    engine::Vec3f head{};
    engine::Vec3f feet{}; // the model's position
    std::string name;     // ASCII
    int level{}, hp{}, max_hp{}, mp{}, max_mp{};
};

class OverheadLabels {
public:
    // A remote player's last presence (seamless co-op): only one in the
    // same stage gets a label.
    struct RemotePlayer {
        std::uint64_t id{};
        std::string name, stage;
        engine::Vec3f position{};
        int species{};
        int level{}, hp{}, max_hp{}, mp{}, max_mp{};
    };
    void set_remote_players(std::vector<RemotePlayer> players) { remote_ = std::move(players); }
    const std::vector<RemotePlayer>& remote_players() const { return remote_; }

    // After the simulation advanced (once per presented update): the
    // labels at the step's state become current, the last ones previous.
    // `own_slot`: this machine's GameSystem slot.
    void capture(engine::Engine& engine, int own_slot);
    // The same from labels already made: `local` (this engine's players,
    // `local_players` of them joined) and the remote players in `stage`.
    void capture(std::vector<OverheadLabel> local, int local_players, std::string_view stage);
    void reset();
    // A label's occlusion eased towards `hidden` (an eighth of the way per
    // presented frame), so a probe that grazes an edge does not blink. A hide
    // counts only after kHideAfterFrames presented frames in a row (a new
    // label starts as it is).
    static constexpr int kHideAfterFrames = 12;
    float ease_occlusion(std::uint64_t key, bool hidden);
    // The labels between the previous and the current capture (alpha 0..1,
    // the renderer's smooth motion blend; a label that jumped far, appeared
    // or vanished is drawn where it is now).
    std::vector<OverheadLabel> blended(float alpha) const;
    const std::vector<OverheadLabel>& current() const { return current_; }
    // Joined players at the last capture (local and remote in the stage).
    int players() const { return players_; }

    // Whether the labels are hidden as a whole: stage loads, no new picture,
    // a demo running, pad input off (cutscenes), a status / shop window, the
    // title and menu stages.
    static bool hidden(engine::Engine& engine);

    // The head point's follow of a local player's head (head_point's
    // `head`: the offset's x and z, the lift as y) for label `key` after
    // `steps` simulation steps (advance_head_follow): `joint` the head joint
    // in the world, `species` the form whose head it is. Without a joint
    // none, and the label's follow is forgotten (the next one starts at the
    // joint itself).
    std::optional<engine::Vec3f> follow_head(std::uint64_t key, const engine::Node& node, int species, std::optional<engine::Vec3f> joint,
                                             int steps = 1);

private:
    std::vector<std::pair<std::uint64_t, HeadFollow>> head_follows_; // per label key
    // The engine frame of the last capture: the follows advance by the
    // simulation steps since (a catch-up update runs several).
    std::optional<std::uint64_t> follow_frame_;

    std::vector<OverheadLabel> previous_, current_;
    std::vector<RemotePlayer> remote_;
    struct Occlusion {
        std::uint64_t key{};
        float value{};        // eased: 0 seen .. 1 hidden
        int hidden_frames{};  // hidden probes in a row (at most kHideAfterFrames)
    };
    std::vector<Occlusion> occlusion_; // per label key
    int players_{};
};

// The name tags' layout (draw_overhead_labels draws it): each label's box
// (ImGui units) after the overlap pass, in `labels` order; `shown` false
// for one that does not show (mode, no HP, off screen, faded out). Its
// opacity is the distance fade, times 0.35 for a hidden player (eased by
// `occlusion`).
struct LabelBox {
    bool shown{};
    float x{}, y{}, width{}, height{}; // top left corner and size
    float anchor_x{}, anchor_y{};      // the head point on the screen
    float alpha{};
};
// `size` gives a label's box size (its bars and texts).
std::vector<LabelBox> layout_overhead_labels(const std::vector<OverheadLabel>& labels, const LabelView& view, LabelMode mode, int players,
                                             const std::function<std::pair<float, float>(const OverheadLabel&)>& size);

} // namespace dw4::game

struct ImDrawList;
struct ImFont;

namespace dw4::game {

// Draws the labels whose mode lets them show (overhead_labels_draw.cpp,
// ImGui). `scale`: the label size factor (window height / 960 times the
// setting), `numbers`: HP / MP numbers beside the bars. Labels that would
// overlap move up above each other (the nearer one keeps its place), an
// occluded one is faint, a local player's name carries the colour of its
// HUD ring (1 red, 2 blue, 3 yellow, 4 green). `font`: the tags' font (null:
// ImGui's current one).
void draw_overhead_labels(ImDrawList* list, const std::vector<OverheadLabel>& labels, const LabelView& view, LabelMode mode,
                          int players, float scale, bool numbers, ImFont* font = nullptr);

} // namespace dw4::game
