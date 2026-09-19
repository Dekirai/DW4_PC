#pragma once

#include "dw4/igb_decode_tables.hpp"
#include "dw4/igb_runtime_types.hpp"
#include "dw4/igb_stream.hpp"

#include <variant>

namespace dw4 {
class IgbTypeMappingError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// Reached igDataList fields. These owners have a constructed creator reference;
// original full-word increments/decrements remain separate from host lifetime.
// Registry lookup containers have a different layout and are not these lists.
template<class T> struct IgbMappingList {
    using Storage=std::vector<T>;
    std::int32_t count{},capacity{}; // +08/+0C.
    std::shared_ptr<Storage> backing=std::make_shared<Storage>(); // +10.
    std::uint32_t reference_word{1}; // +04; low 23 bits select final destruction.
};
using IgbMappedTypeList=IgbMappingList<std::shared_ptr<IgbRuntimeMetaType>>;
using IgbShortSlotList=IgbMappingList<std::int16_t>;
using IgbSlotMapList=IgbMappingList<std::shared_ptr<IgbShortSlotList>>;
using IgbMappingReference=std::variant<std::monostate,std::shared_ptr<IgbMappedTypeList>,
    std::shared_ptr<IgbSlotMapList>,std::shared_ptr<IgbShortSlotList>>;

struct IgbTypeMappingState {
    std::shared_ptr<IgbMappedTypeList> types; // File +C0.
    std::shared_ptr<IgbSlotMapList> slot_maps; // File +3C.
    bool faulted() const noexcept {return fault_;}
private:
    friend class IgbTypeMappingInvocation;
    std::weak_ptr<void> active_;
    bool fault_{};
    // Retain interrupted original references and raw overwritten slots. Host
    // continuation destruction is not an original object release operation.
    std::vector<std::shared_ptr<void>> unreleased_;
};

enum class IgbTypeMappingServiceKind {
    create_type_list,       // 80034380(captured file pool).
    create_slot_map_list,   // 80035244(captured file pool).
    create_short_list,      // 80035418(captured file pool).
    reserve_type_list,      // 8004155C(list,capacity,4).
    reserve_slot_map_list,  // 8004155C(list,capacity,4).
    reserve_short_list,     // 8004155C(list,capacity,2).
    destroy_list,           // 80066E1C after an actual final reference decrement.
    short_converter,        // Fresh singleton getter80025CD4, not file+D0.
    default_pool,           // 80058C80, reloaded at each original call.
    create_metatype,        // 80029E64(captured default pool).
    name_metatype,          // 80065788(new type, captured name).
    parent_metatype,        // 80065640(new type, captured parent).
    create_byte_field,      // 80037B1C(captured file pool).
    initialize_field,       // 80063BA8(field,name,0,-1,new type).
    field_alignment,        // Selected field virtual+6C, original returned word.
    append_field,           // 80065844(new type, selected field).
    field_metadata,         // Selected field instance virtual+58.
    create_field,           // 8006546C(metadata,captured default pool).
    begin_registration,     // 8003E450(current registry singleton).
    register_metatype       // 8003E574(current registry singleton,new type).
};
struct IgbTypeMappingCall {
    IgbTypeMappingServiceKind kind{};
    std::shared_ptr<LoaderRequestFile> file;
    std::shared_ptr<IgbTypeMappingState> state;
    std::shared_ptr<ModelSceneObject> pool;
    std::shared_ptr<IgbRuntimeMetaType> metadata,parent;
    std::shared_ptr<IgbDecodeRuntimeField> field;
    std::shared_ptr<IgbRuntimeTypeRegistry> registry;
    std::shared_ptr<IgbMappedTypeList> type_list;
    std::shared_ptr<IgbSlotMapList> slot_maps;
    std::shared_ptr<IgbShortSlotList> short_list;
    IgbMappingReference reference;
    SystemStageString name;
    std::optional<IgbRuntimeNameSource> name_source; // Live pointer for name_metatype80065788.
    std::int32_t capacity{};
};
struct IgbTypeMappingOperation {
    std::function<MessengerCallResult()> resume;
    std::shared_ptr<IgbMappedTypeList> type_list_result;
    std::shared_ptr<IgbSlotMapList> slot_maps_result;
    std::shared_ptr<IgbShortSlotList> short_list_result;
    std::shared_ptr<IgbEndianField> converter_result;
    std::shared_ptr<ModelSceneObject> pool_result;
    std::shared_ptr<IgbRuntimeMetaType> metadata_result;
    std::shared_ptr<IgbDecodeRuntimeField> field_result;
};
using IgbTypeMappingFactory=std::function<std::shared_ptr<IgbTypeMappingOperation>(const IgbTypeMappingCall&)>;
struct IgbTypeMappingServices {
    std::shared_ptr<IgbRuntimeTypesServices> runtime;
    std::optional<std::uint8_t> allow_dynamic; // Original global8056213D.
    IgbTypeMappingFactory factory;
};

// Original8004FC94: known type/version reconciliation, inherited signed slot
// maps, and the dynamic-type caller. Every unrecovered constructor, field
// operation, registration or final destructor remains an explicit boundary.
class IgbTypeMappingInvocation {
public:
    IgbTypeMappingInvocation(std::shared_ptr<LoaderRequestFile>,std::shared_ptr<IgbStreamState>,
        std::shared_ptr<IgbDecodeTablesState>,std::shared_ptr<IgbTypeMappingState>);
    ~IgbTypeMappingInvocation();
    IgbTypeMappingInvocation(const IgbTypeMappingInvocation&)=delete;
    IgbTypeMappingInvocation& operator=(const IgbTypeMappingInvocation&)=delete;
    MessengerCallResult resume(std::shared_ptr<IgbTypeMappingServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    std::uint32_t value() const;
private:struct Impl;std::unique_ptr<Impl> impl_;
};
} // namespace dw4
