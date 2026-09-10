#include "dw4/player_runtime_binding.hpp"
#include "dw4/player_visual.hpp"

#include <algorithm>
#include <limits>
#include <set>

namespace dw4 {
namespace {
constexpr std::size_t max_instances=1024, max_actors=64, max_links=4u*1024u*1024u;
[[noreturn]] void fail(const std::string& reason) { throw PlayerRuntimeBindingError("Player runtime binding: "+reason); }
struct Reader {
    const IgbDocument& doc;
    std::size_t links{},text_bytes{};
    const IgbObject& exact(std::uint32_t ref,std::string_view type) const {
        const auto& value=doc.object(ref);
        if (doc.metadata.object_types.at(value.type_index).name!=type) fail("expected exact "+std::string(type));
        return value;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,std::string_view type,
        std::optional<std::size_t> width=4) const {
        const auto* value=doc.field(object,slot);
        if (!value || doc.metadata.field_types.at(value->type_index).name!=type || (width && value->size!=*width))
            fail("unexpected field type or width at slot "+std::to_string(slot));
        return *value;
    }
    std::uint32_t ref(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igObjectRefMetaField");
        const auto value=doc.field_reference(object,slot); if (!value) fail("required reference is null"); return *value;
    }
    void null(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igObjectRefMetaField");
        if (doc.field_reference(object,slot)) fail("serialized ONE work requires player restoration");
    }
    std::string text(const IgbObject& object,std::uint16_t slot) {
        const auto& value=field(object,slot,"igStringMetaField",{});
        if (value.size>4104 || value.size>max_links-text_bytes) fail("serialized name budget exceeded");
        text_bytes+=value.size;
        auto result=std::string(doc.field_string(object,slot));
        if (result.size()>4096 || result.find('\0')!=std::string::npos) fail("invalid name bytes");
        return result;
    }
    std::vector<std::uint32_t> list(std::uint32_t ref,std::string_view type,std::size_t limit) {
        const auto& object=exact(ref,type);
        field(object,2,"igIntMetaField"); field(object,3,"igIntMetaField"); field(object,4,"igMemoryRefMetaField");
        const auto count=doc.field_u32(object,2),capacity=doc.field_u32(object,3);
        if (count>capacity || capacity>limit || count>max_links-links) fail("list allocation or aggregate link budget exceeded");
        links+=count;
        const auto memory=doc.field_reference(object,4);
        if ((!memory && capacity) || (memory && doc.memory(*memory).size()!=std::size_t(capacity)*4))
            fail("list storage differs from declared capacity");
        std::vector<std::uint32_t> result;
        for (const auto entry:doc.object_list(ref)) {
            if (!entry) fail("required list contains a null entry");
            result.push_back(*entry);
        }
        return result;
    }
    void unique(std::set<std::size_t>& seen,std::uint32_t ref,const char* label) const {
        doc.object(ref);
        if (!seen.insert(doc.references.at(ref).value_index).second)
            fail(std::string("aliased ")+label+" requires shared player ownership");
    }
    PlayerRuntimeActor actor(const ActionActorDefinition& definition) {
        const auto& appearance=exact(definition.appearance_reference,"igAppearance");
        const std::array<std::string_view,4> extra_types{
            "igSkinList","igModelViewMatrixBoneSelectList","igStringObjList","igNodeList"};
        for (std::uint16_t i=0;i<extra_types.size();++i)
            if (!list(ref(appearance,static_cast<std::uint16_t>(4+i)),extra_types[i],max_actors).empty())
                fail("additional appearance attachments require traversal effects");
        const auto skin_ref=ref(appearance,3);
        const auto selector_ref=ref(exact(skin_ref,"igSkin"),3);
        const auto& selector=exact(selector_ref,"igBlendMatrixSelect");
        const auto children=list(ref(selector,7),"igNodeList",max_actors);
        if (children.size()!=1) fail("main skin requires one geometry node");
        const auto& geometry=exact(children[0],"igGeometry");
        const auto attrs=list(ref(geometry,8),"igAttrList",max_actors);
        std::optional<std::uint32_t> geometry_ref;
        for (const auto attr:attrs) {
            const auto& object=doc.object(attr);
            if (!doc.is_a(object,"igAttr")) fail("geometry attribute list contains a non-attribute");
            if (doc.metadata.object_types.at(object.type_index).name=="igGeometryAttr1_5") {
                if (geometry_ref) fail("main skin has multiple geometry attributes");
                geometry_ref=attr;
            }
        }
        if (!geometry_ref) fail("main skin has no geometry attribute");
        return {definition.reference,definition.appearance_reference,skin_ref,selector_ref,*geometry_ref};
    }
};
} // namespace

struct PlayerRuntimeBinding::Data {
    std::string source;
    std::size_t occurrence;
    PlayerRuntimeRequest request;
    std::uint32_t model_ram,one,node,lod;
    std::size_t selected_actor;
    std::vector<PlayerRuntimeActor> actors;
    ModelSequenceProgram sequence;
    ActionProgram action;
    ActorSkinBinding skin;
};

const std::string& PlayerRuntimeBinding::source() const noexcept { return data_->source; }
std::size_t PlayerRuntimeBinding::source_occurrence() const noexcept { return data_->occurrence; }
const std::string& PlayerRuntimeBinding::model_name() const noexcept { return data_->action.name(); }
std::uint32_t PlayerRuntimeBinding::model_reference() const noexcept { return data_->request.model_reference; }
std::uint32_t PlayerRuntimeBinding::model_ram_reference() const noexcept { return data_->model_ram; }
std::uint32_t PlayerRuntimeBinding::one_reference() const noexcept { return data_->one; }
std::uint32_t PlayerRuntimeBinding::node_reference() const noexcept { return data_->node; }
std::uint32_t PlayerRuntimeBinding::sequence_reference() const noexcept { return data_->sequence.reference_index(); }
std::uint32_t PlayerRuntimeBinding::action_reference() const noexcept { return data_->request.action_reference; }
std::uint32_t PlayerRuntimeBinding::action_ram_reference() const noexcept { return data_->action.rams()[instance_index()].reference; }
std::uint32_t PlayerRuntimeBinding::combiner_reference() const noexcept { return data_->action.rams()[instance_index()].combiner_reference; }
std::uint32_t PlayerRuntimeBinding::instance_index() const noexcept { return data_->request.instance_index; }
std::uint32_t PlayerRuntimeBinding::branch_index() const noexcept { return data_->request.branch_index; }
std::uint32_t PlayerRuntimeBinding::lod_reference() const noexcept { return data_->lod; }
const std::vector<PlayerRuntimeActor>& PlayerRuntimeBinding::actors() const noexcept { return data_->actors; }
std::size_t PlayerRuntimeBinding::selected_actor_index() const noexcept { return data_->selected_actor; }
const ModelSequenceProgram& PlayerRuntimeBinding::sequence_program() const noexcept { return data_->sequence; }
const ActionProgram& PlayerRuntimeBinding::action_program() const noexcept { return data_->action; }
const ActorSkinBinding& PlayerRuntimeBinding::skin_binding() const noexcept { return data_->skin; }

PlayerRuntimeBinding decode_player_runtime_binding(const IgbDocument& doc,PlayerRuntimeRequest request,
    std::string source,std::size_t occurrence) {
    try {
        if (source.empty() || source.size()>4096 || source.find('\0')!=std::string::npos)
            fail("a bounded explicit source string is required");
        if (occurrence==std::numeric_limits<std::size_t>::max()) fail("source occurrence uses the invalid sentinel");
        if (doc.metadata.header.byte_order!=IgbByteOrder::little) fail("big-endian player bindings are unverified");
        Reader read{doc};
        if (!doc.info_list_reference) fail("explicit info exports are absent");
        const auto exports=read.list(*doc.info_list_reference,"igInfoList",4096);
        std::set<std::size_t> export_ids;
        for (const auto ref:exports) {
            if (!doc.is_a(doc.object(ref),"igInfo")) fail("export is not an igInfo");
            read.unique(export_ids,ref,"export");
        }
        for (const auto ref:{request.model_reference,request.action_reference})
            if (std::find(exports.begin(),exports.end(),ref)==exports.end()) fail("selected info is not an explicit export");
        const auto& model=read.exact(request.model_reference,"beModelCtrlInfo");
        read.exact(request.action_reference,"beAction2Info");
        if (read.text(model,7)!="KIND_PLAYER") fail("model is not KIND_PLAYER");
        const auto model_name=read.text(model,2);
        auto action=decode_action_program(doc,request.action_reference,source);
        if (action.name()!=model_name) fail("MODELCTRL and ACTION names do not identify the same target");
        const auto instances=read.list(read.ref(model,6),"beBaseInfoRamList",max_instances);
        if (instances.empty() || instances.size()!=action.rams().size() || request.instance_index>=instances.size())
            fail("MODELCTRL and ACTION instance counts or selection differ");
        std::optional<std::uint32_t> sequence_ref;
        for (const auto ref:read.list(read.ref(model,5),"beBaseInfoDataList",4096)) {
            const auto& value=doc.object(ref);
            if (!doc.is_a(value,"beBaseInfoData")) fail("model data list contains a non-data object");
            if (read.text(value,2)!="SEQ") continue;
            read.exact(ref,"beModelCtrlInfoDataSeq");
            if (sequence_ref) fail("ambiguous SEQ data requires original dictionary ownership");
            sequence_ref=ref;
        }
        if (!sequence_ref) fail("model has no explicit SEQ data");
        auto sequence=decode_model_sequences(doc,*sequence_ref,source);
        std::set<std::size_t> model_rams,ones,nodes,action_rams,combiners,actor_ids,appearance_ids;
        std::optional<ActionActorGroup> selected_group;
        std::uint32_t selected_one=0,selected_node=0;
        for (std::size_t i=0;i<instances.size();++i) {
            read.unique(model_rams,instances[i],"MODELCTRL RAM");
            const auto& ram=read.exact(instances[i],"beBaseInfoRam");
            const auto data=read.list(read.ref(ram,10),"beBaseInfoDataList",4096);
            if (data.size()!=1) fail("MODELCTRL RAM requires exactly one ONE record");
            read.unique(ones,data[0],"ONE record");
            const auto& one=read.exact(data[0],"beModelCtrlInfoRamData");
            if (read.text(one,2)!="ONE") fail("model RAM data is not ONE");
            read.field(one,3,"igIntMetaField"); read.null(one,5);
            const auto node=read.ref(one,4); read.exact(node,"beModelCtrlNode2"); read.unique(nodes,node,"model node");
            const auto& owner=action.rams()[i];
            if (node!=owner.node_reference) fail("MODELCTRL ONE node differs from its ACTION RAM node");
            read.unique(action_rams,owner.reference,"ACTION RAM"); read.unique(combiners,owner.combiner_reference,"combiner");
            auto actors=decode_action_actor_group(doc,owner.reference,owner.combiner_reference,source);
            for (const auto& actor:actors.definitions()) {
                read.unique(actor_ids,actor.reference,"actor"); read.unique(appearance_ids,actor.appearance_reference,"appearance");
            }
            if (i==request.instance_index) { selected_group=std::move(actors); selected_one=data[0]; selected_node=node; }
        }
        const auto visual=inspect_igb_player_visual(doc,{request.model_reference,request.instance_index,request.branch_index});
        if (!visual.supported) fail(visual.diagnostic);
        if (visual.model_node_reference!=selected_node) fail("visual chain differs from the bound model node");
        const auto branches=read.list(read.ref(read.exact(visual.lod_reference,"igLod"),7),"igNodeList",max_actors);
        const auto& definitions=selected_group->definitions();
        if (branches.size()!=definitions.size()) fail("LOD branches and ordered ACTION actors differ");
        std::set<std::size_t> branch_ids;
        for (const auto branch:branches) read.unique(branch_ids,branch,"LOD actor");
        std::vector<PlayerRuntimeActor> actors;
        std::optional<std::size_t> selected_actor;
        for (std::size_t i=0;i<definitions.size();++i) {
            const auto& definition=definitions[i];
            if (std::find(branches.begin(),branches.end(),definition.reference)==branches.end())
                fail("ACTION actor is absent from the actual model LOD");
            actors.push_back(read.actor(definition));
            if (definition.reference==visual.actor_reference) selected_actor=i;
        }
        if (!selected_actor) fail("selected visual actor is absent from its ACTION RAM");
        const auto& selected=actors[*selected_actor];
        if (selected.appearance_reference!=visual.appearance_reference || selected.skin_reference!=visual.skin_reference ||
            selected.geometry_reference!=visual.geometry_reference) fail("selected actor skin chains differ");
        auto skin=decode_actor_skin_binding(doc,*selected_group,*selected_actor,selected.geometry_reference);
        return PlayerRuntimeBinding(std::make_shared<const PlayerRuntimeBinding::Data>(PlayerRuntimeBinding::Data{
            std::move(source),occurrence,request,instances[request.instance_index],selected_one,selected_node,
            visual.lod_reference,*selected_actor,std::move(actors),std::move(sequence),std::move(action),std::move(skin)}));
    } catch (const PlayerRuntimeBindingError&) { throw; }
      catch (const std::exception& error) { fail(error.what()); }
}
} // namespace dw4
