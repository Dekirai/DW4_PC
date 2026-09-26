#pragma once

#include "dw4/igb_pointer_list.hpp"

namespace dw4 {
class IgbFieldOperationsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
// Publish reached buffer stores to optional physical constructor storage too.
void igb_field_store_size(const std::shared_ptr<IgbDecodeRuntimeField>&,std::uint16_t);
void igb_field_store_default(const std::shared_ptr<IgbDecodeRuntimeField>&,
    const IgbRuntimeFieldDefaultSource&,std::uint32_t address);
enum class IgbFieldOperationKind {
    field_virtual,    //Fresh field vtable+5C/+64/+8C.
    allocation_pool, //590A0(captured default buffer).
    allocation_size, //Captured pool+150(captured default buffer).
    object_pool,     //68430 ->5929C(live field reference high byte).
    allocate_default,//68254 ->captured pool+E0(captured bytes,1).
    free_default     //6834C ->captured pool+108(captured nonnull buffer).
};
struct IgbFieldOperationCall {
    IgbFieldOperationKind kind{};
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<ModelSceneObject> pool;
    IgbRuntimeFieldDefaultSource buffer;
    // Captured nonnull scalar input to virtual+8C; its bytes survive suspension.
    std::optional<IgbRuntimeFieldDefaultSource> source;
    std::optional<std::uint32_t> source_address;
    std::uint32_t vtable_word{},virtual_slot{},argument{},handle{},bytes{},alignment{},address{};
};
struct IgbFieldOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbDecodeRuntimeField> field_result;
    std::shared_ptr<ModelSceneObject> pool_result;
    IgbRuntimeFieldDefaultSource buffer_result;
    std::uint32_t buffer_address{};
    // Preferred native continuation; never retain the borrowed parent guard.
    std::function<MessengerCallResult(const std::function<void()>&)> resume_checked;
};
using IgbFieldOperationsFactory=std::function<std::shared_ptr<IgbFieldOperation>(const IgbFieldOperationCall&)>;
struct IgbFieldOperationsServices {
    std::shared_ptr<IgbDependenciesServices> dependencies;
    std::shared_ptr<IgbPointerListServices> pointer_lists;
    IgbRuntimeFieldSlot unsigned_int_prototype; //Plain getter94140/global805614C0.
    IgbFieldOperationsFactory factory;
    std::vector<std::shared_ptr<void>> unfinished;
};
// Actual63D54: retain initial *source, release live old name, reload *source.
// An aliasing shared_ptr can point source directly at field.name_string.
class IgbFieldNameInvocation {
public:
    IgbFieldNameInvocation(std::shared_ptr<IgbDecodeRuntimeField>,std::shared_ptr<IgbFieldNameSlot> source);
    ~IgbFieldNameInvocation();
    IgbFieldNameInvocation(const IgbFieldNameInvocation&)=delete;
    IgbFieldNameInvocation& operator=(const IgbFieldNameInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldOperationsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual63C74, including size truncation, live signed size comparison,
// free-before-allocation and raw default-pointer publication.
class IgbFieldBufferInvocation {
public:
    explicit IgbFieldBufferInvocation(std::shared_ptr<IgbDecodeRuntimeField>);
    ~IgbFieldBufferInvocation();
    IgbFieldBufferInvocation(const IgbFieldBufferInvocation&)=delete;
    IgbFieldBufferInvocation& operator=(const IgbFieldBufferInvocation&)=delete;
    // Borrowed only during this resume; guards parent state after callbacks
    // before this child can publish its size/default-buffer stores.
    MessengerCallResult resume(std::shared_ptr<IgbFieldOperationsServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
class IgbFieldFinalizeInvocation {
public:
    explicit IgbFieldFinalizeInvocation(std::shared_ptr<IgbDecodeRuntimeField>);
    ~IgbFieldFinalizeInvocation();
    IgbFieldFinalizeInvocation(const IgbFieldFinalizeInvocation&)=delete;
    IgbFieldFinalizeInvocation& operator=(const IgbFieldFinalizeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldOperationsServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual65844 ->63F14, then65820 ->53E6C ->53BC0.
class IgbFieldAppendInvocation {
public:
    IgbFieldAppendInvocation(std::shared_ptr<IgbRuntimeMetaType>,std::shared_ptr<IgbDecodeRuntimeField>);
    ~IgbFieldAppendInvocation();
    IgbFieldAppendInvocation(const IgbFieldAppendInvocation&)=delete;
    IgbFieldAppendInvocation& operator=(const IgbFieldAppendInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldOperationsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Exact80476E6C:94140 plain prototype getter,759D8 size4 and63D04 with
// null source. The latter executes actual63C74; nonnull copies stay explicit.
IgbFieldOperationsFactory make_native_igb_unsigned_int_field_factory(
    std::shared_ptr<IgbFieldOperationsServices>,IgbFieldOperationsFactory remaining={});
} // namespace dw4
