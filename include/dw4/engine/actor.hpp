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

private:
    ObjectView object_;
    Skeleton skeleton_;
    std::vector<ObjectView> animations_;
    std::map<std::string, std::unique_ptr<Clip>, std::less<>> clips_;
};

// Pose in actor space: world matrix per bone (row-vector layout).
struct ActorPose {
    const Skeleton* skeleton{};
    std::vector<Mat4> bone_world;
    bool bind_pose{}; // true: palette is identity (stored vertices)
};

// One layer of the simple original-style motion state: current clip with a
// cross-fade from the previous pose over `blend` nanoseconds.
struct MotionState {
    std::string clip;
    std::int64_t start{};   // node time when started
    bool loop = true;
    std::int64_t blend{};
    std::string previous_clip;
    std::int64_t previous_start{};
    bool previous_loop = true;
    std::int64_t blend_start{};
};

// Evaluate pose from clips at node time `time`.
bool evaluate_pose(AnimationDatabase& database, const MotionState& motion, std::int64_t time, ActorPose& pose);
// Local clip time (looping or clamped) of a motion at node time.
std::int64_t motion_local_time(const Clip& clip, std::int64_t start, bool loop, std::int64_t time);

} // namespace dw4::engine
