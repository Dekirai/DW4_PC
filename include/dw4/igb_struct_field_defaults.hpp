#pragma once

#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_list_defaults.hpp"

namespace dw4 {
class IgbStructFieldDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbStructFieldDefaultsServices {
    IgbStructFieldDefaultsServices();
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration; //Canonical80561560.
    // Actual batch/schema, name lookup, clone+54, scalar+8C, replacement and
    // destruction services. An unavailable unsigned-short clone stays pending.
    std::shared_ptr<IgbListDefaultsServices> lists;
    std::shared_ptr<IgbFieldFactoryTable> factories; //8046351C.
    std::shared_ptr<IgbFieldNameTable> names; //80463554.
    std::shared_ptr<IgbFieldGlobalTable> globals; //80463560.
    std::shared_ptr<IgbFieldOffsetTable> offsets; //8046356C.
    std::shared_ptr<IgbRuntimeFieldSlot> size_descriptor; //80561568, distinct from the base _size.
    std::vector<std::shared_ptr<void>> retained;
};
// Actual246C8: append three fields, clone and replace inherited _size, set
// halfword/word defaults to1, then configure the three local fields with659C0.
class IgbStructFieldDefaultsInvocation {
public:
    IgbStructFieldDefaultsInvocation();
    ~IgbStructFieldDefaultsInvocation();
    IgbStructFieldDefaultsInvocation(const IgbStructFieldDefaultsInvocation&)=delete;
    IgbStructFieldDefaultsInvocation& operator=(const IgbStructFieldDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbStructFieldDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_struct_field_defaults_factory(
    std::shared_ptr<IgbStructFieldDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
