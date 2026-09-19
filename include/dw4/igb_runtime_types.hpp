#pragma once

#include "dw4/system_stage.hpp"

#include <optional>

namespace dw4 {
struct IgbScratchBuffer;
struct SystemStageLoader;
struct IgbObjectInstanceList;
class IgbRuntimeTypesError : public std::runtime_error {public: using std::runtime_error::runtime_error;};
// Captured original C-string pointer into a live, initialized scratch allocation.
// Capturing this source does not read any byte or manufacture a host string.
struct IgbRuntimeNameSource {std::shared_ptr<IgbScratchBuffer> buffer;std::uint32_t offset{};};
SystemStageString igb_runtime_read_name(const IgbRuntimeNameSource& source);

// Canonical runtime field instances, not serialized IgbFieldType records.
struct IgbRuntimeFieldDefaultSource {
    std::shared_ptr<IgbScratchBuffer> buffer;
    std::uint32_t offset{};
};
struct IgbDecodeRuntimeField {
    std::shared_ptr<ModelSceneObject> owner;
    std::uint32_t offset{}; // +08.
    std::uint16_t ordinal{},size{}; // +12/+14.
    std::uint8_t flag_26{};
    // Independently mutable packed word of a represented constructed owner.
    std::uint32_t reference_word{1};
    std::uint8_t flag_25{}; // Zero is counted by original8006603C.
    std::optional<std::uint8_t> default_mode; // Signed byte+24, read by80063DD0.
    std::optional<std::uint32_t> vtable_word; // Physical descriptor+00.
    // Descriptor+20 pointer. Absence is unknown; a null buffer is known null.
    std::optional<IgbRuntimeFieldDefaultSource> default_bytes;
};
struct IgbDecodeFieldRegistry {
    using Storage=std::vector<std::shared_ptr<IgbDecodeRuntimeField>>;
    std::int32_t count{}; // +0C; distinct from an igDataList count at +08.
    std::shared_ptr<Storage> backing; // +08.
};

// A single runtime metadata identity is shared by field metadata virtual+58
// and the registered object types consumed by original8004FC94.
struct IgbRuntimeMetaType {
    SystemStageString name; // +1C. Kept first for existing metadata fixtures.
    std::uint32_t major{},minor{}; // +10/+14.
    std::uint8_t flag_1a{};
    std::uint32_t class_flags{}; // +24.
    std::shared_ptr<IgbDecodeFieldRegistry> fields; // +28.
    std::uint32_t native_size{}; // +48.
    std::uint16_t native_alignment{}; // +58.
    // Lookup and slot-count reads do not acquire/release the original owner.
    std::uint32_t reference_word{1};
    // Factory/instance fields are not inferred from a type-name registration.
    std::optional<std::uint32_t> allocation_prefix; // +20.
    std::optional<std::uint32_t> factory_redirect; // +3C, zero is represented null.
    std::optional<SystemStageString> default_pool_name; // +60.
    std::optional<std::uint32_t> type_word; // +5C, published through ArkCore+394.
    std::optional<std::uint32_t> instance_count; // +2C, wrapping live count.
    std::optional<std::shared_ptr<IgbObjectInstanceList>> instances; // +30.
    std::optional<std::uint32_t> post_layout_callback; // +4C, zero is a null function.
    std::optional<std::uint32_t> field_callback; // +54, assigned by80066204.
};
using IgbDecodeRuntimeType=IgbRuntimeMetaType;

// Original lookup registry layout, not the file+C0 igMetaObjectList layout.
struct IgbRuntimeTypeList {
    using Storage=std::vector<std::shared_ptr<IgbRuntimeMetaType>>;
    std::int32_t count{},capacity{}; // Count +0C; capacity is not read by lookup.
    std::shared_ptr<Storage> backing; // +08, reloaded at each original read site.
};
struct IgbRuntimeTypeRegistry {
    std::uint8_t enabled{}; // Singleton80562100 +00.
    std::shared_ptr<IgbRuntimeTypeList> types; // +18.
    // The same igArkCore owner also stores the resource loader at +40.
    // Its constructor does not initialize this field. Absence is therefore
    // unrepresented state; an engaged null is a represented null pointer.
    std::optional<std::shared_ptr<SystemStageLoader>> loader;
    std::optional<std::uint32_t> vtable_offset; // +394, captured by800667F4.
    std::optional<std::uint8_t> field_initialization_enabled; // +14 gate in80066F40.
    std::optional<std::shared_ptr<ModelSceneObject>> registration_callback_pool; // +34, callback-list backing allocation.
    std::optional<std::shared_ptr<ModelSceneObject>> registration_filter_pool; // +60.
    std::optional<std::int32_t> registration_filter_enabled; // +64.
    std::optional<std::shared_ptr<ModelSceneObject>> registration_filter_names; // +68.
};
struct IgbRuntimeNameField {
    std::uint32_t offset{0x1c}; // Descriptor8056176C +08, registered _name.
};
struct IgbRuntimeTypesServices {
    // Disengaged means the global has not been represented. Engaged null is
    // the original null pointer and fails only when its dereference is reached.
    std::optional<std::shared_ptr<IgbRuntimeTypeRegistry>> singleton;
    std::optional<std::shared_ptr<IgbRuntimeNameField>> name_field;
};

// Actual80065D60 -> 8003E474 -> 80053910. This lookup never creates a type.
// Original string comparison is synchronous; there is no invented callback.
class IgbRuntimeTypeLookupInvocation {
public:
    explicit IgbRuntimeTypeLookupInvocation(SystemStageString name);
    IgbRuntimeTypeLookupInvocation(std::shared_ptr<IgbScratchBuffer> name_buffer,std::uint32_t offset);
    ~IgbRuntimeTypeLookupInvocation();
    IgbRuntimeTypeLookupInvocation(const IgbRuntimeTypeLookupInvocation&)=delete;
    IgbRuntimeTypeLookupInvocation& operator=(const IgbRuntimeTypeLookupInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbRuntimeTypesServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::shared_ptr<IgbRuntimeMetaType> result() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Actual80065D88: read meta+28, then that registry's raw signed +0C word.
// Neither physical field backing nor serialized type slot counts are consulted.
std::int32_t igb_runtime_slot_count(const std::shared_ptr<IgbRuntimeMetaType>& metadata);
// Actual800658E4: physical field access, independent of the registry count.
std::shared_ptr<IgbDecodeRuntimeField> igb_runtime_field_at(
    const std::shared_ptr<IgbRuntimeMetaType>& metadata,std::int32_t index);
// Actual8006603C: count fields whose byte+25 is zero, reloading the runtime
// count and field owner at each original read. No serialized count is used.
std::int32_t igb_runtime_local_field_count(const std::shared_ptr<IgbRuntimeMetaType>& metadata);
} // namespace dw4
