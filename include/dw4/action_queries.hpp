#pragma once

#include "dw4/messenger.hpp"
#include "dw4/player_runtime_binding.hpp"

namespace dw4 {
// Original ACTION manager +90 returns zero; +8C implements checked ISCOMEND.
// Retains the live actor-aware runtime and its binding, not a state snapshot.
// Invalid identity throws MessengerError. Missing selected query work and other
// commands stop explicitly before appending a result. No activity gate, sampling,
// clock update or broadcast is implied. Registration and activity remain owned
// by the caller, which must not assign one runtime to different qualified RAMs.
MessengerManager make_action_query_manager(const PlayerRuntimeBinding& binding,
    std::shared_ptr<ActionRuntime> runtime);
} // namespace dw4
