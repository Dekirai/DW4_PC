#pragma once

#include "dw4/model_scene.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
class SystemStageError : public std::runtime_error {public: using std::runtime_error::runtime_error;};
using SystemStageString=std::shared_ptr<const std::string>;
struct SystemStageLoader;
struct LoaderDirectoryMutex;
struct LoaderDirectoryList;

// Reached igDirectory/igIGBFile fields only, supplied by an actual loader.
struct SystemStageDirectory {
    virtual ~SystemStageDirectory()=default;
    SystemStageString filename; // +14.
    std::shared_ptr<ModelSceneList> infos; // +1C, physical ordered exports.
    std::uint8_t loading{}; // +30; 80070130 returns true only for zero.
    std::weak_ptr<SystemStageLoader> loader; // +20, nonowning parent pointer.
    std::int32_t claim_count{}; // +34, separate from object references.
    std::shared_ptr<LoaderDirectoryMutex> mutex; // +44.
    std::uint32_t reference_word{1}; // Constructed owner +04, distinct from loading claims.
private:
    friend class LoaderDirectoryClaimInvocation;
    std::weak_ptr<void> active_claim_;
    bool claim_fault_{};
};
struct SystemStageLoader {
    SystemStageString primary_prefix,secondary_prefix; // +14/+18, nullable C strings.
    std::uint8_t option_08{},async_1c{},initialize_after_load_34{},format_44{};
    std::uint32_t option_48{};
    std::shared_ptr<ModelSceneObject> pool_38,queue_20;
    std::shared_ptr<ModelSceneObject> pool_3c,pool_40;
    std::shared_ptr<SystemStageDirectory> root_directory; // +0C, igImageDirectory.
    std::shared_ptr<LoaderDirectoryList> directories; // +10, ordered physical list.
private:
    friend class LoaderDirectoryReleaseInvocation;
    friend class LoaderDirectoryRequestInvocation;
    std::weak_ptr<void> active_request_;
    bool request_fault_{};
    std::vector<std::shared_ptr<ModelSceneObject>> unpublished_request_objects_;
    std::vector<std::shared_ptr<LoaderDirectoryMutex>> unpublished_request_mutexes_;
    std::weak_ptr<void> active_release_;
    bool release_fault_{};
    std::vector<std::shared_ptr<SystemStageDirectory>> unreleased_directories_;
};
struct SystemStageState {
    SystemStageState()=default;
    SystemStageState(const SystemStageState&)=delete;
    SystemStageState& operator=(const SystemStageState&)=delete;
    SystemStageString stage_name,next_stage; // beSystem +38/+3C.
    std::shared_ptr<SystemStageDirectory> boot_directory; // +44.
    bool faulted() const noexcept {return fault_;}
private:
    friend class SystemStageInvocation;
    std::weak_ptr<void> active_;
    bool fault_{};
    std::vector<std::shared_ptr<ModelSceneObject>> unpublished_objects_;
    std::vector<std::shared_ptr<SystemStageDirectory>> unpublished_directories_;
};

enum class SystemStageServiceKind {
    object_pool,          // 80068430(system), before testing the pool-mode byte.
    named_pool,           // 800607F4(selected live global pool handle).
    prepare_loading,      // 8006F794(captured live loader).
    load_directory,       // 8006FD34(captured live loader, current next_stage).
    create_string_pool,   // 80054140(16), conditional80053F28, before publication.
    register_info,        // 8028A7DC(captured live context, physical info).
    release_claim,        // 80070140(captured live loader, live directory name).
    finish_loading       // 8006F8CC(captured live loader), after clearing +44.
};
struct SystemStageRequest {
    SystemStageServiceKind kind{};
    std::shared_ptr<ModelSceneSystem> system;
    std::shared_ptr<SystemStageLoader> loader;
    std::shared_ptr<ModelSceneObject> handle,info;
    SystemStageString filename;
    std::shared_ptr<ModelSceneContext> context;
};
// A selected original operation keeps its arguments and continuation while
// pending. Typed result owners are read only when resume reports completion.
struct SystemStageOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<ModelSceneObject> object_result;
    std::shared_ptr<SystemStageDirectory> directory_result;
};
using SystemStageFactory=std::function<std::shared_ptr<SystemStageOperation>(const SystemStageRequest&)>;
struct SystemStageServices {
    std::shared_ptr<SystemStageLoader> loader; // Original singleton80562104.
    std::uint8_t use_object_pool{}; // Original global80562298.
    std::shared_ptr<ModelSceneObject> common_pool_handle,stage_pool_handle; // 80562258/25C.
    std::shared_ptr<ModelSceneObject> string_pool; // Singleton80562140.
    SystemStageFactory factory;
};

// Native caller80316368..803167B4 only. The state may alias a member of the
// same allocation as system; the base context is reloaded per registration.
// No-stage and still-loading branches complete this stage portion normally.
// Missing reached operations remain pending; they are never reported loaded.
class SystemStageInvocation {
public:
    SystemStageInvocation(std::shared_ptr<ModelSceneSystem>,std::shared_ptr<SystemStageState>);
    ~SystemStageInvocation();
    SystemStageInvocation(const SystemStageInvocation&)=delete;
    SystemStageInvocation& operator=(const SystemStageInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<SystemStageServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
