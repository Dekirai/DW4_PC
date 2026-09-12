#include "dw4/meter_control.hpp"
#include "dw4/action_event_runtime.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace dw4 {
namespace {
constexpr std::size_t max_rows=65536,max_entries=262144;
[[noreturn]] void fail(const char* text) {throw MeterControlError(text);}
void finite(float value) {if(!std::isfinite(value))fail("Meter state requires a finite original scalar result.");}
float rounded(float value,double exact) {
    finite(value);
    return std::abs(exact)<std::numeric_limits<float>::min() ? std::copysign(0.0f,value) : value;
}
float add(float a,float b) {return rounded(a+b,double(a)+double(b));}
float multiply(float a,float b) {return rounded(a*b,double(a)*double(b));}
float divide(float a,float b) {
    if(b==0)fail("Original meter division reaches an unusable zero denominator.");
    const float value=a/b;finite(value);
    // Compare exact scaled operands for NI tininess before rounding.
    return std::abs(double(a))<double(std::numeric_limits<float>::min())*std::abs(double(b)) ? std::copysign(0.0f,value) : value;
}
std::int32_t integer(float value) {
    finite(value);
    if(double(value)<-2147483648.0 || double(value)>=2147483648.0)
        fail("Original meter fctiwz is outside the represented signed integer range.");
    return static_cast<std::int32_t>(value);
}
std::int64_t time_integer(float value) {
    finite(value);
    // Actual __cvt_dbl_usll800A4130 saturates magnitudes with exponent>=63.
    if(double(value)>=0x1p63)return std::numeric_limits<std::int64_t>::max();
    if(double(value)<=-0x1p63)return std::numeric_limits<std::int64_t>::min();
    return static_cast<std::int64_t>(value);
}
float percent(float value,float maximum) {
    const bool nonzero=value!=0;
    const auto scaled=divide(multiply(value,maximum),100.0f);
    return scaled<1.0f && nonzero ? 1.0f : scaled;
}
float clamp(float value,float maximum) {return value>maximum ? maximum : value<0 ? 0.0f : value;}
void initialize(MeterOneState& one) {
    auto number=integer(one.maximum);std::int32_t digits=1;
    while((number/=10)!=0)++digits;
    one.digit_count=digits;
    one.time_scale=divide(2000000000.0f,one.maximum);
    one.step=divide(one.maximum,60.0f);if(one.step<1.0f)one.step=1.0f;
    one.dirty=1;
}
struct Reader {
    std::shared_ptr<ActorSceneResource> resource;
    std::size_t entries{},text{};
    const IgbDocument& doc() const {return resource->document();}
    const IgbObject& object(std::uint32_t ref,std::string_view type) const {
        const auto& value=doc().object(ref);
        if(doc().metadata.object_types.at(value.type_index).name!=type)fail("Unexpected meter resource object type.");
        std::set<std::uint16_t> slots;
        for(const auto& f:value.fields)if(!slots.insert(f.slot).second)fail("Meter resource repeats a field slot.");
        resource->canonical_reference(ref);return value;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,std::string_view type,std::optional<std::size_t> width=4) const {
        const auto* f=doc().field(object,slot);
        if(!f || doc().metadata.field_types.at(f->type_index).name!=type || (width && f->size!=*width))
            fail("Meter resource field type or width differs.");
        doc().field_bytes(*f);return *f;
    }
    std::optional<std::uint32_t> ref(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igObjectRefMetaField");const auto value=doc().field_reference(object,slot);
        if(value)resource->canonical_reference(*value);return value;
    }
    std::uint32_t required(const IgbObject& object,std::uint16_t slot) const {
        const auto value=ref(object,slot);if(!value)fail("Meter resource requires its actual referenced owner.");return *value;
    }
    std::uint8_t byte(const IgbObject& object,std::uint16_t slot) const {return doc().field_bytes(field(object,slot,"igBoolMetaField",1))[0];}
    float scalar(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igFloatMetaField");const auto value=doc().field_f32(object,slot);finite(value);return value;
    }
    std::int32_t word(const IgbObject& object,std::uint16_t slot) const {field(object,slot,"igIntMetaField");return doc().field_i32(object,slot);}
    std::int64_t long_word(const IgbObject& object,std::uint16_t slot) const {
        const auto data=doc().field_bytes(field(object,slot,"igLongMetaField",8));std::uint64_t value{};
        for(unsigned i=0;i<8;++i)value|=std::uint64_t(data[i])<<(8*i);return std::bit_cast<std::int64_t>(value);
    }
    std::optional<std::string> name(const IgbObject& object) {
        const auto& f=field(object,2,"igStringMetaField",{});const auto value=doc().field_string(object,2);
        if(f.size>4104 || value.size()>4096 || value.find('\0')!=std::string_view::npos || value.size()>8u*1024u*1024u-text)
            fail("Meter resource name exceeds its bounded byte profile.");
        text+=value.size();return f.size==4 ? std::nullopt : std::optional<std::string>{value};
    }
    DatabaseListIdentity list(std::uint32_t ref,std::string_view type,std::size_t stride=4) {
        const auto& value=object(ref,type);const auto count=word(value,2),capacity=word(value,3);
        field(value,4,"igMemoryRefMetaField");
        if(count<0 || capacity<count || capacity>static_cast<std::int32_t>(max_rows) || std::size_t(count)>max_entries-entries)
            fail("Meter resource list exceeds its capacity or aggregate entry budget.");
        entries+=static_cast<std::size_t>(count);
        const auto memory=doc().field_reference(value,4);std::optional<std::uint32_t> owner;
        if((!memory && capacity) || (memory && doc().memory(*memory).size()!=std::size_t(capacity)*stride))
            fail("Meter list memory does not match its declared capacity.");
        if(memory)owner=doc().memories.at(doc().references.at(*memory).value_index).reference_index;
        return {ref,value.reference_index,memory,owner,static_cast<std::uint32_t>(count),static_cast<std::uint32_t>(capacity)};
    }
    std::vector<std::uint32_t> members(const DatabaseListIdentity& list) const {
        std::vector<std::uint32_t> result;
        for(const auto item:doc().object_list(list.reference)) {if(!item)fail("Meter object list contains a null owner.");result.push_back(*item);}
        return result;
    }
};
struct BindCall {
    std::shared_ptr<MeterOneState> one;
    std::optional<MessengerDispatch> dispatch;
    NativeMessenger* messenger{};
    std::uint64_t id{};std::size_t row{};
    bool started{},failed{},executing{},complete{};
};
}
struct MeterProgram::Data {
    std::shared_ptr<ActorSceneResource> resource;
    std::vector<MeterInfoDefinition> infos;
};
const std::shared_ptr<ActorSceneResource>& MeterProgram::resource() const noexcept {return data_->resource;}
const std::string& MeterProgram::source() const noexcept {return resource()->source();}
std::uint32_t MeterProgram::source_occurrence() const noexcept {return resource()->source_occurrence();}
const std::vector<MeterInfoDefinition>& MeterProgram::infos() const noexcept {return data_->infos;}

MeterProgram decode_meter_controls(std::shared_ptr<ActorSceneResource> resource) {
    try {
        if(!resource)fail("Meter decoding requires its actual loaded resource.");
        const auto& doc=resource->document();if(!doc.info_list_reference)fail("Meter resource requires its exported info list.");
        auto data=std::make_shared<MeterProgram::Data>();data->resource=resource;Reader read{resource};
        std::map<std::uint32_t,std::shared_ptr<const MeterNodeDefinition>> nodes;
        std::map<std::uint32_t,MeterOneDefinition> ones;
        std::map<std::uint32_t,std::shared_ptr<GameDataInt>> integers;
        const auto integer_owner=[&](std::optional<std::uint32_t> ref)->std::shared_ptr<GameDataInt> {
            if(!ref)return {};
            const auto& object=read.object(*ref,"beDataObjInt");const auto owner=object.reference_index;
            if(const auto found=integers.find(owner);found!=integers.end())return found->second;
            auto value=std::make_shared<GameDataInt>(GameDataInt{read.name(object),read.word(object,3)});
            integers.emplace(owner,value);return value;
        };
        for(const auto ref:read.members(read.list(*doc.info_list_reference,"igInfoList"))) {
            const auto& candidate=doc.object(ref);
            if(!doc.is_a(candidate,"igInfo"))fail("Meter export contains a non-info object.");
            if(doc.metadata.object_types.at(candidate.type_index).name!="beMeterCtrlInfo")continue;
            const auto& info=read.object(ref,"beMeterCtrlInfo");MeterInfoDefinition out;
            out.reference=ref;out.owner_reference=info.reference_index;out.name=read.name(info);out.active=read.byte(info,4);
            out.data_reference=read.ref(info,5);if(out.data_reference)read.object(*out.data_reference,"beBaseInfoDataList");
            out.ram_list=read.list(read.required(info,6),"beBaseInfoRamList");
            for(const auto ram_ref:read.members(out.ram_list)) {
                const auto& ram=read.object(ram_ref,"beBaseInfoRam");DatabaseRamDefinition value;
                value.reference=ram_ref;value.owner_reference=ram.reference_index;value.authored_index=read.word(ram,2);value.active=read.byte(ram,5);
                for(std::uint16_t slot=6;slot<=9;++slot)value.authored_words[slot-6]=read.word(ram,slot);
                value.data_reference=read.ref(ram,10);if(value.data_reference)read.object(*value.data_reference,"beBaseInfoDataList");
                out.rams.push_back(value);
            }
            out.one_list=read.list(read.required(info,7),"igNamedObjectList");
            for(const auto one_ref:read.members(out.one_list)) {
                const auto& object=read.object(one_ref,"beMeterCtrlOneData");const auto canonical=object.reference_index;
                auto found=ones.find(canonical);
                if(found==ones.end()) {
                    MeterOneDefinition one;one.owner_reference=canonical;one.name=read.name(object);
                    const auto node_ref=read.required(object,3);const auto& node=read.object(node_ref,"beMeterCtrlNode");
                    auto shared=nodes.find(node.reference_index);
                    if(shared==nodes.end()) {
                        auto value=std::make_shared<MeterNodeDefinition>();value->node=resource->node(node_ref);value->meter_mode=read.byte(node,8);
                        value->time_list=read.list(read.required(node,9),"igLongList",8);
                        if(value->time_list.memory_reference) {
                            const auto bytes=doc.memory(*value->time_list.memory_reference);
                            for(std::size_t i=0;i<value->time_list.count;++i) {
                                std::uint64_t word{};for(unsigned b=0;b<8;++b)word|=std::uint64_t(bytes[i*8+b])<<(8*b);
                                value->times.push_back(std::bit_cast<std::int64_t>(word));
                            }
                        }
                        const auto bytes=doc.field_bytes(read.field(node,10,"igVec3fMetaField",12));
                        for(unsigned i=0;i<3;++i) {
                            std::uint32_t word{};for(unsigned b=0;b<4;++b)word|=std::uint32_t(bytes[i*4+b])<<(8*b);
                            value->step_translation[i]=std::bit_cast<float>(word);finite(value->step_translation[i]);
                        }
                        shared=nodes.emplace(node.reference_index,std::move(value)).first;
                    }
                    one.node=shared->second;one.maximum=read.scalar(object,4);one.time_scale=read.scalar(object,5);
                    one.step=read.scalar(object,6);one.target=read.scalar(object,7);one.current=read.scalar(object,8);
                    one.minimum_time=read.long_word(object,9);one.maximum_time=read.long_word(object,10);one.digit_count=read.word(object,11);
                    one.dirty=read.byte(object,12);one.trim_zeroes=read.byte(object,13);one.integer_mode=read.byte(object,14);
                    one.bound_value=integer_owner(read.ref(object,15));one.bound_maximum=integer_owner(read.ref(object,16));
                    found=ones.emplace(canonical,std::move(one)).first;
                }
                auto one=found->second;one.reference=one_ref;out.ones.push_back(std::move(one));
            }
            data->infos.push_back(std::move(out));
        }
        return MeterProgram(std::move(data));
    }catch(const MeterControlError&){throw;}catch(const std::exception& error){throw MeterControlError(error.what());}
}

struct MeterRuntime::Impl {
    ControllerObjectId manager=ControllerObjectId::allocate_native();ActionEventRuntime* events{};
    std::vector<MeterProgram> sources;
    std::map<ControllerObjectId,std::shared_ptr<MeterRamState>> rams;
    std::map<ControllerObjectId,std::shared_ptr<MeterOneState>> ones;
    std::map<ControllerObjectId,ControllerObjectId> ram_aliases,one_aliases;
    bool failed{};
    std::shared_ptr<MeterRamState> ram(ControllerObjectId id) const {
        const auto found=ram_aliases.find(id);if(found==ram_aliases.end())fail("Meter RAM belongs to another loaded manager.");return rams.at(found->second);
    }
    std::shared_ptr<MeterOneState> one(ControllerObjectId id) const {
        const auto found=one_aliases.find(id);if(found==one_aliases.end())fail("Meter ONE belongs to another loaded manager.");return ones.at(found->second);
    }
    std::shared_ptr<MeterOneState> selected(const MeterRamState& ram) const {
        const auto& rows=ram.program->infos().at(ram.info_index).ones;
        if(ram.index<0 || std::size_t(ram.index)>=rows.size())fail("Meter live RAM index is outside its actual parent ONE list.");
        return one({ram.program->source_occurrence(),rows[static_cast<std::size_t>(ram.index)].owner_reference});
    }
    void usable() const {if(failed)fail("An uncertain meter operation cannot be replayed.");}
    MessengerCallResult invoke(BindCall& call,MeterRamState& ram,NativeMessenger& messenger,
        const MessengerDelivery& delivery,std::string_view command) {
        usable();if(call.failed)fail("The retained meter delivery is faulted.");
        if(delivery.recipient!=ram.id)fail("Meter delivery belongs to another canonical RAM.");
        if(call.executing)fail("A meter delivery cannot recursively resume itself.");
        if(call.started && (call.messenger!=&messenger || call.id!=delivery.dispatch_id || call.row!=delivery.row_index || command!="SETMODELHP"))
            fail("Meter binding requires its retained original delivery.");
        struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(call.executing);
        try {
            if(!call.started)call.one=selected(ram);
            auto& one=*call.one;
            const auto argument=[&] {
                if(messenger.buffer().floats().empty())fail("Meter command requires float argument zero.");
                return messenger.buffer().floats()[0];
            };
            if(command=="GETVALUE")messenger.buffer().add_float(one.target);
            else if(command=="SETVALUE") {one.target=argument();one.current=one.target;one.dirty=1;}
            else if(command=="ADDVALUE")one.target=clamp(add(argument(),one.target),one.maximum);
            else if(command=="GETPERCENT")messenger.buffer().add_float(divide(one.target,one.maximum));
            else if(command=="SETPERCENT") {if(!one.bound_value)one.target=percent(argument(),one.maximum);}
            else if(command=="ADDPERCENT") {
                const auto maximum=one.maximum;
                one.target=clamp(add(divide(multiply(argument(),maximum),100.0f),one.target),one.maximum);
            } else if(command=="SETMAXVALUE") {one.maximum=argument();initialize(one);}
            else if(command=="SETMODELHP") {
                if(call.complete)return {true,1,{}};
                if(!call.started) {
                    if(messenger.buffer().strings().empty() || messenger.buffer().integers().empty())
                        fail("SETMODELHP requires its actual model name and RAM index.");
                    const auto name=messenger.buffer().strings()[0];const auto index=messenger.buffer().integers()[0];
                    call.started=true;call.messenger=&messenger;call.id=delivery.dispatch_id;call.row=delivery.row_index;
                    messenger.push_buffer();call.dispatch.emplace(messenger.begin_send_to("MODELCTRL",name,index,"GETHP"));
                }
                const auto result=events->resume(*call.dispatch);
                if(result.stop==MessengerStop::faulted)fail("SETMODELHP nested GETHP delivery faulted.");
                if(result.stop!=MessengerStop::completed)return {false,0,result.diagnostic};
                const auto object=[&](std::size_t index)->std::shared_ptr<GameDataInt> {
                    if(index>=messenger.buffer().objects().size())fail("SETMODELHP requires both actual GETHP object results.");
                    const auto& value=messenger.buffer().objects()[index];
                    if(std::holds_alternative<std::monostate>(value))return {};
                    const auto* pointer=std::get_if<std::shared_ptr<GameDataInt>>(&value);
                    if(!pointer || !*pointer)fail("SETMODELHP result is not a represented integer wrapper.");return *pointer;
                };
                // The original publishes HP before fetching MHP. A bad second
                // result preserves the first shared binding and pushed packet.
                one.bound_value=object(0);one.bound_maximum=object(1);messenger.pop_buffer();call.complete=true;
            } else return {true,0,{}};
            return {true,1,{}};
        }catch(...){call.failed=failed=true;throw;}
    }
};
MeterRuntime::MeterRuntime(ActionEventRuntime& runtime):impl_(std::make_shared<Impl>()),runtime_(runtime) {impl_->events=&runtime;}
MeterRuntime::~MeterRuntime()=default;
ControllerObjectId MeterRuntime::manager_id() const noexcept {return impl_->manager;}
const std::vector<MeterProgram>& MeterRuntime::sources() const noexcept {return impl_->sources;}
const MeterRamState& MeterRuntime::ram(ControllerObjectId id) const {return *impl_->ram(id);}
const MeterOneState& MeterRuntime::one(ControllerObjectId id) const {return *impl_->one(id);}
const MeterOneState& MeterRuntime::selected_one(ControllerObjectId id) const {return *impl_->selected(*impl_->ram(id));}
void MeterRuntime::set_active(ControllerObjectId id,std::uint8_t value) {impl_->usable();impl_->ram(id)->active=value;}
void MeterRuntime::set_index(ControllerObjectId id,std::int32_t value) {impl_->usable();impl_->ram(id)->index=value;}
bool MeterRuntime::faulted() const noexcept {return impl_->failed;}

void MeterRuntime::update(ControllerObjectId id) {
    impl_->usable();auto ram=impl_->ram(id);auto one=impl_->selected(*ram);
    try {
        if(ram->words[0]==0) {initialize(*one);ram->words[0]=1;ram->words[1]=ram->words[2]=ram->words[3]=0;}
        if(one->bound_value) {
            if(!one->bound_maximum)fail("Bound meter value requires its actual maximum wrapper.");
            const auto product=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(one->bound_value->data)*100u);
            const auto divisor=one->bound_maximum->data;
            if(divisor==0 || (product==std::numeric_limits<std::int32_t>::min() && divisor==-1))
                fail("Bound meter percent reaches an unrepresented exceptional divw result.");
            one->target=percent(static_cast<float>(product/divisor),one->maximum);
        }
        auto delta=add(one->target,-one->current);
        if(delta!=0) {
            one->dirty=1;const bool positive=delta>=0;if(!positive)delta=-delta;
            if(delta>one->step)delta=one->step;
            auto value=positive ? add(delta,one->current) : add(one->current,-delta);
            // Update tests the lower bound first; ADD* commands test upper first.
            if(value<0)value=0.0f;else if(value>one->maximum)value=one->maximum;
            if(one->integer_mode) {
                const bool nonzero=value!=0;value=static_cast<float>(integer(value));if(value==0 && nonzero)value=1.0f;
            }
            one->current=value;
        }
        if(!one->dirty)return;
        one->dirty=0;auto& times=*one->node->times;times.clear();
        if(one->node->definition->meter_mode) {
            if(one->current<=0) {times.push_back(1000000000);one->current=0;}
            else if(one->current>=one->maximum) {times.push_back(3000000000);one->current=one->maximum;}
            else {
                auto time=time_integer(multiply(one->current,one->time_scale));
                if(one->current>0 && time<one->minimum_time)time=one->minimum_time;
                else if(one->current<one->maximum && time>one->maximum_time)time=one->maximum_time;
                times.push_back(std::bit_cast<std::int64_t>(std::bit_cast<std::uint64_t>(time)+1000000000ULL));
            }
        } else {
            auto value=integer(one->current);
            if(one->digit_count>static_cast<std::int32_t>(max_rows))fail("Meter digit output exceeds its bounded original list profile.");
            for(std::int32_t digit=0;digit<one->digit_count;++digit) {
                const auto remainder=value%10;times.push_back(1000000000LL+std::int64_t(remainder)*1000000000LL);
                value/=10;if(one->trim_zeroes && value==0)break;
            }
        }
    }catch(...){impl_->failed=true;throw;}
}

void MeterRuntime::register_source(MeterProgram program) {
    impl_->usable();for(const auto& source:impl_->sources)if(source.source_occurrence()==program.source_occurrence())
        fail("Meter source occurrence is already registered.");
    if(impl_->sources.size()>=4096)fail("Meter source count exceeds its host budget.");
    auto sources=impl_->sources;sources.push_back(program);auto owner=std::make_shared<const MeterProgram>(std::move(program));
    decltype(impl_->rams) rams;decltype(impl_->ones) ones;decltype(impl_->ram_aliases) ram_aliases,one_aliases;
    std::map<std::uint32_t,std::shared_ptr<MeterNodeState>> nodes;
    std::map<std::uint32_t,std::shared_ptr<std::vector<std::int64_t>>> lists;
    std::vector<MessengerRegistration> rows;
    for(std::size_t i=0;i<owner->infos().size();++i) {
        const auto& info=owner->infos()[i];
        for(const auto& value:info.ones) {
            const ControllerObjectId id{owner->source_occurrence(),value.owner_reference};
            if(!ones.contains(id)) {
                const auto ref=value.node->node->reference();auto found=nodes.find(ref);
                if(found==nodes.end()) {
                    const auto list_ref=value.node->time_list.owner_reference;auto list=lists.find(list_ref);
                    if(list==lists.end())list=lists.emplace(list_ref,std::make_shared<std::vector<std::int64_t>>(value.node->times)).first;
                    found=nodes.emplace(ref,std::make_shared<MeterNodeState>(MeterNodeState{value.node,list->second})).first;
                }
                ones.emplace(id,std::make_shared<MeterOneState>(MeterOneState{id,found->second,value.maximum,value.time_scale,value.step,
                    value.target,value.current,value.minimum_time,value.maximum_time,value.digit_count,value.dirty,value.trim_zeroes,
                    value.integer_mode,value.bound_value,value.bound_maximum}));
            }
            one_aliases.emplace(ControllerObjectId{owner->source_occurrence(),value.reference},id);one_aliases.emplace(id,id);
        }
        for(std::size_t index=0;index<info.rams.size();++index) {
            const auto& value=info.rams[index];const ControllerObjectId id{owner->source_occurrence(),value.owner_reference};
            auto found=rams.find(id);if(found==rams.end())found=rams.emplace(id,std::make_shared<MeterRamState>()).first;
            *found->second={id,impl_->manager,{owner->source_occurrence(),info.owner_reference},static_cast<std::int32_t>(index),value.active,{},owner,i};
            ram_aliases.emplace(ControllerObjectId{owner->source_occurrence(),value.reference},id);ram_aliases.emplace(id,id);
            rows.push_back({id,"METERCTRL",info.name.value_or(""),static_cast<std::int32_t>(index)});
        }
    }
    std::vector<PreparedControllerRegistration> providers;
    for(const auto& [id,ram]:rams)providers.push_back({id,{
        [ram]()->std::optional<std::uint8_t>{return ram->active;},
        [impl=impl_,ram] {
            auto call=std::make_shared<BindCall>();return MessengerManager{
                [](NativeMessenger&,const MessengerDelivery&){return MessengerCallResult{true,0,{}};},
                [impl,ram,call](NativeMessenger& messenger,const MessengerDelivery& delivery,std::string_view command) {
                    return impl->invoke(*call,*ram,messenger,delivery,command);
                }};
        }},owner->source()});
    runtime_.register_controllers(providers,rows);impl_->rams.merge(rams);impl_->ones.merge(ones);
    impl_->ram_aliases.merge(ram_aliases);impl_->one_aliases.merge(one_aliases);impl_->sources.swap(sources);
}
} // namespace dw4
