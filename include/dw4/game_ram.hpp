#pragma once

#include "dw4/controllers.hpp"
#include "dw4/game_data.hpp"

#include <memory>

namespace dw4 {
class ActionEventRuntime;

// Explicit generated beNDMWGameRam ownership on an already prepared action-
// event messenger. Native factory data are not selected-player/save state.
// Skill selection retains actual row/database owners through nested delivery;
// full skill growth and selected-player stat/equipment preparation are required.
class GameRamRuntime {
public:
    explicit GameRamRuntime(ActionEventRuntime& runtime);
    ~GameRamRuntime();
    GameRamRuntime(const GameRamRuntime&)=delete;
    GameRamRuntime& operator=(const GameRamRuntime&)=delete;
    GameRamRuntime(GameRamRuntime&&)=delete;
    GameRamRuntime& operator=(GameRamRuntime&&)=delete;
    ControllerObjectId manager_id() const noexcept;
    ControllerObjectId info_id() const noexcept;
    ControllerObjectId ram_id() const noexcept;
    // GETPLAYER returns these existing row objects. Their shared wrappers can
    // be bound directly to model work; a row reset never retargets old aliases.
    const GameDataState& state() const noexcept;
    std::size_t pending_count() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace dw4
