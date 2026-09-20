#pragma once

#include "dw4/igb_ark_core_bootstrap.hpp"
#include "dw4/igb_metadata_operations.hpp"

namespace dw4 {
class IgbTypeListFactoryError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbTypeListFactoryEntry {children,registry}; //8002DE88 /8002DB4C.
struct IgbTypeListBacking {
    IgbBootstrapPointer allocation;
    // Explicit metadata-element identity mapping. Allocation alone does not
    // establish initialized null pointer entries or allocator capacity.
    std::shared_ptr<IgbRuntimeTypeList::Storage> types;
};
enum class IgbTypeListServiceKind {
    register_type, //2E054 or2DD18, unless actual callback services are bound.
    reflected_factory, //6546C with an explicitly typed metadata-list result.
    object_pool, //68430 header byte ->5929C; separate from607F4 manager handles.
    allocate_backing, //Captured pool+D8(16).
    object_virtual //Fresh unknown tail binding; verified BLR leaves are native.
};
struct IgbTypeListCall {
    IgbTypeListServiceKind kind{};
    IgbTypeListFactoryEntry entry{};
    IgbBootstrapPointer object;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t function{},handle{},bytes{},vtable_word{},virtual_slot{},argument{};
};
struct IgbTypeListOperation {
    std::function<MessengerCallResult()> resume;
    IgbBootstrapPointer allocation_result;
    std::shared_ptr<ModelSceneObject> pool_result;
    IgbTypeListBacking backing_result;
};
using IgbTypeListFactory=std::function<std::shared_ptr<IgbTypeListOperation>(const IgbTypeListCall&)>;
struct IgbTypeListServices {
    std::shared_ptr<IgbBootstrapServices> bootstrap;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    // Aliases of canonical globals. registry_metadata can alias the existing
    // IgbFieldRegistryServices::metadata cell; never seed a second cache.
    std::shared_ptr<IgbRegistrationMetaSlot> children_metadata; //805619D4.
    std::shared_ptr<IgbRegistrationMetaSlot> registry_metadata; //805619CC.
    IgbTypeListFactory factory;
    // Keeps explicit physical allocations available to later native list
    // append bindings after an adapter returns only the canonical owner.
    std::vector<IgbBootstrapPointer> completed_allocations;
private:
    friend class IgbTypeListConstructorInvocation;
    friend class IgbTypeListFactoryInvocation;
    std::vector<std::shared_ptr<void>> unfinished_;
};
enum class IgbTypeListConstructor {derived,base,reset,derived_reset}; //53CB8 /5368C /536F8 /53CF4.
class IgbTypeListConstructorInvocation {
public:
    IgbTypeListConstructorInvocation(IgbBootstrapPointer,IgbTypeListConstructor entry=IgbTypeListConstructor::base);
    ~IgbTypeListConstructorInvocation();
    IgbTypeListConstructorInvocation(const IgbTypeListConstructorInvocation&)=delete;
    IgbTypeListConstructorInvocation& operator=(const IgbTypeListConstructorInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbTypeListServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeTypeList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
class IgbTypeListFactoryInvocation {
public:
    explicit IgbTypeListFactoryInvocation(std::shared_ptr<ModelSceneObject> pool={},IgbTypeListFactoryEntry entry=IgbTypeListFactoryEntry::children);
    ~IgbTypeListFactoryInvocation();
    IgbTypeListFactoryInvocation(const IgbTypeListFactoryInvocation&)=delete;
    IgbTypeListFactoryInvocation& operator=(const IgbTypeListFactoryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbTypeListServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeTypeList> result()const;
    IgbBootstrapPointer allocation()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbMetadataOperationsFactory make_native_igb_type_list_metadata_factory(
    std::shared_ptr<IgbTypeListServices>,IgbMetadataOperationsFactory remaining={});
IgbArkCoreFactory make_native_igb_type_list_bootstrap_factory(
    std::shared_ptr<IgbTypeListServices>,IgbArkCoreFactory remaining={});
} // namespace dw4
