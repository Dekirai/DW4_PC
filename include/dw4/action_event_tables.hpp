#pragma once

#include "dw4/model_subcommands.hpp"

#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace dw4 {
class IgbDocument;
class ActionEventTableError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
enum class ActionEventConsumerKind { se, action_starter };
struct ActionEventSound {
    float delay{}; // Original frame delay, sign-extended from serialized short.
    std::optional<std::string> name;
};
using ActionEventPayload=std::variant<ActionEventSound,ModelEffectSubcommand>;
struct ActionEventActionRow {
    std::uint32_t reference{};
    std::string type;
    std::optional<std::string> name; // Null DATA keys match the empty event key.
    ActionEventPayload payload;
};
struct ActionEventModelRow {
    std::uint32_t reference{};
    std::optional<std::string> name;
    std::vector<ActionEventActionRow> actions; // Ordered occurrences, including duplicates.
};

// Immutable table data only. RAM occurrences retain aliases; this is not
// manager registration, messenger delivery or sound/effect execution.
class ActionEventTableProgram {
public:
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    std::uint32_t reference() const noexcept;
    const std::optional<std::string>& name() const noexcept;
    ActionEventConsumerKind kind() const noexcept;
    const std::vector<std::uint32_t>& rams() const noexcept;
    // One canonical physical object identity per raw RAM occurrence. Distinct
    // references resolving to the same value_index share the same owner.
    const std::vector<std::uint32_t>& ram_owners() const noexcept;
    const std::vector<ActionEventModelRow>& models() const noexcept;
private:
    struct Data;
    explicit ActionEventTableProgram(std::shared_ptr<const Data> data) : data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
    friend ActionEventTableProgram decode_action_event_tables(const IgbDocument&,
        std::uint32_t,std::string,std::uint32_t);
};
struct ActionEventTableMatch {
    std::size_t model_index{},action_index{};
    std::uint32_t model_reference{},action_reference{};
    ActionEventPayload payload; // Value-owned snapshot for an external continuation.
    bool has_rotation{}; // Exact Data2 sends a second vector; base sends only offset.
};

ActionEventTableProgram decode_action_event_tables(const IgbDocument& document,
    std::uint32_t info_reference,std::string source,std::uint32_t source_occurrence);
// 803130D4 chooses Se's LAST model row; 802F228C chooses ActionStarter's
// FIRST. Both then emit every exact matching action row in original order.
// Incoming ACTION model/action keys are non-null; DATA null uses empty.
std::vector<ActionEventTableMatch> match_action_event_tables(const ActionEventTableProgram& program,
    std::string_view model,std::string_view action);
} // namespace dw4
