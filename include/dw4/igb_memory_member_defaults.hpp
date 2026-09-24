#pragma once

#include "dw4/igb_memory_member_copy.hpp"

namespace dw4 {
// Actual62638: live ownership/element gates, object ancestry test, physical
// allocation-size count and full reference increments for nonnull elements.
class IgbMemoryMemberRetainInvocation {
public:
    IgbMemoryMemberRetainInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance);
    ~IgbMemoryMemberRetainInvocation();
    IgbMemoryMemberRetainInvocation(const IgbMemoryMemberRetainInvocation&)=delete;
    IgbMemoryMemberRetainInvocation& operator=(const IgbMemoryMemberRetainInvocation&)=delete;
    // Borrow the enclosing continuation's validity check for this resume only.
    MessengerCallResult resume(std::shared_ptr<IgbMemoryMemberCopyServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual62798: release elements, live free flag+41, old buffer free, read the
// default pointer, native5647C copy, fresh member offset publication, retain.
class IgbMemoryMemberDefaultsInvocation {
public:
    IgbMemoryMemberDefaultsInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance);
    ~IgbMemoryMemberDefaultsInvocation();
    IgbMemoryMemberDefaultsInvocation(const IgbMemoryMemberDefaultsInvocation&)=delete;
    IgbMemoryMemberDefaultsInvocation& operator=(const IgbMemoryMemberDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryMemberCopyServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbObjectInitFactory make_native_igb_memory_member_defaults_factory(
    std::shared_ptr<IgbMemoryMemberCopyServices>,IgbObjectInitFactory remaining={});
} // namespace dw4
