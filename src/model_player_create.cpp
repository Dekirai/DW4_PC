#include "dw4/model_player_create.hpp"

#include "dw4/action_event_runtime.hpp"
#include "dw4/model_placement.hpp"

#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw ModelSceneError(message);}
AnimationMatrix identity() {return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
}

struct ModelPlayerCreate::Impl {
    std::shared_ptr<ModelSceneRam> ram;
    std::shared_ptr<ModelSceneInfo> parent;
    std::shared_ptr<ModelSceneOne> one,placement_one;
    std::shared_ptr<ModelPlayerWork> work;
    ModelPlayerStartDefaults defaults;
    ModelPlayerMapRules maps;
    NativeMessenger* messenger{};
    ActionEventRuntime* events{};
    std::optional<MessengerDispatch> dispatch;
    std::optional<ModelPlacementInvocation> placement;
    std::unique_ptr<ModelPlayerStartup> startup;
    AnimationMatrix point{};
    ModelPlayerCreatePhase phase=ModelPlayerCreatePhase::capture;
    bool executing{},failed{};

    Impl(std::shared_ptr<ModelSceneRam> value,ModelPlayerStartDefaults names,ModelPlayerMapRules rules)
        :ram(std::move(value)),defaults(std::move(names)),maps(std::move(rules)) {}
    std::optional<ModelPlayerCreateResult> deliver() {
        const auto result=events->resume(*dispatch);
        if(failed)fail("A player construction callback attempted to resume its own active invocation.");
        if(result.stop==MessengerStop::faulted)fail("Original player construction message delivery faulted.");
        if(result.stop!=MessengerStop::completed)
            return ModelPlayerCreateResult{ModelPlayerCreateStop::service_required,result.diagnostic};
        dispatch.reset();return {};
    }
};

ModelPlayerCreate::ModelPlayerCreate(std::shared_ptr<ModelSceneRam> ram,
    ModelPlayerStartDefaults defaults,ModelPlayerMapRules maps) {
    if(!ram)fail("Original player construction requires the actual registered RAM owner.");
    impl_=std::make_unique<Impl>(std::move(ram),std::move(defaults),std::move(maps));
}
ModelPlayerCreate::~ModelPlayerCreate()=default;
ModelPlayerCreate::ModelPlayerCreate(ModelPlayerCreate&&) noexcept=default;
ModelPlayerCreate& ModelPlayerCreate::operator=(ModelPlayerCreate&&) noexcept=default;
ModelPlayerCreatePhase ModelPlayerCreate::phase() const {
    if(!impl_)fail("A moved player construction has no phase.");return impl_->phase;
}
bool ModelPlayerCreate::returned() const noexcept {return impl_ && impl_->phase==ModelPlayerCreatePhase::returned;}
bool ModelPlayerCreate::faulted() const noexcept {return !impl_ || impl_->failed;}
const std::shared_ptr<ModelPlayerWork>& ModelPlayerCreate::created_work() const {
    if(!impl_)fail("A moved player construction has no captured work.");return impl_->work;
}
const std::shared_ptr<ModelSceneInfo>& ModelPlayerCreate::captured_parent() const {
    if(!impl_)fail("A moved player construction has no captured parent.");return impl_->parent;
}
const std::shared_ptr<ModelSceneOne>& ModelPlayerCreate::captured_one() const {
    if(!impl_)fail("A moved player construction has no captured ONE.");return impl_->one;
}
const std::shared_ptr<ModelSceneOne>& ModelPlayerCreate::placement_one() const {
    if(!impl_)fail("A moved player construction has no placement ONE.");return impl_->placement_one;
}
const ModelPlayerStartup* ModelPlayerCreate::startup() const noexcept {return impl_ ? impl_->startup.get():nullptr;}

ModelPlayerCreateResult ModelPlayerCreate::resume(NativeMessenger& messenger,ActionEventRuntime& events,
    const ModelPlayerClock& clock,const ModelPlayerMapReader& map_reader) {
    events.require_model_execution_access();
    if(!impl_ || impl_->failed)fail("An uncertain player construction cannot be replayed.");
    auto& state=*impl_;
    if(state.executing) {
        state.failed=true;
        fail("A player construction callback cannot recursively resume its active invocation.");
    }
    if(state.messenger && (state.messenger!=&messenger || state.events!=&events))
        fail("A retained player construction requires the same nonrecursive messenger owners.");
    state.messenger=&messenger;state.events=&events;
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(state.executing);
    try {
        while(state.phase!=ModelPlayerCreatePhase::returned) {
            switch(state.phase) {
            case ModelPlayerCreatePhase::capture:
                // D640 captures this parent before any player factory or send.
                state.parent=state.ram->parent.lock();
                state.one=find_model_one(*state.ram);
                if(!state.one)fail("Original player construction reached a missing named ONE after ensuring RAM data.");
                state.phase=ModelPlayerCreatePhase::install;
                break;
            case ModelPlayerCreatePhase::install:
                state.work=make_model_player_work();
                state.one->work=state.work; // D76C replaces the reference, never old work fields.
                state.phase=ModelPlayerCreatePhase::player_fields;
                break;
            case ModelPlayerCreatePhase::player_fields:
                state.work->status.is_player=1;
                state.work->motion.turn.rate={0,7,0};
                state.work->turn_attack_timers=std::make_shared<std::vector<std::int32_t>>(8,0);
                state.phase=ModelPlayerCreatePhase::placement_choice;
                break;
            case ModelPlayerCreatePhase::placement_choice:
                // The vector-count branch precedes the matrix-count read.
                state.phase=!messenger.buffer().vectors().empty() || !messenger.buffer().matrices().empty() ?
                    ModelPlayerCreatePhase::placement_lookup:ModelPlayerCreatePhase::point_query;
                break;
            case ModelPlayerCreatePhase::point_query:
                if(!state.dispatch) {
                    state.point=identity();
                    messenger.push_buffer();
                    messenger.select("PLAYERPOINT",std::nullopt,-1);
                    messenger.buffer().add_int(0);
                    messenger.buffer().add_int(state.ram->number);
                    state.dispatch.emplace(messenger.begin_send("GETMATRIX"));
                }
                if(auto result=state.deliver())return *result;
                // Every matrix in the messenger has already passed its original
                // PSQ copy. No result leaves the actual local identity intact.
                if(!messenger.buffer().matrices().empty())state.point=messenger.buffer().matrices()[0];
                state.phase=ModelPlayerCreatePhase::point_restore;
                break;
            case ModelPlayerCreatePhase::point_restore:
                messenger.pop_buffer();
                messenger.buffer().add_matrix(state.point);
                state.phase=ModelPlayerCreatePhase::placement_lookup;
                break;
            case ModelPlayerCreatePhase::placement_lookup:
                // Placement is a fresh RAM-data/ONE lookup, independently of
                // the player scope's earlier ONE and installed work.
                state.placement_one=find_model_one(*state.ram);
                if(!state.placement_one)fail("Original player placement reached a missing named ONE.");
                state.placement.emplace([one=state.placement_one] {
                    const auto node=one->node;
                    return node ? node->placement_node():std::shared_ptr<ModelPlacementNode>{};
                });
                state.phase=ModelPlayerCreatePhase::placement;
                break;
            case ModelPlayerCreatePhase::placement: {
                const auto result=state.placement->resume(messenger.buffer());
                if(!result.completed)return {ModelPlayerCreateStop::service_required,result.diagnostic};
                state.phase=ModelPlayerCreatePhase::get_player;
                break;
            }
            case ModelPlayerCreatePhase::get_player:
                if(!state.dispatch) {
                    messenger.select("GAMERAM",std::nullopt,-1);
                    messenger.buffer().add_int(state.ram->number);
                    state.dispatch.emplace(messenger.begin_send("GETPLAYER"));
                }
                if(auto result=state.deliver())return *result;
                {
                    const auto& objects=messenger.buffer().objects();
                    const auto* row=objects.empty() ? nullptr:std::get_if<MessengerDataList>(&objects[0]);
                    if(!row || !*row)fail("Original player construction requires GETPLAYER object0 to be an actual nonnull game-data row.");
                    ModelPlayerStartCapture capture{
                        std::shared_ptr<ModelScriptFields>(state.work,&state.work->script),
                        state.parent ? std::shared_ptr<std::optional<std::string>>(state.parent,&state.parent->name):nullptr,
                        std::shared_ptr<std::int32_t>(state.ram,&state.ram->number)};
                    state.startup=std::make_unique<ModelPlayerStartup>(std::move(capture),*row,state.defaults,state.maps);
                }
                state.phase=ModelPlayerCreatePhase::startup;
                break;
            case ModelPlayerCreatePhase::startup: {
                const auto result=state.startup->resume(messenger,events,clock,map_reader);
                if(state.failed)fail("A player startup callback attempted to resume its own construction invocation.");
                if(result.stop!=ModelPlayerStartupStop::completed)
                    return {result.stop==ModelPlayerStartupStop::map_context_required ?
                        ModelPlayerCreateStop::map_context_required:ModelPlayerCreateStop::service_required,result.diagnostic};
                state.phase=ModelPlayerCreatePhase::returned;
                break;
            }
            case ModelPlayerCreatePhase::returned:break;
            }
        }
        return {ModelPlayerCreateStop::outer_generate_required,{}};
    }catch(...) {state.failed=true;throw;}
}
} // namespace dw4
