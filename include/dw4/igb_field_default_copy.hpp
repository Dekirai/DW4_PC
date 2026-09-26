#pragma once

#include "dw4/igb_field_values.hpp"

namespace dw4 {
class IgbFieldDefaultCopyError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
// Actual63E50: capture offset+08, size halfword+14, then default pointer+20;
// actual031E8 compares unsigned guest addresses and reads/writes one byte at
// a time. This does not prepare/grow a buffer or substitute another field VT.
class IgbFieldDefaultCopyInvocation {
public:
    IgbFieldDefaultCopyInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance);
    ~IgbFieldDefaultCopyInvocation();
    IgbFieldDefaultCopyInvocation(const IgbFieldDefaultCopyInvocation&)=delete;
    IgbFieldDefaultCopyInvocation& operator=(const IgbFieldDefaultCopyInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldValuesServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
