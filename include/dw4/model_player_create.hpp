#pragma once

#include "dw4/model_player_startup.hpp"
#include "dw4/model_scene.hpp"

namespace dw4 {
enum class ModelPlayerCreatePhase { capture,install,player_fields,placement_choice,
    point_query,point_restore,placement_lookup,placement,get_player,startup,returned };
enum class ModelPlayerCreateStop { service_required,map_context_required,outer_generate_required };
struct ModelPlayerCreateResult {
    ModelPlayerCreateStop stop=ModelPlayerCreateStop::service_required;
    std::string diagnostic;
};

// Original player virtual +98, 8034D61C..8034DF3C. It operates on the registered
// RAM's actual mutable owner graph. Factory installation, fresh placement ONE
// lookup, GETPLAYER and startup are real operations, not replacement services.
// Returning here still requires the outer GENERATE continuation at 8030DA58.
class ModelPlayerCreate {
public:
    ModelPlayerCreate(std::shared_ptr<ModelSceneRam> actual_ram,
        ModelPlayerStartDefaults,ModelPlayerMapRules);
    ~ModelPlayerCreate();
    ModelPlayerCreate(ModelPlayerCreate&&) noexcept;
    ModelPlayerCreate& operator=(ModelPlayerCreate&&) noexcept;
    ModelPlayerCreate(const ModelPlayerCreate&)=delete;
    ModelPlayerCreate& operator=(const ModelPlayerCreate&)=delete;
    ModelPlayerCreateResult resume(NativeMessenger&,ActionEventRuntime&,
        const ModelPlayerClock&,const ModelPlayerMapReader& = {});
    ModelPlayerCreatePhase phase() const;
    bool returned() const noexcept;
    bool faulted() const noexcept;
    // Inspection only: the outer caller must reload its own captured ONE.work.
    const std::shared_ptr<ModelPlayerWork>& created_work() const;
    const std::shared_ptr<ModelSceneInfo>& captured_parent() const;
    const std::shared_ptr<ModelSceneOne>& captured_one() const;
    const std::shared_ptr<ModelSceneOne>& placement_one() const;
    const ModelPlayerStartup* startup() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
