#pragma once

#include "dw4/game_database.hpp"

namespace dw4 {
class GameNameConverter;
struct GameNameServices {
    // Resolve the actual beFont in this prepared manager's context. A mapping
    // table alone does not establish a font or its mutable conversion scratch.
    std::function<std::shared_ptr<GameNameConverter>()> font;
};
using GameStartName=std::variant<GameDataObject,std::optional<std::string>>;

class GameBaseToCurrent {
public:
    GameBaseToCurrent(std::int32_t source_ram,std::int32_t species);
    ~GameBaseToCurrent();
    GameBaseToCurrent(GameBaseToCurrent&&) noexcept;
    GameBaseToCurrent& operator=(GameBaseToCurrent&&) noexcept;
    GameBaseToCurrent(const GameBaseToCurrent&)=delete;
    GameBaseToCurrent& operator=(const GameBaseToCurrent&)=delete;
    MessengerCallResult resume(GameDataState&,GameDatabaseCache&,NativeMessenger&,ActionEventRuntime&);
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// One selected original GAMESTART route, after the dispatcher's mandatory
// packet reads/cardinality checks. All common phases precede name replacement.
class GameStartRuntime {
public:
    GameStartRuntime(std::int32_t source_ram,std::optional<std::string> model,GameStartName name);
    ~GameStartRuntime();
    GameStartRuntime(GameStartRuntime&&) noexcept;
    GameStartRuntime& operator=(GameStartRuntime&&) noexcept;
    GameStartRuntime(const GameStartRuntime&)=delete;
    GameStartRuntime& operator=(const GameStartRuntime&)=delete;
    MessengerCallResult resume(GameDataState&,GameDatabaseCache&,NativeMessenger&,ActionEventRuntime&,const GameNameServices&);
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
