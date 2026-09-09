#pragma once

#include "dw4/model_control.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace dw4 {
enum class ModelSequenceOpcode { model, label, label_control, action, jump, unsupported };
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
enum class ModelSequenceStop { end, label_boundary, action_required, controller_required, unsupported_command };
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

// Recovered base-model command runner on caller-prepared work. Owns its program,
// label cache, command cursor and supported control state. This is not complete
// player construction or the outer controller's virtual dispatch/frame loop.
class ModelSequenceRuntime {
public:
    ModelSequenceRuntime(ModelSequenceProgram program,ModelControlState model,
        std::string info_name,std::int32_t ram_no);
    ModelControlState& model() noexcept { return model_; }
    const ModelControlState& model() const noexcept { return model_; }
    std::string_view current_sequence() const noexcept;
    std::uint32_t command_index() const noexcept { return cursor_; }
    std::uint32_t label_count() const noexcept { return label_count_; }
    const std::vector<ModelSequenceLabel>& labels() const noexcept { return labels_; }
    // 8030B014: replace the first sequence-key entry or append; invalidate offset.
    void set_label(std::optional<std::string> sequence,std::string label);
    // Owned selection/control/motion effects of ordinary sequence entry only.
    // Required outer subcommand, collision, wait/callback and virtual effects
    // must be implemented by the caller; this does not mark those as complete.
    // Missing sequences/labels and invalid motion stop parameters are atomic.
    void select(std::string_view sequence);
    // 8030C3AC. Successful command effects are retained as an ordered prefix.
    // Missing ACTION dispatch stops BEFORE consuming the action. A supplied
    // dispatcher must perform the real effect or throw; logging is not playback.
    // Ordinary local jumps retain nested state without restoring old cursors.
    // Erase/nonlocal commands stop for the outer controller. No automatic frame
    // resume is implied; original wait checks and per-player callbacks still apply.
    ModelSequenceResult run(std::uint64_t world_time,const ModelActionDispatch& action = {});
private:
    std::shared_ptr<const ModelSequenceProgram> program_;
    ModelControlState model_;
    std::string info_name_;
    std::int32_t ram_no_{};
    std::vector<ModelSequenceLabel> labels_;
    std::optional<std::size_t> current_;
    std::uint32_t cursor_{},label_count_{};
    bool running_{};
};
} // namespace dw4
