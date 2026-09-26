#pragma once

#include "dw4/igb_gamecube_thread_init.hpp"
#include "dw4/igb_thread_startup.hpp"
#include "dw4/igb_semaphore_runtime_init.hpp"

namespace dw4 {
class IgbThreadManagerInsertError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbThreadManagerInsertServiceKind {grow_list,sleep_thread,wake_thread};
struct IgbThreadManagerInsertCall {
    IgbThreadManagerInsertServiceKind kind{};
    IgbObjectInstance object;
    // Actual OS queue argument is semaphore+1C. No fabricated host condition.
    IgbBootstrapPointer queue;
    // Actual41660 receives requested signedcount as raw32bits and elementbytes4.
    std::uint32_t count{},element_bytes{};
};
struct IgbThreadManagerInsertOperation {
    std::function<MessengerCallResult()> resume;
    // Borrowed for one resume; never retain the enclosing guard.
    std::function<MessengerCallResult(const std::function<void()>&)> resume_checked;
};
using IgbThreadManagerInsertFactory=std::function<std::shared_ptr<IgbThreadManagerInsertOperation>(const IgbThreadManagerInsertCall&)>;
struct IgbThreadManagerInsertServices {
    std::shared_ptr<IgbThreadRuntimeInitServices> runtime; // Object mappings and semaphore virtual calls.
    // Canonical status cells 805622D0/D4 and explicitly bound list allocations.
    std::shared_ptr<IgbMemoryFieldConstructServices> memory;
    IgbInterruptFactory interrupts; // Same canonical exclusion state as activation.
    IgbThreadManagerInsertFactory factory;
    std::vector<std::shared_ptr<void>> retained;
};
// Actual7761C/+8C wait or774B0/+7C signal. Includes actual80260E00/80260EC4
// count and interrupt transitions. Sleep80261F20/wake8026200C stay explicit.
class IgbSemaphoreRuntimeAccessInvocation {
public:
    IgbSemaphoreRuntimeAccessInvocation(IgbObjectInstance,std::uint32_t virtual_slot,
        std::shared_ptr<IgbScratchBuffer> result);
    ~IgbSemaphoreRuntimeAccessInvocation();
    IgbSemaphoreRuntimeAccessInvocation(const IgbSemaphoreRuntimeAccessInvocation&)=delete;
    IgbSemaphoreRuntimeAccessInvocation& operator=(const IgbSemaphoreRuntimeAccessInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbThreadManagerInsertServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual75460: threadretain, capturedwait, freshlist/signedcount/growth/store,
// freshsignal. The two hidden statuses are ignored, not assumed successful.
class IgbThreadManagerInsertInvocation {
public:
    IgbThreadManagerInsertInvocation(IgbBootstrapPointer manager,IgbBootstrapPointer thread);
    ~IgbThreadManagerInsertInvocation();
    IgbThreadManagerInsertInvocation(const IgbThreadManagerInsertInvocation&)=delete;
    IgbThreadManagerInsertInvocation& operator=(const IgbThreadManagerInsertInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbThreadManagerInsertServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbThreadRuntimeInitFactory make_native_igb_semaphore_runtime_access_factory(
    std::shared_ptr<IgbThreadManagerInsertServices>,IgbThreadRuntimeInitFactory remaining={});
IgbThreadStartupFactory make_native_igb_thread_manager_insert_factory(
    std::shared_ptr<IgbThreadManagerInsertServices>,IgbThreadStartupFactory remaining={});
} // namespace dw4
