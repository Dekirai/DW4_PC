#pragma once

#include "dw4/igb_thread_dependencies.hpp"
#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_file_factory.hpp"
#include "dw4/igb_named_type_registration.hpp"

#include <unordered_map>

namespace dw4 {
class IgbGamecubeThreadRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbGamecubeThreadRegistrationState {
    IgbRegistrationMetaSlot metadata; // Canonical80562074; absence is unknown.
};
enum class IgbGamecubeThreadLookup {fully_registered,plain_parent};
struct IgbGamecubeThreadRegistrationServices {
    IgbGamecubeThreadRegistrationServices();
    std::shared_ptr<IgbGamecubeThreadRegistrationState> state;
    std::shared_ptr<IgbNamedTypeRegistrationServices> parent; // Existing80561504 thread cell.
    std::shared_ptr<IgbThreadDependenciesServices> dependencies; // Existing805615EC semaphore cell.
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFieldFactoryTable> factories;
    std::shared_ptr<IgbFieldNameTable> names;
    std::shared_ptr<IgbFieldGlobalTable> globals;
    std::shared_ptr<IgbFieldOffsetTable> offsets;
    // Actual3AFC0 uses a0x388-byte stack object. Only common header/name and
    // the thread-done semaphore member+380 are initialized by this probe.
    std::function<IgbBootstrapPointer()> local_storage;
    std::vector<IgbBootstrapPointer> physical_metadata;
    // Captured original character-pointer identity for fresh+08 cleanup.
    std::unordered_map<std::uint32_t,std::shared_ptr<IgbInternedString>> strings;
    std::vector<std::shared_ptr<void>> retained;
};
std::shared_ptr<IgbRegistrationMetaSlot> igb_gamecube_thread_metadata_slot(
    const std::shared_ptr<IgbGamecubeThreadRegistrationServices>&,
    IgbGamecubeThreadLookup=IgbGamecubeThreadLookup::fully_registered);
IgbTypeRegistrationArguments igb_gamecube_thread_registration_arguments(
    const std::shared_ptr<IgbGamecubeThreadRegistrationState>&);
// Actual3B0E0: prerequisite21B94 precedes capture of the66204 arguments.
class IgbGamecubeThreadRegistrationInvocation {
public:
    IgbGamecubeThreadRegistrationInvocation();~IgbGamecubeThreadRegistrationInvocation();
    IgbGamecubeThreadRegistrationInvocation(const IgbGamecubeThreadRegistrationInvocation&)=delete;
    IgbGamecubeThreadRegistrationInvocation& operator=(const IgbGamecubeThreadRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbGamecubeThreadRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Full3AF84 runs3B0B8/66188 as required, then reloads80562074.
// Plain3B154 loads80561504 without registration or allocation.
class IgbGamecubeThreadMetadataInvocation {
public:
    explicit IgbGamecubeThreadMetadataInvocation(IgbGamecubeThreadLookup=IgbGamecubeThreadLookup::fully_registered);
    ~IgbGamecubeThreadMetadataInvocation();
    IgbGamecubeThreadMetadataInvocation(const IgbGamecubeThreadMetadataInvocation&)=delete;
    IgbGamecubeThreadMetadataInvocation& operator=(const IgbGamecubeThreadMetadataInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbGamecubeThreadRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual3AFC0 captures ArkCore before publishing the named-object table, writes
// null name/semaphore members, reads the selected word and restores the tables.
// Cleanup skips the semaphore via retained literal zero but reloads the name.
class IgbGamecubeThreadProbeInvocation {
public:
    explicit IgbGamecubeThreadProbeInvocation(IgbBootstrapPointer);
    ~IgbGamecubeThreadProbeInvocation();
    IgbGamecubeThreadProbeInvocation(const IgbGamecubeThreadProbeInvocation&)=delete;
    IgbGamecubeThreadProbeInvocation& operator=(const IgbGamecubeThreadProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbGamecubeThreadRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual3B15C: batch nine fields; struct sizes318/28; lazy26020 semaphore type
// and construct-byte stores; bool3EC68(1); schema659C0; fresh metadata+40=23CD4.
class IgbGamecubeThreadDefaultsInvocation {
public:
    IgbGamecubeThreadDefaultsInvocation();~IgbGamecubeThreadDefaultsInvocation();
    IgbGamecubeThreadDefaultsInvocation(const IgbGamecubeThreadDefaultsInvocation&)=delete;
    IgbGamecubeThreadDefaultsInvocation& operator=(const IgbGamecubeThreadDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbGamecubeThreadRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_gamecube_thread_registration_function_factory(
    std::shared_ptr<IgbGamecubeThreadRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_gamecube_thread_registration_type_factory(
    std::shared_ptr<IgbGamecubeThreadRegistrationServices>,IgbTypeRegistrationFactory remaining={});
// Redirect23E20 returns full metadata3AF84 to6546C. It does not construct a
// thread, initialize OS thread state or create a host thread.
IgbFileFactoryFactory make_native_igb_gamecube_thread_redirect_factory(
    std::shared_ptr<IgbGamecubeThreadRegistrationServices>,IgbFileFactoryFactory remaining={});
} // namespace dw4
