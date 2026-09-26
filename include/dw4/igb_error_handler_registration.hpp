#pragma once

#include "dw4/igb_thread_dependencies.hpp"

namespace dw4 {
class IgbErrorHandlerError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbErrorHandlerLookup {fully_registered,plain};
struct IgbErrorHandlerState {
    // Unknown until original storage has a represented producer. Allocating
    // this owner does not execute static initialization or create a singleton.
    std::optional<IgbBootstrapPointer> singleton; //80562120.
    std::optional<std::uint8_t> teardown_guard; //80562124.
    std::vector<IgbBootstrapPointer> retained;
};
enum class IgbErrorHandlerServiceKind {pool_for_handle,create_object,destroy_object};
struct IgbErrorHandlerCall {
    IgbErrorHandlerServiceKind kind{};
    IgbBootstrapPointer object;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t handle{};
};
struct IgbErrorHandlerOperation {
    std::function<MessengerCallResult()> resume;
    IgbBootstrapPointer object_result;
    std::shared_ptr<ModelSceneObject> pool_result;
    // Borrow for this resume only. Never retain the caller's guard.
    std::function<MessengerCallResult(const std::function<void()>&)> resume_checked;
};
using IgbErrorHandlerFactory=std::function<std::shared_ptr<IgbErrorHandlerOperation>(const IgbErrorHandlerCall&)>;
struct IgbErrorHandlerServices {
    std::shared_ptr<IgbRegistrationMetaSlot> metadata; //80561C2C.
    std::shared_ptr<IgbErrorHandlerState> state;
    // Alias the existing805621F0 owner, for example thread startup's member.
    // This is distinct from the metadata allocation handle805621F4.
    std::shared_ptr<std::optional<std::uint32_t>> pool_handle;
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFilePoolsServices> pools;
    std::shared_ptr<IgbFileFactoryServices> reflected;
    std::function<IgbBootstrapPointer()> local_storage;
    IgbErrorHandlerFactory factory;
    std::vector<std::shared_ptr<void>> retained;
};
IgbTypeRegistrationArguments igb_error_handler_type_registration_arguments(
    const std::shared_ptr<IgbErrorHandlerServices>&);
// Actual31278: prerequisites21B94, then the existing native66204 registrar.
class IgbErrorHandlerRegistrationInvocation {
public:
    IgbErrorHandlerRegistrationInvocation();~IgbErrorHandlerRegistrationInvocation();
    IgbErrorHandlerRegistrationInvocation(const IgbErrorHandlerRegistrationInvocation&)=delete;
    IgbErrorHandlerRegistrationInvocation& operator=(const IgbErrorHandlerRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbErrorHandlerServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual311D4/312F0 versus the plain94010 global load. There is no recovered
// allocation-only getter for this class at31198.
class IgbErrorHandlerLookupInvocation {
public:
    explicit IgbErrorHandlerLookupInvocation(IgbErrorHandlerLookup=IgbErrorHandlerLookup::fully_registered);
    ~IgbErrorHandlerLookupInvocation();
    IgbErrorHandlerLookupInvocation(const IgbErrorHandlerLookupInvocation&)=delete;
    IgbErrorHandlerLookupInvocation& operator=(const IgbErrorHandlerLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbErrorHandlerServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual31210, including6665C and the ArkCore capture before the class VT store.
class IgbErrorHandlerLayoutProbeInvocation {
public:
    explicit IgbErrorHandlerLayoutProbeInvocation(IgbBootstrapPointer);
    ~IgbErrorHandlerLayoutProbeInvocation();
    IgbErrorHandlerLayoutProbeInvocation(const IgbErrorHandlerLayoutProbeInvocation&)=delete;
    IgbErrorHandlerLayoutProbeInvocation& operator=(const IgbErrorHandlerLayoutProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbErrorHandlerServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual3119C: always31250, fresh metadata, existing6546C with captured pool.
class IgbErrorHandlerObjectFactoryInvocation {
public:
    explicit IgbErrorHandlerObjectFactoryInvocation(std::shared_ptr<ModelSceneObject> pool);
    ~IgbErrorHandlerObjectFactoryInvocation();
    IgbErrorHandlerObjectFactoryInvocation(const IgbErrorHandlerObjectFactoryInvocation&)=delete;
    IgbErrorHandlerObjectFactoryInvocation& operator=(const IgbErrorHandlerObjectFactoryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbErrorHandlerServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    IgbBootstrapPointer result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual470F4, called by31338->47080. Known nonnull/null and unknown globals
// remain distinct; only the reached constructor publishes the singleton.
class IgbErrorHandlerSingletonInvocation {
public:
    IgbErrorHandlerSingletonInvocation();~IgbErrorHandlerSingletonInvocation();
    IgbErrorHandlerSingletonInvocation(const IgbErrorHandlerSingletonInvocation&)=delete;
    IgbErrorHandlerSingletonInvocation& operator=(const IgbErrorHandlerSingletonInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbErrorHandlerServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    IgbBootstrapPointer result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual470A0, called by deferred31358. Destruction66E1C is an explicit
// retained operation, before the null singleton and guard-byte stores.
class IgbErrorHandlerTeardownInvocation {
public:
    IgbErrorHandlerTeardownInvocation();~IgbErrorHandlerTeardownInvocation();
    IgbErrorHandlerTeardownInvocation(const IgbErrorHandlerTeardownInvocation&)=delete;
    IgbErrorHandlerTeardownInvocation& operator=(const IgbErrorHandlerTeardownInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbErrorHandlerServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbErrorHandlerFactory make_native_igb_error_handler_service_factory(
    std::shared_ptr<IgbErrorHandlerServices>,IgbErrorHandlerFactory remaining={});
IgbRegistrationCallbacksFactory make_native_igb_error_handler_function_factory(
    std::shared_ptr<IgbErrorHandlerServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_error_handler_type_factory(
    std::shared_ptr<IgbErrorHandlerServices>,IgbTypeRegistrationFactory remaining={});
// Only the four reached literal BLRs and plain94010. These do not read the
// receiver. Other804759D8 virtual functions remain explicit dependencies.
IgbObjectInitFactory make_native_igb_error_handler_object_init_factory(
    std::shared_ptr<IgbErrorHandlerServices>,IgbObjectInitFactory remaining={});
} // namespace dw4
