#pragma once

#include "dw4/igb_metadata_factory.hpp"
#include "dw4/igb_registration_callbacks.hpp"
#include "dw4/igb_file_pools.hpp"

namespace dw4 {
class IgbMetadataRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbMetadataRegistrationServices {
    // The same8056174C cell used by80029E64; never a separate lookup cache.
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbFilePoolsServices> pools;
private:
    friend class IgbMetadataLookupInvocation;
    std::vector<std::shared_ptr<IgbRuntimeMetaType>> unpublished_metadata_;
};
IgbTypeRegistrationArguments igb_metadata_type_registration_arguments(
    const std::shared_ptr<IgbMetadataFactoryServices>&);
// Actual8002A0D0. Unlike many class bodies it does not call80021B94.
class IgbMetadataTypeRegistrationInvocation {
public:
    IgbMetadataTypeRegistrationInvocation();~IgbMetadataTypeRegistrationInvocation();
    IgbMetadataTypeRegistrationInvocation(const IgbMetadataTypeRegistrationInvocation&)=delete;
    IgbMetadataTypeRegistrationInvocation& operator=(const IgbMetadataTypeRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataRegistrationServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
enum class IgbMetadataLookup {allocate_if_null,fully_registered}; //80029F84 /80029FC0,8002A144.
class IgbMetadataLookupInvocation {
public:
    explicit IgbMetadataLookupInvocation(IgbMetadataLookup entry=IgbMetadataLookup::fully_registered);
    ~IgbMetadataLookupInvocation();
    IgbMetadataLookupInvocation(const IgbMetadataLookupInvocation&)=delete;
    IgbMetadataLookupInvocation& operator=(const IgbMetadataLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataRegistrationServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_metadata_registration_function_factory(
    std::shared_ptr<IgbMetadataRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_metadata_registration_type_factory(
    std::shared_ptr<IgbMetadataRegistrationServices>,IgbTypeRegistrationFactory remaining={});
IgbMetadataFactory make_native_igb_metadata_registration_factory(
    std::shared_ptr<IgbMetadataRegistrationServices>,IgbMetadataFactory remaining={});
} // namespace dw4
