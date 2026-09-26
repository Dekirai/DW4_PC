#pragma once

#include "dw4/igb_thread_runtime_init.hpp"

namespace dw4 {
class IgbSemaphoreRuntimeInitError : public std::runtime_error {
public:using std::runtime_error::runtime_error;
};
enum class IgbInterruptServiceKind {disable,restore};
struct IgbInterruptCall {
    IgbInterruptServiceKind kind{};
    std::uint32_t restore_enabled{};
};
struct IgbInterruptOperation {std::function<MessengerCallResult()> resume;};
using IgbInterruptFactory=std::function<std::shared_ptr<IgbInterruptOperation>(const IgbInterruptCall&)>;

// Only the original execution-exclusion bit is represented here. This is not
// a host CPU register and does not implement scheduling, wait or signal.
// No initial state is implied. The future native scheduler must consume the
// same canonical exclusion state before asynchronous work can be admitted.
struct IgbExecutionInterruptState {std::optional<bool> enabled;};
IgbInterruptFactory make_native_igb_interrupt_factory(std::shared_ptr<IgbExecutionInterruptState>);

struct IgbSemaphoreRuntimeInitServices {
    IgbInterruptFactory interrupts;
    // Alias the existing memory-construct status_success cell, actual805622D0.
    // The pointed-to optional is read fresh after activation publication.
    std::shared_ptr<std::optional<std::uint32_t>> status_success;
    std::vector<std::shared_ptr<void>> retained;
};

// Actual77434 with actual80260DA8 and8026110C queue/count stores. The selected
// interrupt operations remain explicit, retained services; the native state
// adapter above reconstructs8025EFF0/8025F018 for prepared execution state.
class IgbSemaphoreRuntimeInitInvocation {
public:
    IgbSemaphoreRuntimeInitInvocation(IgbObjectInstance,std::shared_ptr<IgbScratchBuffer> result);
    ~IgbSemaphoreRuntimeInitInvocation();
    IgbSemaphoreRuntimeInitInvocation(const IgbSemaphoreRuntimeInitInvocation&)=delete;
    IgbSemaphoreRuntimeInitInvocation& operator=(const IgbSemaphoreRuntimeInitInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbSemaphoreRuntimeInitServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Exact object VT80470314+5C only. Other runtime calls remain unbound/delegated.
IgbThreadRuntimeInitFactory make_native_igb_semaphore_runtime_init_factory(
    std::shared_ptr<IgbSemaphoreRuntimeInitServices>,IgbThreadRuntimeInitFactory remaining={});
} // namespace dw4
