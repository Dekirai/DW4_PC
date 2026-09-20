#pragma once

#include "dw4/igb_field_schema.hpp"
#include "dw4/igb_metadata_factory.hpp"

namespace dw4 {
class IgbMetadataDefaultsError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbMetadataDefaultsCall {
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::uint32_t function{0x80071694u},argument{}; // Reached string-default setter.
};
struct IgbMetadataDefaultsOperation {std::function<MessengerCallResult()> resume;};
using IgbMetadataDefaultsFactory=std::function<std::shared_ptr<IgbMetadataDefaultsOperation>(const IgbMetadataDefaultsCall&)>;
struct IgbMetadataDefaultsServices {
    IgbMetadataDefaultsServices();
    // Canonical8056174C belongs to the existing metadata factory services.
    std::shared_ptr<IgbMetadataFactoryServices> metadata;
    std::shared_ptr<IgbFieldSchemaServices> schema;
    // Lazy raw getter cells805619CC/805619D4; the getters allocate actual
    // metadata with80029E64 when represented null. Absence is not null.
    std::shared_ptr<IgbRegistrationMetaSlot> field_list_metadata=std::make_shared<IgbRegistrationMetaSlot>();
    std::shared_ptr<IgbRegistrationMetaSlot> metadata_list_metadata=std::make_shared<IgbRegistrationMetaSlot>();
    std::shared_ptr<IgbRegistrationMetaSlot> object_metadata; //805622A4 plain read.
    std::shared_ptr<IgbFieldFactoryTable> factories; //80464304.
    std::shared_ptr<IgbFieldNameTable> names; //80464478.
    std::shared_ptr<IgbFieldGlobalTable> globals; //804644DC; cells80561750..805617B0.
    std::shared_ptr<IgbFieldOffsetTable> offsets; //80464540.
    IgbMetadataDefaultsFactory factory;
    std::vector<std::shared_ptr<void>> unfinished;
};
// Actual8002A164, including its captured metadata/count and25-field table.
// Specialized factories and string-default ownership remain reached services.
class IgbMetadataDefaultsInvocation {
public:
    IgbMetadataDefaultsInvocation();~IgbMetadataDefaultsInvocation();
    IgbMetadataDefaultsInvocation(const IgbMetadataDefaultsInvocation&)=delete;
    IgbMetadataDefaultsInvocation& operator=(const IgbMetadataDefaultsInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbMetadataDefaultsServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbTypeRegistrationFactory make_native_igb_metadata_defaults_factory(
    std::shared_ptr<IgbMetadataDefaultsServices>,IgbTypeRegistrationFactory remaining={});
} // namespace dw4
