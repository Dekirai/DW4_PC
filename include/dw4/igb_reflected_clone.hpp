#pragma once

#include "dw4/igb_field_clone.hpp"
#include "dw4/igb_file_factory.hpp"
#include "dw4/igb_field_member_copy.hpp"

namespace dw4 {
class IgbReflectedCloneError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbReflectedCloneServiceKind {object_virtual,tagged_pool,reflected_factory,field_virtual};
struct IgbReflectedCloneCall {
    IgbReflectedCloneServiceKind kind{};
    IgbObjectInstance object,destination,source;
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<ModelSceneObject> pool;
    std::uint32_t vtable_word{},virtual_slot{},handle{};
};
struct IgbReflectedCloneOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
    std::shared_ptr<ModelSceneObject> pool_result;
    IgbObjectInstance object_result;
};
using IgbReflectedCloneFactory=std::function<std::shared_ptr<IgbReflectedCloneOperation>(const IgbReflectedCloneCall&)>;
struct IgbReflectedCloneServices {
    std::shared_ptr<IgbFileFactoryServices> reflected;
    std::optional<std::shared_ptr<IgbRuntimeMetaType>> object_metadata; //805622A4.
    IgbReflectedCloneFactory factory;
    std::vector<IgbObjectInstance> unfinished;
};
// Actual6426C/64294/642BC ->641C4. Signed policy bytes select virtual90..A0;
// unsupported modes do not read the vtable or either object.
class IgbFieldCopyDispatchInvocation {
public:
    IgbFieldCopyDispatchInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance destination,
        IgbObjectInstance source,std::uint32_t policy_offset=0x28);
    ~IgbFieldCopyDispatchInvocation();
    IgbFieldCopyDispatchInvocation(const IgbFieldCopyDispatchInvocation&)=delete;
    IgbFieldCopyDispatchInvocation& operator=(const IgbFieldCopyDispatchInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbReflectedCloneServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual68060 and all three selected passes:67C28(mode0),67B60(mode1),
// 67ADC(mode2). Each keeps captured registry/count and reloads its backing.
class IgbObjectCopyInvocation {
public:
    IgbObjectCopyInvocation(IgbObjectInstance destination,IgbObjectInstance source,std::uint32_t mode=1);
    ~IgbObjectCopyInvocation();
    IgbObjectCopyInvocation(const IgbObjectCopyInvocation&)=delete;
    IgbObjectCopyInvocation& operator=(const IgbObjectCopyInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbReflectedCloneServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual680B4: source metadata, tagged pool,6546C, then68060 even for null.
// A missing reflected service leaves an explicit factory endpoint available
// for bounded caller research; normal composition supplies actual6546C.
class IgbReflectedCloneInvocation {
public:
    explicit IgbReflectedCloneInvocation(IgbObjectInstance source,std::uint32_t mode=1);
    ~IgbReflectedCloneInvocation();
    IgbReflectedCloneInvocation(const IgbReflectedCloneInvocation&)=delete;
    IgbReflectedCloneInvocation& operator=(const IgbReflectedCloneInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbReflectedCloneServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    IgbObjectInstance result_instance()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Reuse the existing metadata58 providers and verified scalar/string BLR hooks.
IgbReflectedCloneFactory make_native_igb_reflected_clone_virtual_factory(
    std::shared_ptr<IgbObjectInitServices>,IgbReflectedCloneFactory remaining={});
IgbReflectedCloneFactory make_native_igb_reflected_member_copy_factory(
    std::shared_ptr<IgbFieldMemberCopyServices>,IgbReflectedCloneFactory remaining={});
IgbFieldCloneFactory make_native_igb_reflected_field_clone_factory(
    std::shared_ptr<IgbReflectedCloneServices>,IgbFieldCloneFactory remaining={});
} // namespace dw4
