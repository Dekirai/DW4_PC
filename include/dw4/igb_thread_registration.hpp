#pragma once

#include "dw4/igb_registration_callbacks.hpp"
#include "dw4/igb_file_pools.hpp"
#include "dw4/igb_bootstrap_object.hpp"

namespace dw4 {
class IgbThreadRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbThreadRegistrationState {
    IgbRegistrationMetaSlot metadata; // Canonical805614E8; absence is unknown.
};
// Only the proven store800947EC inside800946A0. This does not initialize
// allocators, run the entire static initializer, or supply an actual pool.
void publish_original_igb_metadata_pool_handle(IgbTypeRegistrationServices&) noexcept;
IgbTypeRegistrationArguments igb_thread_type_registration_arguments(const std::shared_ptr<IgbThreadRegistrationState>&);
// Actual800238A8: prerequisite guard then the exact generic registration.
class IgbThreadTypeRegistrationInvocation {
public:
    IgbThreadTypeRegistrationInvocation();~IgbThreadTypeRegistrationInvocation();
    IgbThreadTypeRegistrationInvocation(const IgbThreadTypeRegistrationInvocation&)=delete;
    IgbThreadTypeRegistrationInvocation& operator=(const IgbThreadTypeRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbThreadRegistrationState>,
        std::shared_ptr<IgbRegistrationCallbacksServices>,std::shared_ptr<IgbTypeRegistrationServices>);
    bool completed() const noexcept;bool faulted() const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual80023844, also reached by80023920. Runs the native80066188 wrapper
// if the canonical cell is null or its ready bit is clear, then reloads it.
// Bind the matching state through the function adapter when composing services.
class IgbThreadMetadataInvocation {
public:
    explicit IgbThreadMetadataInvocation(std::shared_ptr<IgbThreadRegistrationState>);
    ~IgbThreadMetadataInvocation();
    IgbThreadMetadataInvocation(const IgbThreadMetadataInvocation&)=delete;
    IgbThreadMetadataInvocation& operator=(const IgbThreadMetadataInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbRegistrationCallbacksServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_thread_registration_function_factory(
    std::shared_ptr<IgbThreadRegistrationState>,std::shared_ptr<IgbRegistrationCallbacksServices>,
    std::shared_ptr<IgbTypeRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
// Handles exact800238A8 register_parent and80023844/80023920 get_parent.
IgbTypeRegistrationFactory make_native_igb_thread_type_factory(
    std::shared_ptr<IgbThreadRegistrationState>,std::shared_ptr<IgbRegistrationCallbacksServices>,
    std::shared_ptr<IgbTypeRegistrationServices>,IgbTypeRegistrationFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_registration_pool_factory(
    std::shared_ptr<IgbFilePoolsServices>,IgbTypeRegistrationFactory remaining={});
IgbBootstrapFactory make_native_igb_bootstrap_pool_factory(
    std::shared_ptr<IgbFilePoolsServices>,IgbBootstrapFactory remaining={});
} // namespace dw4
