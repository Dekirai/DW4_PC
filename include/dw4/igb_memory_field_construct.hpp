#pragma once

#include "dw4/igb_field_values.hpp"
#include "dw4/igb_field_type_registration.hpp"
#include "dw4/igb_object_init.hpp"

namespace dw4 {
class IgbMemoryFieldConstructError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbMemoryFieldConstructServiceKind {field_virtual,named_pool,tagged_pool,allocate};
struct IgbMemoryFieldConstructCall {
    IgbMemoryFieldConstructServiceKind kind{};
    std::shared_ptr<IgbDecodeRuntimeField> field;
    IgbObjectInstance object;
    std::shared_ptr<ModelSceneObject> pool;
    IgbDependencyName name;
    std::uint32_t vtable_word{},virtual_slot{},handle{},bytes{},alignment{};
};
struct IgbMemoryFieldConstructOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<ModelSceneObject> pool_result;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
    IgbPointerListBacking backing_result;
    // Optional native continuation; borrow the enclosing guard for one resume.
    std::function<MessengerCallResult(const std::function<void()>&)> resume_checked;
};
using IgbMemoryFieldConstructFactory=std::function<std::shared_ptr<IgbMemoryFieldConstructOperation>(const IgbMemoryFieldConstructCall&)>;
struct IgbMemoryFieldConstructServices {
    std::shared_ptr<IgbFieldValuesServices> values;
    std::shared_ptr<IgbRegistrationMetaSlot> reference_metadata; //80561684, captured before68128.
    std::optional<std::uint32_t> status_success,status_failure; //805622D0/805622D4, not inferred BSS values.
    IgbMemoryFieldConstructFactory factory;
    // Late explicit physical/typed mappings, independent from allocator calls.
    std::vector<IgbPointerListBacking> allocations;
    std::vector<std::shared_ptr<void>> retained;
};
void igb_bind_memory_field_allocation(const std::shared_ptr<IgbMemoryFieldConstructServices>&,IgbPointerListBacking);

// Actual62BB8. Retains the incoming count/pool, reloads the element separately
// for size, alignment and ancestry, and publishes after conditional zero fill.
class IgbMemoryFieldAllocateInvocation {
public:
    IgbMemoryFieldAllocateInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance target,
        std::uint32_t count,std::shared_ptr<ModelSceneObject> pool);
    ~IgbMemoryFieldAllocateInvocation();
    IgbMemoryFieldAllocateInvocation(const IgbMemoryFieldAllocateInvocation&)=delete;
    IgbMemoryFieldAllocateInvocation& operator=(const IgbMemoryFieldAllocateInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryFieldConstructServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual62524, memory-reference descriptor80476088 virtual+7C. The helper's
// status is read in original order and then ignored by this outer wrapper.
class IgbMemoryFieldConstructInvocation {
public:
    IgbMemoryFieldConstructInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance target);
    ~IgbMemoryFieldConstructInvocation();
    IgbMemoryFieldConstructInvocation(const IgbMemoryFieldConstructInvocation&)=delete;
    IgbMemoryFieldConstructInvocation& operator=(const IgbMemoryFieldConstructInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryFieldConstructServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Verified object-reference element table804762FC only: size6A538=4,
// alignment6CD08=4 and metadata94090=canonical805616E8. Other slots delegate.
IgbMemoryFieldConstructFactory make_native_igb_memory_field_virtual_factory(
    std::shared_ptr<IgbFieldTypeRegistrationServices>,IgbMemoryFieldConstructFactory remaining={});
} // namespace dw4
