#include "dw4/model_wait_actual.hpp"

#include "dw4/action_event_runtime.hpp"
#include "dw4/model_dispatch.hpp"
#include "dw4/model_pad.hpp"
#include "dw4/model_scene.hpp"
#include "dw4/model_sequence_entry.hpp"
#include "dw4/pad.hpp"

#include <optional>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw ModelSceneError(message);}
struct WaitOwners {
    std::shared_ptr<ModelSceneRam> ram;
    std::shared_ptr<ModelSceneInfo> parent;
    std::shared_ptr<ModelPlayerWork> work;
    std::weak_ptr<ModelSceneManager> manager;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    bool executing{},fault{},done{};
    void check_prefix() const {if(fault)fail("A caught recursive failure invalidated this actual wait invocation.");}
    void bind(NativeMessenger& supplied,ActionEventRuntime& runtime) {
        if(fault || executing || (messenger && (messenger!=&supplied || events!=&runtime))) {
            fault=true;fail("Actual wait requires its original nonrecursive messenger owners and an unfaulted continuation.");
        }
        const auto current=runtime.model_manager();
        if(messenger) {
            if(!current || current!=manager.lock()) {fault=true;fail("Actual wait lost its captured controller manager.");}
        } else {
            if(&runtime.messenger()!=&supplied || !current) {fault=true;fail("Actual wait requires the supplied manager's own messenger.");}
            manager=current;messenger=&supplied;events=&runtime;
        }
    }
    ModelWaitState& wait() const {
        if(!work)fail("Original wait dereferences its captured null work.");
        return work->sequence.wait;
    }
    const std::string& name() const {
        const auto& value=wait().name;
        if(!value)fail("Original wait comparison dereferences a null wait name.");
        return *value;
    }
    MessengerCallResult complete() {done=true;return {true,0,{}};}
};
struct Processing {
    WaitOwners& owner;
    explicit Processing(WaitOwners& value):owner(value) {owner.executing=true;}
    ~Processing(){owner.executing=false;}
};
struct ActionQuery {
    std::string name;
    std::int32_t number{};
    std::optional<MessengerDispatch> dispatch;
    explicit ActionQuery(const WaitOwners& owner) {
        // Captured parent and live RAM fields are consumed before SendTo.
        if(!owner.parent)fail("Original ACTION wait query dereferences its captured null parent.");
        name=owner.parent->name.value_or(std::string{});
        if(!owner.ram)fail("Original ACTION wait query dereferences its null RAM number.");
        number=owner.ram->number;
    }
    std::optional<bool> resume(WaitOwners& owner) {
        if(!dispatch) {
            if(!owner.messenger->has_category("ACTION"))return {};
            dispatch.emplace(owner.messenger->begin_send_to("ACTION",name,number,"ISCOMEND"));
        }
        const auto result=owner.events->resume(*dispatch);owner.check_prefix();
        if(result.stop==MessengerStop::faulted)throw MessengerError(result.diagnostic);
        if(result.stop!=MessengerStop::completed)return {};
        // 80306A1C reads physical boolean[0], without a logical count test.
        // MessengerBuffer represents addressable elements only; no fabricated
        // false result stands in for an absent physical slot.
        const auto& values=owner.messenger->buffer().booleans();
        if(values.empty())fail("Original ACTION ISCOMEND returned without its addressable boolean slot zero.");
        const bool complete=values.front()!=0;dispatch.reset();return complete;
    }
};
MessengerCallResult commands(WaitOwners& owner,std::shared_ptr<ModelSequenceResume>& invocation) {
    if(!invocation)invocation=owner.events->begin_model_sequence_resume(owner.ram,owner.parent,owner.work);
    const auto result=invocation->resume(*owner.events);owner.check_prefix();return result;
}
MessengerCallResult dispatch_wait(WaitOwners& owner,std::unique_ptr<ModelBaseInvocation>& invocation) {
    if(!invocation)invocation=std::make_unique<ModelBaseInvocation>(owner.ram,"SQ_WAIT");
    const auto result=invocation->resume(*owner.messenger,*owner.events,owner.events->player_creation_services());
    owner.check_prefix();return result;
}
}
bool model_wait_motion_active(const std::shared_ptr<MotionChannelList>& list,ModelWaitMotion mode) {
    if(!list)fail("Original motion activity query dereferences a null movement list.");
    if(list->size()==0)return false;
    const auto active=[&](std::size_t index) {
        if(index>=list->size())fail("Original motion activity query has no represented physical entry.");
        const auto entry=list->entry_owner(index);
        if(!entry)fail("Original motion activity query dereferences a null reached entry.");
        return entry->active;
    };
    switch(mode) {
    case ModelWaitMotion::moving:return active(0) || active(1) || active(2);
    case ModelWaitMotion::rotation:return active(3);
    case ModelWaitMotion::vertical:return active(1);
    }
    fail("This narrow wait activity reader implements only the three reached original modes.");
}

struct ModelPlayerWait::Impl : WaitOwners {
    enum class Phase {capture,branch,query,commands,sequence_then_query,sequence,completed};
    enum class QueryKind {attack_one,attack_two};
    Phase phase=Phase::capture;
    QueryKind query_kind=QueryKind::attack_one;
    std::uint32_t held{};
    std::optional<ActionQuery> query;
    std::shared_ptr<ModelSequenceResume> command_resume;
    std::unique_ptr<ModelBaseInvocation> sequence;
    explicit Impl(std::shared_ptr<ModelSceneRam> value) {ram=std::move(value);}
};
ModelPlayerWait::ModelPlayerWait(std::shared_ptr<ModelSceneRam> ram):impl_(std::make_unique<Impl>(std::move(ram))) {}
ModelPlayerWait::~ModelPlayerWait()=default;
bool ModelPlayerWait::completed() const noexcept {return impl_->done;}
bool ModelPlayerWait::faulted() const noexcept {return impl_->fault;}
const std::shared_ptr<ModelSceneInfo>& ModelPlayerWait::captured_parent() const noexcept {return impl_->parent;}
const std::shared_ptr<ModelPlayerWork>& ModelPlayerWait::captured_work() const noexcept {return impl_->work;}
MessengerCallResult ModelPlayerWait::resume(NativeMessenger& messenger,ActionEventRuntime& events) {
    events.require_model_execution_access();
    auto& state=*impl_;state.bind(messenger,events);if(state.done)return {true,0,{}};
    Processing processing(state);
    try {
        for(;;)switch(state.phase) {
        case Impl::Phase::capture: {
            if(!state.ram)fail("Original player wait dereferences a null RAM.");
            state.parent=state.ram->parent.lock();
            const auto one=find_model_one(*state.ram);
            if(!one)fail("Original player wait dereferences a missing first named ONE record.");
            state.work=one->work;
            if(state.wait().control_enabled!=0) {
                const auto manager=state.manager.lock();
                if(!manager)fail("Original player wait lost its captured controller manager.");
                const auto pad=model_pad(manager->pads,state.ram->number);
                if(!pad)fail("Original player wait dereferences a null selected physical pad.");
                state.held=pad->held;
            }
            state.phase=Impl::Phase::branch;break;
        }
        case Impl::Phase::branch: {
            const auto& name=state.name();auto& wait=state.wait();
            if(name=="Atck01") {
                if(wait.attack_state!=0 && wait.attack_state!=1)return state.complete();
                if(wait.attack_state==0) {wait.attack_mode=0;wait.attack_state=1;}
                if((state.held&pad_button::a)==0)wait.attack_mode=1;
                state.query_kind=Impl::QueryKind::attack_one;state.phase=Impl::Phase::query;
            } else if(name=="Atck02") {
                state.query_kind=Impl::QueryKind::attack_two;
                state.phase=(state.held&pad_button::a)==0 ? Impl::Phase::sequence_then_query : Impl::Phase::query;
            } else if(name=="Atck03" && (state.held&pad_button::a)==0) {
                wait.name.reset();state.phase=Impl::Phase::commands;
            } else return state.complete();
            break;
        }
        case Impl::Phase::query: {
            if(!state.query)state.query.emplace(state);
            const auto result=state.query->resume(state);
            if(!result)return {false,0,"Original player wait awaits its retained ACTION ISCOMEND query."};
            state.query.reset();
            if(!*result)return state.complete();
            auto& wait=state.wait();wait.name.reset();
            if(state.query_kind==Impl::QueryKind::attack_one) {
                wait.attack_state=0;
                state.phase=wait.attack_mode==0 ? Impl::Phase::commands : Impl::Phase::sequence;
            } else state.phase=Impl::Phase::commands;
            break;
        }
        case Impl::Phase::commands: {
            const auto result=commands(state,state.command_resume);
            if(!result.completed)return result;
            state.command_resume.reset();return state.complete();
        }
        case Impl::Phase::sequence_then_query: {
            const auto result=dispatch_wait(state,state.sequence);
            if(!result.completed)return result;
            state.sequence.reset();state.phase=Impl::Phase::query;break;
        }
        case Impl::Phase::sequence: {
            const auto result=dispatch_wait(state,state.sequence);
            if(!result.completed)return result;
            state.sequence.reset();return state.complete();
        }
        case Impl::Phase::completed:return state.complete();
        }
    }catch(...){state.fault=true;throw;}
}

struct ModelCommonWait::Impl : WaitOwners {
    enum class Phase {branch,query,commands,player};
    Phase phase=Phase::branch;
    std::optional<ActionQuery> query;
    std::shared_ptr<ModelSequenceResume> command_resume;
    std::unique_ptr<ModelPlayerWait> player;
    Impl(std::shared_ptr<ModelSceneRam> actual,std::shared_ptr<ModelSceneInfo> captured_parent,
        std::shared_ptr<ModelPlayerWork> captured_work) {
        ram=std::move(actual);parent=std::move(captured_parent);work=std::move(captured_work);
    }
};
ModelCommonWait::ModelCommonWait(std::shared_ptr<ModelSceneRam> ram,std::shared_ptr<ModelSceneInfo> parent,
    std::shared_ptr<ModelPlayerWork> work):impl_(std::make_unique<Impl>(std::move(ram),std::move(parent),std::move(work))) {}
ModelCommonWait::~ModelCommonWait()=default;
bool ModelCommonWait::completed() const noexcept {return impl_->done;}
bool ModelCommonWait::faulted() const noexcept {return impl_->fault;}
const std::shared_ptr<ModelSceneInfo>& ModelCommonWait::captured_parent() const noexcept {return impl_->parent;}
const std::shared_ptr<ModelPlayerWork>& ModelCommonWait::captured_work() const noexcept {return impl_->work;}
const ModelPlayerWait* ModelCommonWait::player_fallback() const noexcept {return impl_->player.get();}
MessengerCallResult ModelCommonWait::resume(NativeMessenger& messenger,ActionEventRuntime& events) {
    events.require_model_execution_access();
    auto& state=*impl_;state.bind(messenger,events);if(state.done)return {true,0,{}};
    Processing processing(state);
    try {
        for(;;)switch(state.phase) {
        case Impl::Phase::branch: {
            const auto& name=state.name();
            if(name=="TurnY" || name=="Landing" || name=="Moving" || name=="Rotation") {
                const auto mode=name=="Moving" ? ModelWaitMotion::moving : name=="Rotation" ? ModelWaitMotion::rotation : ModelWaitMotion::vertical;
                if(model_wait_motion_active(state.work->control.movement,mode))return state.complete();
                state.wait().name.reset();state.phase=Impl::Phase::commands;
            } else if(name=="Action")state.phase=Impl::Phase::query;
            else state.phase=Impl::Phase::player;
            break;
        }
        case Impl::Phase::query: {
            if(!state.query)state.query.emplace(state);
            const auto result=state.query->resume(state);
            if(!result)return {false,0,"Original common wait awaits its retained ACTION ISCOMEND query."};
            state.query.reset();
            if(!*result)return state.complete();
            state.wait().name.reset();state.phase=Impl::Phase::commands;break;
        }
        case Impl::Phase::commands: {
            const auto result=commands(state,state.command_resume);
            if(!result.completed)return result;
            state.command_resume.reset();return state.complete();
        }
        case Impl::Phase::player: {
            if(!state.player)state.player=std::make_unique<ModelPlayerWait>(state.ram);
            const auto result=state.player->resume(messenger,events);state.check_prefix();
            if(!result.completed)return result;
            return state.complete();
        }
        }
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
