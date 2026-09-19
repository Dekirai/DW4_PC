#pragma once

#include "dw4/igb_bootstrap_object.hpp"
#include "dw4/igb_dependencies.hpp"
#include "dw4/igb_registration_callbacks.hpp"

namespace dw4 {
class IgbMetaFieldFactoryError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
// Bind an original descriptor+0C pointer to its canonical string header.
// This raw publication does not acquire or release a guest reference.
void igb_meta_field_set_name(const IgbBootstrapPointer&,
    const std::shared_ptr<IgbInternedString>&,std::uint32_t original_address);

enum class IgbMetaFieldServiceKind {
    register_type, //80022CB0 ->66188(80022CD8).
    reflected_factory, //8006546C, explicit typed descriptor result.
    object_virtual //Fresh original+2C/+24; verified exact BLRs execute natively.
};
struct IgbMetaFieldCall {
    IgbMetaFieldServiceKind kind{};
    IgbBootstrapPointer object;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t function{},vtable_word{},virtual_slot{},argument{};
};
struct IgbMetaFieldOperation {
    std::function<MessengerCallResult()> resume;
    IgbBootstrapPointer allocation_result;
};
using IgbMetaFieldFactory=std::function<std::shared_ptr<IgbMetaFieldOperation>(const IgbMetaFieldCall&)>;
struct IgbMetaFieldFactoryServices {
    std::shared_ptr<IgbBootstrapServices> bootstrap;
    std::shared_ptr<IgbDependenciesServices> dependencies;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbRegistrationMetaSlot> metadata; //Alias of805614BC.
    IgbMetaFieldFactory factory;
    std::vector<IgbBootstrapPointer> completed_allocations;
private:
    friend class IgbMetaFieldConstructorInvocation;
    friend class IgbMetaFieldFactoryInvocation;
    std::vector<std::shared_ptr<void>> unfinished_;
};
// 759E0(object,1),6388C,638E0,75A2C respectively. The specialized
// constructor executes the original base reset twice.
enum class IgbMetaFieldConstructor {specialized,base,reset,specialized_reset};
class IgbMetaFieldConstructorInvocation {
public:
    IgbMetaFieldConstructorInvocation(IgbBootstrapPointer,
        IgbMetaFieldConstructor=IgbMetaFieldConstructor::specialized);
    ~IgbMetaFieldConstructorInvocation();
    IgbMetaFieldConstructorInvocation(const IgbMetaFieldConstructorInvocation&)=delete;
    IgbMetaFieldConstructorInvocation& operator=(const IgbMetaFieldConstructorInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetaFieldFactoryServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual80022A90. Allocation and reflection results require explicit original
// addresses and typed bindings; a null allocation still reaches tail access.
class IgbMetaFieldFactoryInvocation {
public:
    explicit IgbMetaFieldFactoryInvocation(std::shared_ptr<ModelSceneObject> pool={});
    ~IgbMetaFieldFactoryInvocation();
    IgbMetaFieldFactoryInvocation(const IgbMetaFieldFactoryInvocation&)=delete;
    IgbMetaFieldFactoryInvocation& operator=(const IgbMetaFieldFactoryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetaFieldFactoryServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
    IgbBootstrapPointer allocation()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
