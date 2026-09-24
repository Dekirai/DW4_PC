#pragma once

#include "dw4/igb_named_type_registration.hpp"
#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_field_values.hpp"

#include <array>

namespace dw4 {
class IgbNamedTypeDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbNamedTypeDefaultsTables {
    std::shared_ptr<IgbFieldFactoryTable> factories;
    std::shared_ptr<IgbFieldNameTable> names;
    std::shared_ptr<IgbFieldGlobalTable> globals;
    std::shared_ptr<IgbFieldOffsetTable> offsets;
};
struct IgbNamedTypeDefaultsServices {
    IgbNamedTypeDefaultsServices();
    std::shared_ptr<IgbNamedTypeRegistrationServices> registration;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFieldValuesServices> values;
    // Indexed by IgbNamedTypeKind. Semaphore has no local field tables.
    std::array<IgbNamedTypeDefaultsTables,3> tables;
    // Original static words8055D070/74/78/7C passed by address to46E58.
    // Their storage remains live through the reached virtual+8C operation.
    std::array<std::optional<IgbRuntimeFieldDefaultSource>,4> enum_default_sources;
    std::vector<IgbBootstrapPointer> physical_metadata;
    std::vector<std::shared_ptr<void>> retained;
};
// Actual29DE8(named object),23CFC(thread),2614C(semaphore). This runs the
// original field batch, setter and schema bodies, storing enum getter and
// factory redirect words without invoking those functions.
class IgbNamedTypeDefaultsInvocation {
public:
    explicit IgbNamedTypeDefaultsInvocation(IgbNamedTypeKind);
    ~IgbNamedTypeDefaultsInvocation();
    IgbNamedTypeDefaultsInvocation(const IgbNamedTypeDefaultsInvocation&)=delete;
    IgbNamedTypeDefaultsInvocation& operator=(const IgbNamedTypeDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbNamedTypeDefaultsServices> services={});
    bool completed()const noexcept;
    bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_named_type_defaults_factory(
    std::shared_ptr<IgbNamedTypeDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
