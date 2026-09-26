#pragma once

#include "dw4/igb_thread_dependencies.hpp"

namespace dw4 {
class IgbDataListRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbDataListKind {data_list,nonref_object,thread_list,ref_object,ref_thread,context_info,context_extension,string_ref,signed_int};
enum class IgbDataListLookup {fully_registered,plain,allocate_if_null};
struct IgbDataListProfile {
    const char* name;
    std::uint32_t global,wrapper,body,parent,parent_getter,field_callback,probe,defaults;
    std::uint32_t full_getter,lazy_getter,plain_getter;
};
struct IgbDataListRegistrationServices {
    std::shared_ptr<IgbRegistrationMetaSlot> data_metadata; // Canonical80561D3C.
    std::shared_ptr<IgbRegistrationMetaSlot> object_metadata; // Canonical80561730.
    // Share80561530 with the original ThreadManager allocation-only getter.
    std::shared_ptr<IgbThreadDependenciesServices> dependencies;
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFilePoolsServices> pools;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    // Representation of the original20-byte stack object, including unknown
    // bytes. The probe calls6665C and does not construct a ready list instance.
    std::function<IgbBootstrapPointer(IgbDataListKind)> local_storage;
    std::vector<IgbBootstrapPointer> allocations;
    std::vector<std::shared_ptr<void>> retained;
    // Reference-counted classes have separate globals and descriptors.
    std::shared_ptr<IgbRegistrationMetaSlot> ref_object_metadata; // Canonical80561708.
    std::shared_ptr<IgbRegistrationMetaSlot> ref_thread_metadata; // Canonical80561534.
    std::shared_ptr<IgbRegistrationMetaSlot> context_info_metadata; // Canonical80561DE4.
    std::shared_ptr<IgbRegistrationMetaSlot> context_extension_metadata; // Canonical80561DE0.
    std::shared_ptr<IgbRegistrationMetaSlot> string_ref_metadata; // Canonical8056158C.
    std::shared_ptr<IgbRegistrationMetaSlot> signed_int_metadata; // Canonical80561DC4.
};
const IgbDataListProfile& igb_data_list_profile(IgbDataListKind);
std::shared_ptr<IgbRegistrationMetaSlot> igb_data_list_metadata_slot(
    const std::shared_ptr<IgbDataListRegistrationServices>&,IgbDataListKind);
IgbTypeRegistrationArguments igb_data_list_type_registration_arguments(
    const std::shared_ptr<IgbDataListRegistrationServices>&,IgbDataListKind);
// Actual33A14/29694/23F50/2907C/240F4/37168/36F94/24C8C/366AC,
// with21B94 before66204.
class IgbDataListTypeRegistrationInvocation {
public:
    explicit IgbDataListTypeRegistrationInvocation(IgbDataListKind);
    ~IgbDataListTypeRegistrationInvocation();
    IgbDataListTypeRegistrationInvocation(const IgbDataListTypeRegistrationInvocation&)=delete;
    IgbDataListTypeRegistrationInvocation& operator=(const IgbDataListTypeRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDataListRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Full getters compose the actual66188 wrapper. Plain reads do not test ready
// flags. Lazy33934/23E40/28F84/23FE4/37058/36E84/24B94/365B4 allocate
// only on known null, without forcing full registration.
class IgbDataListLookupInvocation {
public:
    IgbDataListLookupInvocation(IgbDataListKind,IgbDataListLookup=IgbDataListLookup::fully_registered);
    ~IgbDataListLookupInvocation();
    IgbDataListLookupInvocation(const IgbDataListLookupInvocation&)=delete;
    IgbDataListLookupInvocation& operator=(const IgbDataListLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDataListRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual339AC/29614/23EB8/28FFC/2405C/370D0/36EFC/24C0C/3662C:
//6665C, ordered vtable writes and
// interleaved ArkCore capture. Return only the represented local physical word.
class IgbDataListLayoutProbeInvocation {
public:
    IgbDataListLayoutProbeInvocation(IgbBootstrapPointer,IgbDataListKind);
    ~IgbDataListLayoutProbeInvocation();
    IgbDataListLayoutProbeInvocation(const IgbDataListLayoutProbeInvocation&)=delete;
    IgbDataListLayoutProbeInvocation& operator=(const IgbDataListLayoutProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbDataListRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_data_list_registration_function_factory(
    std::shared_ptr<IgbDataListRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_data_list_registration_type_factory(
    std::shared_ptr<IgbDataListRegistrationServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
