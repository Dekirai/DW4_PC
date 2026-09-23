#pragma once

#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_list_defaults.hpp"

namespace dw4 {
class IgbMemoryFieldDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbMemoryFieldDefaultsServices {
    IgbMemoryFieldDefaultsServices();
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration;
    // Reuse actual batch/schema, scalar virtuals, name lookup, clone, destroy
    // and replacement services. Constructor31378 uses schema->factory.
    std::shared_ptr<IgbListDefaultsServices> lists;
    std::shared_ptr<IgbFieldFactoryTable> factories; //80464A14.
    std::shared_ptr<IgbFieldNameTable> names; //80464A88.
    std::shared_ptr<IgbFieldGlobalTable> globals; //80464AA8.
    std::shared_ptr<IgbFieldOffsetTable> offsets; //80464AC8.
    std::shared_ptr<IgbRuntimeFieldSlot> shallow_copy; //80561820.
    std::shared_ptr<IgbRuntimeFieldSlot> reset_method; //80561834.
    // Original static word3 passed to46E58, not a prepared method result.
    std::optional<IgbRuntimeFieldDefaultSource> reset_default_source; //8055D2A8.
    std::vector<IgbBootstrapPointer> physical_metadata;
    std::vector<std::shared_ptr<void>> retained;
};
// Actual2AF40: eight local fields, inherited method replacements, explicit
// scalar virtuals, lazy metadata getter2A638 and final659C0 schema.
class IgbMemoryFieldDefaultsInvocation {
public:
    IgbMemoryFieldDefaultsInvocation();
    ~IgbMemoryFieldDefaultsInvocation();
    IgbMemoryFieldDefaultsInvocation(const IgbMemoryFieldDefaultsInvocation&)=delete;
    IgbMemoryFieldDefaultsInvocation& operator=(const IgbMemoryFieldDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryFieldDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_memory_field_defaults_factory(
    std::shared_ptr<IgbMemoryFieldDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
