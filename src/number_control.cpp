#include "dw4/number_control.hpp"
#include "dw4/action_event_runtime.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <map>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw NumberControlError(reason);}
void checked_text(std::string_view text) {
    if(text.size()>4096 || text.find('\0')!=std::string_view::npos)
        fail("NUMBERCTRL source or name exceeds its checked byte profile.");
}
std::int32_t add(std::int32_t a,std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a)+std::bit_cast<std::uint32_t>(b));
}
std::int32_t subtract(std::int32_t a,std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a)-std::bit_cast<std::uint32_t>(b));
}
std::int32_t clamp(std::int32_t value,std::int32_t maximum) {
    if(value<0)value=0;
    if(value>maximum)value=maximum;
    return value;
}
std::int32_t change_step(std::int32_t delta) {
    if(!delta)return 0;
    auto step=delta/128+(delta>0?1:-1);
    if(step>=100)step=99;
    if(step<=-100)step=-99;
    if(step%10==0)step+=delta>0?1:-1;
    return step;
}
struct Reader {
    const IgbDocument& doc;
    std::size_t links{},text_bytes{},uv_bytes{};
    std::map<std::uint32_t,std::shared_ptr<const NumberUvDefinition>> vertices;
    const IgbObject& object(std::uint32_t ref,std::string_view type) const {
        const auto& value=doc.object(ref);
        if(doc.metadata.object_types.at(value.type_index).name!=type)
            fail("Unexpected NUMBERCTRL object type.");
        return value;
    }
    const IgbFieldValue& field(const IgbObject& value,std::uint16_t slot,std::string_view type,
        std::optional<std::size_t> width=4) const {
        const IgbFieldValue* found{};
        for(const auto& candidate:value.fields)if(candidate.slot==slot) {
            if(found)fail("NUMBERCTRL object repeats a field slot.");found=&candidate;
        }
        if(!found || doc.metadata.field_types.at(found->type_index).name!=type || (width && found->size!=*width))
            fail("Unexpected NUMBERCTRL field type or width.");
        doc.field_bytes(*found);return *found;
    }
    std::optional<std::uint32_t> ref(const IgbObject& value,std::uint16_t slot) const {
        field(value,slot,"igObjectRefMetaField");const auto result=doc.field_reference(value,slot);
        if(result)doc.object(*result);return result;
    }
    std::uint32_t required(const IgbObject& value,std::uint16_t slot) const {
        const auto result=ref(value,slot);if(!result)fail("NUMBERCTRL required object reference is null.");return *result;
    }
    std::int32_t integer(const IgbObject& value,std::uint16_t slot) const {
        field(value,slot,"igIntMetaField");return doc.field_i32(value,slot);
    }
    std::uint8_t byte(const IgbObject& value,std::uint16_t slot) const {
        return doc.field_bytes(field(value,slot,"igBoolMetaField",1))[0];
    }
    std::optional<std::string> name(const IgbObject& value) {
        const auto& part=field(value,2,"igStringMetaField",{});
        if(part.size>4104 || part.size>8u*1024u*1024u-text_bytes)
            fail("NUMBERCTRL aggregate name budget exceeded.");
        text_bytes+=part.size;const auto result=doc.field_string(value,2);checked_text(result);
        return part.size==4?std::optional<std::string>{}:std::string(result);
    }
    std::pair<NumberListIdentity,std::vector<std::uint32_t>> list(std::uint32_t reference,
        std::string_view type,std::uint32_t limit) {
        const auto& value=object(reference,type);
        field(value,2,"igIntMetaField");field(value,3,"igIntMetaField");field(value,4,"igMemoryRefMetaField");
        const auto count=doc.field_u32(value,2),capacity=doc.field_u32(value,3);
        if(count>capacity || capacity>limit || count>262144u-links)
            fail("NUMBERCTRL object list exceeds its checked capacity.");
        links+=count;const auto memory=doc.field_reference(value,4);
        if((!memory && capacity) || (memory && doc.memory(*memory).size()!=std::size_t(capacity)*4))
            fail("NUMBERCTRL object list storage differs from declared capacity.");
        std::optional<std::uint32_t> memory_owner;
        if(memory)memory_owner=doc.memories.at(doc.references.at(*memory).value_index).reference_index;
        NumberListIdentity identity{reference,value.reference_index,memory,memory_owner,count,capacity};
        std::vector<std::uint32_t> entries;entries.reserve(count);
        for(const auto item:doc.object_list(reference)) {
            if(!item)fail("NUMBERCTRL object list contains a null element.");entries.push_back(*item);
        }
        return {identity,std::move(entries)};
    }
    std::shared_ptr<const NumberUvDefinition> uv(std::uint32_t geometry,std::uint32_t& attribute) {
        const auto& node=object(geometry,"igGeometry");
        const auto attributes=list(required(node,8),"igAttrList",4096).second;
        if(attributes.empty())fail("NUMBERCTRL geometry has no first attribute.");
        attribute=attributes.front();const auto& attr=object(attribute,"igGeometryAttr1_5");
        const auto vertex_ref=required(attr,4);const auto& vertex=object(vertex_ref,"igVertexArray1_1");
        field(vertex,2,"igMemoryRefMetaField");field(vertex,3,"igUnsignedIntMetaField");
        field(vertex,4,"igUnsignedIntMetaField");
        const auto found=vertices.find(vertex.reference_index);
        if(found!=vertices.end())return found->second;
        const auto table_ref=doc.field_reference(vertex,2);
        if(!table_ref)fail("NUMBERCTRL vertex stream table is null.");
        const auto table=doc.memory(*table_ref);
        if(table.size()!=80)fail("NUMBERCTRL vertex stream table does not have twenty entries.");
        const auto memory=std::uint32_t(table[44])|(std::uint32_t(table[45])<<8)|
            (std::uint32_t(table[46])<<16)|(std::uint32_t(table[47])<<24);
        const auto bytes=doc.memory(memory);
        if(bytes.size()>64u*1024u*1024u-uv_bytes)fail("NUMBERCTRL aggregate UV budget exceeded.");
        uv_bytes+=bytes.size();
        auto decoded=std::make_shared<NumberUvDefinition>();
        decoded->vertex_array_reference=vertex_ref;decoded->vertex_array_owner_reference=vertex.reference_index;
        decoded->memory_reference=memory;
        decoded->memory_owner_reference=doc.memories.at(doc.references.at(memory).value_index).reference_index;
        // Existing verified float2 decoder additionally checks geometry topology,
        // all populated stream references and exact vertex_count * 8 storage.
        decoded->texcoords=decode_igb_texcoords(doc,attribute);
        vertices.emplace(vertex.reference_index,decoded);return decoded;
    }
};
}
struct NumberControlProgram::Data {
    std::string source;
    std::uint32_t occurrence{};
    std::vector<NumberInfoDefinition> infos;
};
const std::string& NumberControlProgram::source() const noexcept {return data_->source;}
std::uint32_t NumberControlProgram::source_occurrence() const noexcept {return data_->occurrence;}
const std::vector<NumberInfoDefinition>& NumberControlProgram::infos() const noexcept {return data_->infos;}

NumberControlProgram decode_number_controls(const IgbDocument& doc,std::string source,std::uint32_t occurrence) {
    try {
        checked_text(source);
        if(source.empty() || occurrence==std::numeric_limits<std::uint32_t>::max())
            fail("NUMBERCTRL requires an explicit source and valid loaded occurrence.");
        if(doc.metadata.header.byte_order!=IgbByteOrder::little)
            fail("Big-endian NUMBERCTRL serialization is unverified.");
        if(!doc.info_list_reference)fail("NUMBERCTRL requires an explicit exported info list.");
        Reader read{doc};auto data=std::make_shared<NumberControlProgram::Data>();
        data->source=std::move(source);data->occurrence=occurrence;std::size_t ram_count{};
        for(const auto reference:read.list(*doc.info_list_reference,"igInfoList",65536).second) {
            const auto& info=doc.object(reference);
            if(!doc.is_a(info,"igInfo"))fail("NUMBERCTRL exported list contains a non-info object.");
            if(doc.metadata.object_types.at(info.type_index).name!="beNumberCtrlInfo")continue;
            if(data->infos.size()>=4096)fail("NUMBERCTRL info count exceeds its budget.");
            NumberInfoDefinition result;result.reference=reference;result.owner_reference=info.reference_index;
            result.name=read.name(info);result.active=read.byte(info,4);result.data_reference=read.ref(info,5);
            if(result.data_reference)read.object(*result.data_reference,"beBaseInfoDataList");
            auto [identity,entries]=read.list(read.required(info,6),"beBaseInfoRamList",4096);
            if(entries.size()>4096u-ram_count)fail("NUMBERCTRL aggregate RAM budget exceeded.");
            ram_count+=entries.size();result.rams_source=identity;
            for(const auto ram_ref:entries) {
                const auto& ram=read.object(ram_ref,"beNumberCtrlInfoRam");NumberRamDefinition decoded;
                decoded.reference=ram_ref;decoded.owner_reference=ram.reference_index;
                decoded.authored_index=read.integer(ram,2);decoded.active=read.byte(ram,5);
                for(std::uint16_t slot=6;slot<=9;++slot)decoded.authored_words[slot-6]=read.integer(ram,slot);
                decoded.data_reference=read.ref(ram,10);
                if(decoded.data_reference)read.object(*decoded.data_reference,"beBaseInfoDataList");
                decoded.geometry_reference=read.ref(ram,11);
                if(decoded.geometry_reference) {
                    decoded.geometry_owner_reference=doc.object(*decoded.geometry_reference).reference_index;
                    std::uint32_t attribute{};decoded.uv=read.uv(*decoded.geometry_reference,attribute);
                    decoded.attribute_reference=attribute;
                    decoded.vertex_array_reference=read.required(doc.object(attribute),4);
                    decoded.vertex_array_owner_reference=doc.object(*decoded.vertex_array_reference).reference_index;
                }
                read.field(ram,12,"igFloatMetaField");decoded.offset_next=doc.field_f32(ram,12);
                if(!std::isfinite(decoded.offset_next) || !std::isfinite(std::fma(9.0f,decoded.offset_next,decoded.offset_next)))
                    fail("NUMBERCTRL atlas offset produces a non-finite coordinate.");
                decoded.maximum=read.integer(ram,13);decoded.mode=read.integer(ram,14);
                if(const auto digits=read.ref(ram,15)) {
                    auto [digit_identity,digit_entries]=read.list(*digits,"beNumVerDataList",256);
                    decoded.digits_source=digit_identity;
                    for(const auto digit_ref:digit_entries) {
                        const auto& digit=read.object(digit_ref,"beNumVerData");NumberDigitDefinition output;
                        output.reference=digit_ref;output.owner_reference=digit.reference_index;output.display=read.integer(digit,2);
                        auto [mapping_identity,mappings]=read.list(read.required(digit,3),"beNumVerInsideDataList",4096);
                        if(mappings.size()<6)fail("NUMBERCTRL digit requires its six original vertex mappings.");
                        output.mappings_source=mapping_identity;
                        for(const auto mapping_ref:mappings) {
                            const auto& mapping=read.object(mapping_ref,"beNumVerInsideData");
                            NumberMappingDefinition entry{mapping_ref,mapping.reference_index,read.integer(mapping,2),read.integer(mapping,3)};
                            if(output.mappings.size()<6 && decoded.uv && (entry.index<0 ||
                                std::size_t(entry.index)>=decoded.uv->texcoords.size()))
                                fail("NUMBERCTRL mapping points outside its actual UV stream.");
                            output.mappings.push_back(entry);
                        }
                        decoded.digits.push_back(std::move(output));
                    }
                }
                if(const auto work_ref=read.ref(ram,16)) {
                    const auto& work=read.object(*work_ref,"beNumberCtrlInfoWork");
                    decoded.work=NumberWorkDefinition{*work_ref,work.reference_index,
                        read.integer(work,2),read.integer(work,3),read.integer(work,4)};
                }
                result.rams.push_back(std::move(decoded));
            }
            data->infos.push_back(std::move(result));
        }
        return NumberControlProgram(std::move(data));
    }catch(const NumberControlError&){throw;}
     catch(const std::exception& error){throw NumberControlError(error.what());}
}

struct NumberControlRuntime::Impl {
    struct Ram {
        NumberRamState state;
        std::shared_ptr<NumberWorkState> work;
        std::shared_ptr<NumberUvState> uv;
        std::vector<std::shared_ptr<NumberDigitState>> digits;
        void ensure_work() {
            if(!work){work=std::make_shared<NumberWorkState>();work->id=ControllerObjectId::allocate_native();state.work=work;}
        }
        void display(std::int32_t value) {
            // Native safety checks precede this helper's committed digit prefix.
            // The immutable decoded profile has already checked all six indices.
            if(!state.digits_source || !state.geometry || !uv)
                fail("NUMBERCTRL display requires its actual prepared digit list and writable geometry.");
            auto number=value;
            for(std::size_t ordinal=0;ordinal<digits.size();++ordinal) {
                auto& digit=*digits[ordinal];
                if(value<0)digit.display=state.mode==0?0:-1;
                else {
                    digit.display=(number!=0 || ordinal==0)?number%10:(state.mode==0?0:-1);
                    number/=10;
                }
            }
            // A digit can occur repeatedly in a shared list. Read its final
            // display word here, after every display assignment has completed.
            for(const auto& digit:digits)for(std::size_t i=0;i<6;++i) {
                const auto& mapping=digit->mappings[i];auto& output=uv->texcoords.at(static_cast<std::size_t>(mapping.index));
                if(digit->display<0){output={0,0};continue;}
                const auto number_float=static_cast<float>(digit->display);
                switch(mapping.kind) {
                case 0:output={number_float*state.offset_next,0};break;
                case 1:output={std::fma(number_float,state.offset_next,state.offset_next),0};break;
                case 2:output={number_float*state.offset_next,1};break;
                case 3:output={std::fma(number_float,state.offset_next,state.offset_next),1};break;
                default:break; // Original switch has no write for another kind.
                }
            }
            ++uv->revision; // The original writable-stream release boundary.
        }
        MessengerCallResult command(NativeMessenger& messenger,const MessengerDelivery& delivery) {
            if(delivery.recipient!=state.id)fail("NUMBERCTRL delivery belongs to another RAM.");
            const auto command=delivery.message.command;
            if(command=="GETVALUE") {
                if(work)messenger.buffer().add_int(work->target);
                return {true,1,{}};
            }
            if(command!="SETVALUE" && command!="ADDVALUE")return {true,0,{}};
            ensure_work(); // Allocation precedes even the empty-argument branch.
            if(messenger.buffer().integers().empty())return {true,1,{}};
            const auto argument=messenger.buffer().integers().front();
            if(command=="SETVALUE") {
                const auto value=clamp(argument,state.maximum);display(value);
                work->current=value;work->target=value;work->step=0;
            }else {
                work->target=clamp(add(work->target,argument),state.maximum);
                work->step=change_step(subtract(work->target,work->current));
            }
            return {true,1,{}};
        }
        void update() {
            if(state.words[0]==2 || state.words[0]==3)return;
            if(state.words[0]!=1) {
                ensure_work();state.words[0]=add(state.words[0],1);
                state.words[1]=state.words[2]=state.words[3]=0;
            }
            if(!work)fail("NUMBERCTRL active update requires its original work owner.");
            if(work->current==work->target)return;
            auto next=add(work->current,work->step);
            if((work->step>0 && next>work->target) || (work->step<0 && next<work->target) || work->step==0)
                next=work->target;
            display(next);work->current=next;
        }
    };
    ControllerObjectId manager=ControllerObjectId::allocate_native();
    std::vector<NumberControlProgram> sources;
    std::map<ControllerObjectId,std::shared_ptr<Ram>> rams;
    std::map<ControllerObjectId,ControllerObjectId> aliases;
    std::shared_ptr<Ram> find(ControllerObjectId id) const {
        const auto found=aliases.find(id);if(found==aliases.end())fail("NUMBERCTRL RAM does not belong to this manager.");
        return rams.at(found->second);
    }
};
NumberControlRuntime::NumberControlRuntime(ActionEventRuntime& runtime):impl_(std::make_shared<Impl>()),runtime_(runtime) {}
NumberControlRuntime::~NumberControlRuntime()=default;
ControllerObjectId NumberControlRuntime::manager_id() const noexcept{return impl_->manager;}
const std::vector<NumberControlProgram>& NumberControlRuntime::sources() const noexcept{return impl_->sources;}
const NumberRamState& NumberControlRuntime::ram(ControllerObjectId id) const{return impl_->find(id)->state;}
void NumberControlRuntime::set_active(ControllerObjectId id,std::uint8_t active){impl_->find(id)->state.active=active;}
void NumberControlRuntime::update(ControllerObjectId id){impl_->find(id)->update();}
void NumberControlRuntime::register_source(NumberControlProgram program) {
    for(const auto& source:impl_->sources)if(source.source_occurrence()==program.source_occurrence())
        fail("NUMBERCTRL source occurrence is already registered.");
    if(impl_->sources.size()>=4096)fail("NUMBERCTRL loaded-source budget exceeded.");
    auto sources=impl_->sources;sources.push_back(program);
    std::map<ControllerObjectId,std::shared_ptr<Impl::Ram>> rams;
    std::map<ControllerObjectId,ControllerObjectId> aliases;
    std::map<ControllerObjectId,std::shared_ptr<NumberDigitState>> digits;
    std::map<ControllerObjectId,std::shared_ptr<NumberWorkState>> works;
    std::map<ControllerObjectId,std::shared_ptr<NumberUvState>> uvs;
    std::vector<MessengerRegistration> rows;
    const auto qualify=[&](std::uint32_t reference){return ControllerObjectId{program.source_occurrence(),reference};};
    for(const auto& info:program.infos())for(std::size_t ordinal=0;ordinal<info.rams.size();++ordinal) {
        const auto& authored=info.rams[ordinal];const auto id=qualify(authored.owner_reference);
        auto found=rams.find(id);
        if(found==rams.end()) {
            auto ram=std::make_shared<Impl::Ram>();auto& state=ram->state;
            state.id=id;state.manager=impl_->manager;state.active=authored.active;
            state.offset_next=authored.offset_next;state.maximum=authored.maximum;state.mode=authored.mode;
            state.digits_source=authored.digits_source;
            if(authored.geometry_owner_reference)state.geometry=qualify(*authored.geometry_owner_reference);
            if(authored.uv) {
                state.vertex_array=qualify(*authored.vertex_array_owner_reference);
                const auto uv_id=qualify(authored.uv->memory_owner_reference);auto uv=uvs.find(uv_id);
                if(uv==uvs.end())uv=uvs.emplace(uv_id,std::make_shared<NumberUvState>(
                    NumberUvState{uv_id,authored.uv->texcoords,0})).first;
                ram->uv=uv->second;state.uv=ram->uv;
            }
            if(authored.work) {
                const auto& source=*authored.work;const auto work_id=qualify(source.owner_reference);auto work=works.find(work_id);
                if(work==works.end())work=works.emplace(work_id,std::make_shared<NumberWorkState>(
                    NumberWorkState{work_id,source.current,source.target,source.step})).first;
                ram->work=work->second;state.work=ram->work;
            }
            for(const auto& source:authored.digits) {
                const auto digit_id=qualify(source.owner_reference);auto digit=digits.find(digit_id);
                if(digit==digits.end())digit=digits.emplace(digit_id,std::make_shared<NumberDigitState>(
                    NumberDigitState{digit_id,source.display,source.mappings_source,source.mappings})).first;
                ram->digits.push_back(digit->second);state.digits.push_back(digit->second);
            }
            found=rams.emplace(id,std::move(ram)).first;
        }
        // Generic initialization writes the live parent/ordinal for each RAM
        // occurrence; registration retains each then-current name and ordinal.
        found->second->state.info=qualify(info.owner_reference);
        found->second->state.index=static_cast<std::int32_t>(ordinal);found->second->state.words={};
        aliases.emplace(qualify(authored.reference),id);aliases.emplace(id,id);
        rows.push_back({id,"NUMBERCTRL",info.name.value_or(""),static_cast<std::int32_t>(ordinal)});
    }
    std::vector<PreparedControllerRegistration> providers;providers.reserve(rams.size());
    for(const auto& [id,ram]:rams)providers.push_back({id,{
        [ram]()->std::optional<std::uint8_t>{return ram->state.active;},
        [ram]{return MessengerManager{
            [ram](NativeMessenger& messenger,const MessengerDelivery& delivery){return ram->command(messenger,delivery);},
            [](NativeMessenger&,const MessengerDelivery&,std::string_view){return MessengerCallResult{true,0,{}};}};}
    },program.source()});
    runtime_.register_controllers(providers,rows);
    impl_->rams.merge(rams);impl_->aliases.merge(aliases);impl_->sources.swap(sources);
}
} // namespace dw4
