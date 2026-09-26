#pragma once

#include "dw4/igb_metadata_operations.hpp"
#include "dw4/igb_ark_core_bootstrap.hpp"

namespace dw4 {
class IgbPointerListError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
// A byte allocation and its explicit pointer-owner mapping are independent.
// Neither a host vector size nor a cached capacity establishes allocator size.
struct IgbPointerListBacking {
    std::shared_ptr<IgbScratchBuffer> bytes;
    std::uint32_t address{},offset{};
    std::shared_ptr<IgbRuntimeTypeList::Storage> types;
    std::shared_ptr<IgbDecodeFieldRegistry::Storage> fields;
};
struct IgbPointerListValue {
    std::uint32_t address{};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field;
};
enum class IgbPointerListServiceKind {
    allocation_pool, //800590A0(captured backing).
    allocation_size, //Captured pool virtual+150(captured backing).
    object_pool,     //8005929C(high byte of live list reference word).
    reallocate       //Captured pool virtual+E8(captured backing,bytes).
};
struct IgbPointerListCall {
    IgbPointerListServiceKind kind{};
    IgbPointerListBacking backing;
    std::shared_ptr<IgbRuntimeTypeList> types;
    std::shared_ptr<IgbDecodeFieldRegistry> fields;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t handle{},bytes{};
};
struct IgbPointerListOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<ModelSceneObject> pool_result;
    std::uint32_t size_result{};
    IgbPointerListBacking backing_result;
};
using IgbPointerListFactory=std::function<std::shared_ptr<IgbPointerListOperation>(const IgbPointerListCall&)>;
struct IgbPointerListServices {
    IgbPointerListFactory factory;
    // Explicit original arithmetic identities, never host-pointer casts.
    std::vector<IgbPointerListValue> values;
    std::vector<std::shared_ptr<void>> unfinished;
};
// Actual53BC0 append and5383C reserve, including5641C/682A4 caller ordering.
// Typed count/backing views are canonical after their physical construction.
class IgbPointerListInvocation {
public:
    IgbPointerListInvocation(std::shared_ptr<IgbRuntimeTypeList>,IgbPointerListValue);
    IgbPointerListInvocation(std::shared_ptr<IgbDecodeFieldRegistry>,IgbPointerListValue);
    IgbPointerListInvocation(std::shared_ptr<IgbRuntimeTypeList>,std::int32_t minimum_index);
    IgbPointerListInvocation(std::shared_ptr<IgbDecodeFieldRegistry>,std::int32_t minimum_index);
    ~IgbPointerListInvocation();
    IgbPointerListInvocation(const IgbPointerListInvocation&)=delete;
    IgbPointerListInvocation& operator=(const IgbPointerListInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbPointerListServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbMetadataOperationsFactory make_native_igb_pointer_list_metadata_factory(
    std::shared_ptr<IgbPointerListServices>,IgbMetadataOperationsFactory remaining={});
IgbArkCoreFactory make_native_igb_pointer_list_bootstrap_factory(
    std::shared_ptr<IgbPointerListServices>,IgbArkCoreFactory remaining={});
} // namespace dw4
