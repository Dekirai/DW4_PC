#pragma once

#include "dw4/igb_buffer_copy.hpp"
#include "dw4/igb_field_member_copy.hpp"
#include "dw4/igb_field_type_registration.hpp"

namespace dw4 {
class IgbMemoryMemberCopyError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbMemoryMemberCopyServiceKind {element_metadata,destroy_object,clone_object};
struct IgbMemoryMemberCopyCall {
    IgbMemoryMemberCopyServiceKind kind{};
    std::shared_ptr<IgbDecodeRuntimeField> field;
    IgbObjectInstance object;
    std::uint32_t function{},vtable_word{},virtual_slot{},argument{};
};
struct IgbMemoryMemberCopyOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
    IgbObjectInstance object_result;
};
using IgbMemoryMemberCopyFactory=std::function<std::shared_ptr<IgbMemoryMemberCopyOperation>(const IgbMemoryMemberCopyCall&)>;
struct IgbMemoryMemberCopyServices {
    std::shared_ptr<IgbBufferServices> buffers;
    std::shared_ptr<IgbObjectFieldConstructServices> objects;
    std::shared_ptr<IgbFieldTypeRegistrationServices> registration;
    IgbMemoryMemberCopyFactory factory;
    std::vector<std::shared_ptr<void>> unfinished;
};
// Actual626DC: live ownership gate,68128 ancestry, captured backing/count,
// and full reference decrements with reached66E1C callbacks.
class IgbMemoryMemberReleaseInvocation {
public:
    IgbMemoryMemberReleaseInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance destination);
    ~IgbMemoryMemberReleaseInvocation();
    IgbMemoryMemberReleaseInvocation(const IgbMemoryMemberReleaseInvocation&)=delete;
    IgbMemoryMemberReleaseInvocation& operator=(const IgbMemoryMemberReleaseInvocation&)=delete;
    // Borrow the enclosing continuation's validity check for this resume only.
    MessengerCallResult resume(std::shared_ptr<IgbMemoryMemberCopyServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual62824: release/free/copy/publish, then live object or nested-memory
// element copies. Buffer helpers execute natively; unrecovered object virtuals
// and destructors remain required retained operations.
class IgbMemoryMemberCopyInvocation {
public:
    IgbMemoryMemberCopyInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance destination,IgbObjectInstance source);
    ~IgbMemoryMemberCopyInvocation();
    IgbMemoryMemberCopyInvocation(const IgbMemoryMemberCopyInvocation&)=delete;
    IgbMemoryMemberCopyInvocation& operator=(const IgbMemoryMemberCopyInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMemoryMemberCopyServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbFieldMemberCopyFactory make_native_igb_memory_member_copy_factory(
    std::shared_ptr<IgbMemoryMemberCopyServices>,IgbFieldMemberCopyFactory remaining={});
// Verified element getters94090(object) and94068(memory), using canonical cells.
IgbMemoryMemberCopyFactory make_native_igb_memory_member_virtual_factory(
    std::shared_ptr<IgbFieldTypeRegistrationServices>,IgbMemoryMemberCopyFactory remaining={});
} // namespace dw4
