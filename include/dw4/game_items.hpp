#pragma once

#include "dw4/game_data.hpp"
#include "dw4/messenger.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace dw4 {
class ActionEventRuntime;
struct GameDatabaseCache;
struct GameDataWeapon;

enum class GameItemQueryKind { integer,string };

// Original weapon virtual database lookup. Every instance sends a fresh
// DATABASE/weapon/0/GETDATABASE request and reads the live item record only
// after that delivery completes. Completed results are owned values.
class GameWeaponQuery {
public:
    GameWeaponQuery(std::shared_ptr<GameDataWeapon> item,GameItemQueryKind kind,
        std::optional<std::string> column);
    ~GameWeaponQuery();
    GameWeaponQuery(GameWeaponQuery&&) noexcept;
    GameWeaponQuery& operator=(GameWeaponQuery&&) noexcept;
    GameWeaponQuery(const GameWeaponQuery&)=delete;
    GameWeaponQuery& operator=(const GameWeaponQuery&)=delete;
    MessengerCallResult resume(NativeMessenger& messenger,ActionEventRuntime& events);
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::string& diagnostic() const noexcept;
    const std::shared_ptr<GameDataWeapon>& item() const noexcept;
    std::int32_t integer_value() const;
    const std::string& string_value() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Original 8036E124. Capture the current player row/list/species at first
// resume, clear that same DEVICEFOLDER list, then append three generated
// weapon owners through the shared shinka cache and messenger.
class GameDeviceFolderInitialization {
public:
    explicit GameDeviceFolderInitialization(std::int32_t source_ram);
    ~GameDeviceFolderInitialization();
    GameDeviceFolderInitialization(GameDeviceFolderInitialization&&) noexcept;
    GameDeviceFolderInitialization& operator=(GameDeviceFolderInitialization&&) noexcept;
    GameDeviceFolderInitialization(const GameDeviceFolderInitialization&)=delete;
    GameDeviceFolderInitialization& operator=(const GameDeviceFolderInitialization&)=delete;
    MessengerCallResult resume(GameDataState& data,GameDatabaseCache& caches,
        NativeMessenger& messenger,ActionEventRuntime& events);
    std::int32_t source_ram() const noexcept;
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::string& diagnostic() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Original 8036E3B8 with flag byte zero. This is a separate entry and therefore
// captures the then-current row, not the preceding initialization's old row.
// POWERSOCKET receives shared weapon aliases before each MODEL query; the
// corresponding name append precedes the live equipped-byte write.
class GamePowerSocketInitialization {
public:
    explicit GamePowerSocketInitialization(std::int32_t source_ram);
    ~GamePowerSocketInitialization();
    GamePowerSocketInitialization(GamePowerSocketInitialization&&) noexcept;
    GamePowerSocketInitialization& operator=(GamePowerSocketInitialization&&) noexcept;
    GamePowerSocketInitialization(const GamePowerSocketInitialization&)=delete;
    GamePowerSocketInitialization& operator=(const GamePowerSocketInitialization&)=delete;
    MessengerCallResult resume(GameDataState& data,GameDatabaseCache& caches,
        NativeMessenger& messenger,ActionEventRuntime& events);
    std::int32_t source_ram() const noexcept;
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::string& diagnostic() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
