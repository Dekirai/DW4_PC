#pragma once

#include "dw4/igb_gamecube_thread_adopt.hpp"
#include "dw4/igb_thread_manager_insert.hpp"

namespace dw4 {
class IgbOsThreadWakeError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbOsSchedulerState {
    // Canonical full32 cells80565F10/14/18. Startup must produce their values;
    // allocating this host owner does not initialize any original word.
    std::optional<std::uint32_t> run_bits,run_hint,disable_count;
};
struct IgbOsThreadWakeCall {std::uint32_t argument{};}; // Actual8026140C argument.
struct IgbOsThreadWakeOperation {
    std::function<MessengerCallResult()> resume;
    std::function<MessengerCallResult(const std::function<void()>&)> resume_checked;
};
using IgbOsThreadWakeFactory=std::function<std::shared_ptr<IgbOsThreadWakeOperation>(const IgbOsThreadWakeCall&)>;
struct IgbOsThreadWakeServices {
    // The same raw OS allocation mappings/current identity used by adoption.
    std::shared_ptr<IgbOsThreadState> os;
    std::shared_ptr<IgbOsSchedulerState> scheduler;
    IgbInterruptFactory interrupts;
    IgbOsThreadWakeFactory factory; // Scheduling8026140C remains explicit.
    std::vector<std::shared_ptr<void>> retained;
};
// Actual8026200C: ordered dequeue/runqueue/state stores, fresh full32 runhint,
// retained optional scheduler call, then restoration of the captured bit.
// After256 dequeues in one resume, yield pending at the original loop head;
// this bounds represented cyclic queues without changing or repairing links.
// The actual C operation is void; Messenger's ordinary value is unused.
class IgbOsThreadWakeInvocation {
public:
    explicit IgbOsThreadWakeInvocation(IgbBootstrapPointer queue);
    ~IgbOsThreadWakeInvocation();
    IgbOsThreadWakeInvocation(const IgbOsThreadWakeInvocation&)=delete;
    IgbOsThreadWakeInvocation& operator=(const IgbOsThreadWakeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbOsThreadWakeServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbThreadManagerInsertFactory make_native_igb_os_thread_wake_factory(
    std::shared_ptr<IgbOsThreadWakeServices>,IgbThreadManagerInsertFactory remaining={});
} // namespace dw4
