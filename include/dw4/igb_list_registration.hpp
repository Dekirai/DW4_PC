#pragma once

#include "dw4/igb_metadata_factory.hpp"
#include "dw4/igb_type_list_factory.hpp"
#include "dw4/igb_file_pools.hpp"

namespace dw4 {
class IgbListRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbListRegistrationServices {
    // Canonical805619CC/805619D4 are registry_metadata/children_metadata.
    // The former may alias IgbFieldRegistryServices::metadata directly.
    std::shared_ptr<IgbTypeListServices> lists;
    std::shared_ptr<IgbFieldRegistryServices> field_registries;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbFilePoolsServices> pools;
    // Representation of an original16-byte stack local, not a heap allocator.
    std::function<IgbBootstrapPointer(IgbTypeListFactoryEntry)> local_storage;
    std::vector<std::shared_ptr<void>> retained;
};
IgbTypeRegistrationArguments igb_list_type_registration_arguments(
    const std::shared_ptr<IgbListRegistrationServices>&,IgbTypeListFactoryEntry);
// Actual2DD40 /2E07C. Neither body calls prerequisite guard21B94.
class IgbListTypeRegistrationInvocation {
public:
    explicit IgbListTypeRegistrationInvocation(IgbTypeListFactoryEntry);
    ~IgbListTypeRegistrationInvocation();
    IgbListTypeRegistrationInvocation(const IgbListTypeRegistrationInvocation&)=delete;
    IgbListTypeRegistrationInvocation& operator=(const IgbListTypeRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbListRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
enum class IgbListLookup {allocate_if_null,fully_registered,plain};
// Actual2DC6C/2DFA8 lazy getters and2DCA8/2DFE4 full getters.2DDAC/2E0E8
// alias the latter;2DDCC is a plain read of the children metadata cell.
class IgbListLookupInvocation {
public:
    IgbListLookupInvocation(IgbTypeListFactoryEntry,IgbListLookup=IgbListLookup::fully_registered);
    ~IgbListLookupInvocation();
    IgbListLookupInvocation(const IgbListLookupInvocation&)=delete;
    IgbListLookupInvocation& operator=(const IgbListLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbListRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual2DCE4/2E020: reset53CF4/536F8 before the live ArkCore+394 read.
// The selected local word is returned without a destructor or backing allocation.
class IgbListLayoutProbeInvocation {
public:
    IgbListLayoutProbeInvocation(IgbBootstrapPointer,IgbTypeListFactoryEntry);
    ~IgbListLayoutProbeInvocation();
    IgbListLayoutProbeInvocation(const IgbListLayoutProbeInvocation&)=delete;
    IgbListLayoutProbeInvocation& operator=(const IgbListLayoutProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbListRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_list_registration_function_factory(
    std::shared_ptr<IgbListRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_list_registration_type_factory(
    std::shared_ptr<IgbListRegistrationServices>,IgbTypeRegistrationFactory remaining={});
IgbTypeListFactory make_native_igb_list_registration_factory(
    std::shared_ptr<IgbListRegistrationServices>,IgbTypeListFactory remaining={});
IgbFieldRegistryFactory make_native_igb_field_list_registration_factory(
    std::shared_ptr<IgbListRegistrationServices>,IgbFieldRegistryFactory remaining={});
} // namespace dw4
