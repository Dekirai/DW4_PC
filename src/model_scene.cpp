#include "dw4/model_scene.hpp"

#include <algorithm>
#include <bit>
#include <map>
#include <set>

namespace dw4 {
namespace {
constexpr std::size_t max_owners=65536,max_entries=262144,max_text=16u*1024u*1024u,max_depth=128;
[[noreturn]] void fail(const char* reason) { throw ModelSceneError(reason); }
std::uint32_t little32(std::span<const std::uint8_t> bytes,std::size_t offset=0) {
    if(offset>bytes.size() || bytes.size()-offset<4)fail("Model scene scalar is truncated.");
    return std::uint32_t(bytes[offset]) | std::uint32_t(bytes[offset+1])<<8 |
        std::uint32_t(bytes[offset+2])<<16 | std::uint32_t(bytes[offset+3])<<24;
}
std::uint64_t little64(std::span<const std::uint8_t> bytes) {
    return std::uint64_t(little32(bytes)) | std::uint64_t(little32(bytes,4))<<32;
}
void require_name(std::string_view name) {
    if(name.size()>4096 || name.find('\0')!=std::string_view::npos)
        fail("Model scene name exceeds its bounded byte profile.");
}
struct Reader {
    const IgbDocument& doc;
    const IgbObject& object(std::uint32_t ref) const {
        const auto& object=doc.object(ref);
        std::set<std::uint16_t> slots;
        for(const auto& field:object.fields)
            if(!slots.insert(field.slot).second)fail("Model scene object repeats a field slot.");
        return object;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,std::string_view type,
        std::optional<std::size_t> size=4) const {
        const auto* field=doc.field(object,slot);
        if(!field || doc.metadata.field_types.at(field->type_index).name!=type ||
            (size && field->size!=*size))fail("Model scene field type or width differs from its verified schema.");
        doc.field_bytes(*field);return *field;
    }
    std::optional<std::uint32_t> ref(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igObjectRefMetaField");
        const auto ref=doc.field_reference(object,slot);
        if(ref)doc.object(*ref);return ref;
    }
    std::optional<std::string> text(const IgbObject& object,std::uint16_t slot) const {
        const auto bytes=doc.field_bytes(field(object,slot,"igStringMetaField",std::nullopt));
        if(little32(bytes)==0)return std::nullopt;
        const auto text=doc.field_string(object,slot);require_name(text);return std::string(text);
    }
    std::int32_t integer(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igIntMetaField");return doc.field_i32(object,slot);
    }
    std::uint8_t boolean(const IgbObject& object,std::uint16_t slot) const {
        return doc.field_bytes(field(object,slot,"igBoolMetaField",1))[0];
    }
    float number(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igFloatMetaField");return doc.field_f32(object,slot);
    }
    std::array<float,3> vector(const IgbObject& object,std::uint16_t slot) const {
        const auto bytes=doc.field_bytes(field(object,slot,"igVec3fMetaField",12));
        return {std::bit_cast<float>(little32(bytes)),std::bit_cast<float>(little32(bytes,4)),
            std::bit_cast<float>(little32(bytes,8))};
    }
};
struct GenericObject final : ModelSceneObject { explicit GenericObject(std::string type):ModelSceneObject(std::move(type)) {} };
template<class T> std::shared_ptr<T> required_type(const std::shared_ptr<ModelSceneObject>& value,const char* reason) {
    if(!value)return {};
    auto result=std::dynamic_pointer_cast<T>(value);
    if(!result)fail(reason);return result;
}
}

std::span<const std::shared_ptr<ModelSceneObject>> ModelSceneList::values() const {
    if(count<0 || capacity<0 || count>capacity || std::size_t(capacity)>max_entries ||
        (capacity && !backing) || (backing && std::size_t(capacity)>backing->size()))
        fail("Model scene list has an unsupported count, capacity or backing range.");
    if(!count)return {};
    return {backing->data(),static_cast<std::size_t>(count)};
}
void ModelSceneList::append(std::shared_ptr<ModelSceneObject> value) {
    values();
    if(std::size_t(count)>=max_entries)fail("Model scene list exceeds its bounded entry count.");
    if(count==capacity) {
        // 80069128 -> 80041970 -> 80041660/8004155C. Growth replaces this
        // list's backing allocation and copies its old capacity, leaving any
        // other list that retained the old memory on that old allocation.
        auto grown=std::max<std::int32_t>(capacity,4);
        while(grown<count+1)grown=grown<1024 ? grown*2:grown+1024;
        if(std::size_t(grown)>max_entries)fail("Model scene list growth exceeds its backing-entry budget.");
        auto replacement=std::make_shared<Storage>(static_cast<std::size_t>(grown));
        if(backing)std::copy_n(backing->begin(),capacity,replacement->begin());
        backing=std::move(replacement);capacity=grown;
    }
    (*backing)[static_cast<std::size_t>(count)]=std::move(value);++count;
}

struct ModelSceneObjects::Impl {
    ActorSceneResource& resource;
    std::shared_ptr<const IgbDocument> document;
    std::map<std::uint32_t,std::shared_ptr<ModelSceneObject>> objects;
    std::map<std::uint32_t,std::shared_ptr<ModelSceneList::Storage>> memories;
    std::map<std::uint32_t,std::size_t> heights;
    std::set<std::uint32_t> visiting,visiting_memory;
    std::size_t entries{},text{};
    explicit Impl(ActorSceneResource& owner):resource(owner),document(owner.document_owner()) {}

    void require(std::size_t owners,std::size_t extra_entries=0,std::size_t extra_text=0) const {
        if(owners>max_owners-objects.size()-memories.size())fail("Model scene exceeds its aggregate owner budget.");
        if(extra_entries>max_entries-entries)fail("Model scene exceeds its aggregate backing-entry budget.");
        if(extra_text>max_text-text)fail("Model scene exceeds its aggregate copied-text budget.");
    }
    std::shared_ptr<ModelSceneList::Storage> memory(std::uint32_t ref,std::size_t depth,std::size_t& height) {
        if(depth>max_depth)fail("Model scene backing exceeds its path-depth budget.");
        if(ref>=document->references.size() || document->references[ref].kind!=IgbReferenceKind::memory)
            fail("Model scene list references a non-memory object.");
        const auto& record=document->memories.at(document->references[ref].value_index);
        const auto canonical=record.reference_index;
        if(canonical>=document->references.size() || document->references[canonical].kind!=IgbReferenceKind::memory ||
            document->references[canonical].value_index!=document->references[ref].value_index)
            fail("Model scene canonical memory reference does not identify its physical allocation.");
        if(visiting_memory.contains(canonical))fail("Model scene backing graph contains a cycle.");
        if(const auto found=memories.find(canonical);found!=memories.end()) {
            height=heights.at(canonical);
            if(height>max_depth-depth)fail("Cached model scene backing exceeds its path-depth budget.");
            return found->second;
        }
        const auto bytes=document->memory(ref);
        if(bytes.size()%4)fail("Model scene object backing has a partial reference.");
        const auto count=bytes.size()/4;require(1,count);
        auto result=std::make_shared<ModelSceneList::Storage>();result->reserve(count);
        visiting_memory.insert(canonical);
        for(std::size_t i=0;i<count;++i) {
            const auto item=little32(bytes,i*4);
            if(item==0xffffffffu)result->push_back({});
            else {
                std::size_t child_height{};result->push_back(load(item,depth+1,child_height));
                height=std::max(height,child_height+1);
            }
        }
        visiting_memory.erase(canonical);require(1,count);entries+=count;
        memories.emplace(canonical,result);heights.emplace(canonical,height);return result;
    }
    std::shared_ptr<ModelSceneObject> load(std::uint32_t ref,std::size_t depth,std::size_t& height) {
        if(depth>max_depth)fail("Model scene graph exceeds its path-depth budget.");
        ref=resource.canonical_reference(ref);
        if(visiting.contains(ref))fail("Model scene object graph contains a cycle.");
        if(const auto found=objects.find(ref);found!=objects.end()) {
            height=heights.at(ref);
            if(height>max_depth-depth)fail("Cached model scene graph exceeds its path-depth budget.");
            return found->second;
        }
        require(1);visiting.insert(ref);
        Reader read{*document};const auto& authored=read.object(ref);
        const auto& type=document->metadata.object_types.at(authored.type_index).name;
        std::shared_ptr<ModelSceneObject> result;
        if(type=="beModelCtrlInfo")result=std::make_shared<ModelSceneInfo>();
        else if(type=="beBaseInfoRam")result=std::make_shared<ModelSceneRam>();
        else if(type=="beHitLandDelivInfo")result=std::make_shared<ModelHitLandInfo>();
        else if(type=="beHitLandDelivInfoRam")result=std::make_shared<ModelHitLandRam>();
        else if(type=="beHitLandModelInfo")result=std::make_shared<ModelHitLandGeometryInfo>();
        else if(type=="beModelCtrlInfoRamData")result=std::make_shared<ModelSceneOne>();
        else if(type=="beBaseInfoRamTimer")result=std::make_shared<ModelSceneTimer>();
        else if(type=="beModelCtrlInfoDataSeq")result=std::make_shared<ModelSceneSeq>();
        else if(type=="beModelCtrlData")result=std::make_shared<ModelSceneSequence>();
        else if(type=="beModelCtrlInfoDataHit")result=std::make_shared<ModelSceneHit>();
        else if(type=="igAABox")result=std::make_shared<ModelSceneBox>();
        else if(document->is_a(authored,"igObjectList"))result=std::make_shared<ModelSceneList>(type);
        else if(document->is_a(authored,"beBaseInfoData"))result=std::make_shared<ModelSceneData>(type);
        else if(type.starts_with("beModelCtrl") && document->field(authored,2) &&
            document->metadata.field_types.at(document->field(authored,2)->type_index).name=="igStringMetaField")
            result=std::make_shared<ModelSceneCommand>(type);
        else result=std::make_shared<GenericObject>(type);
        result->document_=document;result->source_=resource.source();result->occurrence_=resource.source_occurrence();
        result->reference_=ref;
        const auto* name_field=document->field(authored,2);
        if(name_field && document->metadata.field_types.at(name_field->type_index).name=="igStringMetaField")
            result->name=read.text(authored,2);
        const auto child=[&](std::uint16_t slot) {
            const auto reference=read.ref(authored,slot);
            if(!reference)return std::shared_ptr<ModelSceneObject>{};
            std::size_t child_height{};auto owner=load(*reference,depth+1,child_height);
            height=std::max(height,child_height+1);return owner;
        };
        const auto child_list=[&](std::uint16_t slot,std::string_view expected) {
            auto list=required_type<ModelSceneList>(child(slot),"Model scene reference is not an actual list.");
            if(list && list->type()!=expected)fail("Model scene list has an unsupported concrete type.");
            return list;
        };
        if(auto list=std::dynamic_pointer_cast<ModelSceneList>(result)) {
            list->count=read.integer(authored,2);list->capacity=read.integer(authored,3);
            read.field(authored,4,"igMemoryRefMetaField");
            if(const auto backing=document->field_reference(authored,4)) {
                std::size_t child_height{};list->backing=memory(*backing,depth+1,child_height);
                height=std::max(height,child_height+1);
            }
            // Named lookup and the sequence runner ignore capacity and check
            // only reached backing slots. Retain even a malformed named-list
            // shape until its actual consumer, without allocating by count.
            if(type!="igNamedObjectList" && type!="igStringObjList")list->values();
        } else if(auto sequence=std::dynamic_pointer_cast<ModelSceneSequence>(result)) {
            sequence->commands=child_list(3,"igNamedObjectList");
        } else if(auto command=std::dynamic_pointer_cast<ModelSceneCommand>(result)) {
            if(type=="beModelCtrlMOVE" || type=="beModelCtrlMOVEEX" || type=="beModelCtrlPDMOVE") {
                command->value=read.number(authored,3);command->frames=read.number(authored,4);
                if(type=="beModelCtrlMOVEEX") {
                    command->end_time=read.number(authored,5);command->stop_on_sequence=read.boolean(authored,6);
                    command->stop_value=read.number(authored,7);command->stop_frames=read.number(authored,8);
                }
            } else if(type=="beModelCtrlMUTEKI")command->frames=read.number(authored,3);
            else if(type=="beModelCtrlLABELCTRL" || type=="beModelCtrlLUACALL") {
                command->scope=read.text(authored,3);
                if(command->scope) {require(0,0,command->scope->size());text+=command->scope->size();}
            }
            else if(type=="beModelCtrlHITBOX")command->hitbox={read.integer(authored,3),read.number(authored,4),
                read.number(authored,5),read.vector(authored,6),read.vector(authored,7)};
        } else if(auto info=std::dynamic_pointer_cast<ModelSceneInfo>(result)) {
            info->name=read.text(authored,2);info->active=read.boolean(authored,4);
            info->data=child_list(5,"beBaseInfoDataList");info->rams=child_list(6,"beBaseInfoRamList");
            info->kind=read.text(authored,7);
        } else if(auto land_info=std::dynamic_pointer_cast<ModelHitLandInfo>(result)) {
            land_info->name=read.text(authored,2);land_info->active=read.boolean(authored,4);
            land_info->data=child_list(5,"beBaseInfoDataList");land_info->rams=child_list(6,"beBaseInfoRamList");
        } else if(auto geometry=std::dynamic_pointer_cast<ModelHitLandGeometryInfo>(result)) {
            geometry->active=read.boolean(authored,4);
            geometry->data=child_list(5,"beBaseInfoDataList");geometry->rams=child_list(6,"beBaseInfoRamList");
            if(const auto graph=read.ref(authored,7))geometry->switch_graph=resource.mutable_node(*graph);
            if(const auto check=read.ref(authored,8))geometry->check_switch=resource.mutable_node(*check);
            // Original static categories use igNamedObjectList; the dynamic
            // g_box category authors an empty igStringObjList in this field.
            geometry->names=required_type<ModelSceneList>(child(9),"HITLANDMODEL names reference is not an actual list.");
            geometry->kind=read.integer(authored,10);
        } else if(auto land=std::dynamic_pointer_cast<ModelHitLandRam>(result)) {
            land->number=read.integer(authored,2);
            if((document->field(authored,3) && read.ref(authored,3)) ||
                (document->field(authored,4) && read.ref(authored,4)))
                fail("Serialized non-null HITLANDDELIVER backlinks require original registration services.");
            land->active=read.boolean(authored,5);
            for(std::size_t i=0;i<land->words.size();++i)
                land->words[i]=std::bit_cast<std::uint32_t>(read.integer(authored,static_cast<std::uint16_t>(6+i)));
            land->data=child_list(10,"beBaseInfoDataList");
            if(const auto node=read.ref(authored,11))land->node=resource.mutable_node(*node);
            land->check_flags=read.integer(authored,12);
            land->half_width=read.number(authored,13);land->up_range=read.number(authored,14);land->down_range=read.number(authored,15);
            land->position=read.vector(authored,16);land->previous_position=read.vector(authored,17);
            land->land_position=read.vector(authored,18);land->land_position_ok=read.boolean(authored,19);
        } else if(auto ram=std::dynamic_pointer_cast<ModelSceneRam>(result)) {
            ram->number=read.integer(authored,2);
            // Parent/manager are runtime-only in the supplied serialization.
            // If another input explicitly carries them, null is representable;
            // a non-null pointer is not an already performed registration.
            if((document->field(authored,3) && read.ref(authored,3)) ||
                (document->field(authored,4) && read.ref(authored,4)))
                fail("Serialized non-null RAM registration backlinks require original registration services.");
            ram->active=read.boolean(authored,5);
            for(std::size_t i=0;i<ram->words.size();++i)
                ram->words[i]=std::bit_cast<std::uint32_t>(read.integer(authored,static_cast<std::uint16_t>(6+i)));
            ram->data=child_list(10,"beBaseInfoDataList");
        } else if(auto data=std::dynamic_pointer_cast<ModelSceneData>(result)) {
            data->name=read.text(authored,2);data->value=read.integer(authored,3);
            if(auto one=std::dynamic_pointer_cast<ModelSceneOne>(data)) {
                if(const auto node=read.ref(authored,4))one->node=resource.mutable_node(*node);
                if(read.ref(authored,5))fail("Serialized non-null model work needs a verified work deserializer.");
            } else if(auto seq=std::dynamic_pointer_cast<ModelSceneSeq>(data)) {
                seq->sequences=child_list(4,"igNamedObjectList");
            } else if(auto timer=std::dynamic_pointer_cast<ModelSceneTimer>(data)) {
                read.field(authored,4,"igFloatMetaField");timer->timer.time_scale=document->field_f32(authored,4);
                const auto long_value=[&](std::uint16_t slot) {
                    return little64(document->field_bytes(read.field(authored,slot,"igLongMetaField",8)));
                };
                timer->timer.time_bias=std::bit_cast<std::int64_t>(long_value(5));
                timer->timer.end_time=std::bit_cast<std::int64_t>(long_value(6));
                timer->timer.saved_world_time=long_value(7);
                timer->timer.previous_time=std::bit_cast<std::int64_t>(long_value(8));
                read.field(authored,9,"igFloatMetaField");timer->timer.cached_frames=document->field_f32(authored,9);
            } else if(auto hit=std::dynamic_pointer_cast<ModelSceneHit>(data)) {
                hit->hit_player_bullet=read.boolean(authored,4);
                hit->hit_enemy_bullet=read.boolean(authored,5);
                hit->boxes=child_list(6,"igAABoxList");
                hit->hit_player_talk=read.boolean(authored,7);
            }
        } else if(auto box=std::dynamic_pointer_cast<ModelSceneBox>(result)) {
            box->box=resource.box(ref);
        }
        const auto bytes=result->source_.size()+result->type().size()+(result->name ? result->name->size():0)+
            (std::dynamic_pointer_cast<ModelSceneInfo>(result) && std::static_pointer_cast<ModelSceneInfo>(result)->kind ?
                std::static_pointer_cast<ModelSceneInfo>(result)->kind->size():0);
        require(1,0,bytes);text+=bytes;visiting.erase(ref);objects.emplace(ref,result);heights.emplace(ref,height);return result;
    }
};
ModelSceneObjects::ModelSceneObjects(ActorSceneResource& owner):impl_(std::make_unique<Impl>(owner)) {}
ModelSceneObjects::~ModelSceneObjects()=default;
std::shared_ptr<ModelSceneObject> ModelSceneObjects::object(std::uint32_t ref) {
    // Failed loads publish no partial graph owners. Previously exposed owners
    // and backing allocations retain their exact identities.
    const auto old_objects=impl_->objects;
    const auto old_memories=impl_->memories;
    const auto old_heights=impl_->heights;const auto old_entries=impl_->entries,old_text=impl_->text;
    try {std::size_t height{};return impl_->load(ref,0,height);}
    catch(const std::exception& error) {
        impl_->objects=old_objects;impl_->memories=old_memories;impl_->heights=old_heights;
        impl_->entries=old_entries;impl_->text=old_text;impl_->visiting.clear();impl_->visiting_memory.clear();
        throw ModelSceneError(error.what());
    }
}
std::shared_ptr<ModelSceneInfo> ModelSceneObjects::info(std::uint32_t ref) {
    return required_type<ModelSceneInfo>(object(ref),"Requested model info has a different concrete type.");
}
std::shared_ptr<ModelSceneRam> ModelSceneObjects::ram(std::uint32_t ref) {
    return required_type<ModelSceneRam>(object(ref),"Requested model RAM has a different concrete type.");
}
std::shared_ptr<ModelHitLandInfo> ModelSceneObjects::hit_land_info(std::uint32_t ref) {
    return required_type<ModelHitLandInfo>(object(ref),"Requested HITLANDDELIVER info has a different concrete type.");
}
std::shared_ptr<ModelHitLandRam> ModelSceneObjects::hit_land_ram(std::uint32_t ref) {
    return required_type<ModelHitLandRam>(object(ref),"Requested HITLANDDELIVER RAM has a different concrete type.");
}
std::shared_ptr<ModelHitLandGeometryInfo> ModelSceneObjects::hit_land_geometry_info(std::uint32_t ref) {
    return required_type<ModelHitLandGeometryInfo>(object(ref),"Requested HITLANDMODEL geometry info has a different concrete type.");
}
std::shared_ptr<ModelSceneData> ModelSceneObjects::data(std::uint32_t ref) {
    return required_type<ModelSceneData>(object(ref),"Requested model data is not a base-data owner.");
}
std::shared_ptr<ModelSceneList> ModelSceneObjects::list(std::uint32_t ref) {
    return required_type<ModelSceneList>(object(ref),"Requested model list is not an object-list owner.");
}
std::shared_ptr<ModelSceneObject> find_model_named(const std::shared_ptr<ModelSceneList>& list,std::string_view name) {
    return find_model_named_nullable(list,name);
}
std::shared_ptr<ModelSceneObject> find_model_named_nullable(const std::shared_ptr<ModelSceneList>& list,
    std::optional<std::string_view> name) {
    if(name)require_name(*name);
    if(!list)fail("Original named-data lookup dereferences a null list.");
    // 800695CC reads count once, compares signed indices, ignores capacity,
    // and dereferences each reached entry before checking its nullable name.
    const auto count=list->count;
    if(count<=0)return {};
    for(std::int32_t i=0;i<count;++i) {
        if(std::size_t(i)>=max_entries || !list->backing || std::size_t(i)>=list->backing->size())
            fail("Named-data lookup exceeds its actual addressable backing.");
        const auto& entry=(*list->backing)[static_cast<std::size_t>(i)];
        if(!entry)fail("Original named-data lookup dereferences a null list entry.");
        if(entry->name) {
            if(!name)fail("Original named lookup reaches strcmp with a null requested name.");
            if(*entry->name==*name)return entry;
        }
    }
    return {};
}
std::shared_ptr<ModelSceneData> find_model_data(const std::shared_ptr<ModelSceneList>& list,std::string_view name) {
    return required_type<ModelSceneData>(find_model_named(list,name),"First named model record is not base data.");
}
std::shared_ptr<ModelSceneList> ensure_model_data(ModelSceneRam& ram) {
    if(!ram.data)ram.data=std::make_shared<ModelSceneList>();
    if(ram.data->type()!="beBaseInfoDataList")fail("RAM data has a different concrete list type.");
    return ram.data;
}
std::shared_ptr<ModelSceneOne> find_model_one(ModelSceneRam& ram) {
    return required_type<ModelSceneOne>(find_model_named(ensure_model_data(ram),"ONE"),"First named ONE has a different concrete type.");
}
std::shared_ptr<ModelSceneSeq> find_model_seq(const ModelSceneInfo& info) {
    return required_type<ModelSceneSeq>(find_model_named(info.data,"SEQ"),"First named SEQ has a different concrete type.");
}
std::shared_ptr<ModelSceneSequence> find_model_sequence(const ModelSceneSeq& seq,std::string_view name) {
    return required_type<ModelSceneSequence>(find_model_named(seq.sequences,name),"First named sequence has a different concrete type.");
}
std::shared_ptr<ModelSceneSequence> find_model_sequence_nullable(const ModelSceneSeq& seq,std::optional<std::string_view> name) {
    return required_type<ModelSceneSequence>(find_model_named_nullable(seq.sequences,name),"First named sequence has a different concrete type.");
}
ModelSequenceCommand model_scene_command(const ModelSceneCommand& owner) {
    ModelSequenceCommand result;result.reference_index=owner.reference().value_or(0xffffffffu);
    result.type=owner.type();result.name=owner.name.value_or("");require_name(result.name);
    const auto& type=result.type;const auto& name=result.name;const auto ref=result.reference_index;
    if(type=="beModelCtrlMOVE" || type=="beModelCtrlMOVEEX") {
        MotionCommand motion;motion.reference_index=ref;motion.type=type;motion.name=name;
        motion.setup.value=owner.value;motion.setup.frames=owner.frames;
        if(type=="beModelCtrlMOVE") {
            if(name=="Move") {motion.channel=2;motion.setup.mode=MotionMode::accelerate_positive;}
            else if(name=="Stop") {motion.channel=2;motion.setup.mode=MotionMode::stop_positive;}
            else if(name=="Push") {motion.channel=2;motion.setup.mode=MotionMode::impulse_positive;}
            else if(name=="Back") {motion.channel=2;motion.setup.mode=MotionMode::impulse_negative;}
            else if(name=="Jump") {motion.channel=1;motion.setup.mode=MotionMode::impulse_positive;}
            else if(name=="Fall") {motion.channel=1;motion.setup.mode=MotionMode::accelerate_negative;}
        } else {
            if(name.size()<4)fail("MOVEEX control code needs four bytes.");
            const int channel=static_cast<unsigned char>(name[1])-'X'+(name[0]=='T' ? 0 : 2);
            if(channel<0 || channel>3)fail("MOVEEX control code selects an invalid channel.");
            int mode;
            if(name[3]=='A')mode=0;else if(name[3]=='D')mode=2;else if(name[3]=='T')mode=4;
            else fail("MOVEEX control code has an unverified mode.");
            if(name[2]=='M')++mode;
            motion.channel=static_cast<std::uint32_t>(channel);motion.setup.mode=static_cast<MotionMode>(mode);
            motion.setup.end_time=owner.end_time;
            motion.stop=MotionStop{owner.stop_on_sequence!=0,owner.stop_value,owner.stop_frames};
        }
        result.opcode=ModelSequenceOpcode::model;result.model_command=std::move(motion);
    } else if(type=="beModelCtrlCTRL") {
        result.opcode=ModelSequenceOpcode::model;result.model_command=ModelCtrlCommand{ref,name};
    } else if(type=="beModelCtrlPDMOVE") {
        result.opcode=ModelSequenceOpcode::model;result.model_command=PadMotionCommand{ref,name,owner.value,owner.frames};
    } else if(type=="beModelCtrlMUTEKI") {
        result.opcode=ModelSequenceOpcode::model;result.model_command=ModelMutekiCommand{ref,name,owner.frames};
    } else if(type=="beModelCtrlLABEL")result.opcode=ModelSequenceOpcode::label;
    else if(type=="beModelCtrlLABELCTRL") {result.opcode=ModelSequenceOpcode::label_control;result.label_sequence=owner.scope;}
    else if(type=="beModelCtrlACTION")result.opcode=ModelSequenceOpcode::action;
    else if(type=="beModelCtrlJUMP")result.opcode=ModelSequenceOpcode::jump;
    else if(type=="beModelCtrlWCHECK")result.opcode=ModelSequenceOpcode::wait_check;
    else if(type=="beModelCtrlHITBOX") {
        result.opcode=ModelSequenceOpcode::hitbox;result.hitbox_command=ModelHitboxCommand{ref,name,owner.hitbox};
    } else if(type=="beModelCtrlLUACALL") {result.opcode=ModelSequenceOpcode::lua_call;result.lua_mode=owner.scope;}
    return result;
}
std::shared_ptr<ModelSceneTimer> find_model_timer(ModelSceneRam& ram,std::string_view name) {
    return required_type<ModelSceneTimer>(find_model_named(ensure_model_data(ram),name),"First named timer has a different concrete type.");
}
std::shared_ptr<ModelSceneTimer> start_model_timer(ModelSceneRam& ram,std::string_view name,
    std::uint64_t world_time,std::int64_t end_time) {
    require_name(name);auto timer=find_model_timer(ram,name);
    if(!timer) {
        timer=std::make_shared<ModelSceneTimer>();timer->name=std::string(name);
        ensure_model_data(ram)->append(timer);
    }
    start_motion_timer(timer->timer,world_time,end_time);return timer;
}
float model_timer_frames(ModelSceneRam& ram,std::string_view name,std::uint64_t world_time) {
    const auto timer=find_model_timer(ram,name);
    if(!timer)fail("Elapsed-frame query requires the actual existing named timer.");
    return motion_timer_frames(timer->timer,world_time);
}
void register_model_ram(const std::shared_ptr<ModelSceneInfo>& info,const std::shared_ptr<ModelSceneRam>& ram,
    std::int32_t number,const std::shared_ptr<const ModelSceneManager>& manager) {
    if(!info || !ram || !manager)fail("Model RAM registration requires actual info, RAM and manager owners.");
    ram->number=number;ram->parent=info;ram->manager=manager;
}
} // namespace dw4
