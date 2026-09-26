#pragma once

#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_metadata_factory.hpp"
#include "dw4/igb_registration_callbacks.hpp"

namespace dw4 {
struct IgbObjectFieldConstructServices;
class IgbMemoryPoolRegistrationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
enum class IgbMemoryPoolKind { abstract_pool, malloc_pool };
enum class IgbMemoryPoolLookup { allocate_if_null, fully_registered, plain };
struct IgbMemoryPoolRegistrationServices {
    IgbMemoryPoolRegistrationServices();
    std::shared_ptr<IgbRegistrationMetaSlot> malloc_metadata_slot; // Canonical80561FB0; distinct from the abstract class.
    std::shared_ptr<IgbRegistrationMetaSlot> metadata_slot; // Canonical805618B4; absence is unknown.
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFilePoolsServices> pools;
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbFieldFactoryTable> factories;
    std::shared_ptr<IgbFieldNameTable> names;
    std::shared_ptr<IgbFieldGlobalTable> globals;
    std::shared_ptr<IgbFieldOffsetTable> offsets;
    // Existing typed metadata allocations, also populated by the lazy getter.
    std::vector<IgbBootstrapPointer> physical_metadata;
    std::vector<std::shared_ptr<void>> retained;
    // Actual39CC0 uses a0xA0 local whose unknown bytes remain unknown.
    std::function<IgbBootstrapPointer()> malloc_probe_storage;
    std::shared_ptr<IgbObjectFieldConstructServices> object_cleanup;
};
IgbTypeRegistrationArguments igb_memory_pool_registration_arguments(
    const std::shared_ptr<IgbMemoryPoolRegistrationServices>&,IgbMemoryPoolKind=IgbMemoryPoolKind::abstract_pool);

// Actual2BF70/39D94 directly invoke66204; neither calls21B94. The concrete
// malloc profile reaches native39CC0 and still-unimplemented39E28 defaults.
class IgbMemoryPoolRegistrationInvocation {
public:
    explicit IgbMemoryPoolRegistrationInvocation(IgbMemoryPoolKind=IgbMemoryPoolKind::abstract_pool);
    ~IgbMemoryPoolRegistrationInvocation();
    IgbMemoryPoolRegistrationInvocation(const IgbMemoryPoolRegistrationInvocation&)=delete;
    IgbMemoryPoolRegistrationInvocation& operator=(const IgbMemoryPoolRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryPoolRegistrationServices> services={});
    bool completed()const noexcept;
    bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Actual2BED0 allocates only on null;2BF0C registers on null/flag4 clear;
//38F58 only reads the cell. All preserve fresh post-call canonical reloads.
class IgbMemoryPoolMetadataLookupInvocation {
public:
    explicit IgbMemoryPoolMetadataLookupInvocation(IgbMemoryPoolLookup=IgbMemoryPoolLookup::fully_registered);
    ~IgbMemoryPoolMetadataLookupInvocation();
    IgbMemoryPoolMetadataLookupInvocation(const IgbMemoryPoolMetadataLookupInvocation&)=delete;
    IgbMemoryPoolMetadataLookupInvocation& operator=(const IgbMemoryPoolMetadataLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryPoolRegistrationServices> services={});
    bool completed()const noexcept;
    bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Actual2BFF8: fourteen descriptors; two separately selected Boolean defaults;
//659C0 schema; fresh canonical metadata+3C=2C0A0. The redirect is deferred.
class IgbMemoryPoolDefaultsInvocation {
public:
    IgbMemoryPoolDefaultsInvocation();
    ~IgbMemoryPoolDefaultsInvocation();
    IgbMemoryPoolDefaultsInvocation(const IgbMemoryPoolDefaultsInvocation&)=delete;
    IgbMemoryPoolDefaultsInvocation& operator=(const IgbMemoryPoolDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryPoolRegistrationServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;
    bool faulted()const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Actual39CC0:8B264/58ABC/6665C local construction, captured core+394 word,
// then fresh+94 and+74 object release. Does not initialize a runtime allocator.
class IgbMallocMemoryPoolProbeInvocation {
public:
    explicit IgbMallocMemoryPoolProbeInvocation(IgbBootstrapPointer);
    ~IgbMallocMemoryPoolProbeInvocation();
    IgbMallocMemoryPoolProbeInvocation(const IgbMallocMemoryPoolProbeInvocation&)=delete;
    IgbMallocMemoryPoolProbeInvocation& operator=(const IgbMallocMemoryPoolProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryPoolRegistrationServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;
    bool faulted()const noexcept;
    std::uint32_t value()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_memory_pool_registration_function_factory(
    std::shared_ptr<IgbMemoryPoolRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_memory_pool_registration_type_factory(
    std::shared_ptr<IgbMemoryPoolRegistrationServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
