#pragma once

#include "dw4/igb_bootstrap_object.hpp"
#include "dw4/igb_dependencies.hpp"
#include "dw4/igb_registration_callbacks.hpp"

#include <array>

namespace dw4 {
struct IgbFieldOperationsServices;
class IgbMetaFieldFactoryError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbMetaFieldKind {unsigned_int,signed_int,boolean,string,object,pointer,unsigned_short,memory_reference,signed_short,signed_byte,enumeration};
struct IgbMetaFieldProfile {
    std::uint32_t factory{},constructor{},reset{},vtable{},bytes{},registration{},callback{},metadata_global{};
};
const IgbMetaFieldProfile& igb_meta_field_profile(IgbMetaFieldKind);
std::optional<IgbMetaFieldKind> igb_meta_field_kind_for_factory(std::uint32_t function);
// Bind an original descriptor+0C pointer to its canonical string header.
// This raw publication does not acquire or release a guest reference.
void igb_meta_field_set_name(const IgbBootstrapPointer&,
    const std::shared_ptr<IgbInternedString>&,std::uint32_t original_address);

enum class IgbMetaFieldServiceKind {
    register_type, //Profile's original wrapper ->66188(profile callback).
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
    std::shared_ptr<IgbRegistrationMetaSlot> metadata; //Legacy unsigned-int alias805614BC.
    // Distinct canonical cells for the other profiles; unsigned_int always
    // uses metadata above, retaining the existing API's single global identity.
    std::array<std::shared_ptr<IgbRegistrationMetaSlot>,11> metadata_slots;
    std::shared_ptr<IgbFieldOperationsServices> fields;
    IgbMetaFieldFactory factory;
    std::vector<IgbBootstrapPointer> completed_allocations;
private:
    friend class IgbMetaFieldConstructorInvocation;
    friend class IgbMetaFieldFactoryInvocation;
    std::vector<std::shared_ptr<void>> unfinished_;
};
// Profile constructor/reset, shared6388C/638E0, and reference6CBF4/6CC40.
// Complete scalar constructors execute638E0 twice; reference-derived ones
// execute it three times, preserving all intermediate vtable/field stores.
enum class IgbMetaFieldConstructor {specialized,base,reset,specialized_reset,reference_base,reference_reset};
class IgbMetaFieldConstructorInvocation {
public:
    IgbMetaFieldConstructorInvocation(IgbBootstrapPointer,
        IgbMetaFieldConstructor=IgbMetaFieldConstructor::specialized,
        IgbMetaFieldKind=IgbMetaFieldKind::unsigned_int);
    ~IgbMetaFieldConstructorInvocation();
    IgbMetaFieldConstructorInvocation(const IgbMetaFieldConstructorInvocation&)=delete;
    IgbMetaFieldConstructorInvocation& operator=(const IgbMetaFieldConstructorInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetaFieldFactoryServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual profile factory, default80022A90. Allocation and reflection results
// require explicit original addresses and typed bindings; a null allocation
// still reaches tail access. String/object tails also initialize default bytes.
class IgbMetaFieldFactoryInvocation {
public:
    explicit IgbMetaFieldFactoryInvocation(std::shared_ptr<ModelSceneObject> pool={},
        IgbMetaFieldKind=IgbMetaFieldKind::unsigned_int);
    ~IgbMetaFieldFactoryInvocation();
    IgbMetaFieldFactoryInvocation(const IgbMetaFieldFactoryInvocation&)=delete;
    IgbMetaFieldFactoryInvocation& operator=(const IgbMetaFieldFactoryInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetaFieldFactoryServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
    IgbBootstrapPointer allocation()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Connect the enabled profile factory to actual6546C/667F4. The outer
// profile caller retains the completed physical descriptor allocation.
IgbMetaFieldFactory make_native_igb_reflected_meta_field_factory(
    std::shared_ptr<IgbFileFactoryServices>,IgbMetaFieldFactory remaining={});
} // namespace dw4
