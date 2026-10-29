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

// 30.8 fixed time scaling (managers_core.cpp): ((t >> 18) * scale) << 18.
std::uint64_t scaled_clock(std::int64_t world, float scale);

// One state's clock in the combiner (igAnimationState): its time at node time
// T is ((T >> 18) * speed (+0x38)) << 18 + base (+0x40) (801D3628, through
// scaled_clock's float product and truncation).
struct StateClock {
    float speed = 1.0f;
    std::int64_t base{};
    std::int64_t at(std::int64_t time) const { return static_cast<std::int64_t>(scaled_clock(time, speed)) + base; }
    // The combiner update's start of a state (801CFFB4 0x801D00F8): time 0
    // (its start +0x60, which MOTIONSET's loc_801D26FC leaves 0) at `time`.
    void restart(std::int64_t time) { base = -static_cast<std::int64_t>(scaled_clock(time, speed)); }
    // A new speed with the time kept at `time`: the base moves by
    // ((t >> 18) * old) << 18 - ((t >> 18) * new) << 18 (MOTIONSET's 'S'
    // 0x802F12EC..0x802F1360 / 0x802F138C..0x802F141C, the motion phase's
    // release 0x802F109C..0x802F1130).
    void rescale(float to, std::int64_t time) {
        base += static_cast<std::int64_t>(scaled_clock(time, speed) - scaled_clock(time, to));
        speed = to;
    }
};

// The actor's animation combiner as ACTION's MOTIONSET drives it: the current
// clip, fading in over `blend` nanoseconds from the state before it - the
// previous clip, still playing, or a static pose baked from a cross-fade that
// was still running (loc_801D2840). Loop modes live in the database.
struct MotionState {
    std::string clip;
    StateClock clock;       // `clip`'s state time
    // +0x1c: the state has not been evaluated yet. Every combiner update
    // restarts such a state (801CFFB4 0x801D00D0..0x801D0164: time 0 and the
    // fade's start +0x48 at the update's node time) until the traversal's
    // evaluation (801D0C14 0x801D0E60 / 0x801D0EBC / 0x801D1128) or the motion
    // phase after its update (802F0EB0 0x802F0F58..0x802F1064) clears it.
    bool fresh{};
    std::int64_t blend{};   // fade-in of `clip`; 0: weight 1 at once
    std::int64_t blend_start{};
    std::string previous_clip;
    StateClock previous_clock;
    bool previous_fresh{};
    std::vector<Mat4> previous_pose; // baked bone locals; when set, the fade source
    // The states' times at node time `time` (801E52FC samples them).
    std::int64_t local(std::int64_t time) const { return clock.at(time); }
    std::int64_t previous_local(std::int64_t time) const { return previous_clock.at(time); }
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
    // The combiner update (801CFFB4): states not evaluated yet restart (see
    // `fresh`), then once the current state's weight reaches 1, the states
    // before it retire (801CFEA8) and one state is left.
    void update(std::int64_t time);
    // The combiner's evaluation or the motion phase's update cleared +0x1c.
    void evaluated() { fresh = previous_fresh = false; }
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
// The track of the fixed state loc_801D2840 builds from a cached local matrix
// (combiner +0x40): quaternion through 8012B8A8 into state +0x14, elements
// 12..14 into state +0x24 (0x801D2A3C-0x801D2AC4).
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
