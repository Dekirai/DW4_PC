#pragma once

#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_metadata_registration.hpp"
#include "dw4/igb_object_field_construct.hpp"

namespace dw4 {
class IgbContextInfoError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbContextInfoLookup {fully_registered,plain};
struct IgbContextInfoServices {
    IgbContextInfoServices();
    std::shared_ptr<IgbRegistrationMetaSlot> metadata; //80561DE8, initially unrepresented.
    std::shared_ptr<IgbMetadataRegistrationServices> meta_objects; //Actual29F84, canonical8056174C.
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbRegistrationCallbacksServices> callbacks;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFieldValuesServices> values; //71694 and canonical signed-byte prototype80561E20.
    std::shared_ptr<IgbObjectFieldConstructServices> object_cleanup;
    std::shared_ptr<IgbFieldFactoryTable> factories;
    std::shared_ptr<IgbFieldNameTable> names;
    std::shared_ptr<IgbFieldGlobalTable> globals;
    std::shared_ptr<IgbFieldOffsetTable> offsets;
    std::function<IgbBootstrapPointer()> local_storage; //Actual37268 has a0x14-byte local object.
    std::vector<std::shared_ptr<void>> retained;
};
IgbTypeRegistrationArguments igb_context_info_type_registration_arguments(const std::shared_ptr<IgbContextInfoServices>&);
// Actual37358:21B94 then66204. The returned extra callback is2A0A8.
class IgbContextInfoRegistrationInvocation {
public:
    IgbContextInfoRegistrationInvocation();~IgbContextInfoRegistrationInvocation();
    IgbContextInfoRegistrationInvocation(const IgbContextInfoRegistrationInvocation&)=delete;
    IgbContextInfoRegistrationInvocation& operator=(const IgbContextInfoRegistrationInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbContextInfoServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRegistrationCallbackList> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Full3722C calls37330 as needed, then reloads80561DE8. Plain93EE0 only reads.
class IgbContextInfoLookupInvocation {
public:
    explicit IgbContextInfoLookupInvocation(IgbContextInfoLookup=IgbContextInfoLookup::fully_registered);
    ~IgbContextInfoLookupInvocation();
    IgbContextInfoLookupInvocation(const IgbContextInfoLookupInvocation&)=delete;
    IgbContextInfoLookupInvocation& operator=(const IgbContextInfoLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbContextInfoServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual37E48 aliases values->signed_byte_prototype; known null runs37D00,
// then reloads the same canonical80561E20 cell. It never returns class metadata.
class IgbSignedBytePrototypeInvocation {
public:
    IgbSignedBytePrototypeInvocation();~IgbSignedBytePrototypeInvocation();
    IgbSignedBytePrototypeInvocation(const IgbSignedBytePrototypeInvocation&)=delete;
    IgbSignedBytePrototypeInvocation& operator=(const IgbSignedBytePrototypeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbContextInfoServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::shared_ptr<IgbDecodeRuntimeField> result()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual37268:6665C, capture core, VT and null+08/+0C stores, capture selected
// word, restore VT, skip literal-null string cleanup, freshly release object+08.
class IgbContextInfoProbeInvocation {
public:
    explicit IgbContextInfoProbeInvocation(IgbBootstrapPointer);~IgbContextInfoProbeInvocation();
    IgbContextInfoProbeInvocation(const IgbContextInfoProbeInvocation&)=delete;
    IgbContextInfoProbeInvocation& operator=(const IgbContextInfoProbeInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbContextInfoServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
    std::uint32_t value()const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual373EC: capture metadata/count, append object/string/memory, configure
// first+0 through29F84, fresh+1 through71694(NULL) then35=1, fresh+2 through
//37E48 then+3C store, and real659C0 schema. No ready values are fabricated.
class IgbContextInfoDefaultsInvocation {
public:
    IgbContextInfoDefaultsInvocation();~IgbContextInfoDefaultsInvocation();
    IgbContextInfoDefaultsInvocation(const IgbContextInfoDefaultsInvocation&)=delete;
    IgbContextInfoDefaultsInvocation& operator=(const IgbContextInfoDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbContextInfoServices> services={},const std::function<void()>& enclosing_check={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbRegistrationCallbacksFactory make_native_igb_context_info_registration_function_factory(
    std::shared_ptr<IgbContextInfoServices>,IgbRegistrationCallbacksFactory remaining={});
IgbTypeRegistrationFactory make_native_igb_context_info_registration_type_factory(
    std::shared_ptr<IgbContextInfoServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
