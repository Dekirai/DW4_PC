#pragma once

#include "dw4/igb_meta_field_factory.hpp"
#include "dw4/igb_metadata_factory.hpp"
#include "dw4/igb_file_pools.hpp"

namespace dw4 {
class IgbFieldTypeRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbFieldTypeKind {base,reference,object,signed_int,memory_reference,boolean};
struct IgbFieldTypeProfile {
    const char* name{};
    std::uint32_t metadata_global{},wrapper{},body{},size{},flag{},parent_body{},parent_getter{};
    std::uint32_t field_callback{},probe{},defaults{},lazy_getter{},full_getter{},plain_getter{},extras_global{};
    std::array<std::uint32_t,3> extra_callbacks{};
};
const IgbFieldTypeProfile& igb_field_type_profile(IgbFieldTypeKind);
struct IgbFieldTypeRegistrationServices {
    // Object/int/memory/bool classes share their canonical meta_fields slots.
    std::shared_ptr<IgbMetaFieldFactoryServices> meta_fields;
    std::shared_ptr<IgbRegistrationMetaSlot> base_metadata; //805617BC.
    std::shared_ptr<IgbRegistrationMetaSlot> reference_metadata; //80561684.
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbFilePoolsServices> pools;
    std::vector<std::shared_ptr<void>> retained;
};
std::shared_ptr<IgbRegistrationMetaSlot> igb_field_type_metadata_slot(
    const std::shared_ptr<IgbFieldTypeRegistrationServices>&,IgbFieldTypeKind);
IgbTypeRegistrationArguments igb_field_type_registration_arguments(
    const std::shared_ptr<IgbFieldTypeRegistrationServices>&,IgbFieldTypeKind);
// Actual2A6D8/27360/28A20/2E3B8/2AEAC/38828; no prerequisite guard21B94.
class IgbFieldTypeRegistrationInvocation {
public:
    explicit IgbFieldTypeRegistrationInvocation(IgbFieldTypeKind);
    ~IgbFieldTypeRegistrationInvocation();
    IgbFieldTypeRegistrationInvocation(const IgbFieldTypeRegistrationInvocation&)=delete;
    IgbFieldTypeRegistrationInvocation& operator=(const IgbFieldTypeRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldTypeRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
enum class IgbFieldTypeLookup {allocate_if_null,fully_registered,plain};
// Actual metadata getters only. The object-field prototype getter28BFC is
// separate; returning type metadata would not satisfy that descriptor lookup.
class IgbFieldTypeLookupInvocation {
public:
    IgbFieldTypeLookupInvocation(IgbFieldTypeKind,IgbFieldTypeLookup=IgbFieldTypeLookup::fully_registered);
    ~IgbFieldTypeLookupInvocation();
    IgbFieldTypeLookupInvocation(const IgbFieldTypeLookupInvocation&)=delete;
    IgbFieldTypeLookupInvocation& operator=(const IgbFieldTypeLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldTypeRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_field_type_registration_function_factory(
    std::shared_ptr<IgbFieldTypeRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_field_type_registration_type_factory(
    std::shared_ptr<IgbFieldTypeRegistrationServices>,IgbTypeRegistrationFactory remaining={});
IgbMetaFieldFactory make_native_igb_field_type_registration_factory(
    std::shared_ptr<IgbFieldTypeRegistrationServices>,IgbMetaFieldFactory remaining={});
} // namespace dw4
