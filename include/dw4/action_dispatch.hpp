#pragma once

#include "dw4/action_queries.hpp"

namespace dw4 {
struct ModelSceneManager;

// Connected ACTION messenger adapter for the existing actor-aware runtime.
// Captures source/service owners and retains one exact delivery continuation.
// Its manager backlink is weak: storing the adapter in that manager must not
// create an ownership cycle. Ordinary dispatch takes the current context time
// once, as required by ActionRuntime's explicit timestamp-taking profile; it
// does not claim a complete original per-helper ACTION clock implementation.
// ISCOMEND/GETAPPEARANCELIST retain their existing query-only behavior. Other
// native queries remain explicit service boundaries. Only actual named action
// definitions and supported NDSP/ONFREEZE/OFFREEZE enter ActionRuntime.
MessengerManager make_action_dispatch_manager(const PlayerRuntimeBinding&,
    std::shared_ptr<ActionRuntime>,std::shared_ptr<AnimationSourceRuntime>,
    std::shared_ptr<ActionServices>,std::weak_ptr<const ModelSceneManager>,
    std::shared_ptr<ActionSceneQueryOwner> scene={});
} // namespace dw4
