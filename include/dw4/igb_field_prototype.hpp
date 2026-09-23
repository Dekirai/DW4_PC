#pragma once

#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_field_registry_factory.hpp"
#include "dw4/igb_field_values.hpp"

namespace dw4 {
class IgbFieldPrototypeError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbFieldPrototypeServiceKind {reflected_field,destroy_field};
struct IgbFieldPrototypeCall {
    IgbFieldPrototypeServiceKind kind{};
    std::uint32_t function{};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbDecodeRuntimeField> field;
};
struct IgbFieldPrototypeOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbDecodeRuntimeField> field_result;
    std::optional<std::uint32_t> field_address;
};
using IgbFieldPrototypeFactory=std::function<std::shared_ptr<IgbFieldPrototypeOperation>(const IgbFieldPrototypeCall&)>;
struct IgbFieldPrototypeServices {
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration;
    std::shared_ptr<IgbFieldValuesServices> values; //Canonical concrete descriptor prototypes.
    std::shared_ptr<IgbRuntimeFieldSlot> reference_prototype; //Canonical80561688.
    std::shared_ptr<std::optional<std::shared_ptr<IgbDecodeFieldRegistry>>> registry; //Canonical8056229C.
    std::shared_ptr<IgbFieldRegistryServices> registries;
    // When bound, execute actual6546C and its retained667F4 initializer.
    // File/field result identities remain distinct throughout construction.
    std::shared_ptr<IgbFileFactoryServices> reflected;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    // Explicit typed reflection endpoint when reflected services are absent;
    // destruction remains explicit on the original low23-zero branch.
    IgbFieldPrototypeFactory factory;
    std::vector<std::shared_ptr<void>> retained;
};
std::shared_ptr<IgbRuntimeFieldSlot> igb_field_prototype_slot(
    const std::shared_ptr<IgbFieldPrototypeServices>&,IgbFieldTypeKind);
// Actual31728(enum) /27824(pointer): null-only prototype test, registration,
// then a fresh prototype-cell load. This does not return type metadata.
class IgbFieldPrototypeLookupInvocation {
public:
    explicit IgbFieldPrototypeLookupInvocation(IgbFieldTypeKind);
    ~IgbFieldPrototypeLookupInvocation();
    IgbFieldPrototypeLookupInvocation(const IgbFieldPrototypeLookupInvocation&)=delete;
    IgbFieldPrototypeLookupInvocation& operator=(const IgbFieldPrototypeLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldPrototypeServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual63B5C: null-only registry test,607F4,2DB4C,raw publication,reload.
class IgbFieldPrototypeRegistryInvocation {
public:
    IgbFieldPrototypeRegistryInvocation();~IgbFieldPrototypeRegistryInvocation();
    IgbFieldPrototypeRegistryInvocation(const IgbFieldPrototypeRegistryInvocation&)=delete;
    IgbFieldPrototypeRegistryInvocation& operator=(const IgbFieldPrototypeRegistryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldPrototypeServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeFieldRegistry> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual27454(reference) /28B64(object) /2E440(int) /2B178(memory) /388B0(bool)
// /25628(string) /37DB0(byte) /31690(enum) /2778C(pointer).
// These callbacks return no descriptor;
// descriptor publication is through the canonical global cell only.
class IgbFieldPrototypeInvocation {
public:
    explicit IgbFieldPrototypeInvocation(IgbFieldTypeKind);
    ~IgbFieldPrototypeInvocation();
    IgbFieldPrototypeInvocation(const IgbFieldPrototypeInvocation&)=delete;
    IgbFieldPrototypeInvocation& operator=(const IgbFieldPrototypeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldPrototypeServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_field_prototype_function_factory(
    std::shared_ptr<IgbFieldPrototypeServices>,IgbRegistrationCallbacksFactory remaining={});
} // namespace dw4
