#pragma once

#include "dw4/igb_memory_pool_registration.hpp"

namespace dw4 {
class IgbEventTrackerRegistrationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
enum class IgbEventTrackerLookup { fully_registered, plain };
struct IgbEventTrackerRegistrationServices {
    // Share the actual80561BA4 cell already used by the malloc30C50 lazy getter.
    // This owner also supplies the existing types, callbacks and cleanup map.
    std::shared_ptr<IgbMemoryPoolRegistrationServices> memory_pool;
    // Actual30CC8 uses a0x2530-byte local. Do not initialize unknown padding.
    std::function<IgbBootstrapPointer()> local_storage;
    std::vector<std::shared_ptr<void>> retained;
};
std::shared_ptr<IgbRegistrationMetaSlot> igb_event_tracker_metadata_slot(
    const std::shared_ptr<IgbEventTrackerRegistrationServices>&);
IgbTypeRegistrationArguments igb_event_tracker_registration_arguments(
    const std::shared_ptr<IgbEventTrackerRegistrationServices>&);

// Actual30D78:21B94 then66204. Defaults30E10 remains an explicit lower boundary.
class IgbEventTrackerRegistrationInvocation {
public:
    IgbEventTrackerRegistrationInvocation();
    ~IgbEventTrackerRegistrationInvocation();
    IgbEventTrackerRegistrationInvocation(const IgbEventTrackerRegistrationInvocation&)=delete;
    IgbEventTrackerRegistrationInvocation& operator=(const IgbEventTrackerRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbEventTrackerRegistrationServices> services={});
    bool completed()const noexcept;
    bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Actual30C8C/30DF0 full getter and93D1C plain read; canonical class_flags
// represents metadata+24 as in the existing generic registration services.
// The allocation-only30C50 operation remains in the memory-pool module.
class IgbEventTrackerLookupInvocation {
public:
    explicit IgbEventTrackerLookupInvocation(IgbEventTrackerLookup=IgbEventTrackerLookup::fully_registered);
    ~IgbEventTrackerLookupInvocation();
    IgbEventTrackerLookupInvocation(const IgbEventTrackerLookupInvocation&)=delete;
    IgbEventTrackerLookupInvocation& operator=(const IgbEventTrackerLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbEventTrackerRegistrationServices> services={});
    bool completed()const noexcept;
    bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Actual30CC8:6665C, early ArkCore capture, derived vtable and12 null members,
// captured core+394 local word, then261F4(local,-1) with12 fresh object releases.
// Only this nondeleting cleanup call is exposed; general261F4 flags are not.
class IgbEventTrackerProbeInvocation {
public:
    explicit IgbEventTrackerProbeInvocation(IgbBootstrapPointer);
    ~IgbEventTrackerProbeInvocation();
    IgbEventTrackerProbeInvocation(const IgbEventTrackerProbeInvocation&)=delete;
    IgbEventTrackerProbeInvocation& operator=(const IgbEventTrackerProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbEventTrackerRegistrationServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;
    bool faulted()const noexcept;
    std::uint32_t value()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_event_tracker_registration_function_factory(
    std::shared_ptr<IgbEventTrackerRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_event_tracker_registration_type_factory(
    std::shared_ptr<IgbEventTrackerRegistrationServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
