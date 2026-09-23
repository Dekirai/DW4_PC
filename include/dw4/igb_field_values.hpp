#pragma once

#include "dw4/igb_metadata_defaults.hpp"
#include "dw4/igb_metadata_layout.hpp"

namespace dw4 {
class IgbFieldValuesError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct IgbFieldStringBinding {std::uint32_t address{};std::shared_ptr<IgbInternedString> owner;};
struct IgbFieldBufferBinding {std::shared_ptr<IgbScratchBuffer> buffer;std::uint32_t base_address{};};
struct IgbFieldValuesServices {
    std::shared_ptr<IgbFieldOperationsServices> fields;
    // Plain prototype getters, independent from their classes' metadata cells.
    // The unsigned-int cell805614C0 aliases fields->unsigned_int_prototype.
    IgbRuntimeFieldSlot signed_int_prototype; //805619E4 /94028.
    IgbRuntimeFieldSlot boolean_prototype; //80561E4C /93E84.
    IgbRuntimeFieldSlot string_prototype; //805615B8 /94100.
    IgbRuntimeFieldSlot object_prototype; //805616EC /94098.
    IgbRuntimeFieldSlot pointer_prototype; //80561698 /940D0.
    IgbRuntimeFieldSlot unsigned_short_prototype; //80561498 /94160.
    IgbRuntimeFieldSlot memory_reference_prototype; //8056181C /94070.
    IgbRuntimeFieldSlot signed_short_prototype; //805615DC /940F0.
    IgbRuntimeFieldSlot signed_byte_prototype; //80561E20 /93E94.
    IgbRuntimeFieldSlot enumeration_prototype; //80561C34 /94008.
    // Exact arithmetic addresses identify pointers stored in default buffers.
    // Zero is null and needs no binding; nonzero is never a host pointer cast.
    std::vector<IgbFieldStringBinding> strings;
    std::vector<IgbFieldBufferBinding> buffers; // Optional late arithmetic bindings for partial copies.
    std::vector<std::shared_ptr<void>> unfinished;
};
void igb_bind_field_string(const std::shared_ptr<IgbFieldValuesServices>&,
    std::uint32_t address,const std::shared_ptr<IgbInternedString>&);

// Actual63D04: capture source, prepare buffer, reload destination/size,031E8.
// Absent source is a null pointer; present source retains its original identity.
class IgbFieldValueInvocation {
public:
    IgbFieldValueInvocation(std::shared_ptr<IgbDecodeRuntimeField>,
        std::optional<IgbRuntimeFieldDefaultSource> source={},std::optional<std::uint32_t> source_address={});
    ~IgbFieldValueInvocation();
    IgbFieldValueInvocation(const IgbFieldValueInvocation&)=delete;
    IgbFieldValueInvocation& operator=(const IgbFieldValueInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldValuesServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
// Actual71694: capture field+20 before interning; release old captured-buffer
// string and publish the returned pointer without an additional retain.
class IgbFieldStringDefaultInvocation {
public:
    IgbFieldStringDefaultInvocation(std::shared_ptr<IgbDecodeRuntimeField>,IgbDependencyName);
    ~IgbFieldStringDefaultInvocation();
    IgbFieldStringDefaultInvocation(const IgbFieldStringDefaultInvocation&)=delete;
    IgbFieldStringDefaultInvocation& operator=(const IgbFieldStringDefaultInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbFieldValuesServices> services={});
    bool completed()const noexcept;bool faulted()const noexcept;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
IgbFieldOperationsFactory make_native_igb_field_values_factory(
    std::shared_ptr<IgbFieldValuesServices>,IgbFieldOperationsFactory remaining={});
IgbMetadataLayoutFactory make_native_igb_field_alignment_factory(IgbMetadataLayoutFactory remaining={});
IgbMetadataDefaultsFactory make_native_igb_metadata_string_default_factory(
    std::shared_ptr<IgbFieldValuesServices>,IgbMetadataDefaultsFactory remaining={});
} // namespace dw4
