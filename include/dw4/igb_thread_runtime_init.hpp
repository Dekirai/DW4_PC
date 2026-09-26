#pragma once

#include "dw4/igb_platform_thread_registration.hpp"
#include "dw4/igb_platform_semaphore_registration.hpp"
#include "dw4/igb_thread_dependencies.hpp"
#include "dw4/igb_object_field_construct.hpp"

namespace dw4 {
class IgbThreadRuntimeInitError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbThreadRuntimeInitCall {
    IgbObjectInstance object;
    std::uint32_t vtable_word{},virtual_slot{};
    // Original755C0 passes a four-byte stack-result destination inr3 and
    // the semaphore receiver inr4. Unknown bytes are not zero defaults.
    std::shared_ptr<IgbScratchBuffer> result;
};
struct IgbThreadRuntimeInitOperation {
    std::function<MessengerCallResult()> resume;
    // Optional native continuation with a borrowed guard for this resume only.
    // Never retain the enclosing callback in the selected operation.
    std::function<MessengerCallResult(const std::function<void()>&)> resume_checked;
};
using IgbThreadRuntimeInitFactory=std::function<std::shared_ptr<IgbThreadRuntimeInitOperation>(const IgbThreadRuntimeInitCall&)>;
struct IgbThreadRuntimeInitServices {
    std::shared_ptr<IgbPlatformThreadRegistrationServices> manager_registration;
    std::shared_ptr<IgbThreadDependenciesServices> dependencies;
    std::shared_ptr<IgbObjectFieldConstructServices> objects;
    IgbThreadRuntimeInitFactory factory;
    std::vector<std::shared_ptr<void>> retained;
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> semaphore_registration;
};
// Actual755C0: capture manager+0C, resolve its physical owner, load that
// semaphore's vtable and call+5C with the explicit hidden result destination.
// The returned status is ignored; no ownership adjustment occurs here.
class IgbThreadManagerInitInvocation {
public:
    explicit IgbThreadManagerInitInvocation(IgbObjectInstance);
    ~IgbThreadManagerInitInvocation();
    IgbThreadManagerInitInvocation(const IgbThreadManagerInitInvocation&)=delete;
    IgbThreadManagerInitInvocation& operator=(const IgbThreadManagerInitInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbThreadRuntimeInitServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Exact manager804700A8, thread-list80476CE0 and semaphore80470314 reflected
// init slots only. Metadata getters933F0/94128/93530 are plain fresh loads. Verified
// BLR slots remain distinct from manager+2C and all other runtime operations.
IgbObjectInitFactory make_native_igb_thread_runtime_init_factory(
    std::shared_ptr<IgbThreadRuntimeInitServices>,IgbObjectInitFactory remaining={});
} // namespace dw4
