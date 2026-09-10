#pragma once

#include "dw4/messenger.hpp"
#include "dw4/player_runtime_binding.hpp"
#include "dw4/actor_scene.hpp"

namespace dw4 {
// One initial scene preparation may satisfy a suspended GETAPPEARANCELIST.
// Published owners cannot be replaced by a second equal-looking loaded graph.
class ActionSceneQueryOwner {
public:
    ActionSceneQueryOwner()=default;
    ActionSceneQueryOwner(const ActionSceneQueryOwner&)=delete;
    ActionSceneQueryOwner& operator=(const ActionSceneQueryOwner&)=delete;
    ActionSceneQueryOwner(ActionSceneQueryOwner&&)=delete;
    ActionSceneQueryOwner& operator=(ActionSceneQueryOwner&&)=delete;
    void prepare(const PlayerRuntimeBinding& binding,ActorSceneAction scene);
    const std::optional<ActorSceneAction>& scene() const noexcept { return scene_; }
private:
    friend MessengerManager make_action_query_manager(const PlayerRuntimeBinding&,
        std::shared_ptr<ActionRuntime>,std::shared_ptr<ActionSceneQueryOwner>);
    void bind(const PlayerRuntimeBinding&);
    void validate_binding(const PlayerRuntimeBinding&) const;
    std::optional<PlayerRuntimeBinding> binding_;
    std::optional<ActorSceneAction> scene_;
};
// Original ACTION manager +90 returns zero; +8C implements ISCOMEND and the
// active-gated GETAPPEARANCELIST with its actual prepared scene owners.
// Retains the live actor-aware runtime and its binding, not a state snapshot.
// Invalid identity throws MessengerError. Missing selected query work and other
// commands stop explicitly before appending a result. ISCOMEND has no activity
// gate; neither query samples, advances clocks or broadcasts. Activity stays owned
// by the caller, which must not assign one runtime to different qualified RAMs.
MessengerManager make_action_query_manager(const PlayerRuntimeBinding& binding,
    std::shared_ptr<ActionRuntime> runtime,std::shared_ptr<ActionSceneQueryOwner> scene={});
} // namespace dw4
