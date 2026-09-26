#pragma once

#include "dw4/igb_data_list_registration.hpp"
#include "dw4/igb_field_schema.hpp"

namespace dw4 {
class IgbContextRegistrationError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbContextKind {context,extension};
enum class IgbContextLookup {fully_registered,plain};
struct IgbContextProfile {
    const char* name;
    std::uint32_t global,wrapper,body,parent,parent_getter,field_callback,defaults,full_getter,plain_getter;
};
struct IgbContextRegistrationServices {
    IgbContextRegistrationServices();
    std::shared_ptr<IgbRegistrationMetaSlot> context_metadata; //80561E00.
    std::shared_ptr<IgbRegistrationMetaSlot> extension_metadata; //80561DF8.
    // Share the actual37058/36E84 allocation-only metadata getters and the
    // later37140/36F6C list registrations through one canonical owner.
    std::shared_ptr<IgbDataListRegistrationServices> lists;
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFieldFactoryTable> factories;
    std::shared_ptr<IgbFieldNameTable> names;
    std::shared_ptr<IgbFieldGlobalTable> globals;
    std::shared_ptr<IgbFieldOffsetTable> offsets;
    std::vector<std::shared_ptr<void>> retained;
};
const IgbContextProfile& igb_context_profile(IgbContextKind);
std::shared_ptr<IgbRegistrationMetaSlot> igb_context_metadata_slot(
    const std::shared_ptr<IgbContextRegistrationServices>&,IgbContextKind);
IgbTypeRegistrationArguments igb_context_type_registration_arguments(
    const std::shared_ptr<IgbContextRegistrationServices>&,IgbContextKind);
// Actual37798/37510:21B94 precedes66204. Both abstract classes use flag1
// and no layout probe. No concrete runtime object or VT is fabricated.
class IgbContextRegistrationInvocation {
public:
    explicit IgbContextRegistrationInvocation(IgbContextKind);
    ~IgbContextRegistrationInvocation();
    IgbContextRegistrationInvocation(const IgbContextRegistrationInvocation&)=delete;
    IgbContextRegistrationInvocation& operator=(const IgbContextRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbContextRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Full37734/374AC use the actual66188 wrapper when necessary, then reload.
// Plain37594 loads only80561E00; no plain extension getter is admitted.
class IgbContextLookupInvocation {
public:
    IgbContextLookupInvocation(IgbContextKind,IgbContextLookup=IgbContextLookup::fully_registered);
    ~IgbContextLookupInvocation();
    IgbContextLookupInvocation(const IgbContextLookupInvocation&)=delete;
    IgbContextLookupInvocation& operator=(const IgbContextLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbContextRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual3782C: capture context/count, append object/int/object, configure
// captured first+0 then freshly selected first+2, set only the latter's+34,
// and execute the real659C0 schema. Lazy metadata does not imply readiness.
class IgbContextDefaultsInvocation {
public:
    IgbContextDefaultsInvocation();~IgbContextDefaultsInvocation();
    IgbContextDefaultsInvocation(const IgbContextDefaultsInvocation&)=delete;
    IgbContextDefaultsInvocation& operator=(const IgbContextDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbContextRegistrationServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_context_registration_function_factory(
    std::shared_ptr<IgbContextRegistrationServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_context_registration_type_factory(
    std::shared_ptr<IgbContextRegistrationServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
