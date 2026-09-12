#include "dw4/model_dispatch.hpp"
#include "dw4/action_event_runtime.hpp"
#include "dw4/model_player_sequence.hpp"

#include <algorithm>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw ModelSceneError(message); }
std::shared_ptr<ModelSceneOne> one(ModelSceneRam& ram) {
    auto result=find_model_one(ram);
    if(!result)fail("Original MODELCTRL dereferences a missing first named ONE record.");
    return result;
}
ActorSceneNode& node(const std::shared_ptr<ModelSceneOne>& captured) {
    if(!captured->node)fail("Original GENERATE dereferences a null captured ONE node.");
    return *captured->node;
}
bool controller_command(const std::optional<std::string>& command) {
    // 802CCD64: the exact base dictionary precedes ordinary named sequences.
    // An unimplemented controller command must not fall through as a sequence.
    constexpr std::array<std::string_view,35> names{
        "GENERATE","GENERATE_NDSP","REMOVE","GETINFONAME","GETINFONAME_RUN",
        "SETMATRIX","CHKBULLET","SETLABEL","SETEFFECT","SETSOUND","GETMATRIX",
        "GETMATRIX_ALL","GETMATRIX_ACTIVE","GETTRCDATA","GETHP","SETHP","GETRECNO",
        "ACTION1","ONCTRL","OFCTRL","ONFREEZE","OFFREEZE","SETLODHIGH","RESETLODHIGH",
        "MOVETRANS","SETCONVEYOR","SETQUICKSAND","SETSLIPPEDFLOOR","ISRUN","ISSEQNAME",
        "ONHITBULLETBOX","OFHITBULLETBOX","GETSEQNAMELIST","SQ_ERAS","SQ_ERAS2"};
    return command && std::find(names.begin(),names.end(),*command)!=names.end();
}
thread_local std::size_t active_base_calls{};
}
struct ModelBaseInvocation::Impl {
    enum class Phase { status,command,generate_begin,player,outer_capture,hp,save_box,
        clear_box,heading,ai,direct_entry,final_box,sequence,player_sequence,completed };
    std::shared_ptr<ModelSceneRam> ram;
    std::shared_ptr<ModelSceneInfo> parent;
    std::shared_ptr<ModelSceneOne> one;
    std::shared_ptr<ModelPlayerWork> work,status_work,outer_work;
    std::optional<std::string> command,sequence_override;
    std::unique_ptr<ModelStatusInvocation> status;
    std::unique_ptr<ModelPlayerCreate> player;
    std::unique_ptr<ModelBaseInvocation> direct;
    std::shared_ptr<ModelSequenceEntry> sequence;
    std::unique_ptr<ModelPlayerSequenceInvocation> player_sequence;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    Phase phase=Phase::status;
    bool executing{},fault{};
    std::uint32_t returned{};
    std::uint32_t direct_depth{};
    Impl(std::shared_ptr<ModelSceneRam> actual,std::optional<std::string> name)
        :ram(std::move(actual)),command(std::move(name)) {
        if(!ram)fail("MODELCTRL requires its actual registered RAM owner.");
        parent=ram->parent.lock();
        one=dw4::one(*ram);work=one->work;
    }
};
ModelBaseInvocation::ModelBaseInvocation(std::shared_ptr<ModelSceneRam> ram,std::optional<std::string> command)
    :impl_(std::make_unique<Impl>(std::move(ram),std::move(command))) {}
ModelBaseInvocation::~ModelBaseInvocation()=default;
bool ModelBaseInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::completed;}
bool ModelBaseInvocation::faulted() const noexcept {return impl_->fault;}
const std::shared_ptr<ModelSceneOne>& ModelBaseInvocation::captured_one() const {return impl_->one;}
const std::shared_ptr<ModelPlayerWork>& ModelBaseInvocation::captured_work() const {return impl_->work;}
const std::shared_ptr<ModelPlayerWork>& ModelBaseInvocation::status_work() const {return impl_->status_work;}
const std::shared_ptr<ModelPlayerWork>& ModelBaseInvocation::outer_work() const {return impl_->outer_work;}
const ModelPlayerCreate* ModelBaseInvocation::player_create() const noexcept {return impl_->player.get();}
const ModelBaseInvocation* ModelBaseInvocation::direct_entry() const noexcept {return impl_->direct.get();}
const ModelSequenceEntry* ModelBaseInvocation::sequence_entry() const noexcept {return impl_->sequence.get();}
const ModelPlayerSequenceInvocation* ModelBaseInvocation::player_sequence() const noexcept {return impl_->player_sequence.get();}
MessengerCallResult ModelBaseInvocation::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    const ModelPlayerCreateServices& supplied) {
    auto& state=*impl_;
    if(state.fault || state.executing || (state.messenger && (state.messenger!=&messenger || state.events!=&events))) {
        state.fault=true;
        fail("A MODELCTRL invocation requires its original nonrecursive messenger owners and an unfaulted prefix.");
    }
    if(completed())return {true,state.returned,{}};
    if(active_base_calls>=64) {state.fault=true;fail("Nested original MODELCTRL invocations exceed the bounded native call depth.");}
    state.messenger=&messenger;state.events=&events;
    struct Guard {
        bool& value;explicit Guard(bool& v):value(v){value=true;++active_base_calls;}
        ~Guard(){--active_base_calls;value=false;}
    } guard(state.executing);
    const auto complete=[&](std::uint32_t value) {
        state.returned=value;state.phase=Impl::Phase::completed;return MessengerCallResult{true,value,{}};
    };
    try {
        for(;;)switch(state.phase) {
        case Impl::Phase::status: {
            if(!state.status) {
                // 803AEC90/94 run even for commands absent from the dictionary.
                state.status_work=one(*state.ram)->work;
                std::shared_ptr<ModelStatusWork> status;
                if(state.status_work)status=std::make_shared<ModelStatusWork>(
                    std::shared_ptr<ModelStatusFields>(state.status_work,&state.status_work->status));
                state.status=std::make_unique<ModelStatusInvocation>(ModelStatusOwner{std::move(status),
                    std::shared_ptr<std::int32_t>(state.ram,&state.ram->number)},state.command);
            }
            const auto result=state.status->resume(messenger,events);
            if(state.fault)fail("A recursive callback invalidated this MODELCTRL status invocation.");
            if(!result.completed)return result;
            state.status.reset();
            if((result.value&0xffu)==1)return complete(result.value);
            state.phase=Impl::Phase::command;break;
        }
        case Impl::Phase::command: {
            const auto& command=state.command;
            if(command=="GENERATE") {state.phase=Impl::Phase::generate_begin;break;}
            if(command=="GETINFONAME" || command=="GETINFONAME_RUN") {
                if(command=="GETINFONAME_RUN" && state.ram->active==0)return complete(1);
                if(!state.parent)fail("MODELCTRL name query dereferences its captured null parent.");
                messenger.buffer().add_string(state.parent->name.value_or(std::string{}));return complete(1);
            }
            if(command=="GETHP") {
                if(state.ram->active==0 || !state.work) {messenger.buffer().add_int(0);return complete(1);}
                if(!state.work->script.hp)fail("Active MODELCTRL GETHP dereferences a null current-HP wrapper.");
                messenger.buffer().add_int(state.work->script.hp->data);
                messenger.buffer().add_game_object(state.work->script.hp);
                if(state.work->script.max_hp)messenger.buffer().add_game_object(state.work->script.max_hp);
                else messenger.buffer().add_object({});
                return complete(1);
            }
            if(command=="GETRECNO") {
                if(state.ram->active!=0 && state.work)messenger.buffer().add_int(state.work->script.record);
                return complete(1);
            }
            if(command=="SETLABEL") {
                if(state.ram->active==0 || !state.work)return complete(1);
                const auto& strings=messenger.buffer().strings();
                if(strings.size()<2)fail("Active MODELCTRL SETLABEL requires its two original nullable string slots.");
                ModelSequenceLabels(state.work->sequence.labels).set(strings[0],strings[1]);
                return complete(1);
            }
            if(command=="SETSOUND" || command=="SETEFFECT") {
                if(state.ram->active==0)return complete(1);
                if(!state.work)fail("Active MODELCTRL subcommand append dereferences captured null work.");
                ModelSubcommandQueue queue(std::shared_ptr<std::shared_ptr<ModelSubcommandWrapper>>(state.work,&state.work->subcommands));
                const auto& packet=messenger.buffer();
                if(command=="SETSOUND") {
                    if(packet.floats().empty() || packet.strings().empty())fail("SETSOUND requires its original delay and nullable name.");
                    queue.append_sound(packet.floats()[0],packet.strings()[0]);
                } else {
                    if(packet.floats().empty() || packet.strings().size()<2 || packet.vectors().empty() || packet.booleans().size()<2)
                        fail("SETEFFECT requires its original typed arguments.");
                    ModelEffectSubcommand effect;
                    effect.delay=packet.floats()[0];effect.model_name=packet.strings()[0];effect.sequence_name=packet.strings()[1];
                    effect.offset=packet.vectors()[0];if(packet.vectors().size()==2)effect.rotation=packet.vectors()[1];
                    effect.sequence_dead=packet.booleans()[0]!=0;effect.position_lock=packet.booleans()[1]!=0;
                    queue.append_effect(std::move(effect));
                }
                return complete(1);
            }
            if(controller_command(command)) {
                if((command=="SQ_ERAS" || command=="SQ_ERAS2") && state.ram->active==0)return complete(1);
                return {false,0,"This live MODELCTRL command requires its connected native controller implementation."};
            }
            if(state.ram->active==0)return complete(1);
            state.phase=Impl::Phase::sequence;break;
        }
        case Impl::Phase::generate_begin: {
            state.ram->active=1;
            state.ram->words[0]=0;state.ram->words[3]=0;state.ram->words[2]=0;state.ram->words[1]=0;
            const auto& strings=messenger.buffer().strings();
            if(!strings.empty())state.sequence_override=strings[strings.size()==1 ? 0 : 1];
            state.phase=Impl::Phase::player;break;
        }
        case Impl::Phase::player: {
            if(!state.player) {
                if(!supplied.definitions)return {false,0,"Player creation requires the original DOL default-name and map tables."};
                state.player=std::make_unique<ModelPlayerCreate>(state.ram,supplied.definitions->defaults,supplied.definitions->maps);
            }
            const auto result=state.player->resume(messenger,events,supplied.clock,supplied.map);
            if(state.fault)fail("A recursive callback invalidated this MODELCTRL player invocation.");
            if(result.stop!=ModelPlayerCreateStop::outer_generate_required)return {false,0,result.diagnostic};
            state.phase=Impl::Phase::outer_capture;break;
        }
        case Impl::Phase::outer_capture:
            state.outer_work=state.one->work;
            if(!state.outer_work)fail("Outer GENERATE dereferences its original ONE's post-player null work.");
            state.phase=Impl::Phase::hp;break;
        case Impl::Phase::hp:
            if(!state.outer_work->script.max_hp)fail("Outer GENERATE dereferences its captured null max-HP wrapper.");
            if(state.outer_work->script.max_hp->data==0) {
                state.outer_work->script.max_hp->data=100;
                if(!state.outer_work->script.hp)fail("Outer GENERATE dereferences null HP after publishing max-HP 100.");
                state.outer_work->script.hp->data=100;
            }
            state.phase=Impl::Phase::save_box;break;
        case Impl::Phase::save_box: {
            const auto children=node(state.one).child_list_owner();
            if(!children)fail("Outer GENERATE dereferences a null child-list owner.");
            const auto child=children->first_backing_node();
            if(!child)fail("Outer GENERATE dereferences a null first child in the captured backing.");
            // Field copy retains the same box allocation. Const traversal views
            // do not make the original shared box immutable.
            state.outer_work->base_box=std::const_pointer_cast<CollisionBox>(child->box_owner());
            state.phase=Impl::Phase::clear_box;break;
        }
        case Impl::Phase::clear_box:
            node(state.one).set_box_owner({});state.phase=Impl::Phase::heading;break;
        case Impl::Phase::heading: {
            ModelMotionPose pose(node(state.one).matrix_owner(),
                std::shared_ptr<ModelMotionFields>(state.outer_work,&state.outer_work->motion));
            synchronize_model_heading(pose);state.phase=Impl::Phase::ai;break;
        }
        case Impl::Phase::ai: {
            if(!state.parent)fail("Outer GENERATE AI lookup dereferences its captured null parent.");
            auto data=find_model_named(state.parent->data,"AIM");
            if(!data)data=find_model_named(state.parent->data,"AIH");
            if(data)state.outer_work->ai=std::make_shared<ModelWorkAi>();
            state.phase=Impl::Phase::direct_entry;break;
        }
        case Impl::Phase::direct_entry: {
            if(!state.direct) {
                if(state.direct_depth>=64)fail("MODELCTRL direct-entry nesting exceeds the bounded native profile.");
                state.direct=std::make_unique<ModelBaseInvocation>(state.ram,state.sequence_override.value_or("SQ_WAIT"));
                state.direct->impl_->direct_depth=state.direct_depth+1;
            }
            const auto result=state.direct->resume(messenger,events,supplied);
            if(state.fault)fail("A recursive callback invalidated this MODELCTRL direct entry.");
            if(!result.completed)return result;
            state.phase=Impl::Phase::final_box;break;
        }
        case Impl::Phase::final_box: {
            // 8030D7F4 copies the saved box before transforming that destination.
            // Re-read both the saved field and the original ONE's live node only
            // after the full direct callback has returned.
            const auto source=state.outer_work->base_box;
            if(source) {
                auto& target=node(state.one);
                auto destination=target.box_owner();
                if(!destination) {destination=std::make_shared<CollisionBox>();target.set_box_owner(destination);}
                *destination=*source;
                *destination=original_transform_box(*target.matrix_owner(),*destination);
            }
            return complete(1);
        }
        case Impl::Phase::sequence: {
            if(!state.sequence)state.sequence=events.begin_model_sequence_entry(state.ram,state.parent,state.work,state.command);
            const auto result=state.sequence->resume(events);
            if(state.fault)fail("A recursive callback invalidated this MODELCTRL sequence invocation.");
            if(!result.completed)return result;
            state.phase=Impl::Phase::player_sequence;break;
        }
        case Impl::Phase::player_sequence: {
            if(!state.player_sequence)state.player_sequence=std::make_unique<ModelPlayerSequenceInvocation>(state.ram);
            const auto result=state.player_sequence->resume(messenger,events,supplied);
            if(state.fault)fail("A recursive callback invalidated the MODELCTRL player sequence callback.");
            if(!result.completed)return result;
            return complete(1);
        }
        case Impl::Phase::completed:return {true,state.returned,{}};
        }
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
