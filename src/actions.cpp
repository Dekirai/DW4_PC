#include "dw4/actions.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/model_sequence.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <set>

namespace dw4 {
namespace {
constexpr std::size_t max_actions=4096, max_commands=65536, max_rams=1024, max_text=8u*1024u*1024u;
[[noreturn]] void fail(const char* reason) { throw ActionError(reason); }
void finite(float value) { if (!std::isfinite(value)) fail("Action input must be finite."); }
void name_check(std::string_view value) {
    if (value.size()>4096 || value.find('\0')!=std::string_view::npos) fail("Action name exceeds its byte profile.");
}
void charge(std::size_t value,std::size_t& total,std::size_t limit) {
    if (value>limit-total) fail("Action graph allocation budget exceeded.");
    total+=value;
}
const IgbObject& typed(const IgbDocument& doc,std::uint32_t ref,std::string_view type) {
    const auto& value=doc.object(ref); if (!doc.is_a(value,type)) fail("Unexpected action graph object type."); return value;
}
const IgbFieldValue& field(const IgbDocument& doc,const IgbObject& object,std::uint16_t slot,std::string_view type) {
    const auto* value=doc.field(object,slot);
    if (!value || doc.metadata.field_types.at(value->type_index).name!=type) fail("Unexpected action graph field type.");
    return *value;
}
std::string text(const IgbDocument& doc,const IgbObject& object,std::uint16_t slot,std::size_t& budget) {
    const auto& value=field(doc,object,slot,"igStringMetaField");
    if (value.size>4104) fail("Serialized action string exceeds its budget.");
    charge(value.size,budget,max_text);
    auto result=std::string(doc.field_string(object,slot)); name_check(result); return result;
}
std::uint32_t reference(const IgbDocument& doc,const IgbObject& object,std::uint16_t slot) {
    if (field(doc,object,slot,"igObjectRefMetaField").size!=4) fail("Invalid action reference width.");
    const auto value=doc.field_reference(object,slot);
    if (!value) throw ActionError("Required action reference is null at object "+std::to_string(object.reference_index)+" slot "+std::to_string(slot)+".");
    return *value;
}
float number(const IgbDocument& doc,const IgbObject& object,std::uint16_t slot) {
    if (field(doc,object,slot,"igFloatMetaField").size!=4) fail("Invalid action float width.");
    const auto value=doc.field_f32(object,slot); finite(value); return value;
}
std::int64_t integer64(const IgbDocument& doc,const IgbObject& object,std::uint16_t slot) {
    const auto& value=field(doc,object,slot,"igLongMetaField"); if (value.size!=8) fail("Invalid action clock width.");
    const auto bytes=doc.field_bytes(value); std::uint64_t result=0;
    for (std::size_t i=0;i<8;++i) result|=std::uint64_t(bytes[i])<<(i*8);
    return std::bit_cast<std::int64_t>(result);
}
std::vector<std::uint32_t> list(const IgbDocument& doc,std::uint32_t ref,std::string_view type,std::size_t limit) {
    const auto& object=typed(doc,ref,type);
    for (std::uint16_t slot=2;slot<=3;++slot)
        if (field(doc,object,slot,"igIntMetaField").size!=4 || doc.field_u32(object,slot)>limit) fail("Action list count/capacity exceeds its budget.");
    if (field(doc,object,4,"igMemoryRefMetaField").size!=4) fail("Invalid action list memory width.");
    std::vector<std::uint32_t> result;
    for (auto value:doc.object_list(ref)) { if (!value) fail("Action list contains a null object."); result.push_back(*value); }
    return result;
}
std::uint64_t bits(std::int64_t value) { return std::bit_cast<std::uint64_t>(value); }
std::int64_t signed_bits(std::uint64_t value) { return std::bit_cast<std::int64_t>(value); }
std::int64_t frame_time(float value) {
    // JOINTSET/WAITCHECK do not use MOTIONSET's zero-frame no-interpolation branch.
    return value==0 ? 0 : prepare_animation_blend(value,false).duration;
}
std::uint64_t node_time(const AnimationStateClock& node,std::uint64_t time) {
    // The node path has no shared-state cache or its initial (-1,-1) sentinel.
    auto copy=node; change_animation_speed(copy,time,0); return bits(copy.time_bias);
}
void node_origin(AnimationStateClock& node,std::uint64_t time,std::int64_t origin) {
    auto scale=node; scale.time_bias=0; node.time_bias=signed_bits(bits(origin)-node_time(scale,time));
}
std::array<float,4> unpack(const ActionColor& color) {
    std::array<float,4> result{};
    for (std::size_t i=0;i<4;++i) result[i]=static_cast<float>((color.packed>>(8*i))&255u)/255.0f;
    return result;
}
void color(ActionAppearance& appearance,const std::array<float,4>& channels) {
    std::uint32_t packed=0;
    for (std::size_t i=0;i<4;++i) {
        if (std::isnan(channels[i])) fail("Action color arithmetic produced NaN.");
        const float clamped=std::clamp(channels[i],0.0f,1.0f);
        packed|=static_cast<std::uint32_t>(std::fma(255.0f,clamped,0.5f))<<(8*i);
    }
    appearance.color=ActionColor{channels,packed};
}
struct Running {
    bool& flag;
    explicit Running(bool& value):flag(value) { if (flag) fail("Reentrant action mutation is unsupported."); flag=true; }
    ~Running() { flag=false; }
};
char first(std::string_view value) { return value.empty() ? '\0' : value[0]; }
bool reserved(std::string_view name) {
    constexpr std::array<std::string_view,6> commands{"ISCOMEND","ISRUN","GETNAMELIST","GETAPPEARANCELIST","GETBONEMATRIX","NDSP"};
    return std::find(commands.begin(),commands.end(),name)!=commands.end();
}
} // namespace

ActionProgram decode_action_program(const IgbDocument& doc,std::uint32_t ref,std::string source) {
    try {
        if (doc.metadata.header.byte_order!=IgbByteOrder::little) fail("Big-endian action graphs are unverified.");
        name_check(source); if (source.empty()) fail("Action source qualification must be explicit.");
        ActionProgram result; result.source_=std::move(source); result.reference_=ref;
        std::size_t budget=result.source_.size(), commands=0;
        const auto& info=typed(doc,ref,"beAction2Info"); result.name_=text(doc,info,2,budget);
        const auto& database=typed(doc,reference(doc,info,8),"igAnimationDatabase");
        for (auto animation:list(doc,reference(doc,database,6),"igAnimationList",max_actions))
            result.animations_.emplace_back(animation,text(doc,typed(doc,animation,"igAnimation"),2,budget));
        if (result.animations_.empty()) fail("Action animation database is empty.");
        const auto combiners=list(doc,reference(doc,database,9),"igAnimationCombinerList",max_rams);
        const auto rams=list(doc,reference(doc,info,6),"beBaseInfoRamList",max_rams);
        if (rams.empty() || rams.size()!=combiners.size()) fail("Action RAMs and prepared combiners must correspond.");
        std::set<std::uint32_t> ram_ids,node_ids,combiner_ids;
        for (std::size_t i=0;i<rams.size();++i) {
            const auto& ram=typed(doc,rams[i],"beAction2InfoRam");
            const auto node_ref=reference(doc,ram,11); const auto& node=typed(doc,node_ref,"beModelCtrlNode2");
            if (!ram_ids.insert(rams[i]).second || !node_ids.insert(node_ref).second || !combiner_ids.insert(combiners[i]).second)
                fail("Aliased prepared RAM/node/combiner ownership requires shared actor restoration.");
            typed(doc,combiners[i],"igAnimationCombiner");
            for (const auto slot:std::array<std::uint16_t,2>{10,14}) {
                if (field(doc,ram,slot,"igObjectRefMetaField").size!=4) fail("Invalid action work/timer reference width.");
                if (doc.field_reference(ram,slot)) fail("Serialized action timers/work require full restoration.");
            }
            result.rams_.push_back({rams[i],node_ref,combiners[i],{number(doc,node,13),integer64(doc,node,14),0}});
        }
        for (auto action:list(doc,reference(doc,info,9),"beAction2DataList",max_actions)) {
            const auto& object=typed(doc,action,"beAction2Data"); ActionDefinition value{action,text(doc,object,2,budget),{}};
            if (field(doc,object,3,"igObjectRefMetaField").size!=4) fail("Invalid action command-list reference width.");
            const auto command_list=doc.field_reference(object,3); value.command_list_present=command_list.has_value();
            const auto entries=command_list ? list(doc,*command_list,"igNamedObjectList",max_commands) : std::vector<std::uint32_t>{};
            charge(entries.size(),commands,max_commands);
            for (auto entry:entries) {
                const auto& command=doc.object(entry); ActionCommand decoded; decoded.reference=entry;
                decoded.type=doc.metadata.object_types.at(command.type_index).name; charge(decoded.type.size(),budget,max_text);
                if (decoded.type=="beAction2MOTIONSET") decoded.opcode=ActionOpcode::motion;
                else if (decoded.type=="beAction2JOINTSET") decoded.opcode=ActionOpcode::joint;
                else if (decoded.type=="beAction2FADESET") decoded.opcode=ActionOpcode::fade;
                else if (decoded.type=="beAction2WAITCHECK") decoded.opcode=ActionOpcode::wait;
                else if (decoded.type=="beAction2FLASHSET") decoded.opcode=ActionOpcode::flash;
                if (decoded.opcode!=ActionOpcode::unsupported) decoded.name=text(doc,command,2,budget);
                if (decoded.opcode==ActionOpcode::motion) {
                    decoded.interpolation=text(doc,command,3,budget); decoded.first=number(doc,command,4); decoded.loop=text(doc,command,5,budget);
                    if (decoded.interpolation.size()<2 || decoded.loop.empty()) fail("Incomplete MOTIONSET mode characters.");
                } else if (decoded.opcode==ActionOpcode::joint) {
                    decoded.first=number(doc,command,3); decoded.second=number(doc,command,4); decoded.loop=text(doc,command,5,budget);
                    if (decoded.loop.empty()) fail("Incomplete JOINTSET loop character.");
                } else if (decoded.opcode==ActionOpcode::fade) {
                    decoded.first=number(doc,command,3); decoded.second=number(doc,command,4);
                } else if (decoded.opcode==ActionOpcode::wait) decoded.first=number(doc,command,3);
                value.commands.push_back(std::move(decoded));
            }
            result.actions_.push_back(std::move(value));
        }
        return result;
    } catch (const ActionError&) { throw; }
      catch (const std::exception& error) { throw ActionError(error.what()); }
}

ActionRuntime::ActionRuntime(ActionProgram program,const AnimationSourceRuntime& sources,std::int32_t ram_no,AnimationCombiner combiner)
    :program_(std::make_shared<const ActionProgram>(std::move(program))),ram_no_(ram_no),combiner_(std::move(combiner)) {
    if (ram_no<0 || static_cast<std::size_t>(ram_no)>=program_->rams().size()) fail("Action RAM number is outside the owning info.");
    if (!sources.action_offset_initialized()) fail("Action runtime requires the original manager's source-offset initialization.");
    if (program_->source()!=sources.source() || program_->animations().size()!=sources.clips().size()) fail("Action animation library qualification differs.");
    for (std::size_t i=0;i<sources.clips().size();++i)
        if (program_->animations()[i]!=std::pair{sources.clips()[i].info.reference_index,sources.clips()[i].info.name})
            fail("Action animation library differs from database order or identity.");
    if (!combiner_.states().empty()) fail("Action activation requires a prepared empty combiner.");
    auto copy=sources; combiner_.update(copy,0); // Validate shared library identity before accepting the runtime.
    node_=program_->rams()[ram_no].node_clock;
}
ActionRuntime decode_action_runtime(const IgbDocument& doc,ActionProgram program,const AnimationSourceRuntime& sources,std::int32_t ram_no) {
    if (ram_no<0 || static_cast<std::size_t>(ram_no)>=program.rams().size()) fail("Action RAM selection is out of range.");
    auto combiner=decode_animation_combiner(doc,sources,program.rams()[ram_no].combiner_reference);
    return {std::move(program),sources,ram_no,std::move(combiner)};
}
std::uint64_t ActionRuntime::node_time(std::uint64_t time) const { return dw4::node_time(node_,time); }
std::string_view ActionRuntime::current_action() const noexcept { return current_ ? program_->actions()[*current_].name : std::string_view{}; }
bool ActionRuntime::command_end() const noexcept {
    return current_ && work_.wait_state==0 && cursor_==program_->actions()[*current_].commands.size();
}
ActionResult ActionRuntime::deliver(ActionServices& services,ActionResult result) {
    if (!pending_broadcast_) return result;
    if (!services.broadcast) { result.stop=ActionStop::broadcast_required; return result; }
    services.broadcast(*pending_broadcast_); pending_broadcast_.reset(); return result;
}
ActionResult ActionRuntime::status() const {
    if (suspended_) return {*suspended_,0,program_->actions()[*current_].commands[cursor_].reference};
    return {command_end() || !active_ ? ActionStop::end : ActionStop::waiting,0,{}};
}
ActionResult ActionRuntime::finish_dispatch(ActionServices& services,ActionResult result) {
    if (dispatch_pending_ && (result.stop==ActionStop::end || result.stop==ActionStop::waiting)) {
        pending_broadcast_=ActionBroadcast{program_->source(),program_->name(),std::string(current_action()),ram_no_};
        dispatch_pending_=false;
    }
    return deliver(services,result);
}
ActionResult ActionRuntime::dispatch(AnimationSourceRuntime& sources,ActionServices& services,std::string_view name,std::uint64_t time) {
    name_check(name);
    combiner_.require_sources(sources);
    if (name=="ONFREEZE" || name=="OFFREEZE") { freeze(name=="ONFREEZE",time); return {}; }
    if (reserved(name)) fail("This ACTION query/deactivation requires the outer actor service.");
    Running guard(running_);
    if (pending_broadcast_) {
        const auto result=deliver(services,{}); if (result.stop==ActionStop::broadcast_required) return result;
    }
    const auto& definitions=program_->actions();
    const auto found=std::find_if(definitions.begin(),definitions.end(),[&](const auto& value){return value.name==name;});
    if (found==definitions.end()) fail("Requested action is absent from the owning program.");
    if (!found->command_list_present) fail("Ordinary action execution requires a non-null command list.");
    const auto index=static_cast<std::size_t>(found-definitions.begin());
    if (!active_) {
        auto next=combiner_; next.set_request_time(100000000);
        next.add_base(sources,program_->animations().front().first,next.request_time(time)); combiner_=std::move(next);
        active_=true; work_.motion.state=0; work_.fade_state=0; work_.wait_state=0; work_.motion.no_interpolation=true;
    } else work_.motion.no_interpolation=false;
    visible_=true;
    // Original lookup identity, including repeated references under alias occurrences.
    if (current_ && definitions[*current_].reference==found->reference)
        return status();
    current_=index; cursor_=0; suspended_.reset(); dispatch_pending_=true;
    return finish_dispatch(services,run(sources,services,time));
}
ActionResult ActionRuntime::dispatch(AnimationSourceRuntime& sources,ActionServices& services,const ModelActionRequest& request,std::uint64_t time) {
    if (request.source!=program_->source() || request.info_name!=program_->name() || request.ram_no!=ram_no_)
        fail("Model ACTION recipient differs from this source/info/RAM.");
    return dispatch(sources,services,request.action,time);
}
ActionResult ActionRuntime::run(AnimationSourceRuntime& sources,ActionServices& services,std::uint64_t time) {
    ActionResult result;
    if (!current_) return result;
    const auto& commands=program_->actions()[*current_].commands;
    while (cursor_<commands.size()) {
        const auto& value=commands[cursor_]; result.command_reference=value.reference;
        if (value.opcode==ActionOpcode::unsupported) { suspended_=result.stop=ActionStop::unsupported_command; return result; }
        if (value.opcode==ActionOpcode::wait && first(value.name)=='R' &&
            !(value.name.size()>3 && value.name[3]=='S') && !services.random_word) {
            suspended_=result.stop=ActionStop::random_required; return result;
        }
        suspended_=ActionStop::command_failed;
        auto candidate=*this; auto candidate_sources=sources;
        const bool keep_going=candidate.command(candidate_sources,services,value,time);
        candidate.suspended_.reset();
        ++candidate.cursor_; *this=std::move(candidate); sources=std::move(candidate_sources); ++result.commands_executed;
        if (!keep_going) { result.stop=ActionStop::waiting; return result; }
    }
    result.command_reference.reset(); return result;
}
bool ActionRuntime::command(AnimationSourceRuntime& sources,ActionServices& services,const ActionCommand& value,std::uint64_t time) {
    switch (value.opcode) {
    case ActionOpcode::motion: {
        const auto& animations=program_->animations();
        const auto found=std::find_if(animations.begin(),animations.end(),[&](const auto& item){return item.second==value.name;});
        if (found==animations.end()) fail("MOTIONSET animation is absent from the owning database.");
        apply_animation_motion(combiner_,sources,work_.motion,{found->first,value.first,value.interpolation,value.loop},time,node_time(time)); return true;
    }
    case ActionOpcode::joint:
        work_.joint_state=first(value.loop)=='L' ? 1 : 2;
        work_.joint_start=frame_time(value.first); work_.joint_end=frame_time(value.second); work_.joint_ended=false;
        node_origin(node_,time,work_.joint_start); return true;
    case ActionOpcode::fade: {
        if (first(value.name)=='H') { visible_=false; return true; }
        if (!appearance_.color) appearance_.color=ActionColor{};
        auto channels=unpack(*appearance_.color);
        const bool incoming=first(value.name)=='I'; channels[3]=incoming ? 0 : value.second; color(appearance_,channels);
        appearance_.blend=true; appearance_.lighting=false;
        work_.fade_value=channels[3]; work_.fade_limit=value.second; work_.fade_add=value.second/value.first; work_.fade_state=incoming ? 1 : 2;
        start_motion_timer(timers_["FADE"],time); return true;
    }
    case ActionOpcode::wait: {
        auto frames=value.first;
        switch (first(value.name)) {
        case 'R': {
            if (value.name.size()>3 && value.name[3]=='S') { services.random_lower=frames; return true; }
            finite(services.random_lower);
            const float low=100.0f*services.random_lower;
            const float width=std::fma(100.0f,frames,-low);
            if (!std::isfinite(width) || width< -0x1p31f || width>=0x1p31f) fail("Random wait width requires unverified overflowing integer conversion.");
            const auto divisor=static_cast<std::uint32_t>(static_cast<std::int32_t>(width));
            if (divisor==0) fail("Random wait has an undefined zero divisor.");
            const auto sample=services.random_word();
            const float fraction=static_cast<float>(sample%divisor)/100.0f;
            frames=services.random_lower+fraction; finite(frames); [[fallthrough]];
        }
        case 'F':start_motion_timer(timers_["WAIT"],time,frame_time(frames)); work_.wait_state=1; break;
        case 'M':work_.wait_state=2; break;
        case 'J':work_.wait_state=3; break;
        default:break; // Original yields without installing a new wait state.
        }
        return false;
    }
    case ActionOpcode::flash: {
        if (!appearance_.color) appearance_.color=ActionColor{};
        auto channels=unpack(*appearance_.color); channels[0]=channels[1]=1; color(appearance_,channels);
        appearance_.texture=false; appearance_.lighting=false; work_.flash_state=1;
        start_motion_timer(timers_["FLASH"],time,0x03f940ac); return true;
    }
    case ActionOpcode::unsupported:fail("Unsupported action command reached execution.");
    }
    fail("Invalid action opcode.");
}
void ActionRuntime::joint_update(std::uint64_t time) {
    if (work_.joint_state!=1 && work_.joint_state!=2) return;
    auto local=signed_bits(node_time(time)); bool changed=false;
    for (std::size_t count=0;local>work_.joint_end;++count) {
        if (count==max_commands) fail("Original JOINTSET wrap loop exhausted its work budget.");
        local=work_.joint_state==1 ? signed_bits(bits(local)-bits(work_.joint_end)+bits(work_.joint_start)) : work_.joint_end;
        changed=true;
    }
    if (changed) { node_origin(node_,time,local); work_.joint_ended=true; }
}
void ActionRuntime::fade_update(std::uint64_t time) {
    if (work_.fade_state!=1 && work_.fade_state!=2) return;
    const auto timer=timers_.find("FADE");
    if (timer==timers_.end()) fail("Active fade has no named elapsed timer.");
    const float frames=motion_timer_frames(timer->second,time);
    if (frames==0) return;
    if (!appearance_.color) fail("Active fade has no owned color attribute.");
    auto channels=unpack(*appearance_.color);
    if (work_.fade_state==1) channels[3]=std::fma(frames,work_.fade_add,work_.fade_value);
    else channels[3]=std::max(std::fma(-frames,work_.fade_add,work_.fade_value),0.0f);
    color(appearance_,channels); work_.fade_value=channels[3];
    if (work_.fade_state==1 ? channels[3]<work_.fade_limit : channels[3]>0) return;
    if (work_.fade_state==1) {
        if (!appearance_.texture) { appearance_.color.reset(); appearance_.lighting.reset(); }
        appearance_.blend.reset();
    }
    timers_.erase("FADE"); work_.fade_state=3;
}
void ActionRuntime::flash_update(std::uint64_t time) {
    if (work_.flash_state!=1) return;
    const auto timer=timers_.find("FLASH");
    if (timer!=timers_.end() && !motion_timer_expired(timer->second,time)) return;
    if (!appearance_.blend) { appearance_.color.reset(); appearance_.lighting.reset(); }
    appearance_.texture.reset(); timers_.erase("FLASH"); work_.flash_state=0;
}
bool ActionRuntime::wait_ready(AnimationSourceRuntime& sources,std::uint64_t time) {
    bool ready=false;
    if (work_.wait_state==1) {
        const auto timer=timers_.find("WAIT"); ready=timer==timers_.end() || motion_timer_expired(timer->second,time);
    } else if (work_.wait_state==2 && combiner_.states().size()==1) {
        const auto ref=combiner_.states()[0].clip_reference;
        if (!ref) fail("Motion wait cannot read an animation duration from a fixed snapshot.");
        const auto duration=sources.clip(*ref).info.duration_ticks;
        const auto local=combiner_.advance_state_clock(sources,0,node_time(time));
        ready=bits(local)>=bits(duration); // Original unsigned full-width comparison, including negative local times.
    } else if (work_.wait_state==3) ready=work_.joint_ended;
    if (ready) work_.wait_state=0;
    return ready;
}
ActionResult ActionRuntime::update(AnimationSourceRuntime& sources,ActionServices& services,std::uint64_t time) {
    Running guard(running_);
    combiner_.require_sources(sources);
    auto result=deliver(services,{}); if (result.stop==ActionStop::broadcast_required) return result;
    if (!active_ || work_.frozen || suspended_) return status();
    auto candidate=*this; auto candidate_sources=sources;
    advance_animation_motion(candidate.combiner_,candidate_sources,candidate.work_.motion,candidate.node_time(time),candidate.node_.time_scale);
    candidate.joint_update(time); candidate.fade_update(time);
    const bool resume=candidate.wait_ready(candidate_sources,time);
    *this=std::move(candidate); sources=std::move(candidate_sources);
    if (resume) result=run(sources,services,time);
    else result.stop=command_end() ? ActionStop::end : ActionStop::waiting;
    if (result.stop==ActionStop::unsupported_command || result.stop==ActionStop::random_required) return result;
    auto next=*this; next.flash_update(time); *this=std::move(next); return result;
}
ActionResult ActionRuntime::resume(AnimationSourceRuntime& sources,ActionServices& services,std::uint64_t time) {
    Running guard(running_); combiner_.require_sources(sources);
    const auto delivered=deliver(services,status());
    if (delivered.stop==ActionStop::broadcast_required || !suspended_ || work_.frozen) return delivered;
    return finish_dispatch(services,run(sources,services,time));
}
void ActionRuntime::freeze(bool frozen,std::uint64_t time) {
    Running guard(running_); if (!active_) return;
    auto candidate=*this; const float speed=frozen ? 0.0f : 1.0f;
    for (auto& [name,timer]:candidate.timers_) { (void)name; change_motion_timer_speed(timer,time,speed); }
    change_animation_speed(candidate.node_,time,speed); candidate.work_.frozen=frozen; *this=std::move(candidate);
}
SkinPose ActionRuntime::pose(AnimationSourceRuntime& sources,const SkinningData& skin,std::uint64_t time) {
    Running guard(running_); return combiner_.pose(sources,skin,node_time(time));
}
} // namespace dw4
