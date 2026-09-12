#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
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
    // Optional live captured-work +DC/+E0 read, performed after FIND_SPACE.
    // Results are retained across GENERATE retries. A containing work must not
    // be captured strongly: it owns this wrapper and its retained context.
    std::function<std::optional<ModelSubcommandOwner>()> resolve_attachment{};
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

// Actual ordered igObjectList owner. Entries are separate shared allocations;
// reading this list never copies live sound/effect resource handles.
class ModelSubcommandList {
public:
    class const_iterator {
    public:
        using iterator_category=std::forward_iterator_tag;
        using value_type=ModelSubcommandEntry;
        using difference_type=std::ptrdiff_t;
        using pointer=const ModelSubcommandEntry*;
        using reference=const ModelSubcommandEntry&;
        const_iterator()=default;
        reference operator*() const {return **position_;}
        pointer operator->() const {return position_->get();}
        const_iterator& operator++() {++position_;return *this;}
        const_iterator operator++(int) {const auto before=*this;++*this;return before;}
        bool operator==(const const_iterator&) const = default;
    private:
        friend class ModelSubcommandList;
        explicit const_iterator(std::deque<std::shared_ptr<ModelSubcommandEntry>>::const_iterator position):position_(position) {}
        std::deque<std::shared_ptr<ModelSubcommandEntry>>::const_iterator position_;
    };
    ModelSubcommandList()=default;
    ModelSubcommandList(const ModelSubcommandList&)=delete;
    ModelSubcommandList& operator=(const ModelSubcommandList&)=delete;
    std::size_t size() const noexcept {return entries_.size();}
    bool empty() const noexcept {return entries_.empty();}
    const ModelSubcommandEntry& operator[](std::size_t index) const {return *entries_[index];}
    const ModelSubcommandEntry& at(std::size_t index) const {return *entries_.at(index);}
    const ModelSubcommandEntry& front() const {return *entries_.front();}
    const ModelSubcommandEntry& back() const {return *entries_.back();}
    const_iterator begin() const noexcept {return const_iterator(entries_.begin());}
    const_iterator end() const noexcept {return const_iterator(entries_.end());}
    std::shared_ptr<const ModelSubcommandEntry> entry_owner(std::size_t index) const {return entries_.at(index);}
private:
    friend class ModelSubcommandWrapper;
    std::deque<std::shared_ptr<ModelSubcommandEntry>> entries_;
};

// Actual beModelCtrlSubCom allocation at work+E4, with a distinct igObjectList
// at wrapper+08. Both exist immediately; the initial list has no entry owners.
// Native pending-pass state stays with this wrapper across facade replacement.
class ModelSubcommandWrapper {
public:
    ModelSubcommandWrapper();
    ~ModelSubcommandWrapper();
    ModelSubcommandWrapper(const ModelSubcommandWrapper&)=delete;
    ModelSubcommandWrapper& operator=(const ModelSubcommandWrapper&)=delete;
    std::shared_ptr<ModelSubcommandList> list_owner() noexcept;
    std::shared_ptr<const ModelSubcommandList> list_owner() const noexcept;
private:
    friend class ModelSubcommandQueue;
    struct State;
    std::unique_ptr<State> state_;
};

// Ordered ordinary MODELCTRL sound/effect/hitbox list. External manager resource
// ownership and the complete collision check operation remain in services.
// Move-only facade; additional facades explicitly retain the same wrapper.
// Wrapper/list/entry reference copies never copy live handle payloads or start
// cleanup. A pending cursor and fault state belong to their shared wrapper.
// Destruction does not pretend external cleanup succeeded; call cleanup explicitly.
class ModelSubcommandQueue {
public:
    ModelSubcommandQueue();
    explicit ModelSubcommandQueue(std::shared_ptr<ModelSubcommandWrapper>);
    // Alias the actual work+E4 reference field. New operations resolve its
    // current wrapper; an interrupted pass retains its original wrapper/list.
    explicit ModelSubcommandQueue(std::shared_ptr<std::shared_ptr<ModelSubcommandWrapper>> actual_slot);
    ModelSubcommandQueue(const ModelSubcommandQueue&) = delete;
    ModelSubcommandQueue& operator=(const ModelSubcommandQueue&) = delete;
    ModelSubcommandQueue(ModelSubcommandQueue&&);
    ModelSubcommandQueue& operator=(ModelSubcommandQueue&&);
    const ModelSubcommandList& entries() const;
    std::shared_ptr<ModelSubcommandWrapper> wrapper_owner() noexcept;
    std::shared_ptr<const ModelSubcommandWrapper> wrapper_owner() const noexcept;
    std::shared_ptr<ModelSubcommandList> list_owner();
    std::shared_ptr<const ModelSubcommandList> list_owner() const;
    std::shared_ptr<ModelSubcommandWrapper> pending_wrapper_owner() noexcept;
    std::shared_ptr<const ModelSubcommandWrapper> pending_wrapper_owner() const noexcept;
    std::shared_ptr<const ModelSubcommandEntry> entry_owner(std::size_t index) const {return entries().entry_owner(index);}
    bool pending() const noexcept;
    bool faulted() const noexcept;
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
    // Read-only host preflight before publishing an enclosing sequence prefix.
    // Checks the current slot and retained pass without starting/canceling work.
    void require_cleanup_startable() const;
    // 803108A0: sequence interruption. Cancels a pending update, performs ordered
    // cleanup (except non-sequence-dead effects), then removes all entries.
    // Missing cleanup services retain the list and its completed cleanup cursor
    // for resume(); original entry ownership is released after the whole pass.
    // If the live work slot changed while this facade retains another pending
    // pass, use a separate facade for the new wrapper's cleanup. It cannot
    // discard the old continuation or silently clean the wrong work allocation.
    ModelSubcommandResult cleanup(const ModelSubcommandServices& services = {});
private:
    std::shared_ptr<ModelSubcommandWrapper> wrapper_;
    std::shared_ptr<std::shared_ptr<ModelSubcommandWrapper>> slot_;
    std::shared_ptr<ModelSubcommandWrapper> captured_;
    std::shared_ptr<ModelSubcommandWrapper> live_wrapper() const noexcept;
    std::shared_ptr<ModelSubcommandWrapper> require_live_wrapper() const;
    std::shared_ptr<ModelSubcommandWrapper> retained_wrapper() const noexcept;
    void require_not_executing() const;
    void release_completed_capture() noexcept;
};
} // namespace dw4
