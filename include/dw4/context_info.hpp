#pragma once

#include "dw4/serial_scheduler.hpp"

namespace dw4 {
class ContextInfoError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Actual application info registration8028A7DC. The captured info (including
// null) visits the live unsigned-count manager registry until the first nonzero
// low-byte result. The live application info list then receives one occurrence.
// Separate invocations may nest; only recursive resume of one call is invalid.
class ContextInfoInvocation {
public:
    ContextInfoInvocation(std::shared_ptr<ModelSceneContext>,std::shared_ptr<ModelSceneObject>);
    ~ContextInfoInvocation();
    ContextInfoInvocation(const ContextInfoInvocation&)=delete;
    ContextInfoInvocation& operator=(const ContextInfoInvocation&)=delete;
    MessengerCallResult resume();
    bool completed() const noexcept;
    bool faulted() const noexcept;
    // The original incidental r3 return is the append's backing address. Native
    // callers receive that owner separately; MessengerCallResult.value is zero
    // for this void operation, never a guest pointer or acceptance flag.
    std::shared_ptr<ModelSceneList::Storage> appended_backing() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A single retained call, suitable for a stage register_info service operation.
// Repeated calls after completion do not perform another registration.
ModelManagerRamCall make_context_info_registration(std::shared_ptr<ModelSceneContext>,
    std::shared_ptr<ModelSceneObject>);
} // namespace dw4
