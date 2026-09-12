#include "dw4/model_player_input.hpp"

#include "dw4/action_event_runtime.hpp"
#include "dw4/model_scene.hpp"
#include "dw4/model_timer.hpp"
#include "dw4/pad.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <string_view>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* text) {throw ModelSceneError(text);}
// Original direction-to-history table 804E7BF0 and duration 804E7C30.
constexpr std::array<std::int32_t,16> direction_slots{-1,0,2,1,4,-1,3,-1,6,7,-1,-1,5,-1,-1,-1};
const std::string& sequence(const ModelPlayerWork& work) {
    if(!work.sequence.name)fail("Player input compares a null current sequence string.");
    if(work.sequence.name->size()>65535 || work.sequence.name->find('\0')!=std::string::npos)
        fail("Player input sequence string exceeds the represented profile.");
    return *work.sequence.name;
}
std::uint8_t lobby(const ModelPlayerWork& work) {
    if(!work.script.original_is_main_lobby)fail("Player input requires the actual initialized main-lobby byte.");
    return *work.script.original_is_main_lobby;
}
std::uint8_t boolean(const NativeMessenger& messenger,std::size_t index) {
    const auto& values=messenger.buffer().booleans();
    if(index>=values.size())fail("Player input reads an unavailable physical boolean response slot.");
    return values[index];
}
}

struct ModelPlayerInputInvocation::Impl {
    enum class Phase { capture,pad,movement,camera_capture,camera_inverse,buttons,body_delta,body,
        decide,panel_checked,panel_more,panel_opened,drop_checked,delay_check,attack_choice,
        player_row,timer_start,icon_prepare,panel_run_checked,send_prepare,send,completed };
    enum class Payload { none,work,sound,drop,status };
    explicit Impl(std::shared_ptr<ModelSceneRam> value):ram(std::move(value)) {
        if(!ram)fail("Player input requires its actual RAM owner.");
    }
    std::shared_ptr<ModelSceneRam> ram;
    std::shared_ptr<ModelSceneOne> one;
    std::shared_ptr<ModelPlayerWork> work;
    std::shared_ptr<ModelPadState> pad;
    std::shared_ptr<AnimationMatrix> camera;
    std::weak_ptr<ModelSceneManager> manager;
    std::shared_ptr<GameDataList> row;
    std::unique_ptr<ModelRamTimerAccess> timer;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    std::optional<MessengerDispatch> dispatch;
    Phase phase=Phase::capture,after_send=Phase::completed;
    Payload payload=Payload::none;
    std::string category,command;
    std::optional<std::string> target_name;
    std::int32_t target_number{},direction{},attack_delay{};
    std::uint32_t held{},trigger{};
    float left_x{},left_y{},body_timer{};
    bool spin{},executing{},fault{};
    void send_to(std::string target,std::optional<std::string> name,std::int32_t number,
        std::string message,Payload arguments,Phase after) {
        category=std::move(target);target_name=std::move(name);target_number=number;
        command=std::move(message);payload=arguments;after_send=after;phase=Phase::send_prepare;
    }
    void open_panel(std::string message) {
        send_to("NDMWPANEL",{},ram->number,std::move(message),Payload::work,Phase::panel_opened);
    }
    void sound() {send_to("SOUND",{},-1,"PLAY",Payload::sound,Phase::completed);}
};

ModelPlayerInputInvocation::ModelPlayerInputInvocation(std::shared_ptr<ModelSceneRam> ram)
    :impl_(std::make_unique<Impl>(std::move(ram))) {}
ModelPlayerInputInvocation::~ModelPlayerInputInvocation()=default;
bool ModelPlayerInputInvocation::completed() const noexcept {return impl_->phase==Impl::Phase::completed;}
bool ModelPlayerInputInvocation::faulted() const noexcept {return impl_->fault;}
const std::shared_ptr<ModelSceneOne>& ModelPlayerInputInvocation::captured_one() const noexcept {return impl_->one;}
const std::shared_ptr<ModelPlayerWork>& ModelPlayerInputInvocation::captured_work() const noexcept {return impl_->work;}
const std::shared_ptr<ModelPadState>& ModelPlayerInputInvocation::captured_pad() const noexcept {return impl_->pad;}

MessengerCallResult ModelPlayerInputInvocation::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    const ModelPlayerInputServices& services) {
    events.require_model_execution_access();
    auto& state=*impl_;
    const auto manager=events.model_manager();
    if(state.fault || state.executing || (state.messenger &&
        (state.messenger!=&messenger || state.events!=&events || state.manager.lock()!=manager))) {
        state.fault=true;fail("Player input requires its original nonrecursive execution owners.");
    }
    if(completed())return {true,0,{}};
    state.messenger=&messenger;state.events=&events;state.manager=manager;
    struct Guard {bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(state.executing);
    const auto check=[&] {if(state.fault)fail("A caught recursive call invalidated player input.");};
    const auto need=[](const char* reason) {return MessengerCallResult{false,0,reason};};
    using Phase=Impl::Phase;
    using Payload=Impl::Payload;
    try {
        for(;;)switch(state.phase) {
        case Phase::capture:
            // C4B8/C4BC. There is no parent capture in this virtual callback.
            state.one=find_model_one(*state.ram);
            if(!state.one)fail("Player input dereferences its missing first named ONE.");
            state.work=state.one->work;
            state.phase=Phase::pad;break;
        case Phase::pad:
            state.pad=model_pad(manager->pads,state.ram->number);
            if(!state.pad)fail("Player input dereferences its selected null pad.");
            state.direction=state.pad->direction;state.left_x=state.pad->left_x;state.left_y=state.pad->left_y;
            state.phase=Phase::movement;break;
        case Phase::movement: {
            if(!state.work)fail("Player input dereferences its captured null work.");
            if(state.direction==0) {state.work->key_input=0;state.phase=Phase::buttons;break;}
            const auto history=state.work->turn_attack_timers;
            if(state.direction<0 || static_cast<std::size_t>(state.direction)>=direction_slots.size())
                fail("Player input direction has no represented original lookup-table slot.");
            const auto selected=direction_slots[static_cast<std::size_t>(state.direction)];
            if(selected!=-1) {
                if(!history)fail("Player input dereferences a null turn-attack history list.");
                if(static_cast<std::size_t>(selected)<history->size())(*history)[static_cast<std::size_t>(selected)]=30;
            }
            std::uint32_t active{};
            for(std::size_t index=0;index<8;++index) {
                if(!history || index>=history->size())fail("Player input reads unavailable turn-attack history backing.");
                const auto value=(*history)[index];
                if(value>0) {(*history)[index]=value-1;++active;}
            }
            state.spin=active>=8;state.phase=Phase::camera_capture;break;
        }
        case Phase::camera_capture:
            if(!services.camera_matrix)return need("Player input requires the original camera matrix owner.");
            state.camera=services.camera_matrix(*manager);check();
            if(!state.camera)fail("Player input dereferences its current null camera matrix owner.");
            state.phase=Phase::camera_inverse;break;
        case Phase::camera_inverse: {
            if(!services.inverse_camera)return need("Player input requires original camera inversion 8012A92C.");
            const auto inverse=services.inverse_camera(*state.camera);check();
            if(!inverse)return need("Player input awaits original camera inversion 8012A92C.");
            // C68C: transform +Z including the original zero products.
            std::array<float,3> forward;
            for(std::size_t axis=0;axis<3;++axis) {
                const float y=0.0f*(*inverse)[4+axis];
                const float xy=std::fma(0.0f,(*inverse)[axis],y);
                forward[axis]=std::fma(1.0f,(*inverse)[8+axis],xy);
            }
            if(!std::isfinite(forward[0]) || !std::isfinite(forward[2]) ||
                !std::isfinite(state.left_x) || !std::isfinite(state.left_y))
                fail("Player input heading exceeds the finite native atan2 profile.");
            const float camera_heading=static_cast<float>(std::atan2(static_cast<double>(forward[0]),static_cast<double>(forward[2])));
            const float pad_heading=static_cast<float>(std::atan2(static_cast<double>(state.left_x),-static_cast<double>(state.left_y)));
            state.work->motion.turn.desired[1]=camera_heading+pad_heading;
            state.work->key_input=1;state.phase=Phase::buttons;break;
        }
        case Phase::buttons: {
            // C6CC/C6D0/C6D8: later pad button fields remain live after camera work.
            const auto main_lobby=lobby(*state.work);
            state.trigger=state.pad->trigger;state.held=state.pad->held;
            if(main_lobby!=0) {state.phase=Phase::decide;break;}
            if(sequence(*state.work)!="SQ_JUMP" && (state.trigger&pad_button::b)!=0)
                state.work->key_event_name="SQ_GUARD";
            if((state.held&pad_button::b)==0) {state.work->body_attack_state=0;state.phase=Phase::decide;break;}
            state.body_timer=state.work->body_attack_timer;
            state.phase=state.body_timer>0 ? Phase::body_delta:Phase::body;break;
        }
        case Phase::body_delta: {
            float delta;
            if(services.frame_delta) {
                const auto supplied=services.frame_delta(*manager);check();
                if(!supplied)return need("Player input awaits original system frame delta.");
                delta=*supplied;
            }else {
                // C3FC retains invoking controller r30. C788/C78C read its
                // current system only after C778 captured the positive f2.
                const auto system=manager->system;
                if(!system)fail("Player input dereferences the invoking controller's null current system.");
                delta=system->frame_time_f;
            }
            const float remaining=state.body_timer-delta;
            state.work->body_attack_timer=remaining>=0 ? remaining:0;
            state.phase=Phase::body;break;
        }
        case Phase::body:
            switch(state.work->body_attack_state) {
            case 0:if(state.work->key_input==0)state.work->body_attack_state=1;break;
            case 1:if(state.work->key_input==1) {
                state.work->body_attack_timer=15;state.work->body_attack_pad=state.direction;state.work->body_attack_state=2;
            }break;
            case 2:
                if(state.work->body_attack_timer==0)state.work->body_attack_state=0;
                else if(state.work->key_input==0)state.work->body_attack_state=3;
                break;
            case 3:
                if(state.work->body_attack_timer==0)state.work->body_attack_state=0;
                else if(state.work->key_input!=0) {
                    if(state.direction==state.work->body_attack_pad && sequence(*state.work)!="SQ_JUMP" && sequence(*state.work)!="SQ_FALL") {
                        state.work->key_event_name="SQ_ATCK3";state.work->control.mode=0;state.work->control.phase=0;
                    }
                    state.work->body_attack_state=0;
                }break;
            default:break;
            }
            state.phase=Phase::decide;break;
        case Phase::decide:
            if(sequence(*state.work)=="SQ_JUMP" || sequence(*state.work)=="SQ_FALL") {state.phase=Phase::completed;break;}
            if(state.work->panel!=0) {
                state.send_to("NDMWPANEL",{},state.ram->number,"ISCHECK",Payload::none,Phase::panel_checked);break;
            }
            if((state.trigger&pad_button::x)!=0) {state.work->key_event_name="SQ_JUMP";state.phase=Phase::completed;break;}
            if((state.trigger&pad_button::y)!=0) {state.open_panel("GENEEMGHEAL");break;}
            if((state.trigger&pad_button::l)!=0) {state.open_panel("GENERATEITEM");break;}
            if((state.trigger&pad_button::r)!=0) {state.open_panel("GENERATEWAZA");break;}
            if((state.trigger&pad_button::z)!=0) {state.open_panel("GENERATESTAT");break;}
            if((state.trigger&pad_button::a)!=0) {
                if(lobby(*state.work)!=0) {
                    ModelHitboxSubcommand command;command.kind=60000;command.delay=1;command.length=1;
                    command.offset={0,4,7};command.box_size={3,3,3};
                    ModelSubcommandQueue(state.work->subcommands).append_hitbox(command);state.phase=Phase::completed;
                }else state.send_to("DROPITEM",{},-1,"ACTION1",Payload::drop,Phase::drop_checked);
                break;
            }
            if(state.work->key_input==0 && (state.held&~pad_button::start)==0 && (state.trigger&pad_button::start)!=0)
                state.send_to("NDMWPANEL",{},-1,"ISRUN",Payload::none,Phase::panel_run_checked);
            else state.phase=Phase::completed;
            break;
        case Phase::panel_checked:
            if(messenger.buffer().booleans().empty() || boolean(messenger,0)==0)state.work->panel=0;
            else if(boolean(messenger,2)!=0) {
                if(boolean(messenger,1)!=0)state.work->key_event_name="SQ_ITEM";
                else {
                    const auto& strings=messenger.buffer().strings();
                    if(strings.empty())fail("Player panel response has no physical label string slot.");
                    ModelSequenceLabels(state.work->sequence.labels).set("SQ_WAZA",strings[0]);
                    state.work->key_event_name="SQ_WAZA";
                }
                state.phase=Phase::completed;break;
            }
            state.phase=Phase::panel_more;break;
        case Phase::panel_more:
            if((state.trigger&pad_button::x)!=0) {state.work->key_event_name="SQ_JUMP";state.phase=Phase::completed;}
            else if((state.trigger&pad_button::y)!=0)state.open_panel("GENEEMGHEAL");
            else if(state.work->key_input==0 && (state.held&~pad_button::start)==0 && (state.trigger&pad_button::start)!=0)state.sound();
            else state.phase=Phase::completed;
            break;
        case Phase::panel_opened:
            // 8030690C reads response count only; a false first value still opens.
            if(!messenger.buffer().booleans().empty())state.work->panel=1;
            state.phase=Phase::completed;break;
        case Phase::drop_checked:
            state.phase=messenger.buffer().booleans().empty() ? Phase::delay_check:Phase::completed;break;
        case Phase::delay_check:
            if(!state.timer)state.timer=std::make_unique<ModelRamTimerAccess>(state.ram);
            state.phase=state.timer->expired("ATDELAY") ? Phase::attack_choice:Phase::completed;
            check();break;
        case Phase::attack_choice:
            state.work->key_event_name=state.spin ? "SQ_ATCK2":"SQ_ATCK1";
            if(state.spin) {
                const auto history=state.work->turn_attack_timers;
                if(!history)fail("Spin attack clears a null current turn-attack history list.");
                std::fill(history->begin(),history->end(),0);
            }
            state.phase=Phase::player_row;break;
        case Phase::player_row:
            if(!services.player_row)return need("Player input requires the actual player-data manager row.");
            state.row=services.player_row(*manager,state.ram->number);check();
            if(!state.row)fail("Player input dereferences its selected null player-data row.");
            state.attack_delay=state.row->require<GameDataInt>("ATTACKDELAY")->data;
            state.phase=Phase::timer_start;break;
        case Phase::timer_start:
            if(!state.timer)state.timer=std::make_unique<ModelRamTimerAccess>(state.ram);
            state.timer->start("ATDELAY",static_cast<std::int64_t>(state.attack_delay)*1000000000/60);
            check();state.phase=Phase::icon_prepare;break;
        case Phase::icon_prepare: {
            const auto difference=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(state.attack_delay)-22u);
            const auto index=difference/4;
            const auto number=state.ram->number;
            if(index<0 || index>=13)fail("Player input attack-delay icon has no represented original table slot.");
            const auto ordinal=index+1;
            state.send_to("MODELCTRL","i_stpnl05",number,"SQ_DLTM"+(ordinal<10 ? std::string("0"):std::string{})+
                std::to_string(ordinal),Payload::none,Phase::completed);break;
        }
        case Phase::panel_run_checked:
            if(!messenger.buffer().booleans().empty() && boolean(messenger,0)==1)state.sound();
            else state.send_to("SUBWND",{},0,"GENERATE",Payload::status,Phase::completed);
            break;
        case Phase::send_prepare:
            if(!messenger.has_category(state.category))return {false,0,"Player input requires its actual "+state.category+" provider."};
            messenger.select(state.category,state.target_name,state.target_number);
            switch(state.payload) {
            case Payload::none:break;
            case Payload::work:messenger.buffer().add_game_object(state.work);break;
            case Payload::sound:messenger.buffer().add_string("CURSOR_NG");break;
            case Payload::drop:messenger.buffer().add_int(state.ram->number);messenger.buffer().set_enabled_only(1);break;
            case Payload::status:messenger.buffer().add_int(state.ram->number);messenger.buffer().add_string("STATUS");break;
            }
            state.dispatch.emplace(messenger.begin_send(state.command));state.phase=Phase::send;break;
        case Phase::send: {
            const auto result=events.resume(*state.dispatch);check();
            if(result.stop==MessengerStop::faulted)fail(result.diagnostic.c_str());
            if(result.stop!=MessengerStop::completed)return {false,0,result.diagnostic};
            state.dispatch.reset();state.phase=state.after_send;break;
        }
        case Phase::completed:return {true,0,{}};
        }
    }catch(...){state.fault=true;throw;}
}
} // namespace dw4
