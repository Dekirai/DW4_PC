#include "dw4/model_status_update.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_dispatch.hpp"
#include "dw4/model_timer.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw ModelSceneError(message);}
constexpr std::array<std::string_view,12> actors={
    "i_stdownic01","i_stdownic02","i_stdownic03","i_stdownic04","i_stdownic05","i_stdownic09",
    "i_stdownic10","i_stdownic06","i_stdownic07","i_stdownic08","i_rareicon","i_stdownic00"};
constexpr std::array<std::string_view,10> damage_sequences={
    "SQ_SDMG","SQ_MDMG","SQ_LDMG","SQ_DDMG","SQ_DEAD",
    "SQ_SDMGC","SQ_SDMGF","SQ_SDMGI","SQ_SDMGT","SQ_SDMGD"};
constexpr std::array<std::string_view,12> icon_sequences={
    "SQ_OPEN01","SQ_OPEN02","SQ_OPEN03","SQ_OPEN04","SQ_OPEN05","SQ_OPEN06",
    "SQ_LOOP01","SQ_LOOP02","SQ_LOOP03","SQ_LOOP04","SQ_LOOP05","SQ_LOOP06"};
// These are adjacent physical tables at 804EDD58/70. Original indexed reads
// do not impose a six-position range; retain the represented adjacent words.
std::optional<std::string> icon_sequence(std::int32_t position,bool loop) {
    const auto index=static_cast<std::int64_t>(position)+(loop ? 6:0);
    if(index<0 || index>43)fail("Status icon sequence index exceeds the represented original constant backing.");
    if(index<12)return std::string(icon_sequences[static_cast<std::size_t>(index)]);
    if(index==13 || index==43)fail("Status icon sequence reached the original invalid all-ones string pointer.");
    return std::nullopt; // 804EDD88=0, 804EDD90..804EDDFC=0.
}
template<class T> const std::shared_ptr<std::vector<T>>& require_list(const std::shared_ptr<std::vector<T>>& value) {
    if(!value)fail("Status update reached a null status-list reference.");return value;
}
template<class T> T& item(const std::shared_ptr<std::vector<T>>& value,std::size_t index) {
    const auto& owner=require_list(value);
    if(index>=owner->size())fail("Status update reached unavailable physical list backing.");
    return (*owner)[index];
}
template<class T> void guarded_set(const std::shared_ptr<std::vector<T>>& value,std::size_t index,T data) {
    if(require_list(value)->size()>index)(*value)[index]=data;
}
std::int32_t add(std::int32_t a,std::int32_t b) {
    return std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(a)+std::bit_cast<std::uint32_t>(b));
}
std::int64_t duration(std::int32_t frames) {return (static_cast<std::int64_t>(frames)*1000000000)/60;}
std::shared_ptr<GameDataInt> hp(const ModelPlayerWork& work) {
    if(!work.script.hp)fail("Status update reached its captured work's null HP wrapper.");return work.script.hp;
}
const std::string& sequence(const ModelPlayerWork& work) {
    if(!work.sequence.name)fail("Status update reached a null sequence name.");return *work.sequence.name;
}
std::shared_ptr<ModelSceneObject>& data_slot(const std::shared_ptr<ModelSceneList>& list,std::int32_t index) {
    if(!list || index<0 || !list->backing || static_cast<std::size_t>(index)>=list->backing->size())
        fail("Named status-timer removal reached unavailable backing.");
    return (*list->backing)[static_cast<std::size_t>(index)];
}
void remove_named(ModelSceneRam& ram,std::string_view name) {
    if(!ram.data)return;
    const auto found=find_model_named(ram.data,name);if(!found)return;
    const auto searched=ram.data;const auto count=searched->count;std::int32_t index{};
    for(;index<count;++index)if(data_slot(searched,index)==found)break;
    if(index>=count)index=-1;
    const auto destination=ram.data;data_slot(destination,index).reset();
    const auto live_count=destination->count;
    if(live_count>0 && index>=0 && index<live_count) {
        for(auto next=index;next<live_count-1;++next)data_slot(destination,next)=data_slot(destination,next+1);
        destination->count=live_count-1;
    }
    data_slot(destination,destination->count).reset();
}
struct TimedStatus {std::size_t index;std::string_view name;bool up;std::size_t first,count;};
constexpr std::array<TimedStatus,8> timed={{
    {8,"SLEEP",false,0,0},{9,"STUN",false,0,0},{3,"BIND",false,0,0},
    {1,"ATKDOWN",false,0,4},{2,"POWDOWN",false,4,2},
    {5,"SPEEDUP",true,0,4},{6,"POWERUP",true,4,3},{4,"REFRACT",true,7,4}}};
}

struct ModelStatusUpdateInvocation::Impl {
    enum class Phase {capture,gate,dead_next,dead_clear,live,poison,poison_first,poison_check,
        poison_damage,poison_meter,poison_number,poison_hp,poison_remove,poison_remaining,
        timed_gate,timed_first,timed_after_start,timed_check,timed_expired,timed_clear,timed_stats,
        timed_after_direct,timed_bind,metal_gate,metal_first,metal_check,metal_query,metal_result,
        refresh,refresh_recalc,refresh_hp,refresh_restore,icons,icon_capture,icon_find,icon_resolve,
        icon_generate,icon_generated,icon_existing,icon_move,icon_publish,icon_removed,
        message_start,message_send,direct,completed};
    struct Message {
        std::string category;std::optional<std::string> name;std::int32_t number{};
        std::optional<std::string> command;
        std::function<void(MessengerBuffer&)> arguments;
    };
    explicit Impl(std::shared_ptr<ModelSceneRam> value,std::shared_ptr<ModelRamTimerAccess> access)
        :ram(std::move(value)),timer(std::move(access)) {
        if(!ram)fail("Status update requires its actual RAM owner.");
        if(!timer)timer=std::make_shared<ModelRamTimerAccess>(ram);
        if(timer->ram_owner()!=ram)fail("Status update timer endpoint belongs to another actual RAM.");
    }
    std::shared_ptr<ModelSceneRam> ram,icon_ram;
    std::shared_ptr<ModelSceneOne> one;
    std::shared_ptr<ModelPlayerWork> work;
    std::shared_ptr<ModelRamTimerAccess> timer;
    std::shared_ptr<ModelStatusUpdateServices> services;
    NativeMessenger* messenger{};ActionEventRuntime* events{};
    std::optional<MessengerDispatch> dispatch;
    std::optional<Message> message;
    std::unique_ptr<ModelBaseInvocation> direct;
    Phase phase=Phase::capture,after_message{},after_direct{},after_refresh{};
    std::size_t index{},timed_index{},icon_index{};
    std::int32_t captured_frames{},icon_number{},icon_position{},next_position{},icon_result{};
    bool icon_reposition{},any_icon{},executing{},fault{};
    std::array<float,3> icon_point{};
    std::optional<ControllerObjectId> icon_id;
    void send(std::string category,std::optional<std::string> name,std::int32_t number,
        std::optional<std::string> command,Phase next,std::function<void(MessengerBuffer&)> arguments={}) {
        message=Message{std::move(category),std::move(name),number,std::move(command),std::move(arguments)};
        after_message=next;phase=Phase::message_start;
    }
    void enter(std::string command,Phase next) {
        direct=std::make_unique<ModelBaseInvocation>(ram,std::move(command));after_direct=next;phase=Phase::direct;
    }
    void next_timed() {++timed_index;phase=Phase::timed_gate;}
    void refresh(Phase next) {after_refresh=next;phase=Phase::refresh;}
    void check() const {if(fault)fail("Caught recursive status update invalidated the active invocation.");}
};

ModelStatusUpdateInvocation::ModelStatusUpdateInvocation(std::shared_ptr<ModelSceneRam> ram,
    std::shared_ptr<ModelRamTimerAccess> timer):impl_(std::make_unique<Impl>(std::move(ram),std::move(timer))) {}
ModelStatusUpdateInvocation::~ModelStatusUpdateInvocation()=default;
bool ModelStatusUpdateInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::completed;}
bool ModelStatusUpdateInvocation::faulted() const noexcept {return impl_->fault;}
const std::shared_ptr<ModelSceneOne>& ModelStatusUpdateInvocation::captured_one() const {return impl_->one;}
const std::shared_ptr<ModelPlayerWork>& ModelStatusUpdateInvocation::captured_work() const {return impl_->work;}
const ModelBaseInvocation* ModelStatusUpdateInvocation::direct_entry() const noexcept {return impl_->direct.get();}

MessengerCallResult ModelStatusUpdateInvocation::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    const ModelPlayerCreateServices& creation,std::shared_ptr<ModelStatusUpdateServices> services) {
    events.require_model_execution_access();
    auto& s=*impl_;using P=Impl::Phase;
    if(s.fault)fail("A faulted status update cannot replay its published prefix.");
    if(s.executing || (s.messenger && (s.messenger!=&messenger || s.events!=&events)) ||
        (s.services && services && s.services!=services)) {
        s.fault=true;fail("Status update requires the same nonrecursive execution and service owners.");
    }
    if(completed())return {true,0,{}};
    if(!s.services && services)s.services=std::move(services);
    s.messenger=&messenger;s.events=&events;
    struct Guard {bool& flag;explicit Guard(bool& f):flag(f){flag=true;}~Guard(){flag=false;}} guard(s.executing);
    try {
        for(;;)switch(s.phase) {
        case P::capture:
            s.one=find_model_one(*s.ram);
            if(!s.one)fail("Status update reached a missing or wrong first named ONE.");
            s.work=s.one->work;if(!s.work)fail("Status update reached a null actual work.");
            s.phase=P::gate;break;
        case P::gate:
            if(!s.work->status.timers){s.phase=P::completed;break;}
            s.phase=hp(*s.work)->data==0 ? P::dead_next:P::live;break;
        case P::dead_next:
            if(s.index==12){s.phase=P::dead_clear;break;}
            s.icon_number=item(s.work->status.display_ram_numbers,s.index);
            if(s.icon_number==-1){++s.index;break;}
            s.send("MODELCTRL",std::string(actors[s.index++]),s.icon_number,"SQ_ERAS",P::dead_next);break;
        case P::dead_clear: {
            const auto timers=require_list(s.work->status.timers);std::fill(timers->begin(),timers->end(),0);
            const auto displays=require_list(s.work->status.display_ram_numbers);std::fill(displays->begin(),displays->end(),-1);
            s.phase=P::completed;break;
        }
        case P::live:
            if(!s.work->messenger)s.work->messenger=&messenger;
            s.phase=P::poison;break;
        case P::poison:
            if(!s.work->sequence.wait.control_enabled || item(s.work->status.timers,0)==0){s.phase=P::timed_gate;break;}
            s.phase=item(s.work->status.first_flags,0)!=0 ? P::poison_first:P::poison_check;break;
        case P::poison_first:
            s.timer->start("POISON",2000000000);s.check();
            {const auto value=add(item(s.work->status.timers,0),-1);guarded_set(s.work->status.timers,0,value);}
            guarded_set(s.work->status.first_flags,0,std::uint8_t{0});s.phase=P::poison_check;break;
        case P::poison_check:
            s.phase=s.timer->expired("POISON") ? P::poison_damage:P::timed_gate;s.check();break;
        case P::poison_damage: {
            const auto owner=hp(*s.work);const auto difference=add(owner->data,
                std::bit_cast<std::int32_t>(std::uint32_t{0}-std::bit_cast<std::uint32_t>(s.work->status.poison_damage)));
            owner->data=difference<0 ? 0:difference;
            s.phase=s.work->status.is_player ? P::poison_meter:P::poison_hp;break;
        }
        case P::poison_meter:
            s.send("METERCTRL","hpmt",s.ram->number,"SETPERCENT",P::poison_number,[&](MessengerBuffer& packet) {
                const auto current=hp(*s.work)->data;
                if(!s.work->script.max_hp)fail("Poison meter reached the null max-HP wrapper after selection.");
                const float numerator=100.0f*static_cast<float>(current);
                packet.add_float(numerator/static_cast<float>(s.work->script.max_hp->data));
            });break;
        case P::poison_number:
            s.send("NUMBERCTRL","hpnm",s.ram->number,"SETVALUE",P::poison_hp,
                [&](MessengerBuffer& packet){packet.add_int(hp(*s.work)->data);});break;
        case P::poison_hp:
            if(hp(*s.work)->data<=0) {
                if(s.work->status.is_player)s.enter("SQ_DEAD",P::poison_remove);
                else {hp(*s.work)->data=1;s.phase=P::poison_remove;}
            }else s.phase=P::poison_remaining;
            break;
        case P::poison_remove:
            remove_named(*s.ram,"POISON");guarded_set(s.work->status.timers,0,std::int32_t{0});
            s.phase=P::timed_gate;break;
        case P::poison_remaining: {
            const auto value=add(item(s.work->status.timers,0),-1);guarded_set(s.work->status.timers,0,value);
            if(item(s.work->status.timers,0)<=0)s.phase=P::poison_remove;
            else {s.timer->start("POISON",2000000000);s.check();s.phase=P::timed_gate;}
            break;
        }
        case P::timed_gate: {
            if(s.timed_index==timed.size()){s.phase=P::metal_gate;break;}
            const auto& t=timed[s.timed_index];
            if(!s.work->sequence.wait.control_enabled){s.next_timed();break;}
            s.captured_frames=item(s.work->status.timers,t.index);
            if(s.captured_frames==0 || (t.index==9 && sequence(*s.work)!="SQ_STUN")){s.next_timed();break;}
            s.phase=item(s.work->status.first_flags,t.index)!=0 ? P::timed_first:P::timed_check;break;
        }
        case P::timed_first: {
            const auto& t=timed[s.timed_index];
            // STUN reloads the duration after its first-flag read; the other
            // duration branches retain the earlier r9 value.
            const auto frames=t.index==9 ? item(s.work->status.timers,9):s.captured_frames;
            s.timer->start(t.name,duration(frames));s.check();s.phase=P::timed_after_start;break;
        }
        case P::timed_after_start:
            if(timed[s.timed_index].index==8 && sequence(*s.work)!="SQ_SLEP")s.enter("SQ_SLEP",P::timed_after_direct);
            else s.phase=P::timed_after_direct;
            break;
        case P::timed_after_direct:
            guarded_set(s.work->status.first_flags,timed[s.timed_index].index,std::uint8_t{0});
            s.phase=P::timed_check;break;
        case P::timed_check:
            if(s.timer->expired(timed[s.timed_index].name))s.phase=P::timed_expired;
            else if(timed[s.timed_index].index==3)s.phase=P::timed_bind;
            else s.next_timed();
            s.check();break;
        case P::timed_expired:
            if(timed[s.timed_index].index==8)s.enter("SQ_WAIT",P::timed_clear);
            else s.phase=P::timed_clear;
            break;
        case P::timed_clear: {
            const auto& t=timed[s.timed_index];
            if(t.index==9) {
                guarded_set(s.work->status.timers,9,std::int32_t{0});remove_named(*s.ram,t.name);
            }else {remove_named(*s.ram,t.name);guarded_set(s.work->status.timers,t.index,std::int32_t{0});}
            if(t.index==9 || t.index==3){++s.timed_index;s.enter("SQ_WAIT",P::timed_gate);}
            else if(t.count)s.phase=P::timed_stats;
            else s.next_timed();
            break;
        }
        case P::timed_stats: {
            const auto& t=timed[s.timed_index];
            for(auto at=t.first;at<t.first+t.count;++at)
                guarded_set(t.up ? s.work->status.up_data:s.work->status.down_data,at,std::int32_t{0});
            ++s.timed_index;s.refresh(P::timed_gate);break;
        }
        case P::timed_bind:
            if(std::find(damage_sequences.begin(),damage_sequences.end(),sequence(*s.work))==damage_sequences.end()) {
                const auto list=s.work->control.movement;
                if(!list)fail("BIND reached a null movement list.");
                const auto count=list->size();
                for(std::size_t at=0;at<count;++at)if(at!=1) {
                    const auto channel=list->entry_owner(at);
                    if(!channel)fail("BIND reached a null physical movement entry after preceding stops.");
                    channel->active=false;channel->speed=0;
                }
            }
            s.next_timed();break;
        case P::metal_gate:
            if(!s.work->sequence.wait.control_enabled || item(s.work->status.timers,7)==0){s.phase=P::icons;break;}
            s.phase=item(s.work->status.first_flags,7)!=0 ? P::metal_first:P::metal_check;break;
        case P::metal_first:
            guarded_set(s.work->status.down_data,6,std::int32_t{0});
            s.timer->start("METAL");s.check();
            guarded_set(s.work->status.first_flags,7,std::uint8_t{0});s.phase=P::metal_check;break;
        case P::metal_check:
            s.phase=s.timer->expired("METAL") ? P::metal_query:P::icons;s.check();break;
        case P::metal_query: {
            const auto value=add(item(s.work->status.down_data,6),4);guarded_set(s.work->status.down_data,6,value);
            s.send("GAMERAM",std::nullopt,-1,"GETPLAYER",P::metal_result,
                [&](MessengerBuffer& packet){packet.add_int(s.ram->number);});break;
        }
        case P::metal_result: {
            const auto& objects=messenger.buffer().objects();
            const auto* row=objects.empty() ? nullptr:std::get_if<MessengerDataList>(&objects[0]);
            if(!row || !*row)fail("METAL GETPLAYER requires its actual data row at object zero.");
            const auto delay=(*row)->require<GameDataInt>("ATTACKDELAY")->data;
            const auto frames=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(delay)*120u);
            s.timer->start("METAL",duration(frames));s.check();s.refresh(P::icons);break;
        }
        case P::refresh:
            if(!s.work->status.is_player){s.phase=s.after_refresh;break;}
            messenger.push_buffer();s.phase=P::refresh_recalc;break;
        case P::refresh_recalc:
            s.send("GAMERAM",std::nullopt,-1,"RECALCPLAYER",P::refresh_hp,
                [&](MessengerBuffer& packet){packet.add_int(s.ram->number);});break;
        case P::refresh_hp:
            s.send("GAMERAM",std::nullopt,-1,"ADDHP",P::refresh_restore,
                [&](MessengerBuffer& packet){packet.add_int(s.ram->number);packet.add_int(0);});break;
        case P::refresh_restore:
            messenger.pop_buffer();s.phase=s.after_refresh;break;
        case P::icons: {
            if(s.icon_index==12){s.phase=P::completed;break;}
            if(s.icon_index==11)guarded_set(s.work->status.timers,11,std::int32_t{s.any_icon ? 1:0});
            const auto active=item(s.work->status.timers,s.icon_index);
            if(active==0) {
                s.icon_number=item(s.work->status.display_ram_numbers,s.icon_index);
                if(s.icon_number==-1){++s.icon_index;break;}
                s.send("MODELCTRL",std::string(actors[s.icon_index]),s.icon_number,"SQ_ERAS",P::icon_removed);break;
            }
            s.icon_position=0;s.icon_reposition=false;
            if(s.icon_index<8) {
                const auto previous=item(s.work->status.display_positions,s.icon_index);
                s.icon_position=s.next_position;
                if(s.next_position!=previous) {
                    guarded_set(s.work->status.display_positions,s.icon_index,s.next_position);s.icon_reposition=true;
                }
            }else if(s.icon_index==10)s.icon_position=add(active,-1);
            s.icon_number=item(s.work->status.display_ram_numbers,s.icon_index);
            s.phase=P::icon_capture;break;
        }
        case P::icon_capture: {
            // 803ACB30 independently resolves ONE/node even for an existing
            // icon that will require no allocation or position change.
            const auto one=find_model_one(*s.ram);
            if(!one || !one->node)fail("Status icon helper reached a missing current ONE node.");
            const auto matrix=one->node->matrix_owner();
            s.icon_point={(*matrix)[12],(*matrix)[13],(*matrix)[14]};
            s.phase=s.icon_number==-1 ? P::icon_find:P::icon_existing;break;
        }
        case P::icon_find:
            s.send("MODELCTRL",std::string(actors[s.icon_index]),-1,"FIND_SPACE",P::icon_resolve);break;
        case P::icon_resolve: {
            if(!s.icon_id) {
                const auto& objects=messenger.buffer().objects();
                if(objects.empty()){s.icon_result=-1;s.phase=P::icon_publish;break;}
                const auto* id=std::get_if<ControllerObjectId>(&objects[0]);
                if(!id)fail("Status icon FIND_SPACE object zero is not an actual controller RAM identity.");
                s.icon_id=*id;
            }
            if(!s.services || !s.services->resolve_ram)
                return {false,0,"Status icon FIND_SPACE requires the actual returned RAM owner resolver."};
            s.icon_ram=s.services->resolve_ram(*s.icon_id);s.check();
            if(!s.icon_ram)return {false,0,"Status icon FIND_SPACE RAM owner is not available yet."};
            s.phase=P::icon_generate;break;
        }
        case P::icon_generate:
            s.send("MODELCTRL",std::string(actors[s.icon_index]),s.icon_ram->number,"GENERATE",P::icon_generated,
                [&](MessengerBuffer& packet){packet.add_string(icon_sequence(s.icon_position,false));packet.add_vector(s.icon_point);});break;
        case P::icon_generated:
            s.icon_result=s.icon_ram->number;s.phase=P::icon_publish;break;
        case P::icon_existing:
            if(s.icon_reposition)s.send("MODELCTRL",std::string(actors[s.icon_index]),s.icon_number,
                icon_sequence(s.icon_position,true),P::icon_move);
            else s.phase=P::icon_move;
            break;
        case P::icon_move:
            s.send("MODELCTRL",std::string(actors[s.icon_index]),s.icon_number,"MOVETRANS",P::icon_publish,
                [&](MessengerBuffer& packet){packet.add_vector(s.icon_point);packet.add_vector({0,0,0});});
            // Existing return uses captured r28, not the current target's number.
            s.icon_result=s.icon_number;break;
        case P::icon_publish:
            if(s.icon_index<8)++s.next_position;
            guarded_set(s.work->status.display_ram_numbers,s.icon_index,s.icon_result);
            if(s.icon_index<8)s.any_icon=true;
            s.icon_ram.reset();s.icon_id.reset();++s.icon_index;s.phase=P::icons;break;
        case P::icon_removed:
            guarded_set(s.work->status.display_ram_numbers,s.icon_index,std::int32_t{-1});
            ++s.icon_index;s.phase=P::icons;break;
        case P::message_start:
            if(!messenger.has_category(s.message->category))
                return {false,0,"Status update requires the actual "+s.message->category+" manager."};
            messenger.select(s.message->category,s.message->name,s.message->number);
            if(s.message->arguments)s.message->arguments(messenger.buffer());
            s.check();
            if(!s.message->command)fail("Status icon reached a null command outside the native messenger string profile.");
            s.dispatch.emplace(messenger.begin_send(*s.message->command));s.phase=P::message_send;break;
        case P::message_send: {
            const auto result=events.resume(*s.dispatch);s.check();
            if(result.stop==MessengerStop::faulted)fail("Status update native message delivery faulted.");
            if(result.stop!=MessengerStop::completed)return {false,0,result.diagnostic};
            s.dispatch.reset();s.message.reset();s.phase=s.after_message;break;
        }
        case P::direct: {
            const auto result=s.direct->resume(messenger,events,creation);s.check();
            if(!result.completed)return result;s.direct.reset();s.phase=s.after_direct;break;
        }
        case P::completed:return {true,0,{}};
        }
    }catch(...){s.fault=true;throw;}
}
} // namespace dw4
