#include "dw4/player_point.hpp"
#include "dw4/action_event_runtime.hpp"

#include <bit>
#include <cmath>
#include <map>
#include <set>

namespace dw4 {
namespace {
constexpr std::size_t max_rows=65536,max_entries=262144,max_text=8u*1024u*1024u;
[[noreturn]] void fail(const char* reason) {throw PlayerPointError(reason);}
void finite(const AnimationMatrix& matrix) {
    for(const auto value:matrix)if(!std::isfinite(value))fail("PLAYERPOINT requires a finite matrix.");
}
struct Reader {
    std::shared_ptr<ActorSceneResource> resource;
    std::size_t entries{},text{};
    const IgbDocument& doc() const {return resource->document();}
    const IgbObject& object(std::uint32_t ref,std::string_view type) const {
        const auto& value=doc().object(ref);
        if(doc().metadata.object_types.at(value.type_index).name!=type)fail("Unexpected PLAYERPOINT object type.");
        std::set<std::uint16_t> slots;
        for(const auto& field:value.fields)if(!slots.insert(field.slot).second)fail("PLAYERPOINT object repeats a field slot.");
        resource->canonical_reference(ref);return value;
    }
    const IgbFieldValue& field(const IgbObject& value,std::uint16_t slot,std::string_view type,
        std::optional<std::size_t> size=4) const {
        const auto* f=doc().field(value,slot);
        if(!f || doc().metadata.field_types.at(f->type_index).name!=type || (size && f->size!=*size))
            fail("PLAYERPOINT field type or width differs.");
        doc().field_bytes(*f);return *f;
    }
    std::int32_t word(const IgbObject& value,std::uint16_t slot) const {
        field(value,slot,"igIntMetaField");return doc().field_i32(value,slot);
    }
    std::uint8_t byte(const IgbObject& value,std::uint16_t slot) const {
        return doc().field_bytes(field(value,slot,"igBoolMetaField",1))[0];
    }
    std::optional<std::uint32_t> ref(const IgbObject& value,std::uint16_t slot) const {
        field(value,slot,"igObjectRefMetaField");const auto result=doc().field_reference(value,slot);
        if(result)resource->canonical_reference(*result);return result;
    }
    std::uint32_t required(const IgbObject& value,std::uint16_t slot) const {
        const auto result=ref(value,slot);if(!result)fail("PLAYERPOINT requires its actual list owner.");return *result;
    }
    std::optional<std::string> name(const IgbObject& value) {
        const auto& f=field(value,2,"igStringMetaField",{});
        if(f.size<4 || f.size>4104 || f.size>max_text-text)fail("PLAYERPOINT name exceeds its byte budget.");
        text+=f.size;const auto result=doc().field_string(value,2);
        if(result.size()>4096 || result.find('\0')!=std::string_view::npos)fail("PLAYERPOINT name is invalid.");
        return f.size==4?std::nullopt:std::optional<std::string>{result};
    }
    DatabaseListIdentity list(std::uint32_t ref,std::string_view type) {
        const auto& value=object(ref,type);const auto count=word(value,2),capacity=word(value,3);
        field(value,4,"igMemoryRefMetaField");
        if(count<0 || capacity<count || capacity>static_cast<std::int32_t>(max_rows) || std::size_t(count)>max_entries-entries)
            fail("PLAYERPOINT list exceeds its capacity or aggregate budget.");
        entries+=static_cast<std::size_t>(count);
        const auto memory=doc().field_reference(value,4);std::optional<std::uint32_t> owner;
        if((!memory && capacity) || (memory && doc().memory(*memory).size()!=std::size_t(capacity)*4))
            fail("PLAYERPOINT list memory differs from its declared capacity.");
        if(memory)owner=doc().memories.at(doc().references.at(*memory).value_index).reference_index;
        return {ref,value.reference_index,memory,owner,static_cast<std::uint32_t>(count),static_cast<std::uint32_t>(capacity)};
    }
};
}
struct PlayerPointProgram::Data {
    std::shared_ptr<ActorSceneResource> resource;
    std::vector<PlayerPointInfoDefinition> infos;
};
const std::shared_ptr<ActorSceneResource>& PlayerPointProgram::resource() const noexcept {return data_->resource;}
const std::string& PlayerPointProgram::source() const noexcept {return resource()->source();}
std::uint32_t PlayerPointProgram::source_occurrence() const noexcept {return resource()->source_occurrence();}
const std::vector<PlayerPointInfoDefinition>& PlayerPointProgram::infos() const noexcept {return data_->infos;}
PlayerPointProgram decode_player_points(std::shared_ptr<ActorSceneResource> resource) {
    try {
        if(!resource)fail("PLAYERPOINT decoding requires its actual loaded resource.");
        const auto& doc=resource->document();if(!doc.info_list_reference)fail("PLAYERPOINT resource has no exported info list.");
        auto data=std::make_shared<PlayerPointProgram::Data>();data->resource=resource;Reader read{resource};
        std::map<std::uint32_t,std::shared_ptr<const PlayerPointGroupDefinition>> groups;
        std::map<std::uint32_t,std::shared_ptr<const PlayerPointNodeDefinition>> points;
        const auto point=[&](std::uint32_t ref) {
            const auto& object=read.object(ref,"igTransform");const auto owner=object.reference_index;
            if(const auto found=points.find(owner);found!=points.end())return found->second;
            if(points.size()>=max_rows)fail("PLAYERPOINT point-owner budget exceeded.");
            if(read.word(object,9)!=0 || read.ref(object,11))fail("PLAYERPOINT dynamic transform requires its original update service.");
            const auto bytes=doc.field_bytes(read.field(object,8,"igMatrix44fMetaField",64));AnimationMatrix matrix{};
            for(std::size_t i=0;i<16;++i) {
                const auto at=i*4;const auto bits=std::uint32_t(bytes[at])|(std::uint32_t(bytes[at+1])<<8)|
                    (std::uint32_t(bytes[at+2])<<16)|(std::uint32_t(bytes[at+3])<<24);
                matrix[i]=std::bit_cast<float>(bits);
            }
            finite(matrix);auto result=std::make_shared<const PlayerPointNodeDefinition>(PlayerPointNodeDefinition{resource->node(ref),matrix});
            points.emplace(owner,result);return result;
        };
        const auto group=[&](std::uint32_t ref) {
            const auto& object=read.object(ref,"beGroupKeep");const auto owner=object.reference_index;
            if(const auto found=groups.find(owner);found!=groups.end())return found->second;
            if(groups.size()>=max_rows)fail("PLAYERPOINT group-owner budget exceeded.");
            auto value=std::make_shared<PlayerPointGroupDefinition>();value->reference=ref;value->owner_reference=owner;
            value->name=read.name(object);const auto children=read.ref(object,7);
            if(children) {
                value->child_list=read.list(*children,"igNodeList");
                for(const auto child:doc.object_list(*children))value->points.push_back(child?point(*child):nullptr);
            }
            std::shared_ptr<const PlayerPointGroupDefinition> result=value;groups.emplace(owner,result);return result;
        };
        const auto exports=read.list(*doc.info_list_reference,"igInfoList");
        for(const auto ref:doc.object_list(exports.reference)) {
            if(!ref || !doc.is_a(doc.object(*ref),"igInfo"))fail("PLAYERPOINT export list contains a null or non-info owner.");
            if(doc.metadata.object_types.at(doc.object(*ref).type_index).name!="bePoint01Info")continue;
            const auto& object=read.object(*ref,"bePoint01Info");PlayerPointInfoDefinition out;
            out.reference=*ref;out.owner_reference=object.reference_index;out.name=read.name(object);out.active=read.byte(object,4);
            out.data_reference=read.ref(object,5);if(out.data_reference)read.object(*out.data_reference,"beBaseInfoDataList");
            out.ram_list=read.list(read.required(object,6),"beBaseInfoRamList");
            for(const auto ram_ref:doc.object_list(out.ram_list.reference)) {
                if(!ram_ref)fail("PLAYERPOINT RAM list contains a null owner.");
                const auto& ram=read.object(*ram_ref,"beBaseInfoRam");DatabaseRamDefinition value;
                value.reference=*ram_ref;value.owner_reference=ram.reference_index;value.authored_index=read.word(ram,2);value.active=read.byte(ram,5);
                for(std::uint16_t slot=6;slot<=9;++slot)value.authored_words[slot-6]=read.word(ram,slot);
                value.data_reference=read.ref(ram,10);if(value.data_reference)read.object(*value.data_reference,"beBaseInfoDataList");
                out.rams.push_back(value);
            }
            out.polygon_switch_reference=read.ref(object,7);
            if(out.polygon_switch_reference)out.polygon_switch_owner_reference=read.object(*out.polygon_switch_reference,"igSwitch").reference_index;
            const auto group_list=read.ref(object,8);
            if(group_list) {
                out.group_list=read.list(*group_list,"beGroupKeepList");
                for(const auto ref_group:doc.object_list(*group_list))out.groups.push_back(ref_group?group(*ref_group):nullptr);
            }
            data->infos.push_back(std::move(out));
        }
        return PlayerPointProgram(std::move(data));
    } catch(const PlayerPointError&) {throw;}
      catch(const std::exception& error) {throw PlayerPointError(error.what());}
}

struct PlayerPointRuntime::Impl {
    struct SwitchCall {
        std::uint64_t dispatch{};std::size_t row{};
        std::shared_ptr<const PlayerPointInfoState> info;
        ControllerObjectId target;
        bool show{};
    };
    struct Ram {PlayerPointRamState state;std::optional<SwitchCall> pending;};
    ControllerObjectId manager=ControllerObjectId::allocate_native();
    std::int32_t saved_group{};
    PlayerPointServices services;
    std::vector<PlayerPointProgram> sources;
    std::map<ControllerObjectId,std::shared_ptr<Ram>> rams;
    std::map<ControllerObjectId,ControllerObjectId> aliases,point_aliases;
    std::map<ControllerObjectId,std::shared_ptr<PlayerPointNodeState>> points;
    std::shared_ptr<Ram> find(ControllerObjectId id) const {
        const auto found=aliases.find(id);if(found==aliases.end())fail("PLAYERPOINT RAM does not belong to this manager.");
        return rams.at(found->second);
    }
    std::shared_ptr<PlayerPointNodeState> find_point(ControllerObjectId id) const {
        const auto found=point_aliases.find(id);if(found==point_aliases.end())fail("PLAYERPOINT transform does not belong to this manager.");
        return points.at(found->second);
    }
    MessengerCallResult command(Ram& ram,NativeMessenger& messenger,const MessengerDelivery& delivery) {
        if(delivery.recipient!=ram.state.id)fail("PLAYERPOINT delivery belongs to another RAM.");
        const auto parent=ram.state.parent;
        if(!parent)fail("PLAYERPOINT requires its actual live parent info.");
        const auto& info=parent->program->infos().at(parent->info_index);
        const auto command=delivery.message.command;
        const auto integer=[&](std::size_t index) {
            const auto& values=messenger.buffer().integers();
            if(index>=values.size())fail("PLAYERPOINT command is missing an integer argument.");return values[index];
        };
        if(ram.pending || command=="SHOWPOLYGON" || command=="HIDEPOLYGON") {
            if(!ram.pending) {
                if(!info.polygon_switch_owner_reference)fail("PLAYERPOINT polygon command has a null switch.");
                ram.pending=SwitchCall{delivery.dispatch_id,delivery.row_index,parent,
                    {parent->program->source_occurrence(),*info.polygon_switch_owner_reference},command=="SHOWPOLYGON"};
            }
            const auto call=*ram.pending;
            if(call.dispatch!=delivery.dispatch_id || call.row!=delivery.row_index)fail("PLAYERPOINT switch operation is already pending.");
            if(!services.polygon_switch || !services.polygon_switch(call.info->program->resource(),call.target,call.show))
                return {false,0,"PLAYERPOINT requires the original igSwitch mask mutation service."};
            ram.pending.reset();return {true,1,{}};
        }
        if(command=="SETGENERATEPOS") {saved_group=integer(0);return {true,1,{}};}
        if(command=="GETGENERATEPOS") {messenger.buffer().add_int(saved_group);return {true,1,{}};}
        if(command=="GETPOINTNAME") {
            if(!info.group_list)fail("PLAYERPOINT name query has a null group list.");
            for(const auto& group:parent->groups) {
                if(!group)fail("PLAYERPOINT name query dereferences a null group.");
                messenger.buffer().add_string(group->definition->name.value_or(""));
            }
            return {true,1,{}};
        }
        if(command!="GETMATRIX" && command!="GETMATRIXDIRECT")return {true,0,{}};
        auto group=command=="GETMATRIX"?saved_group:integer(0);const auto player=integer(1);
        if(!info.group_list)fail("PLAYERPOINT matrix query has a null group list.");
        // Original signed comparison clamps only excessive positive indices.
        if(group>static_cast<std::int64_t>(parent->groups.size())-1)group=0;
        if(group<0 || static_cast<std::size_t>(group)>=parent->groups.size())fail("PLAYERPOINT group access is outside its actual list.");
        const auto& selected=parent->groups[static_cast<std::size_t>(group)];
        if(!selected) {
            // Math initializer8012F630 constructs this actual library identity
            // at804F89B8, consumed by the explicit null-group branch80312A40.
            messenger.buffer().add_matrix({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1});return {true,1,{}};
        }
        if(!selected->definition->child_list || player<0 || static_cast<std::size_t>(player)>=selected->points.size())
            fail("PLAYERPOINT child access is outside its actual list.");
        const auto& point=selected->points[static_cast<std::size_t>(player)];
        if(!point)fail("PLAYERPOINT selected child is null.");
        messenger.buffer().add_matrix(point->matrix);return {true,1,{}};
    }
};
PlayerPointRuntime::PlayerPointRuntime(ActionEventRuntime& runtime):impl_(std::make_shared<Impl>()),runtime_(runtime) {}
PlayerPointRuntime::~PlayerPointRuntime()=default;
ControllerObjectId PlayerPointRuntime::manager_id() const noexcept {return impl_->manager;}
const std::vector<PlayerPointProgram>& PlayerPointRuntime::sources() const noexcept {return impl_->sources;}
const PlayerPointRamState& PlayerPointRuntime::ram(ControllerObjectId id) const {return impl_->find(id)->state;}
const PlayerPointNodeState& PlayerPointRuntime::point(ControllerObjectId id) const {return *impl_->find_point(id);}
std::int32_t PlayerPointRuntime::saved_group() const noexcept {return impl_->saved_group;}
void PlayerPointRuntime::set_active(ControllerObjectId id,std::uint8_t value) {impl_->find(id)->state.active=value;}
void PlayerPointRuntime::set_matrix(ControllerObjectId id,const AnimationMatrix& value) {
    finite(value);auto point=impl_->find_point(id);point->matrix=value;++point->revision;
}
void PlayerPointRuntime::set_services(PlayerPointServices services) {impl_->services=std::move(services);}
void PlayerPointRuntime::register_source(PlayerPointProgram program) {
    for(const auto& source:impl_->sources)if(source.source_occurrence()==program.source_occurrence())
        fail("PLAYERPOINT source occurrence is already registered.");
    if(impl_->sources.size()>=4096)fail("PLAYERPOINT loaded-source budget exceeded.");
    auto sources=impl_->sources;sources.push_back(program);auto shared=std::make_shared<const PlayerPointProgram>(program);
    std::map<ControllerObjectId,std::shared_ptr<Impl::Ram>> rams;
    std::map<ControllerObjectId,std::shared_ptr<PlayerPointInfoState>> infos;
    std::map<ControllerObjectId,std::shared_ptr<PlayerPointGroupState>> groups;
    std::map<ControllerObjectId,std::shared_ptr<PlayerPointNodeState>> points;
    std::map<ControllerObjectId,ControllerObjectId> aliases,point_aliases;
    std::vector<MessengerRegistration> rows;
    const auto qualify=[&](std::uint32_t reference){return ControllerObjectId{program.source_occurrence(),reference};};
    for(std::size_t index=0;index<program.infos().size();++index) {
        const auto& info=program.infos()[index];const auto info_id=qualify(info.owner_reference);auto parent=infos.find(info_id);
        if(parent==infos.end()) {
            auto value=std::make_shared<PlayerPointInfoState>();value->id=info_id;value->program=shared;value->info_index=index;
            for(const auto& group:info.groups) {
                if(!group){value->groups.push_back(nullptr);continue;}
                const auto group_id=qualify(group->owner_reference);auto owner=groups.find(group_id);
                if(owner==groups.end()) {
                    auto g=std::make_shared<PlayerPointGroupState>();g->id=group_id;g->definition=group;
                    for(const auto& point:group->points) {
                        if(!point){g->points.push_back(nullptr);continue;}
                        const auto point_id=qualify(point->node->reference());auto p=points.find(point_id);
                        if(p==points.end())p=points.emplace(point_id,std::make_shared<PlayerPointNodeState>(
                            PlayerPointNodeState{point_id,point,point->matrix,0})).first;
                        g->points.push_back(p->second);
                    }
                    owner=groups.emplace(group_id,std::move(g)).first;
                }
                value->groups.push_back(owner->second);
            }
            parent=infos.emplace(info_id,std::move(value)).first;
        }
        for(std::size_t ordinal=0;ordinal<info.rams.size();++ordinal) {
            const auto& authored=info.rams[ordinal];const auto id=qualify(authored.owner_reference);auto found=rams.find(id);
            if(found==rams.end()) {
                auto ram=std::make_shared<Impl::Ram>();ram->state.id=id;ram->state.manager=impl_->manager;ram->state.active=authored.active;
                found=rams.emplace(id,std::move(ram)).first;
            }
            found->second->state.info=info_id;found->second->state.parent=parent->second;
            found->second->state.index=static_cast<std::int32_t>(ordinal);found->second->state.words={};
            aliases.emplace(qualify(authored.reference),id);aliases.emplace(id,id);
            rows.push_back({id,"PLAYERPOINT",info.name.value_or(""),static_cast<std::int32_t>(ordinal)});
        }
    }
    // Accept every raw object-reference alias of a loaded physical point.
    for(std::uint32_t ref=0;ref<program.resource()->document().references.size();++ref) {
        const auto& source=program.resource()->document();
        if(source.references[ref].kind!=IgbReferenceKind::object)continue;
        const auto id=qualify(source.object(ref).reference_index);
        if(points.contains(id))point_aliases.emplace(qualify(ref),id);
    }
    std::vector<PreparedControllerRegistration> providers;
    for(const auto& [id,ram]:rams)providers.push_back({id,{
        [ram]()->std::optional<std::uint8_t>{return ram->state.active;},
        [impl=impl_,ram]{return MessengerManager{
            [impl,ram](NativeMessenger& messenger,const MessengerDelivery& delivery){return impl->command(*ram,messenger,delivery);},
            [](NativeMessenger&,const MessengerDelivery&,std::string_view){return MessengerCallResult{true,0,{}};}};}
    },program.source()});
    runtime_.register_controllers(providers,rows);
    impl_->rams.merge(rams);impl_->aliases.merge(aliases);impl_->points.merge(points);impl_->point_aliases.merge(point_aliases);impl_->sources.swap(sources);
}
} // namespace dw4
