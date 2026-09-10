#include "dw4/action_event_tables.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace dw4 {
namespace {
constexpr std::size_t max_text=8u*1024u*1024u,max_links=128u*1024u;
constexpr std::uint32_t max_models=4096,max_actions=65536,max_rams=4096;
[[noreturn]] void fail(const char* reason) { throw ActionEventTableError(reason); }
void checked_name(std::string_view value) {
    if (value.size()>4096 || value.find('\0')!=std::string_view::npos)
        fail("Action event table name exceeds its bounded byte profile.");
}
struct Reader {
    const IgbDocument& doc;
    std::size_t text_bytes{},links{},actions{};
    void charge(std::size_t count) {
        if (count>max_text-text_bytes) fail("Action event table aggregate text budget exceeded.");
        text_bytes+=count;
    }
    const IgbObject& object(std::uint32_t reference,std::string_view type,bool exact=true) const {
        const auto& value=doc.object(reference);
        if (exact ? doc.metadata.object_types.at(value.type_index).name!=type : !doc.is_a(value,type))
            fail("Unexpected action event table object type.");
        return value;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,std::string_view type,
        std::optional<std::size_t> width=4) const {
        const auto* value=doc.field(object,slot);
        if (!value || doc.metadata.field_types.at(value->type_index).name!=type || (width && value->size!=*width))
            fail("Unexpected action event table field type or width.");
        return *value;
    }
    std::optional<std::uint32_t> nullable_ref(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igObjectRefMetaField");
        const auto ref=doc.field_reference(object,slot); if (ref) doc.object(*ref); return ref;
    }
    std::uint32_t ref(const IgbObject& object,std::uint16_t slot) const {
        const auto ref=nullable_ref(object,slot); if (!ref) fail("Required action event table list is null."); return *ref;
    }
    std::optional<std::string> text(const IgbObject& object,std::uint16_t slot) {
        const auto& value=field(object,slot,"igStringMetaField",{});
        if (value.size>4104) fail("Serialized action event table name exceeds its budget.");
        charge(value.size);
        if (value.size==4) { doc.field_string(object,slot); return {}; }
        auto result=std::string(doc.field_string(object,slot)); checked_name(result); charge(result.size()); return result;
    }
    bool boolean(const IgbObject& object,std::uint16_t slot) const {
        const auto bytes=doc.field_bytes(field(object,slot,"igBoolMetaField",1));
        if (bytes[0]>1) fail("Action event table boolean is outside its serialized profile.");
        return bytes[0]!=0;
    }
    std::array<float,3> vector(const IgbObject& object,std::uint16_t slot) const {
        const auto bytes=doc.field_bytes(field(object,slot,"igVec3fMetaField",12));
        std::array<float,3> result{};
        for (std::size_t i=0;i<result.size();++i) {
            std::uint32_t bits=0;
            for (std::size_t j=0;j<4;++j) bits|=std::uint32_t(bytes[i*4+j])<<(8*j);
            result[i]=std::bit_cast<float>(bits);
            if (!std::isfinite(result[i])) fail("Action event table vector contains a non-finite value.");
        }
        return result;
    }
    std::vector<std::uint32_t> list(std::uint32_t reference,std::string_view type,std::uint32_t limit) {
        const auto& value=object(reference,type);
        field(value,2,"igIntMetaField"); field(value,3,"igIntMetaField"); field(value,4,"igMemoryRefMetaField");
        const auto count=doc.field_u32(value,2),capacity=doc.field_u32(value,3);
        if (count>capacity || capacity>limit || count>max_links-links) fail("Action event table list budget exceeded.");
        links+=count;
        const auto memory=doc.field_reference(value,4);
        if ((!memory && capacity) || (memory && doc.memory(*memory).size()!=std::size_t(capacity)*4))
            fail("Action event table storage differs from declared list capacity.");
        std::vector<std::uint32_t> result;
        for (const auto ref:doc.object_list(reference)) {
            if (!ref) fail("Action event table contains a null object row.");
            result.push_back(*ref);
        }
        return result;
    }
    void ram(std::uint32_t reference) const {
        const auto& value=object(reference,"beBaseInfoRam");
        field(value,2,"igIntMetaField"); boolean(value,5);
        for (std::uint16_t slot=6;slot<=9;++slot) field(value,slot,"igIntMetaField");
        if (const auto data=nullable_ref(value,10)) object(*data,"beBaseInfoDataList");
    }
};
std::string_view key(const std::optional<std::string>& value) { return value ? std::string_view(*value) : std::string_view{}; }
} // namespace

struct ActionEventTableProgram::Data {
    std::string source;
    std::uint32_t occurrence,reference;
    std::optional<std::string> name;
    ActionEventConsumerKind kind;
    std::vector<std::uint32_t> rams;
    std::vector<std::uint32_t> ram_owners;
    std::vector<ActionEventModelRow> models;
};
const std::string& ActionEventTableProgram::source() const noexcept { return data_->source; }
std::uint32_t ActionEventTableProgram::source_occurrence() const noexcept { return data_->occurrence; }
std::uint32_t ActionEventTableProgram::reference() const noexcept { return data_->reference; }
const std::optional<std::string>& ActionEventTableProgram::name() const noexcept { return data_->name; }
ActionEventConsumerKind ActionEventTableProgram::kind() const noexcept { return data_->kind; }
const std::vector<std::uint32_t>& ActionEventTableProgram::rams() const noexcept { return data_->rams; }
const std::vector<std::uint32_t>& ActionEventTableProgram::ram_owners() const noexcept { return data_->ram_owners; }
const std::vector<ActionEventModelRow>& ActionEventTableProgram::models() const noexcept { return data_->models; }

ActionEventTableProgram decode_action_event_tables(const IgbDocument& doc,std::uint32_t reference,
    std::string source,std::uint32_t occurrence) {
    try {
        checked_name(source);
        if (source.empty() || occurrence==std::numeric_limits<std::uint32_t>::max())
            fail("Action event tables require an explicit source and valid loaded occurrence.");
        if (doc.metadata.header.byte_order!=IgbByteOrder::little) fail("Big-endian action event tables are unverified.");
        Reader read{doc}; read.charge(source.size());
        if (!doc.info_list_reference) fail("Action event consumer is not explicitly exported.");
        const auto exports=read.list(*doc.info_list_reference,"igInfoList",max_actions);
        for (const auto ref:exports) read.object(ref,"igInfo",false);
        if (std::find(exports.begin(),exports.end(),reference)==exports.end()) fail("Action event consumer is not explicitly exported.");
        const auto& info=doc.object(reference);
        const auto& type=doc.metadata.object_types.at(info.type_index).name;
        if (type!="beSeInfo" && type!="beActionStarterInfo") fail("Expected an exact exported Se or ActionStarter info.");
        const auto kind=type=="beSeInfo" ? ActionEventConsumerKind::se : ActionEventConsumerKind::action_starter;
        auto name=read.text(info,2); read.boolean(info,4);
        auto rams=read.list(read.ref(info,6),"beBaseInfoRamList",max_rams);
        std::vector<std::uint32_t> ram_owners;
        for (const auto ram:rams) {
            read.ram(ram);
            ram_owners.push_back(doc.object(ram).reference_index);
        } // Preserve occurrences rather than deduplicating shared objects.
        if (kind==ActionEventConsumerKind::action_starter && read.nullable_ref(info,5))
            fail("Additional ActionStarter base data requires separate controller ownership.");
        const auto models=read.list(read.ref(info,kind==ActionEventConsumerKind::se ? 5 : 7),
            kind==ActionEventConsumerKind::se ? "beBaseInfoDataList" : "beAsTargetModelDataList",max_models);
        std::vector<ActionEventModelRow> rows;
        for (const auto model_ref:models) {
            const auto& model=read.object(model_ref,kind==ActionEventConsumerKind::se ? "beSeInfoData" : "beAsTargetModelData");
            ActionEventModelRow row{model_ref,read.text(model,2),{}};
            if (kind==ActionEventConsumerKind::se) read.field(model,3,"igIntMetaField");
            const auto actions=read.list(read.ref(model,kind==ActionEventConsumerKind::se ? 4 : 3),
                kind==ActionEventConsumerKind::se ? "beSeDataList" : "beActionStarterDataList",max_actions);
            if (actions.size()>max_actions-read.actions) fail("Action event table aggregate action budget exceeded.");
            read.actions+=actions.size();
            for (const auto action_ref:actions) {
                const auto& action=read.object(action_ref,kind==ActionEventConsumerKind::se ? "beSeData" : "beActionStarterData",false);
                const auto& action_type=doc.metadata.object_types.at(action.type_index).name;
                if (action_type.size()>256) fail("Action event row type exceeds its name budget.");
                read.charge(action_type.size());
                ActionEventActionRow entry{action_ref,action_type,read.text(action,2),{}};
                auto payload_name=read.text(action,3);
                if (kind==ActionEventConsumerKind::se) {
                    read.field(action,4,"igShortMetaField",2);
                    entry.payload=ActionEventSound{static_cast<float>(doc.field_i16(action,4)),std::move(payload_name)};
                } else {
                    read.field(action,4,"igIntMetaField");
                    ModelEffectSubcommand effect;
                    effect.delay=static_cast<float>(doc.field_i32(action,4));
                    effect.model_name=std::string(key(name)); read.charge(effect.model_name->size());
                    effect.sequence_name=std::move(payload_name);
                    effect.offset=read.vector(action,5);
                    effect.sequence_dead=read.boolean(action,6); effect.position_lock=read.boolean(action,7);
                    // 802F2400 checks EXACT Data2. A descendant retaining that
                    // field still takes the original zero-rotation base branch.
                    if (action_type=="beActionStarterData2") effect.rotation=read.vector(action,8);
                    entry.payload=std::move(effect);
                }
                row.actions.push_back(std::move(entry));
            }
            rows.push_back(std::move(row));
        }
        return ActionEventTableProgram(std::make_shared<const ActionEventTableProgram::Data>(ActionEventTableProgram::Data{
            std::move(source),occurrence,reference,std::move(name),kind,std::move(rams),std::move(ram_owners),std::move(rows)}));
    } catch (const ActionEventTableError&) { throw; }
      catch (const std::exception& error) { throw ActionEventTableError(error.what()); }
}

std::vector<ActionEventTableMatch> match_action_event_tables(const ActionEventTableProgram& program,
    std::string_view model,std::string_view action) {
    checked_name(model); checked_name(action);
    std::optional<std::size_t> selected;
    const auto& models=program.models();
    for (std::size_t i=0;i<models.size();++i) if (key(models[i].name)==model) {
        selected=i;
        if (program.kind()==ActionEventConsumerKind::action_starter) break;
    }
    std::vector<ActionEventTableMatch> result;
    if (!selected) return result;
    const auto& row=models[*selected];
    for (std::size_t i=0;i<row.actions.size();++i) if (key(row.actions[i].name)==action)
        result.push_back({*selected,i,row.reference,row.actions[i].reference,row.actions[i].payload,
            program.kind()==ActionEventConsumerKind::action_starter && row.actions[i].type=="beActionStarterData2"});
    return result;
}
} // namespace dw4
