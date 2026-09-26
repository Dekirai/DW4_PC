#pragma once

#include "dw4/igb_gamecube_thread_registration.hpp"
#include "dw4/igb_thread_runtime_init.hpp"
#include "dw4/igb_memory_field_construct.hpp"

namespace dw4 {
class IgbGamecubeThreadInitError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbGamecubeThreadInitServices {
    std::shared_ptr<IgbGamecubeThreadRegistrationServices> registration;
    // Shares explicit object identities and the selected thread/semaphore calls.
    std::shared_ptr<IgbThreadRuntimeInitServices> runtime;
    // Reuses exact tagged_pool/allocate request types and canonical status cells.
    // A bound callback is still required: this does not implement5929C or pools.
    std::shared_ptr<IgbMemoryFieldConstructServices> memory;
    std::vector<std::shared_ptr<void>> retained;
};
// Actual773AC only: active-byte branch, optional full count store, fresh status,
// hidden result. No interrupt state, queue initialization, wait or signal.
class IgbSemaphoreInitialCountInvocation {
public:
    IgbSemaphoreInitialCountInvocation(IgbObjectInstance,std::uint32_t,std::shared_ptr<IgbScratchBuffer> result);
    ~IgbSemaphoreInitialCountInvocation();
    IgbSemaphoreInitialCountInvocation(const IgbSemaphoreInitialCountInvocation&)=delete;
    IgbSemaphoreInitialCountInvocation& operator=(const IgbSemaphoreInitialCountInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbGamecubeThreadInitServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual77E94: captured+A8, fresh stack size, actual one-byte68430 tag read,
// explicit5929C/+DC services, stack publication, fresh done semaphore+68(arg0).
class IgbGamecubeThreadInitInvocation {
public:
    explicit IgbGamecubeThreadInitInvocation(IgbObjectInstance);
    ~IgbGamecubeThreadInitInvocation();
    IgbGamecubeThreadInitInvocation(const IgbGamecubeThreadInitInvocation&)=delete;
    IgbGamecubeThreadInitInvocation& operator=(const IgbGamecubeThreadInitInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbGamecubeThreadInitServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Thread VT80470110: getter+58, BLRs+0C/+24/+34, actual+2C only.
IgbObjectInitFactory make_native_igb_gamecube_thread_init_factory(
    std::shared_ptr<IgbGamecubeThreadInitServices>,IgbObjectInitFactory remaining={});
// Literal thread+A8=93400 and actual semaphore+68=773AC only.
IgbThreadRuntimeInitFactory make_native_igb_gamecube_thread_runtime_factory(
    std::shared_ptr<IgbGamecubeThreadInitServices>,IgbThreadRuntimeInitFactory remaining={});
} // namespace dw4
