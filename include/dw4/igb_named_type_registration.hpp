#pragma once

#include "dw4/igb_thread_dependencies.hpp"

namespace dw4 {
class IgbNamedTypeRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbNamedTypeKind {named_object,thread,semaphore};
enum class IgbNamedTypeLookup {fully_registered,plain,allocate_if_null};
struct IgbNamedTypeProfile {
    const char* name;
    std::uint32_t global,wrapper,body,flag,native_size;
    std::uint32_t parent,parent_getter,field_callback,probe,defaults;
    std::uint32_t full_getter,lazy_getter,plain_getter;
};
struct IgbNamedTypeRegistrationServices {
    std::shared_ptr<IgbRegistrationMetaSlot> named_metadata; // Canonical80561744.
    std::shared_ptr<IgbRegistrationMetaSlot> thread_metadata; // Canonical80561504.
    // Reuse805615EC from the actual ThreadManager dependency getter26020.
    std::shared_ptr<IgbThreadDependenciesServices> dependencies;
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFilePoolsServices> pools;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    // Original29CA8 uses a12-byte stack object, including unknown bytes.
    std::function<IgbBootstrapPointer()> local_storage;
    std::vector<IgbBootstrapPointer> allocations;
    std::vector<std::shared_ptr<void>> retained;
};
const IgbNamedTypeProfile& igb_named_type_profile(IgbNamedTypeKind);
std::shared_ptr<IgbRegistrationMetaSlot> igb_named_type_metadata_slot(
    const std::shared_ptr<IgbNamedTypeRegistrationServices>&,IgbNamedTypeKind);
IgbTypeRegistrationArguments igb_named_type_registration_arguments(
    const std::shared_ptr<IgbNamedTypeRegistrationServices>&,IgbNamedTypeKind);
// Actual29D58/23C68/260C0:21B94 must return before66204 arguments are captured.
class IgbNamedTypeRegistrationInvocation {
public:
    explicit IgbNamedTypeRegistrationInvocation(IgbNamedTypeKind);
    ~IgbNamedTypeRegistrationInvocation();
    IgbNamedTypeRegistrationInvocation(const IgbNamedTypeRegistrationInvocation&)=delete;
    IgbNamedTypeRegistrationInvocation& operator=(const IgbNamedTypeRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbNamedTypeRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Full getters compose66188 and reload the canonical pointer. Lazy getters
// allocate only on known null; plain named getters23CF4/940B8 do no registration.
class IgbNamedTypeLookupInvocation {
public:
    IgbNamedTypeLookupInvocation(IgbNamedTypeKind,IgbNamedTypeLookup=IgbNamedTypeLookup::fully_registered);
    ~IgbNamedTypeLookupInvocation();
    IgbNamedTypeLookupInvocation(const IgbNamedTypeLookupInvocation&)=delete;
    IgbNamedTypeLookupInvocation& operator=(const IgbNamedTypeLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbNamedTypeRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual29CA8:6665C, captured ArkCore, derived VT/name stores, physical offset
// read, and derived VT cleanup. Its literal-zero cleanup never releases a name.
class IgbNamedObjectProbeInvocation {
public:
    explicit IgbNamedObjectProbeInvocation(IgbBootstrapPointer);
    ~IgbNamedObjectProbeInvocation();
    IgbNamedObjectProbeInvocation(const IgbNamedObjectProbeInvocation&)=delete;
    IgbNamedObjectProbeInvocation& operator=(const IgbNamedObjectProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbNamedTypeRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_named_type_registration_function_factory(
    std::shared_ptr<IgbNamedTypeRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_named_type_registration_type_factory(
    std::shared_ptr<IgbNamedTypeRegistrationServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
