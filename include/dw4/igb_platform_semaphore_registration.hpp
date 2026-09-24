#pragma once

#include "dw4/igb_thread_dependencies.hpp"
#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_file_factory.hpp"

namespace dw4 {
class IgbPlatformSemaphoreRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbPlatformSemaphoreRegistrationState {
    IgbRegistrationMetaSlot metadata; // Canonical805620A0; absence is unknown.
};
enum class IgbPlatformSemaphoreLookup {fully_registered,plain_parent};
struct IgbPlatformSemaphoreRegistrationServices {
    IgbPlatformSemaphoreRegistrationServices();
    std::shared_ptr<IgbPlatformSemaphoreRegistrationState> state;
    std::shared_ptr<IgbThreadDependenciesServices> dependencies; // Existing805615EC parent cell.
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFieldFactoryTable> factories;
    std::shared_ptr<IgbFieldNameTable> names;
    std::shared_ptr<IgbFieldGlobalTable> globals;
    std::shared_ptr<IgbFieldOffsetTable> offsets;
    // Actual3B274 uses a36-byte stack object. Only header and name are written.
    std::function<IgbBootstrapPointer()> local_storage;
    std::vector<IgbBootstrapPointer> physical_metadata;
    std::vector<std::shared_ptr<void>> retained;
};
std::shared_ptr<IgbRegistrationMetaSlot> igb_platform_semaphore_metadata_slot(
    const std::shared_ptr<IgbPlatformSemaphoreRegistrationServices>&,
    IgbPlatformSemaphoreLookup=IgbPlatformSemaphoreLookup::fully_registered);
IgbTypeRegistrationArguments igb_platform_semaphore_registration_arguments(
    const std::shared_ptr<IgbPlatformSemaphoreRegistrationState>&);
// Actual3B344: prerequisite21B94 precedes capture of the66204 arguments.
class IgbPlatformSemaphoreRegistrationInvocation {
public:
    IgbPlatformSemaphoreRegistrationInvocation();~IgbPlatformSemaphoreRegistrationInvocation();
    IgbPlatformSemaphoreRegistrationInvocation(const IgbPlatformSemaphoreRegistrationInvocation&)=delete;
    IgbPlatformSemaphoreRegistrationInvocation& operator=(const IgbPlatformSemaphoreRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Full3B238 runs3B31C/66188 as required, then reloads805620A0.
// Plain3B3B4 loads805615EC without registration or allocation.
class IgbPlatformSemaphoreMetadataInvocation {
public:
    explicit IgbPlatformSemaphoreMetadataInvocation(IgbPlatformSemaphoreLookup=IgbPlatformSemaphoreLookup::fully_registered);
    ~IgbPlatformSemaphoreMetadataInvocation();
    IgbPlatformSemaphoreMetadataInvocation(const IgbPlatformSemaphoreMetadataInvocation&)=delete;
    IgbPlatformSemaphoreMetadataInvocation& operator=(const IgbPlatformSemaphoreMetadataInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual3B274 resets the common header, captures ArkCore, writes the name null
// and derived vtable, captures the selected word, then restores both base tables.
class IgbPlatformSemaphoreProbeInvocation {
public:
    explicit IgbPlatformSemaphoreProbeInvocation(IgbBootstrapPointer);
    ~IgbPlatformSemaphoreProbeInvocation();
    IgbPlatformSemaphoreProbeInvocation(const IgbPlatformSemaphoreProbeInvocation&)=delete;
    IgbPlatformSemaphoreProbeInvocation& operator=(const IgbPlatformSemaphoreProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual3B3BC: batch four fields, two75AC4(1) default setters, struct size0C,
// schema659C0, then a fresh canonical805620A0 reload and metadata+40=2612C.
// The fourth factory2450C remains an explicit igStructMetaField dependency.
class IgbPlatformSemaphoreDefaultsInvocation {
public:
    IgbPlatformSemaphoreDefaultsInvocation();~IgbPlatformSemaphoreDefaultsInvocation();
    IgbPlatformSemaphoreDefaultsInvocation(const IgbPlatformSemaphoreDefaultsInvocation&)=delete;
    IgbPlatformSemaphoreDefaultsInvocation& operator=(const IgbPlatformSemaphoreDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbPlatformSemaphoreRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_platform_semaphore_registration_function_factory(
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_platform_semaphore_registration_type_factory(
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices>,IgbTypeRegistrationFactory remaining={});
// Redirect26160 returns full metadata3B238 to6546C. It does not construct a
// semaphore, initialize its inline OS structure or bind a host synchronization primitive.
IgbFileFactoryFactory make_native_igb_platform_semaphore_redirect_factory(
    std::shared_ptr<IgbPlatformSemaphoreRegistrationServices>,IgbFileFactoryFactory remaining={});
} // namespace dw4
