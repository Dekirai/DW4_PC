#pragma once

#include "dw4/model_game_data.hpp"
#include "dw4/messenger.hpp"

#include <array>
#include <filesystem>

namespace dw4 {
class ActionEventRuntime;

// Owned original byte strings from the four-entry 804E7C70 pointer table.
// Synthetic callers supply explicit strings; no original names are compiled in.
class ModelPlayerStartDefaults {
public:
    explicit ModelPlayerStartDefaults(std::array<std::string,4> names);
    const std::array<std::string,4>& names() const noexcept { return names_; }
private:
    std::array<std::string,4> names_;
};
ModelPlayerStartDefaults read_model_player_start_defaults(const std::filesystem::path& main_dol);

// Explicit already captured caller state, not a reconstructed MODELCTRL RAM.
// work and the GETPLAYER result are retained. captured_parent_info/name identify
// the earlier info capture, even if the live RAM's parent later changes. The
// name cell remains live until GAMESTART packet construction; RAM number is
// re-read at each original use. These field owners must belong to that supplied
// native caller; this helper does not register them or create a player.
struct ModelPlayerStartOwner {
    std::shared_ptr<ModelScriptWork> work;
    ControllerObjectId captured_parent_info;
    std::string parent_source;
    std::shared_ptr<std::optional<std::string>> parent_name;
    std::shared_ptr<std::int32_t> live_ram_no;
};
enum class ModelPlayerStartPhase { binding, reset_modifiers, game_start, base_isuse,
    weapon_generate, recalculate, later_ui };
enum class ModelPlayerStartStop { service_required, later_ui_required };
struct ModelPlayerStartResult {
    ModelPlayerStartStop stop=ModelPlayerStartStop::service_required;
    std::string diagnostic;
};
using ModelPlayerClock=std::function<std::uint64_t()>;

// 8034D990..DCF4 on explicitly prepared state after GETPLAYER has completed.
// Includes HP binding, the short UPDATA/DOWNDATA resets, ISUSE/GAMESTART,
// BASE_ISUSE fill, deferred PLAYERARMS GENERATE and immediate RECALCPLAYER.
// The successful terminal checkpoint still requires original UI/caller work.
class ModelPlayerStart {
public:
    ModelPlayerStart(ModelPlayerStartOwner owner,std::shared_ptr<GameDataList> captured_row,
        ModelPlayerStartDefaults defaults);
    ~ModelPlayerStart();
    ModelPlayerStart(ModelPlayerStart&&) noexcept;
    ModelPlayerStart& operator=(ModelPlayerStart&&) noexcept;
    ModelPlayerStart(const ModelPlayerStart&)=delete;
    ModelPlayerStart& operator=(const ModelPlayerStart&)=delete;
    // The required callback reads the actual prepared beSystem/context clock
    // after deferred preparation, when original scheduling reads it. It must
    // only read that clock. No time is latched when this continuation starts.
    // Missing nested services retain the same dispatch and committed prefix.
    // Exceptions fault this owner; no automatic buffer restoration or replay.
    ModelPlayerStartResult resume(NativeMessenger&,ActionEventRuntime&,const ModelPlayerClock& live_clock);
    // Explicit constant-clock convenience for prepared callers whose context
    // cannot advance during nested callbacks. Not a live clock implementation.
    ModelPlayerStartResult resume(NativeMessenger&,ActionEventRuntime&,std::uint64_t context_time);
    ModelPlayerStartPhase phase() const;
    bool prefix_completed() const noexcept;
    bool faulted() const noexcept;
    const ModelPlayerStartOwner& owner() const;
    const std::shared_ptr<GameDataList>& row() const;
    const ModelGameDataBinding& binding() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
