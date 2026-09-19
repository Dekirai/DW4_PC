#include "dw4/igb_runtime_bootstrap.hpp"

#include <array>
#include <map>
#include <string_view>

namespace dw4 {
namespace {
struct FieldClass {const char* name;std::uint32_t registration,size;};
// Original registration arguments, not an IGB field table. The associated
// singleton extensions and concrete virtual+58 cache getters were inspected
// separately; unrelated field-class schemas remain unrepresented.
constexpr FieldClass field_classes[]{
    {"igVirtualCFuncMetaField",0x80021ce4,0x34},
    {"igUnsignedShortMetaField",0x8002201c,0x34},
    {"igUnsignedShortArrayMetaField",0x800222a0,0x38},
    {"igUnsignedLongMetaField",0x8002265c,0x34},
    {"igUnsignedLongArrayMetaField",0x800228e0,0x38},
    {"igUnsignedIntMetaField",0x80022cd8,0x34},
    {"igUnsignedIntArrayMetaField",0x80022f5c,0x38},
    {"igUnsignedCharMetaField",0x80023234,0x34},
    {"igUnsignedCharArrayMetaField",0x800234c0,0x38},
    {"igStructMetaField",0x80024634,0x40},
    {"igStringMetaField",0x80025404,0x3c},
    {"igStringArrayMetaField",0x800257f8,0x40},
    {"igShortMetaField",0x80025bb4,0x34},
    {"igShortArrayMetaField",0x80025e38,0x38},
    {"igRawRefMetaField",0x80027704,0x38},
    {"igRawRefArrayMetaField",0x8002795c,0x3c},
    {"igObjectRefMetaField",0x80028a20,0x44},
    {"igObjectRefArrayMetaField",0x80028d9c,0x48},
    {"igMemoryRefMetaField",0x8002aeac,0x50},
    {"igMemoryRefArrayMetaField",0x8002b3a8,0x54},
    {"igLongMetaField",0x8002cfd0,0x34},
    {"igLongArrayMetaField",0x8002d224,0x38},
    {"igIntMetaField",0x8002e3b8,0x34},
    {"igIntArrayMetaField",0x8002e63c,0x38},
    {"igFloatMetaField",0x8002faf4,0x34},
    {"igFloatArrayMetaField",0x8002fd80,0x38},
    {"igEnumMetaField",0x80031584,0x38},
    {"igEnumArrayMetaField",0x8003188c,0x3c},
    {"igDoubleMetaField",0x800320c8,0x34},
    {"igDoubleArrayMetaField",0x8003231c,0x38},
    {"igDependencyMetaField",0x800335ac,0x38},
    {"igCharMetaField",0x80037d28,0x34},
    {"igCharArrayMetaField",0x80037fac,0x38},
    {"igBoolMetaField",0x80038828,0x34},
    {"igBoolArrayMetaField",0x80038aac,0x38},
    {"igMemoryDescriptorMetaField",0x8003a298,0x38},
    {"igTrackedElementMetaField",0x800d2ab0,0x38},
    {"igCharMetricsMetaField",0x801150c8,0x38},
    {"igVec4ucMetaField",0x8011e028,0x34},
    {"igVec4fMetaField",0x8011e2a4,0x34},
    {"igVec4fArrayMetaField",0x8011e530,0x38},
    {"igVec3ucMetaField",0x8011ea14,0x34},
    {"igVec3fMetaField",0x8011ec90,0x34},
    {"igVec2fMetaField",0x8011f150,0x34},
    {"igMatrix44fMetaField",0x8011feb4,0x34},
    {"igItemDataBaseField",0x801454a4,0x38},
    {"igInterfaceDeclarationField",0x80146008,0x38}
};
enum class Kind {unsigned_int,integer,memory,string,enumeration,object,raw,boolean,short_integer};
constexpr const char* kind_names[]{"igUnsignedIntMetaField","igIntMetaField","igMemoryRefMetaField",
    "igStringMetaField","igEnumMetaField","igObjectRefMetaField","igRawRefMetaField","igBoolMetaField","igShortMetaField"};
struct Field {std::uint16_t slot;const char* name;Kind kind;std::uint32_t offset;std::uint8_t flag;};
using enum Kind;
constexpr Field object_fields[]{{0,"_refCount",unsigned_int,4,0},{1,"_memPoolIndex",unsigned_int,4,0}};
constexpr Field named_fields[]{{2,"_name",string,8,1}};
constexpr Field dir_fields[]{{3,"_index",integer,12,0},{4,"_resolveMethod",enumeration,16,0},
    {5,"_refType",enumeration,20,0},{6,"_ref",raw,24,0}};
constexpr Field object_dir_fields[]{{6,"_ref",object,24,0},{7,"_typeMeta",object,28,0},
    {8,"_typeMetaName",string,32,0},{9,"_majorVersion",integer,36,0},{10,"_minorVersion",integer,40,0},
    {11,"_typeMetaIndex",integer,48,1},{12,"_memoryPoolHandle",integer,44,1}};
constexpr Field memory_dir_fields[]{{7,"_memSize",integer,28,1},{8,"_memTypeSize",integer,32,0},
    {9,"_memType",object,36,0},{10,"_memTypeIndex",integer,40,1},{11,"_refCounted",boolean,52,1},
    {12,"_alignmentTypeIndex",integer,44,1},{13,"_memoryPoolHandle",integer,48,1}};
constexpr Field external_dir_fields[]{{6,"_ref",object,24,0},{7,"_directoryName",string,28,1},
    {8,"_entryName",string,32,1},{9,"_entryIndex",integer,36,1},{10,"_externalFilePath",string,40,0}};
constexpr Field external_index_fields[]{{7,"_directoryIndex",integer,28,1},{8,"_entryIndex",integer,32,1},
    {9,"_entryType",object,36,0},{10,"_entryTypeIndex",integer,40,1},{11,"_objectType",object,44,0},
    {12,"_objectTypeIndex",integer,48,1}};
constexpr Field external_info_fields[]{{7,"_key",string,28,1},{8,"_directoryIndex",integer,32,1},{9,"_infoName",string,36,1}};
constexpr Field data_list_fields[]{{2,"_count",integer,8,1},{3,"_capacity",integer,12,1},{4,"_data",memory,16,1}};
constexpr Field object_list_fields[]{{4,"_data",memory,16,1}}; // Original clones and replaces this inherited owner.
constexpr Field file_name_fields[]{{3,"_pathList",object,12,1}};
constexpr Field afs_fields[]{{4,"_ptNumber",short_integer,16,1},{5,"_fileNumber",short_integer,18,1}};
constexpr Field setup_data_fields[]{{3,"_fileCount",integer,12,1}};
constexpr Field info_fields[]{{3,"_directory",object,12,0},{4,"_resolveState",boolean,16,1}};
constexpr Field setup_info_fields[]{{5,"_insideFiles",object,20,1},{6,"_dataList",object,24,1}};
struct ObjectClass {const char* name;std::int32_t parent;std::uint32_t registration,size;std::span<const Field> fields;};
// Parent and local fields come from80066204/80065640 and each registration's
// defaults callback. Counts are computed by these additions and replacements.
constexpr ObjectClass object_classes[]{
    {"igObject",-1,0x80066b08,8,object_fields},
    {"igNamedObject",0,0x80029d58,12,named_fields},
    {"igDirEntry",1,0x80032d80,28,dir_fields},
    {"igObjectDirEntry",2,0x80029394,52,object_dir_fields},
    {"igMemoryDirEntry",2,0x8002c918,56,memory_dir_fields},
    {"igExternalDirEntry",2,0x80030a3c,44,external_dir_fields},
    {"igExternalImageEntry",5,0x800d4ab0,44,{}},
    {"igExternalIndexedEntry",2,0x80030704,52,external_index_fields},
    {"igExternalInfoEntry",2,0x80030434,40,external_info_fields},
    {"igDataList",0,0x80033a14,20,data_list_fields},
    {"igObjectList",9,0x8002907c,20,object_list_fields},
    {"igAdxAfsFileList",10,0x802aaab8,20,{}},
    {"igNamedObjectList",10,0x80029b6c,20,{}},
    {"igFileName",1,0x802ab0b0,16,file_name_fields},
    {"igAdxAfsFile",13,0x802aacb8,20,afs_fields},
    {"beNDMWAfsSetupData",1,0x803453dc,16,setup_data_fields},
    {"igInfo",1,0x8002eabc,20,info_fields},
    {"beNDMWAfsSetupInfo",16,0x80344fa8,28,setup_info_fields},
    {"igInfoList",10,0x8002e934,20,{}},
    {"beNDMWAfsSetupDataList",10,0x80345238,20,{}}
};
class FieldOwner final : public ModelSceneObject {
public:explicit FieldOwner(std::string type):ModelSceneObject(std::move(type)) {}
};
std::shared_ptr<IgbRuntimeMetaType> metadata(const char* name,std::uint32_t size) {
    auto result=std::make_shared<IgbRuntimeMetaType>();result->name=std::make_shared<const std::string>(name);
    result->major=1;result->minor=0;result->flag_1a=0;result->class_flags=7;result->native_size=size;return result;
}
}

struct IgbRuntimeBootstrap::Impl {
    std::shared_ptr<IgbRuntimeTypesServices> services=std::make_shared<IgbRuntimeTypesServices>();
    std::shared_ptr<IgbDecodeFieldRegistry> fields=std::make_shared<IgbDecodeFieldRegistry>();
    struct Binding {
        std::shared_ptr<IgbDecodeRuntimeField> field;
        std::shared_ptr<ModelSceneObject> owner;
        std::shared_ptr<IgbRuntimeMetaType> metadata;
    };
    std::vector<Binding> bindings;
};
std::shared_ptr<IgbRuntimeTypesServices> IgbRuntimeBootstrap::type_services() const noexcept {return impl_->services;}
std::shared_ptr<IgbDecodeFieldRegistry> IgbRuntimeBootstrap::field_registry() const noexcept {return impl_->fields;}
std::shared_ptr<IgbRuntimeMetaType> IgbRuntimeBootstrap::field_metadata(const std::shared_ptr<IgbDecodeRuntimeField>& field) const {
    if(field)for(const auto& binding:impl_->bindings)
        if(binding.field==field && binding.owner==field->owner)return binding.metadata;
    throw IgbRuntimeTypesError("Field metadata virtual+58 is not bound for this exact runtime instance.");
}
IgbRuntimeBootstrap make_original_ndmw_runtime_bootstrap() {
    auto state=std::make_shared<IgbRuntimeBootstrap::Impl>();
    auto registry=std::make_shared<IgbRuntimeTypeRegistry>();registry->enabled=1;
    registry->types=std::make_shared<IgbRuntimeTypeList>();registry->types->backing=std::make_shared<IgbRuntimeTypeList::Storage>();
    state->services->singleton=registry;state->services->name_field=std::make_shared<IgbRuntimeNameField>();
    state->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
    std::map<std::string_view,std::shared_ptr<IgbRuntimeMetaType>> field_types;
    for(const auto& description:field_classes) {
        auto type=metadata(description.name,description.size);field_types.emplace(description.name,type);
        registry->types->backing->push_back(type);
        auto field=std::make_shared<IgbDecodeRuntimeField>();field->owner=std::make_shared<FieldOwner>(description.name);
        // Only instance identity and virtual+58 are bound for these singleton
        // representatives. Their global ordinal, defaults and array widths
        // are intentionally not consumed by this metadata-only profile.
        state->fields->backing->push_back(field);state->bindings.push_back({field,field->owner,type});
    }
    std::vector<std::shared_ptr<IgbRuntimeMetaType>> objects;
    for(const auto& description:object_classes) {
        auto type=metadata(description.name,description.size);type->native_alignment=4;
        type->fields=std::make_shared<IgbDecodeFieldRegistry>();type->fields->backing=std::make_shared<IgbDecodeFieldRegistry::Storage>();
        auto& fields=*type->fields->backing;
        if(description.parent>=0)fields=*objects.at(static_cast<std::size_t>(description.parent))->fields->backing;
        for(const auto& description_field:description.fields) {
            auto field=std::make_shared<IgbDecodeRuntimeField>();
            const auto* kind=kind_names[static_cast<unsigned>(description_field.kind)];
            field->owner=std::make_shared<FieldOwner>(kind);field->owner->name=description_field.name;
            field->offset=description_field.offset;field->ordinal=description_field.slot;field->flag_26=description_field.flag;
            field->size=static_cast<std::uint16_t>(description_field.kind==boolean ? 1:description_field.kind==short_integer ? 2:4);
            field->reference_word=0; // Count represented owning class-field lists below.
            if(description_field.slot==fields.size())fields.push_back(field);
            else fields.at(description_field.slot)=field;
            state->bindings.push_back({field,field->owner,field_types.at(kind)});
        }
        type->fields->count=static_cast<std::int32_t>(fields.size());
        for(const auto& field:fields)++field->reference_word;
        objects.push_back(type);registry->types->backing->push_back(type);
    }
    state->fields->count=static_cast<std::int32_t>(state->fields->backing->size());
    registry->types->count=static_cast<std::int32_t>(registry->types->backing->size());registry->types->capacity=registry->types->count;
    return IgbRuntimeBootstrap(std::move(state));
}
} // namespace dw4
