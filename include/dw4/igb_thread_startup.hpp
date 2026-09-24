#pragma once

#include "dw4/igb_thread_registration.hpp"
#include "dw4/igb_file_factory.hpp"

namespace dw4 {
class IgbThreadStartupError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbThreadObjectKind {manager,thread};
enum class IgbThreadStartupServiceKind {pool_for_handle,create_manager,create_thread,object_virtual,destroy_object};
struct IgbThreadStartupCall {
    IgbThreadStartupServiceKind kind{};
    IgbBootstrapPointer object,argument;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t handle{},virtual_slot{},vtable_word{};
};
struct IgbThreadStartupOperation {
    std::function<MessengerCallResult()> resume;
    IgbBootstrapPointer object_result;
    std::shared_ptr<ModelSceneObject> pool_result;
};
using IgbThreadStartupFactory=std::function<std::shared_ptr<IgbThreadStartupOperation>(const IgbThreadStartupCall&)>;
struct IgbThreadStartupServices {
    std::optional<std::uint32_t> pool_handle; //805621F0; distinct from metadata handle805621F4.
    std::optional<IgbBootstrapPointer> manager; //805622F8; absent is unrepresented storage.
    std::shared_ptr<IgbRegistrationMetaSlot> manager_metadata,thread_metadata; //805614E8/80561504.
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFileFactoryServices> reflected;
    std::shared_ptr<IgbFilePoolsServices> pools;
    IgbThreadStartupFactory factory;
    // Raw singleton replacement does not release the displaced original object.
    // Abandoned continuations likewise must not invent guest releases.
    std::vector<IgbBootstrapPointer> retained;
};
// Only the proven800947E8 store inside800946A0; not pool construction.
void publish_original_igb_thread_pool_handle(IgbThreadStartupServices&) noexcept;
// Actual2380C/23B90: always66188(body), fresh metadata, then6546C with
// the captured incoming pool. A returned pointer retains the actual typed
// allocation binding; this interface does not manufacture runtime objects.
class IgbThreadObjectFactoryInvocation {
public:
    IgbThreadObjectFactoryInvocation(IgbThreadObjectKind,std::shared_ptr<ModelSceneObject> pool);
    ~IgbThreadObjectFactoryInvocation();
    IgbThreadObjectFactoryInvocation(const IgbThreadObjectFactoryInvocation&)=delete;
    IgbThreadObjectFactoryInvocation& operator=(const IgbThreadObjectFactoryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbThreadStartupServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    IgbBootstrapPointer result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual23A30 ->755F8. Concrete thread initialization, manager list/semaphore
// operations and destruction remain explicit reached calls, never BLR stubs.
class IgbThreadStartupInvocation {
public:
    IgbThreadStartupInvocation();~IgbThreadStartupInvocation();
    IgbThreadStartupInvocation(const IgbThreadStartupInvocation&)=delete;
    IgbThreadStartupInvocation& operator=(const IgbThreadStartupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbThreadStartupServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Binds only607F4 and the two actual factory callers; virtuals/destruction
// delegate. Registration callbacks and reflected initialization stay real.
IgbThreadStartupFactory make_native_igb_thread_startup_service_factory(
    std::shared_ptr<IgbThreadStartupServices>,IgbThreadStartupFactory remaining={});
IgbRegistrationCallbacksFactory make_native_igb_thread_startup_function_factory(
    std::shared_ptr<IgbThreadStartupServices>,IgbRegistrationCallbacksFactory remaining={});
} // namespace dw4
