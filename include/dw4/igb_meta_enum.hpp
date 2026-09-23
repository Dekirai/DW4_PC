#pragma once

#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_registration_callbacks.hpp"

namespace dw4 {
class IgbMetaEnumError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbMetaEnumCall {
    std::uint32_t function{}; // Exact object-type getter80024B94 or800365B4.
    std::shared_ptr<IgbDecodeRuntimeField> field;
};
struct IgbMetaEnumOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
    std::optional<std::uint32_t> metadata_address;
};
using IgbMetaEnumFactory=std::function<std::shared_ptr<IgbMetaEnumOperation>(const IgbMetaEnumCall&)>;
struct IgbMetaEnumServices {
    IgbMetaEnumServices();
    std::shared_ptr<IgbRegistrationMetaSlot> metadata; // Canonical80561808, never a separate ready cache.
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFieldFactoryTable> factories; //804649C0.
    std::shared_ptr<IgbFieldNameTable> names; //804649CC.
    std::shared_ptr<IgbFieldGlobalTable> globals; //804649D8, cells8056180C/10/14.
    std::shared_ptr<IgbFieldOffsetTable> offsets; //804649E4.
    std::function<IgbBootstrapPointer()> local_storage; // Actual probe's0x14-byte stack object.
    IgbMetaEnumFactory factory;
    std::vector<std::shared_ptr<void>> unfinished;
};
IgbTypeRegistrationArguments igb_meta_enum_registration_arguments(const std::shared_ptr<IgbRegistrationMetaSlot>&);
// Actual8002AAFC: prerequisite80021B94 then generic registration80066204.
class IgbMetaEnumRegistrationInvocation {
public:
    IgbMetaEnumRegistrationInvocation();~IgbMetaEnumRegistrationInvocation();
    IgbMetaEnumRegistrationInvocation(const IgbMetaEnumRegistrationInvocation&)=delete;
    IgbMetaEnumRegistrationInvocation& operator=(const IgbMetaEnumRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetaEnumServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual8002A998 and8002AB74. The getter registers when null or not flag4,
// then reloads80561808. It is not an enum object constructor or a lazy allocator.
class IgbMetaEnumLookupInvocation {
public:
    IgbMetaEnumLookupInvocation();~IgbMetaEnumLookupInvocation();
    IgbMetaEnumLookupInvocation(const IgbMetaEnumLookupInvocation&)=delete;
    IgbMetaEnumLookupInvocation& operator=(const IgbMetaEnumLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetaEnumServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual8002AB94, including captured receiver/count and the two live getters.
class IgbMetaEnumDefaultsInvocation {
public:
    IgbMetaEnumDefaultsInvocation();~IgbMetaEnumDefaultsInvocation();
    IgbMetaEnumDefaultsInvocation(const IgbMetaEnumDefaultsInvocation&)=delete;
    IgbMetaEnumDefaultsInvocation& operator=(const IgbMetaEnumDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetaEnumServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual8002A9D4: native6665C, inline enum construction and captured core+394.
class IgbMetaEnumLayoutProbeInvocation {
public:
    explicit IgbMetaEnumLayoutProbeInvocation(IgbBootstrapPointer);
    ~IgbMetaEnumLayoutProbeInvocation();
    IgbMetaEnumLayoutProbeInvocation(const IgbMetaEnumLayoutProbeInvocation&)=delete;
    IgbMetaEnumLayoutProbeInvocation& operator=(const IgbMetaEnumLayoutProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetaEnumServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_meta_enum_function_factory(
    std::shared_ptr<IgbMetaEnumServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_meta_enum_type_factory(
    std::shared_ptr<IgbMetaEnumServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
