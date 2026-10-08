#pragma once

#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/math.hpp"
#include "dw4/enbaya.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4::engine {

struct Skeleton {
    ObjectView object;
    std::vector<std::string> names;
    std::vector<int> parent;         // bone ordinal, -1 root
    std::vector<int> joint;          // joint index per bone, -1 helper
    std::vector<int> joint_bone;     // bone ordinal per joint
    std::vector<Mat4> inverse_joint; // per joint (row-vector layout)
    std::vector<Vec3f> constant_translation;
    bool valid{};
};

struct Clip {
    std::string name;
    std::int64_t duration{}; // ns
    EnbayaClip channels;
    std::vector<int> track_bone;     // bone ordinal per track
    std::vector<bool> use_sample_translation; // per bone
    bool valid{};
};

// One igAnimationDatabase: skeleton + lazily decoded clips.
class AnimationDatabase {
public:
    explicit AnimationDatabase(const ObjectView& database);
    const Skeleton& skeleton() const { return skeleton_; }
    const Clip* clip(std::string_view name);
    bool has_clip(std::string_view name) const;
    std::string first_clip_name() const;
    const ObjectView& object() const { return object_; }
    // The play mode belongs to the clip's animation source, which every state
    // of that clip shares: a MOTIONSET sets it for all of them (802F1160 at
    // 0x802F1440-0x802F14CC, enbaya source +0xC: 0 loops, 4 plays once). A
    // clip no MOTIONSET has set yet keeps the source's default 0 (loop).
    bool looping(std::string_view name) const;
    void set_looping(std::string_view name, bool loop);

private:
    ObjectView object_;
    Skeleton skeleton_;
    std::vector<ObjectView> animations_;
    std::map<std::string, std::unique_ptr<Clip>, std::less<>> clips_;
    std::map<std::string, bool, std::less<>> looping_;
};

// Pose in actor space: world matrix per bone (row-vector layout).
struct ActorPose {
    const Skeleton* skeleton{};
    std::vector<Mat4> bone_world;
    bool bind_pose{}; // true: palette is identity (stored vertices)
};

// The actor's animation combiner as ACTION's MOTIONSET drives it: the current
// clip, fading in over `blend` nanoseconds from the state before it - the
// previous clip, still playing, or a static pose baked from a cross-fade that
// was still running (loc_801D2840). Loop modes live in the database.
struct MotionState {
    std::string clip;
    std::int64_t start{};   // node time when started
    std::int64_t blend{};
    std::int64_t blend_start{};
    std::string previous_clip;
    std::int64_t previous_start{};
    std::vector<Mat4> previous_pose; // baked bone locals; when set, the fade source
    // Two combiner states or more: the cross-fade has not reached weight 1.
    bool fading(std::int64_t time) const {
        return blend > 0 && (!previous_clip.empty() || !previous_pose.empty()) && time - blend_start < blend;
    }
};

// Bone locals of the combined motion at node time `time`.
bool motion_locals(AnimationDatabase& database, const MotionState& motion, std::int64_t time, std::vector<Mat4>& locals);
// Evaluate pose from clips at node time `time`.
bool evaluate_pose(AnimationDatabase& database, const MotionState& motion, std::int64_t time, ActorPose& pose);
// Seconds the clip's enbaya source samples for a state `local` ns after its
// start (801E52FC -> 801E4FE8): a looping clip wraps at its duration, a clip
// played once holds at duration - 1/30 s.
float motion_source_time(const Clip& clip, std::int64_t local, bool loop);

} // namespace dw4::engine
