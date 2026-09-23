#pragma once

#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_field_schema.hpp"

namespace dw4 {
class IgbEnumFieldDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbEnumFieldDefaultsServices {
    IgbEnumFieldDefaultsServices();
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFieldFactoryTable> factories; //8055D518: pointer factory274EC.
    std::shared_ptr<IgbFieldNameTable> names; //8055D51C: _getMetaEnumFunction.
    std::shared_ptr<IgbFieldGlobalTable> globals; //8055D520: descriptor cell80561C38.
    std::shared_ptr<IgbFieldOffsetTable> offsets; //8055D524: member offset34.
    std::vector<std::shared_ptr<void>> retained;
};
// Actual31614: capture metadata/count, batch one pointer descriptor, select its
// live physical registry slot, clear +26, then execute the complete schema.
// This does not initialize an enum instance's derived function member at +34.
class IgbEnumFieldDefaultsInvocation {
public:
    IgbEnumFieldDefaultsInvocation();
    ~IgbEnumFieldDefaultsInvocation();
    IgbEnumFieldDefaultsInvocation(const IgbEnumFieldDefaultsInvocation&)=delete;
    IgbEnumFieldDefaultsInvocation& operator=(const IgbEnumFieldDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbEnumFieldDefaultsServices> services={});
    bool completed()const noexcept;
    bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_enum_field_defaults_factory(
    std::shared_ptr<IgbEnumFieldDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
