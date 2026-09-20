#pragma once

#include "dw4/igb_field_operations.hpp"
#include "dw4/igb_meta_field_factory.hpp"
#include "dw4/igb_file_pools.hpp"

namespace dw4 {
class IgbFieldSchemaError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
using IgbFieldFactoryTable=std::vector<std::optional<std::uint32_t>>;
using IgbFieldNameTable=std::vector<std::optional<SystemStageString>>;
struct IgbFieldGlobalBinding {
    std::shared_ptr<IgbRuntimeFieldSlot> slot;
    std::uint32_t address{};
};
using IgbFieldGlobalTable=std::vector<std::optional<IgbFieldGlobalBinding>>;
using IgbFieldOffsetTable=std::vector<std::optional<std::uint32_t>>;
enum class IgbFieldSchemaServiceKind {create_field,destroy_field};
struct IgbFieldSchemaCall {
    IgbFieldSchemaServiceKind kind{};
    std::uint32_t function{};
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbDecodeRuntimeField> field;
};
struct IgbFieldSchemaOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbDecodeRuntimeField> field_result;
    std::optional<std::uint32_t> field_address;
};
using IgbFieldSchemaFactory=std::function<std::shared_ptr<IgbFieldSchemaOperation>(const IgbFieldSchemaCall&)>;
struct IgbFieldSchemaServices {
    std::shared_ptr<IgbTypeRegistrationServices> types;
    std::shared_ptr<IgbFilePoolsServices> pools;
    std::shared_ptr<IgbDependenciesServices> dependencies;
    std::shared_ptr<IgbFieldOperationsServices> fields;
    std::shared_ptr<IgbMetaFieldFactoryServices> meta_fields;
    IgbFieldSchemaFactory factory;
    std::vector<std::shared_ptr<void>> unfinished;
};
// Actual65924: per-entry pool lookup, live factory, raw65820 append, creator
// release. Fields are intentionally not configured or finalized here.
class IgbFieldBatchInvocation {
public:
    IgbFieldBatchInvocation(std::shared_ptr<IgbRuntimeMetaType>,std::shared_ptr<IgbFieldFactoryTable>,std::uint32_t count);
    ~IgbFieldBatchInvocation();
    IgbFieldBatchInvocation(const IgbFieldBatchInvocation&)=delete;
    IgbFieldBatchInvocation& operator=(const IgbFieldBatchInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldSchemaServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual63BA8: name assignment and temporary release precede three raw field
// stores and buffer preparation. A null name skips all interning operations.
class IgbFieldConfigureInvocation {
public:
    IgbFieldConfigureInvocation(std::shared_ptr<IgbDecodeRuntimeField>,SystemStageString,
        IgbFieldGlobalBinding,std::uint32_t offset,std::shared_ptr<IgbRuntimeMetaType> declaring_type);
    ~IgbFieldConfigureInvocation();
    IgbFieldConfigureInvocation(const IgbFieldConfigureInvocation&)=delete;
    IgbFieldConfigureInvocation& operator=(const IgbFieldConfigureInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldSchemaServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual659C0: capture count once; reload field-list/backing at every iteration.
// Parallel input tables start at zero even when first_index skips inherited fields.
class IgbFieldSchemaInvocation {
public:
    IgbFieldSchemaInvocation(std::shared_ptr<IgbRuntimeMetaType>,std::shared_ptr<IgbFieldNameTable>,
        std::shared_ptr<IgbFieldGlobalTable>,std::shared_ptr<IgbFieldOffsetTable>,std::uint32_t first_index);
    ~IgbFieldSchemaInvocation();
    IgbFieldSchemaInvocation(const IgbFieldSchemaInvocation&)=delete;
    IgbFieldSchemaInvocation& operator=(const IgbFieldSchemaInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldSchemaServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Known factory22A90 enters the authored unsigned-int descriptor constructor.
// Every other function identity remains an explicit reached operation.
IgbFieldSchemaFactory make_native_igb_field_schema_factory(
    std::shared_ptr<IgbFieldSchemaServices>,IgbFieldSchemaFactory remaining={});
} // namespace dw4
