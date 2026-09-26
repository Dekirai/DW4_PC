#pragma once

#include "dw4/igb_field_default_copy.hpp"

namespace dw4 {
class IgbStructMemberError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbStructMemberServiceKind {guest_function,field_virtual};
struct IgbStructMemberCall {
    IgbStructMemberServiceKind kind{};
    std::shared_ptr<IgbDecodeRuntimeField> field;
    IgbObjectInstance object;
    // Function is a guest code identity, never a cast host function pointer.
    // Address is captured receiver+offset for a guest function. A fresh
    // non-struct vtable selected by74B0C is represented by table and slot.
    std::uint32_t function{},address{},vtable_word{},virtual_slot{};
};
struct IgbStructMemberOperation {std::function<MessengerCallResult()> resume;};
using IgbStructMemberFactory=std::function<std::shared_ptr<IgbStructMemberOperation>(const IgbStructMemberCall&)>;
struct IgbStructMemberServices {
    std::shared_ptr<IgbFieldValuesServices> values;
    IgbStructMemberFactory factory;
    std::vector<std::shared_ptr<void>> retained;
};
// Exact80470E00 field slots84/74 (BLR),7C/80 (optional constructor/destructor),
// F0 (live constructor predicate),70 (default/reset) and78 (fresh+70 dispatch).
// Unknown canonical words remain pending; represented zero is tested exactly
// where the original tests it. In70's nonzero-constructor path, a zero selected
// destructor or freshly reloaded constructor is a null indirect-call fault.
class IgbStructMemberInvocation {
public:
    IgbStructMemberInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance,std::uint32_t slot);
    ~IgbStructMemberInvocation();
    IgbStructMemberInvocation(const IgbStructMemberInvocation&)=delete;
    IgbStructMemberInvocation& operator=(const IgbStructMemberInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbStructMemberServices> services={},
        const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbObjectInitFactory make_native_igb_struct_member_factory(
    std::shared_ptr<IgbStructMemberServices>,IgbObjectInitFactory remaining={});
} // namespace dw4
