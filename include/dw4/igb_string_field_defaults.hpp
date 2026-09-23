#pragma once

#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_list_defaults.hpp"

namespace dw4 {
class IgbStringFieldDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbStringFieldDefaultsServices {
    IgbStringFieldDefaultsServices();
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration;
    // Shared native batch/schema, scalar defaults and physical replacement.
    // Unknown inherited clone bodies remain required list-factory operations.
    std::shared_ptr<IgbListDefaultsServices> lists;
    std::shared_ptr<IgbFieldFactoryTable> factories; //8055D0F0.
    std::shared_ptr<IgbFieldNameTable> names; //8055D0F4.
    std::shared_ptr<IgbFieldGlobalTable> globals; //8055D0F8.
    std::shared_ptr<IgbFieldOffsetTable> offsets; //8055D0FC.
    std::shared_ptr<IgbRuntimeFieldSlot> shallow_copy; //805615BC.
    std::shared_ptr<IgbRuntimeFieldSlot> destruct; //805615C0.
    std::vector<std::shared_ptr<void>> retained;
};
// Actual2549C: one Boolean field, inherited shallow-copy/destruct replacements,
// then the local true default and final659C0 schema.
class IgbStringFieldDefaultsInvocation {
public:
    IgbStringFieldDefaultsInvocation();
    ~IgbStringFieldDefaultsInvocation();
    IgbStringFieldDefaultsInvocation(const IgbStringFieldDefaultsInvocation&)=delete;
    IgbStringFieldDefaultsInvocation& operator=(const IgbStringFieldDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbStringFieldDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_string_field_defaults_factory(
    std::shared_ptr<IgbStringFieldDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
