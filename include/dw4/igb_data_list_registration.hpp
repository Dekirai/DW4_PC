#pragma once

#include "dw4/igb_thread_dependencies.hpp"

namespace dw4 {
class IgbDataListRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbDataListKind {data_list,nonref_object,thread_list};
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
};
const IgbDataListProfile& igb_data_list_profile(IgbDataListKind);
std::shared_ptr<IgbRegistrationMetaSlot> igb_data_list_metadata_slot(
    const std::shared_ptr<IgbDataListRegistrationServices>&,IgbDataListKind);
IgbTypeRegistrationArguments igb_data_list_type_registration_arguments(
    const std::shared_ptr<IgbDataListRegistrationServices>&,IgbDataListKind);
// Actual33A14/29694/23F50, including prerequisite21B94 before66204.
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
// flags. Lazy DataList33934 and ThreadList23E40 allocate only on known null.
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
// Actual339AC/29614/23EB8:6665C, ordered vtable writes, and the original
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
