#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace dw4 {
struct ModelEffectSubcommand {
    float delay{}; // Original cached-frame units, not seconds.
    std::optional<std::string> model_name, sequence_name;
    std::array<float,3> offset{}, rotation{};
    bool sequence_dead{}, position_lock{};
};
struct ModelHitboxSubcommand {
    std::int32_t kind{};
    float delay{}, length{}; // Original cached-frame units.
    std::array<float,3> offset{}, box_size{}; // Full world-axis box extents.
};
struct ModelSubcommandOwner {
    std::string model_name; // Caller applies original null-name -> empty fallback.
    std::int32_t ram_no{};
    bool operator==(const ModelSubcommandOwner&) const = default;
};
struct ModelSubcommandContext {
    ModelSubcommandOwner source;
    // ONE work +dc/+e0 when attached; otherwise GENERATE uses source again.
    std::optional<ModelSubcommandOwner> attachment;
};
struct ModelHitboxCheckRequest {
    std::uint64_t entry_id{}; // Stable within this queue; not a guest object ID.
    ModelHitboxSubcommand command;
    std::int32_t bullet_mode{};
    ModelSubcommandContext context;
};
struct ModelEffectGenerateRequest {
    std::optional<std::string> model_name, sequence_name;
    std::int32_t ram_no = -1;
    ModelSubcommandOwner source, attachment;
    std::array<float,3> offset{}, rotation{};
    bool position_lock{};
};
struct ModelSoundPlayResult {
    bool available{};
    std::uint64_t handle{};
    std::int32_t type{};
};
struct ModelEffectSpaceResult {
    bool available{};
    std::optional<std::int32_t> ram_no; // Available + null means original no match.
};
struct ModelSubcommandServices {
    // These callbacks must perform the actual manager effects. Logging is not
    // completion. Unavailable/false means NO side effect occurred and is retryable.
    // Any exception faults the owner: a possibly committed call is never replayed.
    std::function<ModelSoundPlayResult(const std::optional<std::string>&)> play_sound;
    std::function<bool(std::uint64_t)> sound_volume_down; // SOUND VOLUMECTL("DOWN", handle).
    std::function<ModelEffectSpaceResult(const std::optional<std::string>&)> find_effect_space;
    std::function<bool(const ModelEffectGenerateRequest&)> generate_effect;
    std::function<bool(const std::optional<std::string>&,std::int32_t)> erase_effect; // SQ_ERAS.
    // Owns the complete 8030FEEC geometry/debug/CHKBULLET/BHit operation. Unlike
    // the services above, false may retain a completed internal prefix; the
    // callback must resume that same request without replaying its effects.
    std::function<bool(const ModelHitboxCheckRequest&)> check_hitbox;
};
struct ModelSoundSubcommandState {
    std::optional<std::string> name;
    std::uint64_t handle{};
};
struct ModelEffectSubcommandState {
    ModelEffectSubcommand command;
    std::int32_t ram_no = -1;
    bool space_selected{}; // Native resume boundary after completed FIND_SPACE.
};
struct ModelHitboxSubcommandState {
    ModelHitboxSubcommand command;
    std::int32_t bullet_mode{};
    float end_frame{};
};
struct ModelSubcommandEntry {
    std::uint64_t id{};
    float elapsed{}, delay{};
    bool started{};
    std::variant<ModelSoundSubcommandState,ModelEffectSubcommandState,ModelHitboxSubcommandState> value;
};
enum class ModelSubcommandStop {
    complete, sound_play_required, sound_volume_down_required,
    effect_space_required, effect_generate_required, effect_erase_required,
    hitbox_check_required
};
struct ModelSubcommandResult {
    ModelSubcommandStop stop = ModelSubcommandStop::complete;
    std::optional<std::uint64_t> entry_id;
    bool complete() const noexcept { return stop==ModelSubcommandStop::complete; }
};

// Ordered ordinary MODELCTRL sound/effect/hitbox list. External manager resource
// ownership and the complete collision check operation remain in services.
// Move-only: copying live sound/effect handles would duplicate cleanup ownership.
// Destruction does not pretend external cleanup succeeded; call cleanup explicitly.
class ModelSubcommandQueue {
public:
    ModelSubcommandQueue() = default;
    ModelSubcommandQueue(const ModelSubcommandQueue&) = delete;
    ModelSubcommandQueue& operator=(const ModelSubcommandQueue&) = delete;
    ModelSubcommandQueue(ModelSubcommandQueue&&);
    ModelSubcommandQueue& operator=(ModelSubcommandQueue&&);
    const std::deque<ModelSubcommandEntry>& entries() const noexcept { return entries_; }
    bool pending() const noexcept { return operation_!=Operation::none; }
    bool faulted() const noexcept { return faulted_; }
    std::uint64_t append_sound(float delay,std::optional<std::string> name);
    std::uint64_t append_effect(ModelEffectSubcommand command);
    std::uint64_t append_hitbox(ModelHitboxSubcommand command);
    // 80310A68 with exact SCHitBox type: remove matching entries immediately,
    // retaining every sound/effect entry and any pending pass position.
    // An invoked, incomplete hitbox check must finish before clear or cleanup.
    void clear_hitboxes();
    // 803107E0/80310754. Caller supplies beSystem::_frameTimeF (+28), computed
    // from world delta / 16666667.0f. This is NOT the capped per-RAM MotionTimer.
    // Every original entry advances once; callback appends wait
    // until the next pass. Negative finite frame deltas remain meaningful.
    // A pending pass must be resumed or interrupted with cleanup before update.
    ModelSubcommandResult update(float cached_frames,ModelSubcommandContext context,
        const ModelSubcommandServices& services = {});
    ModelSubcommandResult resume(const ModelSubcommandServices& services = {});
    // 803108A0: sequence interruption. Cancels a pending update, performs ordered
    // cleanup (except non-sequence-dead effects), then removes all entries.
    // Missing cleanup services retain the list and its completed cleanup cursor
    // for resume(); original entry ownership is released after the whole pass.
    ModelSubcommandResult cleanup(const ModelSubcommandServices& services = {});
private:
    enum class Operation { none, update, cleanup };
    void require_usable() const;
    void require_appendable() const;
    ModelSubcommandResult process(const ModelSubcommandServices&);
    ModelSubcommandResult start(ModelSubcommandEntry&,const ModelSubcommandServices&,bool& keep);
    ModelSubcommandResult check_hitbox(ModelSubcommandEntry&,const ModelSubcommandServices&);
    ModelSubcommandResult clean(ModelSubcommandEntry&,const ModelSubcommandServices&);
    void transfer(ModelSubcommandQueue&&);
    std::deque<ModelSubcommandEntry> entries_;
    std::uint64_t next_id_{};
    Operation operation_ = Operation::none;
    std::size_t cursor_{}, remaining_{};
    float frame_delta_{};
    ModelSubcommandContext context_;
    bool current_advanced_{}, current_start_{}, processing_{}, faulted_{};
    bool hitbox_check_pending_{};
};
} // namespace dw4
