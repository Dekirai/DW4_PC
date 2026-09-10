#pragma once

#include "dw4/messenger.hpp"

#include <cstdint>
#include <memory>

namespace dw4 {
class ActionEventRuntime;
class GameDataState;
struct GameDatabaseCache;

// One original 8036FDE8 invocation. Completed publications and actual nested
// database requests remain owned by this continuation across service pauses.
class GameStatsRuntime final {
public:
    GameStatsRuntime(std::int32_t source_ram,std::uint8_t modifiers);
    ~GameStatsRuntime();
    GameStatsRuntime(const GameStatsRuntime&)=delete;
    GameStatsRuntime& operator=(const GameStatsRuntime&)=delete;
    GameStatsRuntime(GameStatsRuntime&&) noexcept;
    GameStatsRuntime& operator=(GameStatsRuntime&&) noexcept;

    MessengerCallResult resume(GameDataState&,GameDatabaseCache&,NativeMessenger&,ActionEventRuntime&);
    bool completed() const noexcept;
    bool faulted() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
