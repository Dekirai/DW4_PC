#include "dw4/model_player_sequence.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_dispatch.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw ModelSceneError(reason);}
constexpr std::array<std::string_view,7> panel_sequences={
    "SQ_SDMG","SQ_MDMG","SQ_LDMG","SQ_DDMG","SQ_DEAD","SQ_SLEP","SQ_STUN"};
constexpr std::array<std::string_view,12> status_actors={
    "i_stdownic01","i_stdownic02","i_stdownic03","i_stdownic04","i_stdownic05","i_stdownic09",
    "i_stdownic10","i_stdownic06","i_stdownic07","i_stdownic08","i_rareicon","i_stdownic00"};
const std::string& sequence_name(const ModelPlayerWork& work) {
    if(!work.sequence.name)fail("Player sequence callback reached a null sequence string.");
    if(work.sequence.name->size()>65535 || work.sequence.name->find('\0')!=std::string::npos)
        fail("Player sequence callback requires its bounded original string profile.");
    return *work.sequence.name;
}
std::shared_ptr<ModelPlayerWork> current_work(ModelSceneRam& ram) {
    const auto one=find_model_one(ram);
    if(!one)fail("Player sequence callback reached a missing actual ONE record.");
    if(!one->work)fail("Player sequence callback reached a null actual work record.");
    return one->work;
}
template<class T> const std::shared_ptr<std::vector<T>>& list(const std::shared_ptr<std::vector<T>>& value) {
    if(!value)fail("Player status reconciliation reached a null status list.");
    return value;
}
template<class T> T& item(const std::shared_ptr<std::vector<T>>& value,std::size_t index) {
    const auto& owner=list(value);
    if(index>=owner->size())fail("Player status reconciliation reached unavailable list backing.");
    return (*owner)[index];
}
std::shared_ptr<ModelSceneObject>& slot(const std::shared_ptr<ModelSceneList>& value,std::int32_t index) {
    if(!value || index<0 || !value->backing || static_cast<std::size_t>(index)>=value->backing->size())
        fail("Original named data removal reached unavailable list backing.");
    return (*value->backing)[static_cast<std::size_t>(index)];
}
// 8031E8BC does not ensure a missing list or require a timer concrete type.
// It finds by name, locates that identity in the current list, releases the
// slot, compacts the logical prefix, and clears the newly unused physical slot.
void remove_named(ModelSceneRam& ram,std::string_view name) {
    if(!ram.data)return;
    const auto record=find_model_named(ram.data,name);
    if(!record)return;
    const auto searched=ram.data;
    if(!searched)fail("Original named data removal lost its current list.");
    const auto count=searched->count;std::int32_t index{};
    for(;index<count;++index)if(slot(searched,index)==record)break;
    if(index>=count)index=-1;
    const auto destination=ram.data;
    slot(destination,index).reset();
    // 80041C10 checks the current count after the reference release.
    const auto live_count=destination->count;
    if(live_count>0 && index>=0 && index<live_count) {
        for(auto next=index;next<live_count-1;++next)slot(destination,next)=slot(destination,next+1);
        destination->count=live_count-1;
    }
    slot(destination,destination->count).reset();
}
}

struct ModelPlayerSequenceInvocation::Impl {
    enum class Phase { capture,effect_prepare,effect_select,effect_send,panel,panel_prepare,panel_select,panel_send,
        reconcile_capture,reconcile_gate,erase_next,erase_prepare,erase_send,clear_timers,clear_displays,
        sleep,sleep_remove,sleep_clear,stun,stun_remove,stun_clear,stun_erase_prepare,
        stun_erase_select,stun_erase_send,direct,completed };
    explicit Impl(std::shared_ptr<ModelSceneRam> value):ram(std::move(value)) {
        if(!ram)fail("Player sequence callback requires its actual RAM owner.");
    }
    std::shared_ptr<ModelSceneRam> ram;
    std::shared_ptr<ModelSceneInfo> parent;
    std::shared_ptr<ModelPlayerWork> work,reconcile;
    std::optional<MessengerDispatch> dispatch;
    std::unique_ptr<ModelBaseInvocation> direct;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    Phase phase=Phase::capture;
    std::size_t erase_index{};
    std::optional<std::string> target_name;
    std::int32_t target_number{};
    bool executing{},fault{};
};

ModelPlayerSequenceInvocation::ModelPlayerSequenceInvocation(std::shared_ptr<ModelSceneRam> ram)
    :impl_(std::make_unique<Impl>(std::move(ram))) {}
ModelPlayerSequenceInvocation::~ModelPlayerSequenceInvocation()=default;
bool ModelPlayerSequenceInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::completed;}
bool ModelPlayerSequenceInvocation::faulted() const noexcept {return impl_->fault;}
const std::shared_ptr<ModelSceneInfo>& ModelPlayerSequenceInvocation::captured_parent() const {return impl_->parent;}
const std::shared_ptr<ModelPlayerWork>& ModelPlayerSequenceInvocation::captured_work() const {return impl_->work;}
const std::shared_ptr<ModelPlayerWork>& ModelPlayerSequenceInvocation::reconciliation_work() const {return impl_->reconcile;}
const ModelBaseInvocation* ModelPlayerSequenceInvocation::direct_entry() const noexcept {return impl_->direct.get();}

MessengerCallResult ModelPlayerSequenceInvocation::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    const ModelPlayerCreateServices& services) {
    auto& state=*impl_;
    if(state.fault)fail("A faulted player sequence callback cannot replay its published prefix.");
    if(state.executing || (state.messenger && (state.messenger!=&messenger || state.events!=&events))) {
        state.fault=true;fail("Player sequence callback requires the same nonrecursive execution owners.");
    }
    if(completed())return {true,0,{}};
    state.messenger=&messenger;state.events=&events;
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(state.executing);
    try {
        for(;;)switch(state.phase) {
        case Impl::Phase::capture:
            state.parent=state.ram->parent.lock();
            state.work=current_work(*state.ram);
            state.phase=sequence_name(*state.work)=="SQ_WAIT" ? Impl::Phase::effect_prepare:Impl::Phase::panel;
            break;
        case Impl::Phase::effect_prepare:
            if(!state.parent)fail("Player idle effect reached its captured null parent.");
            state.target_name=state.parent->name.value_or("");state.target_number=state.ram->number;
            state.phase=Impl::Phase::effect_select;break;
        case Impl::Phase::effect_select:
            if(!messenger.has_category("MODELCTRL"))return {false,0,"Player idle effect requires the actual MODELCTRL provider."};
            messenger.select("MODELCTRL",state.target_name,state.target_number);
            messenger.buffer().add_string("i_pcnumic");
            messenger.buffer().add_string("SQ_OPENP"+std::to_string(state.ram->number));
            messenger.buffer().add_float(480);
            messenger.buffer().add_vector({0,10,0});
            messenger.buffer().add_bool(1);messenger.buffer().add_bool(1);
            state.dispatch.emplace(messenger.begin_send("SETEFFECT"));state.phase=Impl::Phase::effect_send;break;
        case Impl::Phase::effect_send:
        case Impl::Phase::panel_send:
        case Impl::Phase::erase_send:
        case Impl::Phase::stun_erase_send: {
            const auto result=events.resume(*state.dispatch);
            if(state.fault || result.stop==MessengerStop::faulted)fail("Player sequence message delivery faulted.");
            if(result.stop!=MessengerStop::completed)return {false,0,result.diagnostic};
            state.dispatch.reset();
            if(state.phase==Impl::Phase::effect_send)state.phase=Impl::Phase::panel;
            else if(state.phase==Impl::Phase::panel_send)state.phase=Impl::Phase::reconcile_capture;
            else if(state.phase==Impl::Phase::erase_send){++state.erase_index;state.phase=Impl::Phase::erase_next;}
            else state.phase=Impl::Phase::completed;
            break;
        }
        case Impl::Phase::panel:
            if(state.work->panel!=0) {
                const auto& name=sequence_name(*state.work);
                // 800633C8 writes success status zero for every exact match;
                // the enum values 0..6 are not used by this gate.
                if(std::find(panel_sequences.begin(),panel_sequences.end(),name)!=panel_sequences.end()) {
                    state.work->panel=0;state.phase=Impl::Phase::panel_prepare;break;
                }
            }
            state.phase=Impl::Phase::reconcile_capture;break;
        case Impl::Phase::panel_prepare:
            state.target_number=state.ram->number;state.phase=Impl::Phase::panel_select;break;
        case Impl::Phase::panel_select:
            if(!messenger.has_category("NDMWPANEL"))return {false,0,"Player panel removal requires the actual NDMWPANEL provider."};
            messenger.select("NDMWPANEL",std::nullopt,state.target_number);
            state.dispatch.emplace(messenger.begin_send("REMOVE"));state.phase=Impl::Phase::panel_send;break;
        case Impl::Phase::reconcile_capture:
            state.reconcile=current_work(*state.ram);state.phase=Impl::Phase::reconcile_gate;break;
        case Impl::Phase::reconcile_gate:
            if(!state.reconcile->status.display_ram_numbers){state.phase=Impl::Phase::completed;break;}
            if(!state.reconcile->status.display_ram_numbers->empty()) {
                if(!state.reconcile->script.hp)fail("Player status reconciliation reached its null HP wrapper.");
                if(state.reconcile->script.hp->data==0){state.phase=Impl::Phase::erase_next;break;}
            }
            state.phase=Impl::Phase::sleep;break;
        case Impl::Phase::erase_next: {
            if(state.erase_index==12){state.phase=Impl::Phase::clear_timers;break;}
            const auto number=item(state.reconcile->status.display_ram_numbers,state.erase_index);
            if(number==-1){++state.erase_index;break;}
            state.target_number=number;state.phase=Impl::Phase::erase_prepare;break;
        }
        case Impl::Phase::erase_prepare:
            if(!messenger.has_category("MODELCTRL"))return {false,0,"Player status icon removal requires the actual MODELCTRL provider."};
            messenger.select("MODELCTRL",std::string(status_actors[state.erase_index]),state.target_number);
            state.dispatch.emplace(messenger.begin_send("SQ_ERAS"));state.phase=Impl::Phase::erase_send;break;
        case Impl::Phase::clear_timers: {
            const auto timers=list(state.reconcile->status.timers);
            std::fill(timers->begin(),timers->end(),0);state.phase=Impl::Phase::clear_displays;break;
        }
        case Impl::Phase::clear_displays: {
            const auto displays=list(state.reconcile->status.display_ram_numbers);
            std::fill(displays->begin(),displays->end(),-1);state.phase=Impl::Phase::completed;break;
        }
        case Impl::Phase::sleep:
            if(list(state.reconcile->status.timers)->empty()){state.phase=Impl::Phase::completed;break;}
            state.phase=item(state.reconcile->status.timers,8)!=0 && sequence_name(*state.reconcile)!="SQ_SLEP"
                ? Impl::Phase::sleep_remove:Impl::Phase::stun;
            break;
        case Impl::Phase::sleep_remove:
            remove_named(*state.ram,"SLEEP");state.phase=Impl::Phase::sleep_clear;break;
        case Impl::Phase::sleep_clear:
            if(list(state.reconcile->status.first_flags)->size()>8)(*state.reconcile->status.first_flags)[8]=0;
            if(list(state.reconcile->status.timers)->size()>8)(*state.reconcile->status.timers)[8]=0;
            state.phase=Impl::Phase::stun;break;
        case Impl::Phase::stun:
            if(item(state.reconcile->status.timers,9)==0){state.phase=Impl::Phase::completed;break;}
            if(sequence_name(*state.reconcile)=="SQ_WAIT") {
                state.direct=std::make_unique<ModelBaseInvocation>(state.ram,"SQ_STUN");state.phase=Impl::Phase::direct;break;
            }
            state.phase=sequence_name(*state.reconcile)=="SQ_STUN" ? Impl::Phase::completed:Impl::Phase::stun_remove;
            break;
        case Impl::Phase::stun_remove:
            remove_named(*state.ram,"STUN");state.phase=Impl::Phase::stun_clear;break;
        case Impl::Phase::stun_clear:
            if(list(state.reconcile->status.first_flags)->size()>9)(*state.reconcile->status.first_flags)[9]=0;
            if(list(state.reconcile->status.timers)->size()>9)(*state.reconcile->status.timers)[9]=0;
            state.phase=Impl::Phase::stun_erase_prepare;break;
        case Impl::Phase::stun_erase_prepare:
            state.target_number=item(state.reconcile->status.display_ram_numbers,9);
            state.phase=Impl::Phase::stun_erase_select;break;
        case Impl::Phase::stun_erase_select:
            if(!messenger.has_category("MODELCTRL"))return {false,0,"Player stun icon removal requires the actual MODELCTRL provider."};
            messenger.select("MODELCTRL",std::string(status_actors[9]),state.target_number);
            state.dispatch.emplace(messenger.begin_send("SQ_ERAS"));state.phase=Impl::Phase::stun_erase_send;break;
        case Impl::Phase::direct: {
            const auto result=state.direct->resume(messenger,events,services);
            if(state.fault)fail("Recursive player sequence callback invalidated its nested entry.");
            if(!result.completed)return result;
            state.phase=Impl::Phase::completed;break;
        }
        case Impl::Phase::completed:return {true,0,{}};
        }
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
