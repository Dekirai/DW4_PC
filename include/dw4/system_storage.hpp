#pragma once

#include "dw4/model_scene.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
class SystemStorageError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Original beSystem fields, shared with its enclosing scheduled update.
struct SystemStorageState {
    std::int32_t dvd_status{},error_phase{}; // +54/+58, reflected zero defaults.
    std::uint8_t is_dvd_error{},is_now_freeze{},is_now_pad_enable{}; // +5C..5E.
    // Native continuation ownership, not original fields.
    std::weak_ptr<void> active_invocation;
    bool faulted{};
};
struct SystemStorageMessenger {
    std::shared_ptr<NativeMessenger> messenger;
    std::shared_ptr<MessengerServices> delivery;
    std::shared_ptr<ModelSceneContext> context; // Messenger +0C, read only when pausing.
};
// Captured viewer-manager +14 and, for restore only, +18 owners. Their
// unrecovered concrete operations remain explicit services; these handles
// preserve identity/lifetime and do not stand in for implemented scene code.
struct SystemStorageViewer {
    std::shared_ptr<void> scene_info,renderer;
};
struct SystemStorageValueResult {
    bool completed{};
    std::int32_t value{};
    std::string diagnostic;
};
template<class T> struct SystemStorageOwnerResult {
    bool completed{};
    std::shared_ptr<T> owner;
    std::string diagnostic;
};
enum class SystemStorageOperation {
    set_world_factor,          // 8028B054, value=0 or 1.
    set_managers_enabled,      // Original registered-manager type sweep, value=0/1.
    save_viewer_children,      // 80316C08..78, append then remove each first child.
    register_error_infos,      // 80316C88..AC, live +4C list and context lookup.
    select_error_switch,       // 80316CC0..D4C, original first-child/type path.
    clear_error_switch,        // 80206760; selected target retained separately.
    set_error_switch_bit,      // 802066A8, value=original selected bit.
    remove_error_children,     // 80316E18..74.
    restore_viewer_children    // 80316F58..FD4, includes reached 8040B994 rebuild.
};
struct SystemStorageOperationRequest {
    SystemStorageOperation operation{};
    std::shared_ptr<ModelSceneSystem> system;
    std::shared_ptr<ModelSceneContext> context; // Captured r3 for set_world_factor.
    std::shared_ptr<SystemStorageViewer> viewer;
    std::shared_ptr<void> target; // Retained select_error_switch result.
    std::int32_t value{};
};
struct SystemStorageOperationResult {
    bool completed{};
    std::shared_ptr<void> owner; // Only select_error_switch publishes an owner.
    std::string diagnostic;
};
using SystemStorageCall=std::function<SystemStorageOperationResult()>;
struct SystemStorageServices {
    // Calls that suspend retain their selected function until completion.
    std::function<SystemStorageValueResult()> drive_status; // Original DVDGetDriveStatus.
    std::function<SystemStorageOwnerResult<SystemStorageMessenger>(const std::shared_ptr<ModelSceneContext>&)> messenger;
    // false captures +14 only; true captures +14 and +18 before messenger lookup.
    std::function<SystemStorageOwnerResult<SystemStorageViewer>(const std::shared_ptr<ModelSceneContext>&,bool)> viewer;
    // Binding must have no operation effects. An empty result means unavailable;
    // a returned call owns every partial effect of its individual boundary.
    std::function<SystemStorageCall(const SystemStorageOperationRequest&)> operation;
};

// Original 80316974. The state may alias a field in the retained system owner.
// Completion value is its boolean return, not a generic success indicator.
// A completed error phase can return zero; missing services return incomplete.
class SystemStorageInvocation {
public:
    SystemStorageInvocation(std::shared_ptr<ModelSceneSystem>,std::shared_ptr<SystemStorageState>);
    ~SystemStorageInvocation();
    SystemStorageInvocation(const SystemStorageInvocation&)=delete;
    SystemStorageInvocation& operator=(const SystemStorageInvocation&)=delete;
    MessengerCallResult resume(const std::shared_ptr<SystemStorageServices>& services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
