#pragma once

#include "dw4/igb_thread_registration.hpp"
#include "dw4/igb_file_factory.hpp"

namespace dw4 {
class IgbPlatformThreadRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbPlatformThreadRegistrationState {
    IgbRegistrationMetaSlot metadata; // Canonical80562070; absence is unknown.
};
enum class IgbPlatformThreadLookup {fully_registered,plain_parent};
struct IgbPlatformThreadRegistrationServices {
    std::shared_ptr<IgbPlatformThreadRegistrationState> state;
    std::shared_ptr<IgbThreadRegistrationState> parent; // Existing805614E8.
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    // Actual3AE84 uses a16-byte stack object. Its +08/+0C remain untouched.
    std::function<IgbBootstrapPointer()> local_storage;
    // Explicit metadata allocation identities, optional for canonical-only work.
    std::vector<IgbBootstrapPointer> physical_metadata;
    std::vector<std::shared_ptr<void>> retained;
};
std::shared_ptr<IgbRegistrationMetaSlot> igb_platform_thread_metadata_slot(
    const std::shared_ptr<IgbPlatformThreadRegistrationServices>&,
    IgbPlatformThreadLookup=IgbPlatformThreadLookup::fully_registered);
IgbTypeRegistrationArguments igb_platform_thread_registration_arguments(
    const std::shared_ptr<IgbPlatformThreadRegistrationState>&);
// Actual3AEF8: prerequisite21B94 precedes capture of the66204 arguments.
class IgbPlatformThreadRegistrationInvocation {
public:
    IgbPlatformThreadRegistrationInvocation();~IgbPlatformThreadRegistrationInvocation();
    IgbPlatformThreadRegistrationInvocation(const IgbPlatformThreadRegistrationInvocation&)=delete;
    IgbPlatformThreadRegistrationInvocation& operator=(const IgbPlatformThreadRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbPlatformThreadRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Full3AE48 calls3AED0/66188 before its reload; plain3AF68 reads the abstract
// manager cell directly. Neither path supplies a constructed runtime manager.
class IgbPlatformThreadMetadataInvocation {
public:
    explicit IgbPlatformThreadMetadataInvocation(IgbPlatformThreadLookup=IgbPlatformThreadLookup::fully_registered);
    ~IgbPlatformThreadMetadataInvocation();
    IgbPlatformThreadMetadataInvocation(const IgbPlatformThreadMetadataInvocation&)=delete;
    IgbPlatformThreadMetadataInvocation& operator=(const IgbPlatformThreadMetadataInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbPlatformThreadRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual3AE84:6665C, capture ArkCore, base/derived vtable stores, then the
// captured core's selected physical word. No initialization of local+08/+0C.
class IgbPlatformThreadProbeInvocation {
public:
    explicit IgbPlatformThreadProbeInvocation(IgbBootstrapPointer);
    ~IgbPlatformThreadProbeInvocation();
    IgbPlatformThreadProbeInvocation(const IgbPlatformThreadProbeInvocation&)=delete;
    IgbPlatformThreadProbeInvocation& operator=(const IgbPlatformThreadProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbPlatformThreadRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual3AF70 reloads80562070 and stores23920 at+40; it creates no fields.
class IgbPlatformThreadDefaultsInvocation {
public:
    IgbPlatformThreadDefaultsInvocation();~IgbPlatformThreadDefaultsInvocation();
    IgbPlatformThreadDefaultsInvocation(const IgbPlatformThreadDefaultsInvocation&)=delete;
    IgbPlatformThreadDefaultsInvocation& operator=(const IgbPlatformThreadDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbPlatformThreadRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_platform_thread_registration_function_factory(
    std::shared_ptr<IgbPlatformThreadRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_platform_thread_registration_type_factory(
    std::shared_ptr<IgbPlatformThreadRegistrationServices>,IgbTypeRegistrationFactory remaining={});
// Actual23A10 returns full concrete metadata3AE48 to6546C; no runtime object is
// allocated by this redirect adapter.
IgbFileFactoryFactory make_native_igb_platform_thread_redirect_factory(
    std::shared_ptr<IgbPlatformThreadRegistrationServices>,IgbFileFactoryFactory remaining={});
} // namespace dw4
