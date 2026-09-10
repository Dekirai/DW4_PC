#pragma once

#include "dw4/messenger.hpp"

namespace dw4 {
class ActionEventRuntime;
struct GameDatabaseCache {
    MessengerDatabase shinka,pc_tech;
};

// One actual DATABASE/name/0/GETDATABASE operation. Lazy manager caches remain
// with their manager; item queries deliberately create a fresh operation each
// time. The result is current object zero after completed shared delivery.
class GameDatabaseQuery {
public:
    explicit GameDatabaseQuery(std::string name);
    ~GameDatabaseQuery();
    GameDatabaseQuery(GameDatabaseQuery&&) noexcept;
    GameDatabaseQuery& operator=(GameDatabaseQuery&&) noexcept;
    GameDatabaseQuery(const GameDatabaseQuery&)=delete;
    GameDatabaseQuery& operator=(const GameDatabaseQuery&)=delete;
    MessengerCallResult resume(NativeMessenger& messenger,ActionEventRuntime& events);
    const MessengerDatabase& database() const;
    bool completed() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
