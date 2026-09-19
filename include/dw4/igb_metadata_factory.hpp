#pragma once

#include "dw4/igb_field_registry_factory.hpp"
#include "dw4/igb_dependencies.hpp"

namespace dw4 {
class IgbMetadataFactoryError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Bind a live original +60 string pointer and its canonical header owner.
// This is for represented callbacks; it does not retain/release the pointer.
void igb_metadata_set_pool_name(const IgbBootstrapPointer&,
    const std::shared_ptr<IgbInternedString>&,std::uint32_t original_address);

enum class IgbMetadataFactoryServiceKind {
    register_type, //8002A0A8, only on the enabled branch.
    reflected_factory, //8006546C with an explicit typed metadata allocation.
    create_fields, //8002DB4C(NULL); native field-registry services take precedence.
    object_virtual //Unbound fresh physical virtual+2C/+24; exact leaves are native.
};
struct IgbMetadataFactoryCall {
    IgbMetadataFactoryServiceKind kind{};
    IgbBootstrapPointer object;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t vtable_word{},virtual_slot{},argument{};
};
struct IgbMetadataFactoryOperation {
    std::function<MessengerCallResult()> resume;
    IgbBootstrapPointer allocation_result;
    std::shared_ptr<IgbDecodeFieldRegistry> fields_result;
};
using IgbMetadataFactory=std::function<std::shared_ptr<IgbMetadataFactoryOperation>(const IgbMetadataFactoryCall&)>;
struct IgbMetadataFactoryServices {
    std::shared_ptr<IgbBootstrapServices> bootstrap;
    std::shared_ptr<IgbFieldRegistryServices> field_registries;
    // Shares actual80562140, string allocation/initialization and release calls.
    std::shared_ptr<IgbDependenciesServices> dependencies;
    std::optional<std::shared_ptr<IgbRuntimeMetaType>> metadata; //8056174C.
    IgbMetadataFactory factory;
private:
    friend class IgbMetadataConstructorInvocation;
    friend class IgbMetadataFactoryInvocation;
    std::vector<std::shared_ptr<void>> unfinished_;
};
enum class IgbMetadataConstructor {construct,reset}; //651B4 or65218.
class IgbMetadataConstructorInvocation {
public:
    IgbMetadataConstructorInvocation(IgbBootstrapPointer,
        IgbMetadataConstructor entry=IgbMetadataConstructor::construct);
    ~IgbMetadataConstructorInvocation();
    IgbMetadataConstructorInvocation(const IgbMetadataConstructorInvocation&)=delete;
    IgbMetadataConstructorInvocation& operator=(const IgbMetadataConstructorInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataFactoryServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
class IgbMetadataFactoryInvocation {
public:
    explicit IgbMetadataFactoryInvocation(std::shared_ptr<ModelSceneObject> pool={});
    ~IgbMetadataFactoryInvocation();
    IgbMetadataFactoryInvocation(const IgbMetadataFactoryInvocation&)=delete;
    IgbMetadataFactoryInvocation& operator=(const IgbMetadataFactoryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataFactoryServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result() const;
    IgbBootstrapPointer allocation() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_metadata_factory(
    std::shared_ptr<IgbMetadataFactoryServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
