#pragma once

#include "dw4/igb_field_string_init.hpp"
#include "dw4/igb_object_field_construct.hpp"

namespace dw4 {
class IgbFieldMemberCopyError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbFieldMemberCopyCall {
    std::shared_ptr<IgbDecodeRuntimeField> field;
    IgbObjectInstance destination,source;
    std::uint32_t function{};
};
struct IgbFieldMemberCopyOperation {std::function<MessengerCallResult()> resume;};
using IgbFieldMemberCopyFactory=std::function<std::shared_ptr<IgbFieldMemberCopyOperation>(const IgbFieldMemberCopyCall&)>;
struct IgbFieldMemberCopyServices {
    std::shared_ptr<IgbFieldValuesServices> values;
    std::shared_ptr<IgbObjectFieldConstructServices> objects;
    // Other reached functions remain explicit operations, including62824.
    IgbFieldMemberCopyFactory factory;
    std::vector<std::shared_ptr<void>> unfinished;
};
// Actual642E4/64318/6434C raw member copies,71B58/71BC4 string wrappers,
// and6A81C object assignment wrapper. Source field members are read live from
// their canonical view; generic bytes and field padding remain physical.
class IgbFieldMemberCopyInvocation {
public:
    IgbFieldMemberCopyInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbObjectInstance destination,
        IgbObjectInstance source,std::uint32_t function);
    ~IgbFieldMemberCopyInvocation();
    IgbFieldMemberCopyInvocation(const IgbFieldMemberCopyInvocation&)=delete;
    IgbFieldMemberCopyInvocation& operator=(const IgbFieldMemberCopyInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldMemberCopyServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
