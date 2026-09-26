#pragma once

#include "dw4/igb_memory_field_construct.hpp"

namespace dw4 {
class IgbAllocationCopyError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
// Actual031E8 over explicitly mapped allocations. Source<destination selects
// backwards byte reads/writes, including distinct allocations. Zero count does
// not resolve or access either pointer. Returns the original destination.
class IgbAllocationCopyInvocation {
public:
    IgbAllocationCopyInvocation(std::uint32_t destination,std::uint32_t source,std::uint32_t count);
    ~IgbAllocationCopyInvocation();
    IgbAllocationCopyInvocation(const IgbAllocationCopyInvocation&)=delete;
    IgbAllocationCopyInvocation& operator=(const IgbAllocationCopyInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryFieldConstructServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
