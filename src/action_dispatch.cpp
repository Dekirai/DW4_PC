#include "dw4/action_dispatch.hpp"
#include "dw4/model_scene.hpp"

#include <algorithm>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* reason) {throw MessengerError(reason);}
bool query(std::string_view name) {
    return name=="ISCOMEND" || name=="ISRUN" || name=="GETNAMELIST" ||
        name=="GETAPPEARANCELIST" || name=="GETBONEMATRIX";
}
bool special(std::string_view name) {return name=="NDSP" || name=="ONFREEZE" || name=="OFFREEZE";}
struct ActionDispatchOwner {
    PlayerRuntimeBinding binding;
    std::shared_ptr<ActionRuntime> runtime;
    std::shared_ptr<AnimationSourceRuntime> sources;
    std::shared_ptr<ActionServices> services;
    std::weak_ptr<const ModelSceneManager> manager;
    MessengerManager queries;
    struct Pending {
        std::uint64_t dispatch{};
        std::size_t row{};
        ControllerObjectId recipient;
        std::string name;
        bool previous_broadcast{},accepted{};
        std::uint64_t time{};
        std::shared_ptr<const void> state;
    };
    std::optional<Pending> pending;
    bool running{},faulted{};

    ActionDispatchOwner(const PlayerRuntimeBinding& bound,std::shared_ptr<ActionRuntime> action,
        std::shared_ptr<AnimationSourceRuntime> library,std::shared_ptr<ActionServices> callbacks,
        std::weak_ptr<const ModelSceneManager> parent,std::shared_ptr<ActionSceneQueryOwner> scene)
        :binding(bound),runtime(std::move(action)),sources(std::move(library)),services(std::move(callbacks)),
          manager(std::move(parent)),queries(make_action_query_manager(binding,runtime,std::move(scene))) {
        if(!sources || !services)fail("ACTION dispatch requires retained animation source and service owners.");
        runtime->combiner().require_sources(*sources);
    }
    MessengerCallResult call(NativeMessenger& messenger,const MessengerDelivery& delivery,std::string_view command) {
        if(faulted)fail("This ACTION delivery endpoint is faulted and cannot be replayed.");
        // Queries may run synchronously inside an action broadcast. They do
        // not mutate the runtime or claim its pending execution continuation.
        MessengerCallResult checked;
        try {checked=queries.fallback(messenger,delivery,command);}
        catch(...) {if(pending)faulted=true;throw;}
        if(query(command))return checked;
        if(running) {faulted=true;fail("Recursive ACTION delivery cannot re-enter the same runtime endpoint.");}
        struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(running);
        try {
            // The provider above validates exact binding/program/actor and
            // recipient owners on every call. Unknown names publish nothing.
            if(!special(command) && std::none_of(runtime->program().actions().begin(),runtime->program().actions().end(),
                [&](const auto& action){return action.name==command;}))
                return {false,0,"ACTION requires its original native command service or an actual named action definition."};
            if(pending) {
                if(pending->dispatch!=delivery.dispatch_id || pending->row!=delivery.row_index ||
                    pending->recipient!=delivery.recipient || pending->name!=command)
                    fail("Resume the retained ACTION delivery before accepting another action command.");
                if(pending->state!=runtime->state_identity())
                    fail("The suspended ACTION runtime changed outside its retained delivery.");
            } else {
                pending=Pending{delivery.dispatch_id,delivery.row_index,delivery.recipient,std::string(command),
                    runtime->broadcast_pending() && command!="ONFREEZE" && command!="OFFREEZE",
                    false,0,runtime->state_identity()};
            }
            auto& call=*pending;
            if(call.previous_broadcast) {
                // Finish the earlier event before accepting this request.
                // Re-dispatching the new name here would lose that distinction.
                const auto result=runtime->resume(*sources,*services,call.time);
                if(faulted)fail("ACTION callback caught recursive endpoint failure; continuation remains faulted.");
                call.state=runtime->state_identity();
                if(result.stop==ActionStop::broadcast_required)return {false,0,"ACTION is delivering its retained earlier broadcast."};
                call.previous_broadcast=false;
            }
            if(!call.accepted) {
                // NDSP does not consume a clock. Inactive freeze is also an
                // original early exit. Other execution uses the prepared
                // runtime's one explicit timestamp from the live context.
                if(call.name!="NDSP" && !((call.name=="ONFREEZE" || call.name=="OFFREEZE") && !runtime->active())) {
                    const auto parent=manager.lock();
                    if(!parent || !parent->context)return {false,0,"ACTION dispatch requires its actual manager context clock."};
                    call.time=parent->context->world_time;
                }
                call.accepted=true;
                const auto result=runtime->dispatch(*sources,*services,call.name,call.time);
                if(faulted)fail("ACTION callback caught recursive endpoint failure; continuation remains faulted.");
                call.state=runtime->state_identity();return finish(result);
            }
            const auto result=runtime->resume(*sources,*services,call.time);
            if(faulted)fail("ACTION callback caught recursive endpoint failure; continuation remains faulted.");
            call.state=runtime->state_identity();return finish(result);
        } catch(...) {faulted=true;throw;}
    }
    MessengerCallResult finish(const ActionResult& result) {
        if(result.stop==ActionStop::end || result.stop==ActionStop::waiting) {
            pending.reset();return {true,1,{}};
        }
        switch(result.stop) {
        case ActionStop::unsupported_command:return {false,0,"ACTION reached an unsupported original command type."};
        case ActionStop::random_required:return {false,0,"ACTION requires its original random service."};
        case ActionStop::broadcast_required:return {false,0,"ACTION is delivering its retained broadcast."};
        case ActionStop::command_failed:return {false,0,"ACTION requires explicit recovery of its retained command failure."};
        default:fail("ACTION returned an unknown native continuation stop.");
        }
    }
};
}
MessengerManager make_action_dispatch_manager(const PlayerRuntimeBinding& binding,
    std::shared_ptr<ActionRuntime> runtime,std::shared_ptr<AnimationSourceRuntime> sources,
    std::shared_ptr<ActionServices> services,std::weak_ptr<const ModelSceneManager> manager,
    std::shared_ptr<ActionSceneQueryOwner> scene) {
    auto owner=std::make_shared<ActionDispatchOwner>(binding,std::move(runtime),std::move(sources),
        std::move(services),std::move(manager),std::move(scene));
    return {
        [](NativeMessenger&,const MessengerDelivery&) {return MessengerCallResult{true,0,{}};},
        [owner=std::move(owner)](NativeMessenger& messenger,const MessengerDelivery& delivery,std::string_view command) {
            return owner->call(messenger,delivery,command);
        }
    };
}
} // namespace dw4
