#include "dw4/model_subcommand_update.hpp"
#include "dw4/model_player_work.hpp"
#include "dw4/model_scene.hpp"

#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) {throw ModelSceneError(message);}
std::shared_ptr<const ModelSceneManager> require_manager(
    const std::weak_ptr<const ModelSceneManager>& reference) {
    auto manager=reference.lock();
    if(!manager)fail("Actual subcommand update reached a null or expired manager reference.");
    return manager;
}
std::shared_ptr<ModelSceneOne> require_one(ModelSceneRam& ram) {
    auto one=find_model_one(ram);
    if(!one)fail("Actual subcommand virtual helper reached a missing ONE record.");
    return one;
}
}

struct ModelSubcommandUpdateInvocation::Impl {
    explicit Impl(std::shared_ptr<ModelSceneRam> ram,std::shared_ptr<ModelSubcommandWrapper> wrapper)
        :queue(std::move(wrapper)) {
        if(!ram)fail("Actual subcommand updating requires its RAM owner.");scope.ram=std::move(ram);
    }
    ModelSubcommandQueue queue;
    ModelSubcommandUpdateScope scope;
    std::shared_ptr<ModelSubcommandActualAccess> access;
    std::shared_ptr<ModelSubcommandUpdateServices> services;
    bool started{},complete{},executing{},fault{};

    void check() const {
        if(fault || queue.faulted())fail("Caught recursive actual subcommand updating invalidated this invocation.");
    }
    ModelSubcommandActualAccess::Frames frames(const ModelSubcommandEntry& entry) {
        // 80310764,68,6C,70: manager first, elapsed next, then system/delta.
        const auto manager=require_manager(scope.ram->manager);
        const auto elapsed=entry.elapsed;
        const auto system=manager->system;
        if(!system)fail("Actual subcommand updating reached the manager's null system owner.");
        return {elapsed,system->frame_time_f};
    }
    void begin(const ModelSubcommandEntry& entry) {
        scope.manager.reset();scope.parent.reset();scope.one.reset();scope.work.reset();
        if(std::holds_alternative<ModelSoundSubcommandState>(entry.value)) {
            // 803105FC: no parent, ONE or work is consumed for SOUND.
            scope.manager=scope.ram->manager;require_manager(scope.manager);
        } else if(std::holds_alternative<ModelEffectSubcommandState>(entry.value)) {
            // 80310368/6C: both captured before the first messenger call.
            scope.manager=scope.ram->manager;scope.parent=scope.ram->parent.lock();
            require_manager(scope.manager);
        } else {
            // 8030FF10 precedes data preparation and ONE lookup. Manager and
            // work are read at FFD0/D4, after the lookup returns its owner.
            scope.parent=scope.ram->parent.lock();scope.one=require_one(*scope.ram);
            scope.manager=scope.ram->manager;scope.work=scope.one->work;
            // Null work is permitted by the later original debug-work gate.
            // Geometry and the subsequent manager+28 read belong to provider.
        }
    }
    ModelSubcommandContext effect_context() {
        // FIND_SPACE has completed and selected the retained effect RAM.
        // 803103BC reads source RAM before 103C4 reads captured parent.name.
        const auto number=scope.ram->number;
        if(!scope.parent)fail("Actual effect generation reached a null captured parent info.");
        ModelSubcommandContext context;
        context.source={scope.parent->name.value_or(""),number};
        // 803103EC..104A8: resolve the CURRENT RAM data/ONE and its work now,
        // independently of both the outer frame and prior effect entries.
        scope.one=require_one(*scope.ram);scope.work=scope.one->work;
        if(!scope.work)fail("Actual effect generation reached its fresh ONE's null work.");
        if(scope.work->hit_parent_name)
            context.attachment=ModelSubcommandOwner{*scope.work->hit_parent_name,scope.work->hit_parent_ram};
        else {
            // Original fallback reloads name and RAM separately, after ONE.
            const auto name=scope.parent->name.value_or("");
            context.attachment=ModelSubcommandOwner{name,scope.ram->number};
        }
        return context;
    }
    ModelSubcommandServices callbacks() {
        ModelSubcommandServices result;
        result.play_sound=[this](const auto& name) {
            if(!services || !services->play_sound)return ModelSoundPlayResult{};
            require_manager(scope.manager);
            const auto result=services->play_sound(scope,name);check();return result;
        };
        result.find_effect_space=[this](const auto& name) {
            if(!services || !services->find_effect_space)return ModelEffectSpaceResult{};
            require_manager(scope.manager);
            const auto result=services->find_effect_space(scope,name);check();return result;
        };
        result.generate_effect=[this](const auto& request) {
            if(!services || !services->generate_effect)return false;
            require_manager(scope.manager);
            const auto result=services->generate_effect(scope,request);check();return result;
        };
        if(services && services->check_hitbox)result.check_hitbox=[this](const auto& request) {
            const auto result=services->check_hitbox(scope,request);check();return result;
        };
        return result;
    }
};

ModelSubcommandUpdateInvocation::ModelSubcommandUpdateInvocation(std::shared_ptr<ModelSceneRam> ram,
    std::shared_ptr<ModelSubcommandWrapper> wrapper)
    :impl_(std::make_shared<Impl>(std::move(ram),std::move(wrapper))) {
    impl_->access=std::make_shared<ModelSubcommandActualAccess>();
    const std::weak_ptr<Impl> weak=impl_;
    const auto retain=[weak] {
        auto owner=weak.lock();
        if(!owner)fail("Actual subcommand access outlived its invocation.");return owner;
    };
    impl_->access->read_frames=[retain](const auto& entry){return retain()->frames(entry);};
    impl_->access->begin_virtual=[retain](const auto& entry){retain()->begin(entry);};
    impl_->access->effect_context=[retain]{return retain()->effect_context();};
}
ModelSubcommandUpdateInvocation::~ModelSubcommandUpdateInvocation()=default;
bool ModelSubcommandUpdateInvocation::completed() const noexcept {return impl_->complete;}
bool ModelSubcommandUpdateInvocation::faulted() const noexcept {return impl_->fault || impl_->queue.faulted();}
const ModelSubcommandUpdateScope& ModelSubcommandUpdateInvocation::captured_scope() const noexcept {return impl_->scope;}
std::shared_ptr<ModelSubcommandWrapper> ModelSubcommandUpdateInvocation::captured_wrapper() const noexcept {
    return impl_->queue.wrapper_owner();
}
ModelSubcommandResult ModelSubcommandUpdateInvocation::resume(std::shared_ptr<ModelSubcommandUpdateServices> services) {
    auto& state=*impl_;
    if(state.fault || state.queue.faulted())fail("A faulted actual subcommand invocation cannot replay its prefix.");
    if(state.executing || (state.services && services && state.services!=services)) {
        state.fault=true;fail("Recursive actual subcommand execution or replacement of its service owner is unsupported.");
    }
    if(state.complete)return {};
    if(!state.services)state.services=std::move(services);
    state.executing=true;
    try {
        const auto callbacks=state.callbacks();
        ModelSubcommandResult result;
        if(!state.started) {
            state.started=true;result=state.queue.update_actual(state.access,callbacks);
        } else result=state.queue.resume_actual(state.access,callbacks);
        state.check();state.complete=result.complete();state.executing=false;return result;
    } catch(...) {state.fault=true;state.executing=false;throw;}
}
} // namespace dw4
