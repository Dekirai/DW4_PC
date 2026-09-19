#pragma once

#include "dw4/igb_registration_callbacks.hpp"

namespace dw4 {
class IgbMetadataLayoutError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbMetadataLayoutKind {
    field_alignment, //Captured descriptor virtual+6C.
    registered_callback //Captured core+2C function(metadata), actual3E574 caller.
};
struct IgbMetadataLayoutCall {
    IgbMetadataLayoutKind kind{};
    std::shared_ptr<IgbRuntimeMetaType> metadata;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<IgbRuntimeTypeRegistry> core;
    std::uint32_t vtable_word{},virtual_slot{},function{};
};
struct IgbMetadataLayoutOperation {
    std::function<MessengerCallResult()> resume; //value supplies alignment return.
};
using IgbMetadataLayoutFactory=std::function<std::shared_ptr<IgbMetadataLayoutOperation>(const IgbMetadataLayoutCall&)>;
struct IgbMetadataLayoutServices {
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    IgbMetadataLayoutFactory factory;
};
// Actual8003E574. The core and initial signed count are captured; list and
// backing are reloaded at every reached callback. No cleanup is implied.
class IgbMetadataCallbacksInvocation {
public:
    IgbMetadataCallbacksInvocation(std::shared_ptr<IgbRuntimeTypeRegistry>,std::shared_ptr<IgbRuntimeMetaType>);
    ~IgbMetadataCallbacksInvocation();
    IgbMetadataCallbacksInvocation(const IgbMetadataCallbacksInvocation&)=delete;
    IgbMetadataCallbacksInvocation& operator=(const IgbMetadataCallbacksInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataLayoutServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual80065A58, including original slot helpers and registered callbacks.
// Exact80476E6C+6C executes80075AF8's constant4 leaf. Other tables remain
// explicit calls. Declaring metadata is a nonowning canonical binding.
class IgbMetadataLayoutInvocation {
public:
    explicit IgbMetadataLayoutInvocation(std::shared_ptr<IgbRuntimeMetaType>);
    ~IgbMetadataLayoutInvocation();
    IgbMetadataLayoutInvocation(const IgbMetadataLayoutInvocation&)=delete;
    IgbMetadataLayoutInvocation& operator=(const IgbMetadataLayoutInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataLayoutServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_metadata_layout_factory(
    std::shared_ptr<IgbMetadataLayoutServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
