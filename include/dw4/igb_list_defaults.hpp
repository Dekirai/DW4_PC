#pragma once

#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_field_values.hpp"
#include "dw4/igb_type_list_factory.hpp"

namespace dw4 {
class IgbListDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
enum class IgbListDefaultsServiceKind {register_object_field,clone_field,destroy_field};
struct IgbListDefaultsCall {
    IgbListDefaultsServiceKind kind{};
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::uint32_t function{},vtable_word{},virtual_slot{},argument{};
};
struct IgbListDefaultsOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbDecodeRuntimeField> field_result;
    std::optional<std::uint32_t> field_address;
};
using IgbListDefaultsFactory=std::function<std::shared_ptr<IgbListDefaultsOperation>(const IgbListDefaultsCall&)>;
struct IgbListDefaultsServices {
    IgbListDefaultsServices();
    std::shared_ptr<IgbTypeListServices> lists; // Canonical805619D4/805619CC aliases.
    std::shared_ptr<IgbFieldSchemaServices> schema;
    std::shared_ptr<IgbFieldValuesServices> values; // Canonical805616EC object-field prototype.
    IgbRuntimeFieldSlot name_descriptor; //805617C4, the field-name descriptor.
    std::shared_ptr<IgbRuntimeFieldSlot> internal_data=std::make_shared<IgbRuntimeFieldSlot>(); //805619D0.
    std::shared_ptr<IgbFieldFactoryTable> factories; //8055D3A4.
    std::shared_ptr<IgbFieldNameTable> names; //8055D3AC.
    std::shared_ptr<IgbFieldGlobalTable> globals; //8055D3B4.
    std::shared_ptr<IgbFieldOffsetTable> offsets; //8055D3BC.
    IgbListDefaultsFactory factory;
    std::vector<std::shared_ptr<void>> unfinished;
};
enum class IgbListDefaultsEntry {children,registry}; //8002E108 /8002DDD4.
class IgbListDefaultsInvocation {
public:
    explicit IgbListDefaultsInvocation(IgbListDefaultsEntry);
    ~IgbListDefaultsInvocation();
    IgbListDefaultsInvocation(const IgbListDefaultsInvocation&)=delete;
    IgbListDefaultsInvocation& operator=(const IgbListDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbListDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual6588C: finalize, reload list, reference assignment, original53C44 store.
class IgbFieldReplaceInvocation {
public:
    IgbFieldReplaceInvocation(std::shared_ptr<IgbRuntimeMetaType>,std::uint32_t index,
        std::shared_ptr<IgbDecodeRuntimeField>);
    ~IgbFieldReplaceInvocation();
    IgbFieldReplaceInvocation(const IgbFieldReplaceInvocation&)=delete;
    IgbFieldReplaceInvocation& operator=(const IgbFieldReplaceInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbListDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_list_defaults_factory(
    std::shared_ptr<IgbListDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
