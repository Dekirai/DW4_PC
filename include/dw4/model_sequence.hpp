#pragma once

#include "dw4/model_control.hpp"
#include "dw4/model_wait.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace dw4 {
enum class ModelSequenceOpcode { model, label, label_control, action, jump, wait_check, unsupported };
struct ModelSequenceCommand {
    std::uint32_t reference_index{};
    std::string type, name;
    ModelSequenceOpcode opcode = ModelSequenceOpcode::unsupported;
    std::optional<ModelCommand> model_command;
    std::optional<std::string> label_sequence; // LABELCTRL's nullable field 3.
};
struct ModelSequenceData {
    std::uint32_t reference_index{};
    std::string name;
    std::vector<ModelSequenceCommand> commands; // Ordered occurrences, including aliases.
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
    std::string label;
    std::int32_t cached_offset = -1;
    bool operator==(const ModelSequenceLabel&) const = default;
};
enum class ModelSequenceStop { end, label_boundary, action_required, controller_required, unsupported_command, sequence_cleanup_required, waiting };
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
// Optional owner hook for ordinary local jumps. The destination and early
// control resets are already visible. False retains that pending selection;
// no destination command runs until this same cleanup completes. The hook
// must not mutate this sequence runtime.
using ModelSequenceCleanup = std::function<bool(std::string_view)>;

// Recovered base-model command runner on caller-prepared work. Owns its program,
// label cache, command cursor and supported control state. This is not complete
// player construction or the outer controller's virtual dispatch/frame loop.
class ModelSequenceRuntime {
public:
    ModelSequenceRuntime(ModelSequenceProgram program,ModelControlState model,
        std::string info_name,std::int32_t ram_no);
    ModelControlState& model();
    const ModelControlState& model() const noexcept { return model_; }
    ModelWaitState& wait_state();
    const ModelWaitState& wait_state() const noexcept { return wait_; }
    std::string_view current_sequence() const noexcept;
    bool selected() const noexcept { return current_.has_value(); }
    std::uint32_t command_index() const noexcept { return cursor_; }
    std::uint32_t label_count() const noexcept { return label_count_; }
    const std::vector<ModelSequenceLabel>& labels() const noexcept { return labels_; }
    bool selection_pending() const noexcept { return selection_pending_; }
    bool jump_pending() const noexcept { return pending_jump_.has_value(); }
    // 8030B014: replace the first sequence-key entry or append; invalidate offset.
    void set_label(std::optional<std::string> sequence,std::string label);
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
    // 8030EA08..8030EA74: after cleanup, set check_fall and apply sequence
    // stops to the current channels. A pending local jump can only finish
    // through run(), so a missing cleanup hook cannot be bypassed here.
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
    ModelSequenceResult run(std::uint64_t world_time,const ModelActionDispatch& action = {},
        const ModelSequenceCleanup& cleanup = {},const ModelActionTryDispatch& try_action = {});
private:
    std::shared_ptr<const ModelSequenceProgram> program_;
    ModelControlState model_;
    ModelWaitState wait_;
    std::string info_name_;
    std::int32_t ram_no_{};
    std::vector<ModelSequenceLabel> labels_;
    std::optional<std::size_t> current_;
    std::uint32_t cursor_{},label_count_{};
    struct PendingJump {
        std::uint32_t reference{};
        std::string type,name;
        bool cleanup_complete{};
    };
    std::optional<PendingJump> pending_jump_;
    bool running_{},selection_pending_{};
    void complete_selection();
};
} // namespace dw4
