#pragma once

// Ghost figures (docs/SEAMLESS.md 3.3 and 12, round 60). The hub and UI
// stages are solo instances: every room member has its own lobby. There the
// other members in the same stage appear as their Digimon, from their
// PRESENCE2 packets (online/presence.hpp samples them between packets): the
// current form with the weapon it holds, at the sampled place and facing,
// playing the motion clip the member plays (or idle / run from its speed),
// with its name tag, fading in on arrival and out on leaving. Look only: no
// collision, no hits, no talk, no effect on anything.
//
// Presentation only, never part of the simulation: each form's package
// (_common/p_<form>.igb, the one the engine loads for a player) is read and
// parsed on a worker thread from a copy of the resources, and its nodes live
// in this class's own scene cache, never the engine's. The figures' draw
// items are made per presented frame by the engine's own traversal and
// skinning (scene_graph.hpp, actor.hpp) on those private nodes and added to
// the frame's list after the simulation's picture was taken and blended:
// they are in no snapshot, fingerprint, replay or lockstep path, and smooth
// motion never sees them (they are drawn at the present's own time). Their
// shadow proxy (the model's "Kage" level of detail) joins the stage shadow's
// casters as the players' proxies do.
//
// A figure near the camera, or between the camera and the own player, turns
// semi-transparent (the depth of its nearest surface first, so it never shows
// its own inside), so it never covers the own player more than a passer-by
// would. At most kMaxFigures, the nearest; a form still loading shows once
// it is ready (it fades in then).
#include "dw4/engine/actor.hpp"
#include "dw4/engine/scene_graph.hpp"
#include "dw4/resources.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dw4::game {

struct GhostTarget {
    std::uint64_t key{};         // the room member
    std::uint32_t segment{};     // changes at a jump: the figure fades over instead of gliding
    engine::Vec3f position{};
    float yaw{};                 // the model's Z axis is (sin yaw, 0, cos yaw)
    int species{};               // the form (presence_data forms(), 0..15); another: no figure
    std::string clip;            // the motion clip the member plays ("": from `speed`)
    bool loop = true;            // the clip loops (else it holds its last picture)
    float speed{};               // units per second
    std::string weapon;          // the weapon package ("" none)
};

// The world matrix of a figure (row vectors: a point is p * M).
engine::Mat4 ghost_matrix(const engine::Vec3f& position, float yaw);
// Stage names compare without letter case ("stagML11" is "stagml11").
bool same_stage(std::string_view a, std::string_view b);
// Where the figures' items go in a frame's list: after the last opaque and
// after the last blended item of the 3D scene (an item without its own
// projection or viewport); the 2D layers drawn after them stay on top.
struct GhostInsert {
    std::size_t opaque{}, blended{};
};
GhostInsert ghost_insert_points(const std::vector<engine::DrawItem>& items);
// The camera fade of a figure (1 none): `eye` the camera, `centre` the
// figure's middle, `own` the own player's middle. Near the eye it falls to
// kCameraAlpha, and so does a figure standing between the eye and the own
// player (within `radius` of that line, nearer to the eye) or in the own
// player (the members of a room all arrive at the same entry point).
inline constexpr float kCameraAlpha = 0.35f;
float ghost_camera_alpha(const engine::Vec3f& eye, const engine::Vec3f& centre, std::optional<engine::Vec3f> own, float radius);

class GhostFigures {
public:
    static constexpr std::size_t kMaxFigures = 9;
    static constexpr double kFadeSeconds = 0.35;      // fade in / out
    static constexpr double kCameraFadeSeconds = 0.2; // the camera fade eases this fast
    static constexpr float kWalkSpeed = 15.0f;        // units per second: above it a figure without a clip runs
    static constexpr float kMotionBlendFrames = 8.0f; // MOTIONSET's blend of the players' wait / run actions

    GhostFigures();
    ~GhostFigures();
    GhostFigures(const GhostFigures&) = delete;
    GhostFigures& operator=(const GhostFigures&) = delete;

    // The resources the loader reads (a copy: never the engine's own, which
    // a catch-up replaces). Before the first, nothing loads.
    void set_resources(const MountedResources& resources);
    // Starts loading a form and a weapon in the background (members anywhere
    // in the room: their figure is ready when they come).
    void preload(int species, const std::string& weapon);
    // The figures to show from now on (`own`: the own player, for the cap
    // to the nearest); the others fade out where they are. Empty: all fade.
    void update(std::vector<GhostTarget> targets, std::optional<engine::Vec3f> own, double now);
    // Drops every figure at once (a stage load, the session's end); the
    // loaded forms stay.
    void clear();
    // The figures' draw items at `now`, inserted into `items` (ghost_insert_points;
    // the lights and fog of the scene's first lit item), their shadow proxies
    // into `shadow`'s casters when the stage casts shadows.
    void draw(std::vector<engine::DrawItem>& items, engine::ShadowFrame* shadow, const engine::FrameCamera& camera,
              std::optional<engine::Vec3f> own, double now);

    // Any figure (also one fading out or waiting for its form).
    bool active() const { return !figures_.empty(); }
    // Members shown: those with a figure on the screen or a staying one whose
    // form is loaded (a form change's crossfade counts once).
    int shown() const;
    // Figures drawn by the last draw (alpha above 0) and loaded forms.
    std::size_t drawn() const { return drawn_; }
    std::size_t loaded_forms() const;
    // One line per figure (traces: DW4_TRACE_GHOSTS).
    std::vector<std::string> describe() const;

private:
    struct Model;
    struct Weapon;
    struct Figure;
    struct Job {
        std::string path;
    };
    void worker();
    void request(const std::string& path);
    void take_results();
    Model* model(int species);
    Weapon* weapon(const std::string& name, const Model& model);
    void pose(Figure& figure, Model& model, double now);

    std::map<int, std::unique_ptr<Model>> models_;
    std::map<std::string, std::unique_ptr<Weapon>> weapons_; // by weapon package name + form
    std::vector<std::unique_ptr<Figure>> figures_;
    double last_update_{}, last_draw_{};
    std::size_t drawn_{};

    // The loader thread: logical paths in, parsed documents out.
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::map<std::string, std::shared_ptr<const IgbDocument>> done_;
    std::map<std::string, std::string> failed_;
    std::map<std::string, bool> requested_;
    std::unique_ptr<MountedResources> resources_;
    bool stop_{};
    std::thread thread_;
};

} // namespace dw4::game
