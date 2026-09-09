#pragma once

#include "dw4/enbaya.hpp"
#include "dw4/skinning.hpp"
#include "dw4/animation_timing.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;

class AnimationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct AnimationInfo {
    bool supported{}; // Graph profile only; compressed decoding is a separate step.
    std::string diagnostic;
    std::uint32_t reference_index{};
    std::string name;
    double duration_seconds{};
    std::uint32_t track_count{};
    std::uint32_t source_reference{};
    std::uint32_t skeleton_reference{};
    std::int32_t priority{};
    std::uint32_t transition_count{};
    std::int64_t duration_ticks{};
    std::string provenance;
};

struct AnimationClip {
    AnimationInfo info;
    EnbayaClip channels;
    std::vector<std::uint32_t> bone_references;
    std::vector<Vec3> constant_translations;
    std::vector<bool> use_sample_translation;
};

// Enumerates the selected skin's owning database, with checked local bindings.
// The current profile requires an identity track/bone binding and one shared
// Enbaya source. No animation is selected by document order or name matching.
std::vector<AnimationInfo> list_igb_animations(const IgbDocument& document, const SkinningData& skin);
AnimationClip decode_igb_animation(const IgbDocument& document, const SkinningData& skin,
    std::uint32_t animation_reference);

// Samples at a finite time in [0, duration]. Looping belongs to the caller.
// Results are in complete bone ordinal order, including helper bones.
// The original translation mask chooses the sampled or skeleton-constant value.
std::vector<EnbayaTransform> sample_animation(const AnimationClip& clip, double time_seconds);
// Single-clip, no-modifier original skeleton profile. Requires an identity helper
// at bone zero, parents before children and joint indices in traversal order.
// This does not execute actor state, blending, root motion or scene placement.
SkinPose sample_animation_pose(const AnimationClip& clip, const SkinningData& skin, double time_seconds);
// Original integer local-time -> shared source-clock mapping -> decoded samples.
// Source clock duration must match this clip. Copies of both caches are published
// only after sampling succeeds. Short-source fallback beyond the decoded clip's
// extent remains an explicit unsupported boundary, never a guessed terminal pose.
std::vector<EnbayaTransform> sample_animation_at_local_time(const AnimationClip& clip,
    std::int64_t local_time, AnimationSourceClock& source, AnimationWrapContext& context);
SkinPose sample_animation_pose_at_local_time(const AnimationClip& clip, const SkinningData& skin,
    std::int64_t local_time, AnimationSourceClock& source, AnimationWrapContext& context);
// Original ordered two-layer bone combination at 801D10C0..801D111C.
// Inputs must already use the same bound bone order and translation selection.
// No endpoint shortcut or normalization: weight zero can still execute the
// original quaternion polynomial branch. Does not select/retire combiner states.
std::vector<EnbayaTransform> blend_animation_samples(const std::vector<EnbayaTransform>& base,
    const std::vector<EnbayaTransform>& next, float weight);
} // namespace dw4
