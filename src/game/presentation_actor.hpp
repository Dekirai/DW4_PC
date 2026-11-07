#pragma once

// The presentation actor core (docs/SURVIVAL.md 13.6): a model of any stage
// package member (or a _common package) drawn by the host, never part of a
// simulation. It is round 60's ghost figure core (ghost_figures.hpp) made
// general: the package is read and parsed on a worker thread from a copy of
// the resources, its nodes live in the actor's own scene cache, its motion
// clips are played by sequence name (SQ_WAIT, SQ_TALK) on the actor's own
// clock, and its draw items are made by the engine's own traversal and
// skinning at the present's time and added to a copy of the frame's list
// (ghost_insert_points), with its shadow proxy, faded in and out over 0.35 s
// and by the camera (ghost_camera_alpha) as the ghost figures are.
//
// Data Overflow's NPC is one: n_gekoa from stag0302's main member at its
// place in the main lobby.
#include "dw4/engine/actor.hpp"
#include "dw4/engine/scene_graph.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/resources.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dw4::game {

class PresentationActor {
public:
    static constexpr double kFadeSeconds = 0.35;
    static constexpr float kMotionBlendFrames = 8.0f;

    // `package`: the logical path of a stage package ("stag0302.igb": its
    // main member) or a plain package ("_common/x.igb"); `model`: the
    // model's info name (n_gekoa).
    PresentationActor(std::string package, std::string model);
    ~PresentationActor();
    PresentationActor(const PresentationActor&) = delete;
    PresentationActor& operator=(const PresentationActor&) = delete;

    // Starts loading (a copy of the resources; the first call wins).
    void load(const MountedResources& resources);
    // Loaded and usable; failed: never will be.
    bool ready();
    bool failed() const { return failed_; }

    // Where it stands, its facing (the model's Z axis is (sin yaw, 0, cos
    // yaw)), whether it shows (it fades), the sequence it plays (its clip:
    // the sequence's first MOTIONSET; "" SQ_WAIT).
    void set_place(engine::Vec3f position, float yaw);
    void set_visible(bool visible, double now);
    void set_sequence(const std::string& sequence);
    // Turns towards a point at most `rate` radians a second (the talk).
    void face(engine::Vec3f point, double now, float rate = 6.0f);
    float alpha() const { return float(alpha_); }
    bool visible() const { return visible_; }
    engine::Vec3f position() const { return position_; }
    float yaw() const { return yaw_; }
    // The parsed package once ready (other pieces of the same member: the
    // face icon's texture).
    std::shared_ptr<const IgbDocument> document() const;
    // The clip the actor plays now ("" none yet).
    const std::string& clip() const { return motion_.clip; }

    // Its items into `items` at `now` (ghost_insert_points), its shadow
    // proxy into `shadow`'s casters.
    void draw(std::vector<engine::DrawItem>& items, engine::ShadowFrame* shadow, const engine::FrameCamera& camera,
              std::optional<engine::Vec3f> own, double now);
    std::size_t drawn_items() const { return drawn_items_; }

private:
    struct Model;
    void worker();
    std::string sequence_clip(const std::string& sequence);
    void update_fade(double now);

    std::string package_, model_name_;
    std::unique_ptr<Model> model_;
    bool failed_ = false;
    engine::Vec3f position_{};
    float yaw_{};
    bool visible_ = false;
    double alpha_{}, camera_ = 1.0, last_fade_{}, last_face_{}, born_{};
    std::string sequence_ = "SQ_WAIT";
    engine::MotionState motion_;
    bool started_ = false;
    engine::ActorPose pose_;
    std::vector<engine::Mat4> locals_;
    std::size_t drawn_items_{};

    std::mutex mutex_;
    std::condition_variable wake_;
    std::unique_ptr<MountedResources> resources_;
    std::shared_ptr<const IgbDocument> document_;
    std::string error_;
    bool requested_ = false, done_ = false, stop_ = false;
    std::thread thread_;
};

} // namespace dw4::game
