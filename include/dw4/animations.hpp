#pragma once

#include "dw4/enbaya.hpp"
#include "dw4/skinning.hpp"

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
} // namespace dw4
