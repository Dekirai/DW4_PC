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
    std::int64_t blend{};   // fade-in of `clip`; 0: weight 1 at once
    std::int64_t blend_start{};
    std::string previous_clip;
    std::int64_t previous_start{};
    std::vector<Mat4> previous_pose; // baked bone locals; when set, the fade source
    // States that stay in the combiner behind `clip` without showing: the
    // activation appends the first animation as a replacing base state
    // (801D2810), which no update retires; the next MOTIONSET's bake removes
    // them with everything else.
    bool hidden{};
    // The current state's fade-in weight at node time `time` (801D0668).
    float weight(std::int64_t time) const;
    // A fade source (the previous clip or the baked pose) is still a state.
    bool has_previous() const { return !previous_clip.empty() || !previous_pose.empty(); }
    // The combiner holds two states or more, as MOTIONSET (0x802F1238) and
    // WAITCHECK 'M' (0x802F0300) count them. Only the combiner updates retire
    // a fade source, see update().
    bool layered() const { return has_previous() || hidden; }
    // The fade source still shows in a pose evaluated at `time`.
    bool fading(std::int64_t time) const { return has_previous() && weight(time) < 1.0f; }
    // The combiner update (801CFFB4): once the current state's weight reaches
    // 1, the states before it retire (801CFEA8) and one state is left.
    void update(std::int64_t time);
};

// One bone's local transform as a combiner state holds it (801D0C14): the
// track quaternion XYZW (state +0x14) and the translation (state +0x24).
struct BoneTrack {
    std::array<float, 4> rotation{0, 0, 0, 1};
    Vec3f translation{};
};
// 801D0C14, path with several states: every further state (status != 2)
// turns the accumulated rotation toward its own through 8012BE90 by its weight
// (state +0x20) and mixes the translation as acc * (1 - w) + w * t, fused
// (0x801D10C0-0x801D111C). Nothing is normalized.
BoneTrack blend_bone_track(const BoneTrack& acc, const BoneTrack& state, float weight);
// The combiner's local matrix of a bone: rotation from the quaternion
// (8012BCDC at 0x801D0E18/0x801D1158), translation into elements 12..14.
Mat4 bone_track_matrix(const BoneTrack& track);
// The track a fixed state rebuilds from a cached local matrix (the snapshot of
// loc_801D2840): quaternion through 8012B8A8, translation from elements 12..14
// (the matrix source samplers 801D11C8/801D1290).
BoneTrack matrix_bone_track(const Mat4& local);

// Bone locals of the combined motion at node time `time`. A fade blends the
// tracks' quaternions (801D0C14), not the matrices.
bool motion_locals(AnimationDatabase& database, const MotionState& motion, std::int64_t time, std::vector<Mat4>& locals);
// Evaluate pose from clips at node time `time`; `locals` gets the combined
// bone locals (empty when the motion has no clip to sample).
bool evaluate_pose(AnimationDatabase& database, const MotionState& motion, std::int64_t time, ActorPose& pose,
                   std::vector<Mat4>& locals);
// Seconds the clip's enbaya source samples for a state `local` ns after its
// start (801E52FC -> 801E4FE8): a looping clip wraps at its duration, a clip
// played once holds at duration - 1/30 s.
float motion_source_time(const Clip& clip, std::int64_t local, bool loop);

} // namespace dw4::engine
