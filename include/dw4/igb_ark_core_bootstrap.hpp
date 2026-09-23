#pragma once

#include "dw4/igb_bootstrap_object.hpp"
#include "dw4/igb_file_pools.hpp"
#include "dw4/igb_registration_callbacks.hpp"

namespace dw4 {
class IgbArkCoreBootstrapError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbArkCoreObject {
    IgbBootstrapPointer allocation;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;
};
// Actual8003D234 stores into represented0x3A0 physical storage. No allocation,
// global publication, metadata registration or loader initialization is implied.
void igb_construct_ark_core(const IgbArkCoreObject&);

enum class IgbArkCoreServiceKind {
    platform_initialize, //80092280.
    allocate_manager,    //8006070C(0x19F4).
    construct_manager,   //8005AF08, supplied physical allocation.
    manager_virtual,     //Fresh fallback manager+A8 / +AC.
    create_type_list,    //8002DB4C with metadata-element identity.
    backing_size,        //8005641C for captured nonnull type-list backing.
    reserve_types,       //8005383C(captured type list,1024).
    allocate_callbacks,  //800560F8(12); actual3E848 stores follow natively.
    final_prototype      //80027824, returned canonical80561698 pointer-field prototype.
};
struct IgbArkCoreCall {
    IgbArkCoreServiceKind kind{};
    IgbArkCoreObject object;
    IgbBootstrapPointer allocation;
    std::shared_ptr<IgbMemoryPoolRegistry> manager;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbRuntimeTypeList> types;
    std::shared_ptr<IgbRuntimeTypeList::Storage> type_backing;
    std::shared_ptr<IgbScratchBuffer> type_backing_bytes;
    std::uint32_t type_backing_address{},type_backing_offset{};
    std::uint32_t bytes{},virtual_slot{},argument{};
};
struct IgbArkCoreOperation {
    std::function<MessengerCallResult()> resume;
    // Address and typed owner are supplied independently. Zero is original null.
    IgbBootstrapPointer allocation_result;
    std::shared_ptr<IgbMemoryPoolRegistry> manager_result;
    std::shared_ptr<IgbRuntimeTypeList> types_result;
    std::shared_ptr<IgbRegistrationCallbackList> callbacks_result;
    std::shared_ptr<IgbDecodeRuntimeField> field_result;
};
using IgbArkCoreFactory=std::function<std::shared_ptr<IgbArkCoreOperation>(const IgbArkCoreCall&)>;
struct IgbArkCoreBootstrapServices {
    std::shared_ptr<IgbBootstrapServices> bootstrap; //Canonical runtime/default handle.
    std::shared_ptr<IgbFilePoolsServices> pools; //Canonical805621C8 and actual607F4.
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::optional<std::shared_ptr<IgbDecodeFieldRegistry>> field_prototypes; //8056229C.
    IgbRuntimeFieldSlot reference_prototype; //80561688, later aliases pointer prototype80561698.
    IgbArkCoreFactory factory;
    std::vector<std::shared_ptr<void>> unfinished_;
};
// Complete original8003D2F8 caller. Registration uses actual66188 with the
// exact body functions; unbound bodies remain pending. Only the original
// post-2A0D0 return point enables reflected factories.
class IgbArkCoreBootstrapInvocation {
public:
    explicit IgbArkCoreBootstrapInvocation(IgbArkCoreObject);
    ~IgbArkCoreBootstrapInvocation();
    IgbArkCoreBootstrapInvocation(const IgbArkCoreBootstrapInvocation&)=delete;
    IgbArkCoreBootstrapInvocation& operator=(const IgbArkCoreBootstrapInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbArkCoreBootstrapServices> services={});
    bool completed() const noexcept;bool faulted() const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
