#pragma once

#include "dw4/igb_bootstrap_object.hpp"

namespace dw4 {
class IgbFieldRegistryFactoryError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

struct IgbFieldRegistryBacking {
    IgbBootstrapPointer allocation;
    // An explicit mapping of physical pointer words to canonical field owners.
    // Construction itself does not read or initialize those pointer entries.
    std::shared_ptr<IgbDecodeFieldRegistry::Storage> fields;
};
enum class IgbFieldRegistryServiceKind {
    register_type,     //8002DD18, enabled branch only.
    reflected_factory,//8006546C with a typed internal-list result.
    object_pool,       //80068430 ->8005929C, distinct from607F4 handle lookup.
    allocate_backing,  //Captured allocation pool virtual+D8(16).
    object_virtual    //Unbound tail virtual; exact list leaves are native.
};
struct IgbFieldRegistryCall {
    IgbFieldRegistryServiceKind kind{};
    IgbBootstrapPointer object;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t handle{},bytes{},vtable_word{},virtual_slot{},argument{};
};
struct IgbFieldRegistryOperation {
    std::function<MessengerCallResult()> resume;
    IgbBootstrapPointer allocation_result;
    std::shared_ptr<ModelSceneObject> pool_result;
    IgbFieldRegistryBacking backing_result;
};
using IgbFieldRegistryFactory=std::function<std::shared_ptr<IgbFieldRegistryOperation>(const IgbFieldRegistryCall&)>;
struct IgbFieldRegistryServices {
    std::shared_ptr<IgbBootstrapServices> bootstrap;
    std::optional<std::shared_ptr<IgbRuntimeMetaType>> metadata; //805619CC.
    IgbFieldRegistryFactory factory;
private:
    friend class IgbFieldRegistryConstructorInvocation;
    friend class IgbFieldRegistryFactoryInvocation;
    std::vector<std::shared_ptr<void>> unfinished_;
};
enum class IgbFieldRegistryConstructor {
    derived, //80053CB8 ->5368C, final vtable80475CB0.
    base,    //8005368C, two base resets and16-byte backing allocation.
    reset,   //800536F8, reset only, no backing allocation.
    derived_reset //80053CF4 ->536F8, final vtable80475CB0, no allocation.
};
class IgbFieldRegistryConstructorInvocation {
public:
    IgbFieldRegistryConstructorInvocation(IgbBootstrapPointer,IgbFieldRegistryConstructor entry=IgbFieldRegistryConstructor::derived);
    ~IgbFieldRegistryConstructorInvocation();
    IgbFieldRegistryConstructorInvocation(const IgbFieldRegistryConstructorInvocation&)=delete;
    IgbFieldRegistryConstructorInvocation& operator=(const IgbFieldRegistryConstructorInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldRegistryServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
    std::shared_ptr<IgbDecodeFieldRegistry> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual8002DB4C. The disabled path constructs the native typed internal list;
// enabled registration/reflected construction remain explicit reached calls.
class IgbFieldRegistryFactoryInvocation {
public:
    explicit IgbFieldRegistryFactoryInvocation(std::shared_ptr<ModelSceneObject> pool={});
    ~IgbFieldRegistryFactoryInvocation();
    IgbFieldRegistryFactoryInvocation(const IgbFieldRegistryFactoryInvocation&)=delete;
    IgbFieldRegistryFactoryInvocation& operator=(const IgbFieldRegistryFactoryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldRegistryServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
    std::shared_ptr<IgbDecodeFieldRegistry> result() const;
    IgbBootstrapPointer allocation() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
