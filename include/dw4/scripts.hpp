#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4 {
class IgbDocument;
class ScriptError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
struct EmbeddedScriptInfo {
    std::uint32_t info_reference{};
    std::uint32_t info_export_index{};
    std::uint32_t data_index{};
    std::uint32_t data_reference{0xffffffffu};
    std::uint32_t data_object_reference{0xffffffffu};
    std::uint32_t memory_reference{0xffffffffu};
    std::string info_name;
    std::string name;
    std::uint32_t byte_size{};
    bool name_valid{}; // False means selection cannot safely skip this record.
    bool supported{}; // Valid embedded bytes; not bytecode execution support.
    std::string diagnostic;
    std::string provenance;
};
struct EmbeddedScript {
    EmbeddedScriptInfo info;
    std::vector<std::uint8_t> bytes;
};
// No pointer is retained in a plan. Input order is explicit registry order,
// not an automatically inferred package dependency or load order.
struct StartupSource {
    const IgbDocument* document{};
    std::string label;
};
enum class StartupPhaseKind { functions, boot, start, first };
enum class StartupBarrierKind {
    package_members_loaded, lua_infos_registered, lua_state_reset,
    common_resources_loaded, deferred_infos_registered, layers_ordered,
    text_initialized, stage_enabled
};
struct StartupNativeBarrier {
    StartupBarrierKind kind{};
    std::string description;
};
struct StartupScriptCall {
    std::uint32_t source_index{};
    EmbeddedScriptInfo script;
};
struct StartupScriptPhase {
    StartupPhaseKind kind{};
    std::string filename;
    std::vector<StartupNativeBarrier> native_prerequisites;
    std::vector<StartupScriptCall> calls;
    bool lookup_complete{}; // Does not mark native prerequisites satisfied.
    std::vector<std::string> diagnostics;
};
struct StartupPlan {
    std::vector<std::string> source_labels;
    std::vector<StartupScriptPhase> phases;
    bool embedded_lookup_complete{}; // Never a claim that startup executed.
};

// Preserves explicit info-export/data-list order and duplicate/shared records.
// Malformed records remain unsupported rows; malformed ownership lists throw.
std::vector<EmbeddedScriptInfo> list_igb_scripts(const IgbDocument& document);
// Returns independent bytes for an exact exported info and data-list ordinal.
EmbeddedScript read_igb_script(const IgbDocument& document,
    std::uint32_t info_reference, std::uint32_t data_index);
// Resolves func/boot/start/first using the first matching name in EACH info,
// then continues with the next info/source. A bad first match is never replaced
// by a later duplicate. Missing embedded matches have no filesystem fallback.
StartupPlan build_igb_startup_plan(std::span<const StartupSource> sources);
} // namespace dw4
