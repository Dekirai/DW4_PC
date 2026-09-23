#pragma once

#include "dw4/igb_field_copy.hpp"
#include "dw4/igb_list_defaults.hpp"

namespace dw4 {
class IgbFieldCloneError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbFieldCloneServiceKind {register_type,reflected_clone};
struct IgbFieldCloneCall {
    IgbFieldCloneServiceKind kind{};
    std::shared_ptr<IgbDecodeRuntimeField> source;
    std::uint32_t function{},argument{};
};
struct IgbFieldCloneOperation {
    std::function<MessengerCallResult()> resume;
    IgbBootstrapPointer allocation_result;
};
using IgbFieldCloneFactory=std::function<std::shared_ptr<IgbFieldCloneOperation>(const IgbFieldCloneCall&)>;
struct IgbFieldCloneServices {
    std::shared_ptr<IgbMetaFieldFactoryServices> fields;
    std::shared_ptr<IgbFieldCopyServices> copy;
    std::shared_ptr<IgbFieldValuesServices> values;
    IgbFieldCloneFactory factory;
    std::vector<IgbBootstrapPointer> completed_allocations;
    std::vector<std::shared_ptr<void>> unfinished;
};
// Memory6331C ->6CC88 ->639E4, signed-byte3FFCC ->639E4, or Boolean3EC04
// ->639E4. Source members stay live between original reads. Only the memory
// profile copies reference flags and a nonowning element pointer.
class IgbFieldCloneConstructorInvocation {
public:
    IgbFieldCloneConstructorInvocation(IgbBootstrapPointer,std::shared_ptr<IgbDecodeRuntimeField> source,
        IgbMetaFieldKind kind=IgbMetaFieldKind::memory_reference);
    ~IgbFieldCloneConstructorInvocation();
    IgbFieldCloneConstructorInvocation(const IgbFieldCloneConstructorInvocation&)=delete;
    IgbFieldCloneConstructorInvocation& operator=(const IgbFieldCloneConstructorInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldCloneServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual2ACD8(memory), 37BC0(signed byte), or38684(Boolean). Disabled reflection
// allocates through66DD8/66D60 and executes the profile's copy constructor.
// Enabled reflection registers the selected class, then requires680B4.
class IgbFieldCloneInvocation {
public:
    explicit IgbFieldCloneInvocation(std::shared_ptr<IgbDecodeRuntimeField> source,std::uint32_t argument=1,
        IgbMetaFieldKind kind=IgbMetaFieldKind::memory_reference);
    ~IgbFieldCloneInvocation();
    IgbFieldCloneInvocation(const IgbFieldCloneInvocation&)=delete;
    IgbFieldCloneInvocation& operator=(const IgbFieldCloneInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldCloneServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
    IgbBootstrapPointer allocation()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbListDefaultsFactory make_native_igb_list_field_clone_factory(
    std::shared_ptr<IgbFieldCloneServices>,IgbListDefaultsFactory remaining={});
} // namespace dw4
