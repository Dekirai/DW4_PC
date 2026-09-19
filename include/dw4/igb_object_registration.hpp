#pragma once

#include "dw4/igb_bootstrap_object.hpp"
#include "dw4/igb_file_pools.hpp"
#include "dw4/igb_registration_callbacks.hpp"

namespace dw4 {
class IgbObjectRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbObjectRegistrationServices {
    // Share the same cell with IgbBootstrapServices::object_metadata, or alias
    // the existing reflected initializer's805622A4 member. No second cache.
    std::shared_ptr<IgbRegistrationMetaSlot> metadata;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbFilePoolsServices> pools;
private:
    friend class IgbObjectRootRegistrationInvocation;
    std::vector<std::shared_ptr<IgbRegistrationCallbackList>> unfinished_;
};
IgbTypeRegistrationArguments igb_object_type_registration_arguments(const std::shared_ptr<IgbRegistrationMetaSlot>&);
// Actual80066B08; there is no prerequisite once-guard call in this body.
class IgbObjectTypeRegistrationInvocation {
public:
    IgbObjectTypeRegistrationInvocation();~IgbObjectTypeRegistrationInvocation();
    IgbObjectTypeRegistrationInvocation(const IgbObjectTypeRegistrationInvocation&)=delete;
    IgbObjectTypeRegistrationInvocation& operator=(const IgbObjectTypeRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbObjectRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual80066A60: metadata-pool lookup,56138(12,pool),conditional3E848.
class IgbObjectRootRegistrationInvocation {
public:
    IgbObjectRootRegistrationInvocation();~IgbObjectRootRegistrationInvocation();
    IgbObjectRootRegistrationInvocation(const IgbObjectRootRegistrationInvocation&)=delete;
    IgbObjectRootRegistrationInvocation& operator=(const IgbObjectRootRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbObjectRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual237D0/66B74/42DEC plain global load: never registers or checks flags.
class IgbObjectMetadataInvocation {
public:
    explicit IgbObjectMetadataInvocation(std::shared_ptr<IgbRegistrationMetaSlot>);
    MessengerCallResult resume();
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:
    std::shared_ptr<IgbRegistrationMetaSlot> slot_;
    std::shared_ptr<IgbRuntimeMetaType> result_;
    bool done_{},fault_{};
};
// Actual66AAC local8-byte probe. The leaf6665C fully writes both words before
// reading ArkCore+394; only aligned reads wholly inside that object are bound.
class IgbObjectLayoutProbeInvocation {
public:
    MessengerCallResult resume(std::shared_ptr<IgbRuntimeTypesServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:
    std::shared_ptr<IgbRuntimeTypeRegistry> core_;
    bool captured_{},done_{},fault_{};
    std::uint32_t value_{};
};
IgbRegistrationCallbacksFactory make_native_igb_object_registration_function_factory(
    std::shared_ptr<IgbObjectRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_object_registration_type_factory(
    std::shared_ptr<IgbObjectRegistrationServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
