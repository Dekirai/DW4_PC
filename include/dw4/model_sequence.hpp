#pragma once

#include "dw4/model_control.hpp"
#include "dw4/model_subcommands.hpp"
#include "dw4/model_wait.hpp"

#include <functional>
#include <memory>
#include <variant>
#include <vector>

namespace dw4 {
class ModelSceneSequence;
class ModelSceneCommand;
struct ModelHitboxCommand {
    std::uint32_t reference_index{};
    std::string name; // Serialized null becomes the original non-null empty name.
    ModelHitboxSubcommand parameters;
};
enum class ModelSequenceOpcode { model, label, label_control, action, jump, wait_check, hitbox, lua_call, unsupported };
struct ModelSequenceCommand {
    std::uint32_t reference_index{};
    std::string type, name;
    ModelSequenceOpcode opcode = ModelSequenceOpcode::unsupported;
    std::optional<ModelCommand> model_command;
    std::optional<std::string> label_sequence; // LABELCTRL's nullable field 3.
    std::optional<ModelHitboxCommand> hitbox_command;
    std::optional<std::string> lua_mode; // LUACALL's nullable field 3; empty differs from null.
};
struct ModelSequenceData {
    std::uint32_t reference_index{};
    std::string name;
    std::vector<ModelSequenceCommand> commands; // Ordered occurrences, including aliases.
};
// One work+18 reference. Prepared records and mutable loaded graph records are
// different profiles; assigning either replaces the other, including null.
class ModelSequenceDataOwner {
public:
    ModelSequenceDataOwner()=default;
    ModelSequenceDataOwner(std::shared_ptr<const ModelSequenceData> value):value_(std::move(value)) {}
    ModelSequenceDataOwner(std::shared_ptr<ModelSceneSequence> value):value_(std::move(value)) {}
    std::shared_ptr<const ModelSequenceData> prepared_owner() const;
    std::shared_ptr<ModelSceneSequence> actual_owner() const;
    explicit operator bool() const noexcept;
    void reset() noexcept {value_=std::shared_ptr<const ModelSequenceData>{};}
    // Compatibility for explicitly prepared callers. An actual owner is never
    // projected into a detached prepared command vector.
    operator std::shared_ptr<const ModelSequenceData>() const {return prepared_owner();}
    const ModelSequenceData* get() const {return prepared_owner().get();}
    const ModelSequenceData* operator->() const {return get();}
    const ModelSequenceData& operator*() const {return *prepared_owner();}
    template<class T> bool owner_before(const std::shared_ptr<T>& other) const {
        return std::visit([&](const auto& owner){return owner.owner_before(other);},value_);
    }
    bool operator==(const ModelSequenceDataOwner&) const=default;
private:
    std::variant<std::shared_ptr<const ModelSequenceData>,std::shared_ptr<ModelSceneSequence>> value_;
};
class ModelSequenceProgram {
public:
    const std::string& source() const noexcept { return source_; }
    std::uint32_t reference_index() const noexcept { return reference_; }
    const std::vector<ModelSequenceData>& sequences() const noexcept { return sequences_; }
private:
    friend ModelSequenceProgram decode_model_sequences(const IgbDocument&,std::uint32_t,std::string);
    std::string source_;
    std::uint32_t reference_{};
    std::vector<ModelSequenceData> sequences_;
};
// One exact, explicitly supplied beModelCtrlInfoDataSeq named SEQ. Owns decoded
// commands and source identity; no references borrow the IGB document's lifetime.
// Unsupported command types are retained and stop execution when reached.
ModelSequenceProgram decode_model_sequences(const IgbDocument& document,
    std::uint32_t sequence_reference,std::string source);

struct ModelSequenceLabel {
    std::optional<std::string> sequence; // Null and empty interned strings differ.
    std::optional<std::string> label; // SETLABEL preserves null; LABELCTRL supplies non-null names.
    std::int32_t cached_offset = -1;
    bool operator==(const ModelSequenceLabel&) const = default;
};
// Actual nullable work+24/+28/+2C references. The lists own the authoritative
// ordered values; ModelSequenceLabel is only a detached inspection snapshot.
// A future complete work owner can embed these fields and supply an aliasing
// shared_ptr to the runtime. No list is allocated until the first SETLABEL.
struct ModelSequenceLabelFields {
    using StringList=std::vector<std::optional<std::string>>;
    using OffsetList=std::vector<std::int32_t>;
    std::shared_ptr<StringList> sequences,names;
    std::shared_ptr<OffsetList> offsets;
};
class ModelSequenceLabels {
public:
    explicit ModelSequenceLabels(std::shared_ptr<ModelSequenceLabelFields> fields);
    ModelSequenceLabels(const ModelSequenceLabels&);
    ModelSequenceLabels(ModelSequenceLabels&&) noexcept = default;
    // Value publication keeps the destination's fields and existing list owners.
    ModelSequenceLabels& operator=(const ModelSequenceLabels&);
    ModelSequenceLabels& operator=(ModelSequenceLabels&&);
    std::vector<ModelSequenceLabel> snapshot() const;
    std::shared_ptr<ModelSequenceLabelFields> fields() const noexcept {return fields_;}
    void set(std::optional<std::string> sequence,std::optional<std::string> label);
    void cache(std::size_t index,std::int32_t offset);
private:
    std::shared_ptr<ModelSequenceLabelFields> fields_;
    void publish(const std::vector<ModelSequenceLabel>& rows,bool allocated);
};
// Canonical work+14..2C and wait/player fields. The host label-field grouping
// owns the three actual nullable guest list references; it is not a guest list.
// command_data retains either a qualified immutable prepared record or the
// exact mutable loaded graph owner. A program index or copied command record
// never substitutes for that owner. Null name and null data are independent.
struct ModelSequenceExecutorLease;
struct ModelSequenceFields {
    ModelSequenceFields()=default;
    ModelSequenceFields(const ModelSequenceFields&)=delete;
    ModelSequenceFields& operator=(const ModelSequenceFields&)=delete;
    ModelSequenceFields(ModelSequenceFields&&)=delete;
    ModelSequenceFields& operator=(ModelSequenceFields&&)=delete;
    std::optional<std::string> name;
    ModelSequenceDataOwner command_data;
    std::int32_t command_index{},label_count{};
    std::shared_ptr<ModelSequenceLabelFields> labels=std::make_shared<ModelSequenceLabelFields>();
    ModelWaitState wait;
private:
    friend class ModelSequenceRuntime;
    friend class ModelSequenceActualFrame;
    // Host lifetime metadata only: never another guest field or a strong
    // back-reference to the facade/work. One executor owns service continuation.
    std::weak_ptr<ModelSequenceExecutorLease> executor_;
    bool executor_unavailable_{};
};
enum class ModelSequenceStop { end, label_boundary, action_required, controller_required, unsupported_command, sequence_cleanup_required, waiting, hitbox_required, lua_required, jump_completed };
struct ModelSequenceResult {
    ModelSequenceStop stop{};
    std::uint32_t commands_executed{};
    std::optional<std::uint32_t> command_reference;
    std::string type, name;
};
struct ModelActionRequest {
    std::string source;
    std::uint32_t command_reference{};
    std::string info_name;
    std::int32_t ram_no{};
    std::string action; // Original ACTION category payload, not an animation clip.
};
using ModelActionDispatch = std::function<void(const ModelActionRequest&)>;
// False retains the ACTION cursor. Any externally completed dispatch prefix
// must be owned by the supplied service and resumed, never repeated on retry.
using ModelActionTryDispatch = std::function<bool(const ModelActionRequest&)>;
using ModelHitboxDispatch = std::function<bool(const ModelHitboxCommand&)>;
struct ModelLuaRequest {
    std::string source;
    std::optional<std::uint32_t> command_reference; // Null for runtime-generated collision callbacks.
    std::string info_name;
    std::int32_t ram_no{};
    std::string script_prefix; // Original slot2 null becomes non-null empty.
    std::optional<std::string> event; // Original nullable modeName, passed unchanged.
};
// Immediate LUACALL requires the real Lua/context owner. False retains the
// command cursor; services own any completed external prefix across retries.
// An exception has an uncertain external outcome and permanently faults this
// sequence owner; no later execution, selection or mutable state access is allowed.
using ModelLuaDispatch = std::function<bool(const ModelLuaRequest&)>;
// Optional owner hook for ordinary local jumps. The destination and early
// control resets are already visible. False retains that pending selection;
// no destination command runs until this same cleanup completes. The hook
// must not mutate this sequence runtime.
using ModelSequenceCleanup = std::function<bool(std::string_view)>;

struct ModelSequenceIdentity {std::string source,info_name;std::int32_t ram_no{};};
struct ModelSequenceActualServices {
    // Read the captured parent and actual RAM at the command's original site.
    // The resulting request is retained across a suspended service invocation.
    std::function<ModelSequenceIdentity()> identity;
    ModelActionTryDispatch action;
    ModelHitboxDispatch hitbox;
    ModelLuaDispatch lua;
    std::function<bool(std::string_view)> jump;
};
// One original +8C ordinary-entry/command-loop invocation, not another work
// executor. Root invocations retain these across cleanup and service suspension.
// A direct JUMP may enter another frame on the same canonical executor.
class ModelSequenceActualFrame {
public:
    ~ModelSequenceActualFrame();
    ModelSequenceActualFrame(const ModelSequenceActualFrame&)=delete;
    ModelSequenceActualFrame& operator=(const ModelSequenceActualFrame&)=delete;
    bool selection_pending() const noexcept {return selection_pending_;}
    bool completed() const noexcept {return completed_;}
    bool faulted() const noexcept {return faulted_;}
private:
    friend class ModelSequenceRuntime;
    ModelSequenceActualFrame()=default;
    std::shared_ptr<ModelSequenceExecutorLease> lease_;
    std::weak_ptr<ModelSequenceFields> fields_;
    std::weak_ptr<ModelSequenceActualFrame> parent_;
    std::shared_ptr<ModelSceneSequence> data_;
    std::shared_ptr<ModelSceneCommand> raw_command_;
    std::optional<ModelSequenceCommand> command_;
    std::optional<ModelSequenceIdentity> identity_;
    std::int32_t cursor_{};
    bool began_{},selection_pending_{},loop_started_{},completed_{},faulted_{};
};

// Recovered base-model command runner on caller-prepared work. Owns its program,
// label cache, command cursor and supported control state. This is not complete
// player construction or the outer controller's virtual dispatch/frame loop.
class ModelSequenceRuntime {
public:
    ModelSequenceRuntime(ModelSequenceProgram program,ModelControlState model,
        std::string info_name,std::int32_t ram_no);
    ModelSequenceRuntime(ModelSequenceProgram program,ModelControlState model,
        std::string info_name,std::int32_t ram_no,std::shared_ptr<ModelSequenceLabelFields> labels);
    // Retains the exact canonical fields and immutable program. Exactly one
    // executor may bind a field generation. The caller must retain that facade
    // while its external services (including its separate wait pass) are pending.
    ModelSequenceRuntime(std::shared_ptr<const ModelSequenceProgram> program,ModelControlState model,
        std::string info_name,std::int32_t ram_no,std::shared_ptr<ModelSequenceFields> fields);
    // Actual profile: the selected mutable graph owner is supplied at each
    // ordinary entry. No fixed whole-program lookup or prepared timer is used.
    ModelSequenceRuntime(ModelControlState actual_model,std::shared_ptr<ModelSequenceFields> fields);
    std::shared_ptr<ModelSequenceActualFrame> make_actual_frame();
    // A later original wait-resume invokes C3AC without ordinary-entry resets.
    std::shared_ptr<ModelSequenceActualFrame> make_actual_resume_frame();
    void begin_actual_select(const std::shared_ptr<ModelSequenceActualFrame>&,
        std::optional<std::string> name,std::shared_ptr<ModelSceneSequence> selected);
    void finish_actual_select(const std::shared_ptr<ModelSequenceActualFrame>&);
    ModelSequenceResult run_actual(const std::shared_ptr<ModelSequenceActualFrame>&,
        const ModelSequenceActualServices& services={});
    // Healthy prepared copies detach mutable values. A pending/executing/faulted
    // copy is inspection-only; it cannot resume another external continuation.
    // Assignment publishes through the
    // destination's field/list owners; move construction transfers the binding.
    // Assignment cannot abandon an executing, pending or faulted destination.
    ModelSequenceRuntime(const ModelSequenceRuntime&);
    ModelSequenceRuntime(ModelSequenceRuntime&&);
    ~ModelSequenceRuntime();
    ModelSequenceRuntime& operator=(const ModelSequenceRuntime&);
    ModelSequenceRuntime& operator=(ModelSequenceRuntime&&);
    std::shared_ptr<ModelSequenceFields> fields();
    std::shared_ptr<const ModelSequenceFields> fields() const noexcept {return fields_;}
    std::shared_ptr<const ModelSequenceProgram> program_owner() const noexcept {return program_;}
    ModelControlState& model();
    const ModelControlState& model() const noexcept { return model_; }
    ModelWaitState& wait_state();
    const ModelWaitState& wait_state() const;
    std::string_view current_sequence() const noexcept;
    bool selected() const noexcept { return fields_ && bool(fields_->command_data); }
    std::int32_t command_index() const;
    std::int32_t label_count() const;
    std::vector<ModelSequenceLabel> labels() const;
    std::shared_ptr<ModelSequenceLabelFields> label_fields();
    std::shared_ptr<const ModelSequenceLabelFields> label_fields() const;
    bool selection_pending() const noexcept {
        const auto frame=actual_leaf_.lock();return selection_pending_ || (frame && frame->selection_pending_);
    }
    bool jump_pending() const noexcept { return pending_jump_.has_value(); }
    bool lua_pending() const noexcept { return lua_pending_; }
    bool lua_faulted() const noexcept { return lua_faulted_; }
    bool external_pending() const noexcept {
        const auto frame=actual_leaf_.lock();
        return action_pending_ || hitbox_pending_ || lua_pending_ || (frame && frame->loop_started_ && !frame->completed_);
    }
    // 8030B014: replace the first sequence-key entry or append; invalidate offset.
    void set_label(std::optional<std::string> sequence,std::optional<std::string> label);
    // Owned selection/control/motion effects of ordinary sequence entry only.
    // Required outer subcommand, collision, wait resolution and virtual effects
    // must be implemented by the caller; this does not mark those as complete.
    // Missing sequences/labels and invalid motion stop parameters are atomic.
    void select(std::string_view sequence);
    // 8030E978..8030E9C4: validate the complete prepared selection, then
    // publish destination/cursor/label count and CTRL/PDMOVE/wait-name resets
    // before the owner's subcommand cleanup. Channels and check_fall remain
    // live; wait scalar fields are preserved. Mutable model/wait access, labels
    // and another selection are blocked pending finish. Other full-work fields
    // and callbacks remain external.
    void begin_select(std::string_view sequence);
    // 8030E9DC..8030EA74: after cleanup, clear BHit/WHit/FHit script names,
    // enable wall/floor/fall checks, clamp strictly negative invulnerability
    // to +0, then apply sequence stops to the current channels. A pending local
    // jump can only finish through run(), so a missing cleanup hook cannot be
    // bypassed here.
    void finish_select();
    // 8030C3AC. Successful command effects are retained as an ordered prefix.
    // Missing ACTION dispatch stops BEFORE consuming the action. A supplied
    // dispatcher must perform the real effect or throw; logging is not playback.
    // try_action takes precedence when supplied; false suspends before consuming
    // ACTION so the owning service can retain a partially completed dispatch.
    // Ordinary local jumps retain nested state without restoring old cursors.
    // Erase/nonlocal commands stop for the outer controller. No automatic frame
    // resume is implied. WCHECK consumes its command and yields without querying
    // its condition. An engaged wait name (including empty) blocks another run;
    // the owning wait resolver must clear it before calling run to resume the
    // retained cursor. Per-player callbacks and wait resolution remain external.
    // HITBOX Set/AllClear and immediate LUACALL require their owning services;
    // missing/false callbacks stop before consuming the command. Exact LUACALL
    // BHit/WHit/FHit modes only assign owned callback prefixes locally.
    // Once ACTION, HITBOX or Lua is invoked and remains incomplete, only run
    // may resume it; selection/mutable access cannot abandon its external prefix.
    // The optional pure cleanup preflight runs at each newly visited local JUMP,
    // immediately before publishing its selection. It may reject an unavailable
    // external cleanup owner without entering that selection. Retrying an
    // already-entered jump does not invoke this validation again.
    ModelSequenceResult run(std::uint64_t world_time,const ModelActionDispatch& action = {},
        const ModelSequenceCleanup& cleanup = {},const ModelActionTryDispatch& try_action = {},
        const ModelHitboxDispatch& hitbox = {},const ModelLuaDispatch& lua = {},
        const std::function<void()>& cleanup_preflight = {});
private:
    std::shared_ptr<const ModelSequenceProgram> program_;
    ModelControlState model_;
    std::shared_ptr<ModelSequenceFields> fields_;
    std::shared_ptr<ModelSequenceExecutorLease> executor_lease_;
    std::string info_name_;
    std::int32_t ram_no_{};
    struct PendingJump {
        std::uint32_t reference{};
        std::string type,name;
        bool cleanup_complete{};
    };
    std::optional<PendingJump> pending_jump_;
    bool running_{},selection_pending_{},lua_faulted_{},lua_pending_{},action_pending_{},hitbox_pending_{};
    bool actual_profile_{},actual_jump_window_{};
    std::weak_ptr<ModelSequenceActualFrame> actual_leaf_;
    struct SelectionPlan {
        std::string name;
        std::shared_ptr<const ModelSequenceData> data;
        std::int32_t cursor{};
        std::optional<std::size_t> cache_index;
        std::int32_t cache_offset{};
    };
    void require_fields() const;
    void acquire_executor();
    static std::shared_ptr<const ModelSequenceProgram> transfer_program(ModelSequenceRuntime&);
    const ModelSequenceData& current_data() const;
    void require_no_lua_fault() const;
    SelectionPlan prepare_selection(std::string_view sequence) const;
    void publish_selection(SelectionPlan&&,ModelControlState&&,bool pending);
    void complete_selection();
    void require_actual_frame(const std::shared_ptr<ModelSequenceActualFrame>&,bool nested=false) const;
    std::int32_t actual_label_offset() const;
};
} // namespace dw4
