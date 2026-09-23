#pragma once

#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_metadata_registration.hpp"

#include <array>

namespace dw4 {
class IgbFieldTypeDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbFieldTypeDefaultsTables {
    std::shared_ptr<IgbFieldFactoryTable> factories;
    std::shared_ptr<IgbFieldNameTable> names;
    std::shared_ptr<IgbFieldGlobalTable> globals;
    std::shared_ptr<IgbFieldOffsetTable> offsets;
};
struct IgbFieldTypeDefaultsServices {
    IgbFieldTypeDefaultsServices();
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbMetadataRegistrationServices> metadata; // Actual29F84 canonical8056174C.
    std::array<IgbFieldTypeDefaultsTables,3> tables; // Indexed by IgbFieldTypeKind.
    std::vector<std::shared_ptr<void>> unfinished;
};
// Actual2A764 (base,18 fields),273EC (reference,2),28AB4 (object,3).
class IgbFieldTypeDefaultsInvocation {
public:
    explicit IgbFieldTypeDefaultsInvocation(IgbFieldTypeKind);
    ~IgbFieldTypeDefaultsInvocation();
    IgbFieldTypeDefaultsInvocation(const IgbFieldTypeDefaultsInvocation&)=delete;
    IgbFieldTypeDefaultsInvocation& operator=(const IgbFieldTypeDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldTypeDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_field_type_defaults_factory(
    std::shared_ptr<IgbFieldTypeDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
