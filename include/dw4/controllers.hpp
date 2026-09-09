#pragma once

#include "dw4/startup_loading.hpp"

#include <array>
#include <optional>

namespace dw4 {
enum class ControllerKind { static_info, switch_control };
struct ControllerPrerequisites {
    bool system_prepared{};
    bool messenger_prepared{};
};
struct ControllerObjectId {
    std::uint32_t file_index{}, reference_index{};
    bool operator==(const ControllerObjectId&) const = default;
};
struct ControllerRamState {
    ControllerObjectId object;
    std::int32_t ram_no{};
    ControllerObjectId parent_info;
    ControllerKind parent_manager{};
    bool is_run{}; // Stored _isRun is preserved by the registration callback.
    std::array<std::int32_t,4> state{}; // Native _st0.._st3, reset by registration.
    std::optional<ControllerObjectId> data_list; // Preserved, never parsed as child RAMs.
};
struct ControllerManagerState {
    ControllerKind kind{};
    std::vector<StartupInfoAddress> accepted_infos;
    std::vector<ControllerObjectId> static_data_infos; // Static callback appends once per RAM occurrence.
    bool is_update_run{};
};
struct ControllerRecipient {
    std::string category, name;
    std::int32_t instance_index{}; // Captured registration key, independent of later RAM alias writes.
    std::uint32_t ram_index{}; // Index into the owned, unique RAM state array.
};
struct ControllerCategory {
    std::string name;
    std::vector<std::uint32_t> recipient_indices; // Ordered rows in recipients; no deduplication.
};
struct ControllerRegistrationStep {
    std::uint32_t deferred_index{};
    StartupInfoAddress source;
    std::string type, name;
    std::vector<std::uint32_t> ram_indices; // Ordered occurrences; duplicates are significant.
    bool completed{};
    std::string diagnostic;
};
struct ControllerInitialization {
    std::vector<ControllerManagerState> managers;
    std::vector<ControllerRamState> rams;
    std::vector<ControllerRecipient> recipients;
    std::vector<ControllerCategory> categories;
    std::vector<std::string> active_managers; // Fresh inspection messenger, followed by activated controllers.
    std::vector<StartupInfoAddress> registered_infos;
    std::vector<ControllerRegistrationStep> steps;
    bool completed{}; // Only the explicitly selected callbacks, never whole startup.
    bool ordered_prefix{};
    std::optional<std::uint32_t> stopped_deferred_index;
    std::string diagnostic;
    std::string next_barrier;
};

// Inspect actual supported callbacks in a fresh native context with the required
// system/messenger dependencies supplied and controller links prepared. This does
// not run original manager construction, the loading-screen gate or later phases.
// Selections are explicit deferred-list ordinals, visited in supplied order.
// Unknown handlers and invalid data stop before publishing that step's effects.
// All returned state is owned; source destruction cannot leave borrowed pointers.
ControllerInitialization initialize_controller_callbacks(std::span<const StartupSource> sources,
    std::span<const StartupInfoAddress> deferred_infos, std::span<const std::uint32_t> selected_indices,
    ControllerPrerequisites prerequisites);
ControllerInitialization initialize_controller_callbacks(const StartupSession& session,
    std::span<const std::uint32_t> selected_indices, ControllerPrerequisites prerequisites);
// Visit every deferred occurrence in order, stopping at the first unsupported
// or malformed callback. Completed effects survive; no later handler is skipped.
// Supplied prerequisites describe an explicitly prepared fresh native context,
// not proof that original manager construction or loading-screen gates ran.
// Limits: 128 sources, 4,096 exports/source, 65,536 deferred rows/RAM visits,
// 256 explicit selections and 8 MiB combined copied/serialized name text.
// The session adapter inspects its aggregate routes. Original /s initialization
// precedes func/boot; these aggregate routes are not the original state-5 queue.
ControllerInitialization initialize_controller_prefix(std::span<const StartupSource> sources,
    std::span<const StartupInfoAddress> deferred_infos, ControllerPrerequisites prerequisites);
ControllerInitialization initialize_controller_prefix(const StartupSession& session,
    ControllerPrerequisites prerequisites);
} // namespace dw4
