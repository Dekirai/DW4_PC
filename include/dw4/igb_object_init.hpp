#pragma once

#include "dw4/igb_runtime_types.hpp"
#include "dw4/igb_stream.hpp"

namespace dw4 {
class IgbObjectInitError : public std::runtime_error {public: using std::runtime_error::runtime_error;};
struct IgbObjectBinding {
    std::uint32_t offset{};
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeFieldRegistry> fields;
    std::shared_ptr<IgbRuntimeTypeList> types;
    std::shared_ptr<IgbDecodeRuntimeField> field;
};
// An explicit native allocation/layout binding. original_address participates
// only in original 32-bit pointer arithmetic; it is never a host pointer.
struct IgbObjectStorage {
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t original_address{};
    std::vector<IgbObjectBinding> bindings;
    bool faulted() const noexcept {return fault_;}
private:
    friend class IgbObjectInitInvocation;
    friend class IgbBootstrapBaseInvocation;
    std::weak_ptr<void> active_;
    bool fault_{};
    std::vector<std::shared_ptr<void>> unreleased_;
};
struct IgbObjectInstance {
    std::shared_ptr<IgbObjectStorage> storage;
    std::uint32_t offset{};
    std::shared_ptr<LoaderRequestFile> file;
};
// While construction owns the allocation, the physical header is authoritative.
// Bound callbacks must use this writer for packed-reference changes so the typed
// file view and physical word remain synchronized. After construction, the
// returned file's canonical reference_word is used by native loader services.
std::uint32_t igb_object_reference_word(const IgbObjectInstance&);
void igb_object_set_reference_word(const IgbObjectInstance&,std::uint32_t value);
struct IgbObjectInstanceList {
    using Storage=std::vector<IgbObjectInstance>;
    std::int32_t count{},capacity{}; // igObjectList +04/+08.
    std::shared_ptr<Storage> storage; // +00.
};
enum class IgbObjectInitServiceKind {
    object_virtual, // Fresh physical vtable; slots58/0C/34/2C/24.
    field_virtual,  // Captured field, slots84/7C/F0/70/74/78.
    allocation_pool, // Actual800590A0(adjusted object address - live metadata+20).
    pool_index,      // Captured allocation pool virtual+16C, result low byte.
    register_instance // Actual80053BC0(captured metadata+30,object).
};
struct IgbObjectInitCall {
    IgbObjectInitServiceKind kind{};
    IgbObjectInstance object;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbObjectInstanceList> instances;
    std::uint32_t virtual_slot{},vtable_word{},argument{},address{};
};
struct IgbObjectInitOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
    std::shared_ptr<ModelSceneObject> pool_result;
};
using IgbObjectInitFactory=std::function<std::shared_ptr<IgbObjectInitOperation>(const IgbObjectInitCall&)>;
struct IgbObjectInitServices {
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    std::optional<std::shared_ptr<IgbRuntimeMetaType>> object_metadata; //805622A4, igObject base.
    IgbObjectInitFactory factory;
};

// Actual800667F4, allocation-base helper80068184 and the three reached field
// pass callers80066F40/80066FF8/8006712C with80063DD0 mode selection. Reached
// field implementations and allocator callbacks remain explicit operations.
class IgbObjectInitInvocation {
public:
    IgbObjectInitInvocation(std::shared_ptr<IgbObjectStorage>,std::uint32_t offset,
        std::shared_ptr<IgbRuntimeMetaType> metadata);
    ~IgbObjectInitInvocation();
    IgbObjectInitInvocation(const IgbObjectInitInvocation&)=delete;
    IgbObjectInitInvocation& operator=(const IgbObjectInitInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbObjectInitServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<LoaderRequestFile> result() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
