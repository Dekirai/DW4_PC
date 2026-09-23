#pragma once

#include "dw4/igb_field_values.hpp"

namespace dw4 {
class IgbFieldStringInitError : public std::runtime_error {public:using std::runtime_error::runtime_error;};

// Actual715E8: capture descriptor+08, retain the first source pointer, release
// the captured destination, then reread the source pointer for the raw store.
class IgbFieldStringAssignInvocation {
public:
    IgbFieldStringAssignInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance,
        IgbRuntimeFieldDefaultSource source);
    ~IgbFieldStringAssignInvocation();
    IgbFieldStringAssignInvocation(const IgbFieldStringAssignInvocation&)=delete;
    IgbFieldStringAssignInvocation& operator=(const IgbFieldStringAssignInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldValuesServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};

// Actual71A9C: read the descriptor's default string pointer, conditionally
// intern it, assign through715E8, and release the retained stack temporary.
class IgbFieldStringInitInvocation {
public:
    IgbFieldStringInitInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance);
    ~IgbFieldStringInitInvocation();
    IgbFieldStringInitInvocation(const IgbFieldStringInitInvocation&)=delete;
    IgbFieldStringInitInvocation& operator=(const IgbFieldStringInitInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldValuesServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
