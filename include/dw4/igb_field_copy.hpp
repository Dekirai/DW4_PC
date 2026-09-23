#pragma once

#include "dw4/igb_meta_field_factory.hpp"
#include "dw4/igb_field_values.hpp"

namespace dw4 {
class IgbFieldCopyError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbFieldCopyServiceKind {
    base_metadata,  // Fresh object virtual+58 for68184;804717F0 reads805622A4.
    allocation_pool, //590A0(object - metadata prefix).
    pool_index,     //5920C pool classification lookup, NOT virtual+16C.
    tagged_pool,    //68430 ->5929C(live reference word high byte).
    allocate_default //681C4 ->captured pool virtual+D8(bytes).
};
struct IgbFieldCopyCall {
    IgbFieldCopyServiceKind kind{};
    IgbBootstrapPointer object;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t vtable_word{},address{},handle{},bytes{};
};
struct IgbFieldCopyOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<ModelSceneObject> pool_result;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
    IgbRuntimeFieldDefaultSource buffer_result;
    std::uint32_t buffer_address{};
};
using IgbFieldCopyFactory=std::function<std::shared_ptr<IgbFieldCopyOperation>(const IgbFieldCopyCall&)>;
struct IgbFieldCopyServices {
    std::shared_ptr<IgbMetaFieldFactoryServices> fields;
    std::shared_ptr<IgbFieldValuesServices> values;
    IgbFieldCopyFactory factory;
    std::vector<std::shared_ptr<void>> unfinished;
};
// Actual639E4, including66690, full string references,681C4 and031E8.
// Composes inside a derived constructor's existing field construction lease.
class IgbFieldCopyInvocation {
public:
    IgbFieldCopyInvocation(IgbBootstrapPointer,std::shared_ptr<IgbDecodeRuntimeField> source);
    ~IgbFieldCopyInvocation();
    IgbFieldCopyInvocation(const IgbFieldCopyInvocation&)=delete;
    IgbFieldCopyInvocation& operator=(const IgbFieldCopyInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldCopyServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
