#pragma once

#include "dw4/animation_math.hpp"
#include "dw4/animations.hpp"

#include <map>
#include <limits>
#include <memory>
#include <optional>

namespace dw4 {
// One document-qualified immutable library, with mutable source clocks shared
// by its combiners. Copies share decoded data but own independent clock state.
class AnimationSourceRuntime {
public:
    AnimationSourceRuntime(std::string source, std::vector<AnimationClip> clips);
    const std::string& source() const noexcept;
    const std::vector<AnimationClip>& clips() const noexcept { return library_->clips; }
    const AnimationClip& clip(std::uint32_t reference) const;
    void initialize_action_offset() noexcept { wrapping_.initialize_action_offset(); action_offset_initialized_=true; }
    bool action_offset_initialized() const noexcept { return action_offset_initialized_; }
    void set_mode(std::uint32_t clip_reference, AnimationPlayMode mode);
    AnimationPlayMode mode(std::uint32_t clip_reference) const;
private:
    friend class AnimationCombiner;
    struct Library {
        std::string source;
        std::vector<AnimationClip> clips;
        std::map<std::uint32_t,std::size_t> indices, sources;
    };
    std::shared_ptr<const Library> library_;
    std::vector<AnimationSourceClock> clocks_;
    AnimationClockCache state_clock_;
    AnimationWrapContext wrapping_;
    bool action_offset_initialized_{};
    std::vector<EnbayaTransform> sample(std::uint32_t clip_reference, std::int64_t time);
};

struct AnimationCombinerState {
    std::uint64_t id{}; // Owned identity, not an original guest pointer.
    std::optional<std::uint32_t> clip_reference; // Null means a fixed snapshot.
    std::int32_t priority{}, combine_mode{}, transition_mode{}, status{};
    bool is_new = true;
    float weight{};
    AnimationStateClock clock;
    AnimationBlendWindow blend;
    std::int64_t animation_start_time{};
    std::uint64_t time_requested{};
    std::vector<EnbayaTransform> channels;
};

// Recovered full-binding combiner path used by explicit MOTIONSET transitions.
// Seed matrices must come from the caller's prepared original actor state.
// Named transitions, partial bindings, mirroring, modifiers and cycle matching
// require additional graph/runtime integration; they are not inferred here.
class AnimationCombiner {
public:
    AnimationCombiner(const AnimationSourceRuntime& sources, std::vector<AnimationMatrix> cached_matrices);
    const std::vector<AnimationCombinerState>& states() const noexcept { return states_; }
    const std::vector<AnimationMatrix>& cached_matrices() const noexcept { return matrices_; }
    bool cache_valid() const noexcept { return cache_valid_; }
    void set_request_time(std::uint64_t time) noexcept { clock_override_=time; }
    std::uint64_t request_time(std::uint64_t system_time) const noexcept {
        return clock_override_==std::numeric_limits<std::uint64_t>::max() ? system_time : clock_override_;
    }
    // 801D2810: original first-activation base, with scale 1 and bias 0.
    void add_base(const AnimationSourceRuntime& sources, std::uint32_t clip_reference, std::uint64_t requested_time);
    // 801D26FC with combine/transition/status = 1/1/1, explicit start sentinel.
    void append_blend(const AnimationSourceRuntime& sources, std::uint32_t clip_reference,
        AnimationBlendWindow blend, std::uint64_t requested_time, std::int64_t animation_start_time = 0);
    // 801D2840: use the LAST evaluated matrices, even after time has advanced.
    void snapshot();
    // 801CFFB4: advance clocks, start explicit blends, retire earlier tracks.
    // Returns the state count from before retirement, as the original does.
    std::size_t update(AnimationSourceRuntime& sources, std::uint64_t world_time);
    // 801D0C14: preserve same-time cached matrices; update before sampling.
    const std::vector<AnimationMatrix>& evaluate(AnimationSourceRuntime& sources, std::uint64_t world_time);
    SkinPose pose(AnimationSourceRuntime& sources, const SkinningData& skin, std::uint64_t world_time);
    void clear_new_flags() noexcept;
    void set_state_speed(std::size_t index, std::uint64_t world_time, float speed);
    // ACTION2 WAITCHECK refreshes the sole state's clock without evaluating its pose.
    std::int64_t advance_state_clock(AnimationSourceRuntime& sources, std::size_t index, std::uint64_t world_time);
private:
    friend class ActionRuntime;
    friend AnimationCombiner decode_animation_combiner(const IgbDocument&, const AnimationSourceRuntime&, std::uint32_t);
    std::shared_ptr<const AnimationSourceRuntime::Library> library_;
    std::vector<AnimationCombinerState> states_;
    std::vector<AnimationMatrix> matrices_;
    std::uint64_t next_id_ = 1, cached_time_{};
    std::uint64_t clock_override_ = std::numeric_limits<std::uint64_t>::max();
    bool cache_valid_{};
    void require_sources(const AnimationSourceRuntime& sources) const;
    std::vector<std::size_t> order() const;
    void add(const AnimationSourceRuntime& sources, std::uint32_t clip_reference,
        std::uint64_t requested_time, bool blend);
    std::size_t update_impl(AnimationSourceRuntime& sources, std::uint64_t world_time);
    void evaluate_impl(AnimationSourceRuntime& sources, std::uint64_t world_time);
};

// Load an explicitly selected original combiner's cached matrices and clock.
// Requires empty state/track lists with the complete owning skeleton binding.
// Actor/hierarchy setup and the optional cleanCombinerStates debug path remain separate.
AnimationCombiner decode_animation_combiner(const IgbDocument& document,
    const AnimationSourceRuntime& sources, std::uint32_t combiner_reference);

struct AnimationMotionCommand {
    std::uint32_t animation_reference{}; // Already resolved in the owning database.
    float blend_frames{};
    std::string interpolation_mode, loop_mode;
};
struct AnimationMotionWork {
    bool no_interpolation{}, is_motion{};
    std::int32_t state{};
};
// Numeric/combiner/source effects of ACTION2 MOTIONSET and its motion update.
// The caller supplies original system time AND node-adjusted time explicitly.
// These functions do not create actors, resolve ACTION names or broadcast events.
void apply_animation_motion(AnimationCombiner& combiner, AnimationSourceRuntime& sources,
    AnimationMotionWork& work, const AnimationMotionCommand& command,
    std::uint64_t system_time, std::uint64_t node_time);
void advance_animation_motion(AnimationCombiner& combiner, AnimationSourceRuntime& sources,
    AnimationMotionWork& work, std::uint64_t node_time, float node_speed);
} // namespace dw4
