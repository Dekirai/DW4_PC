#pragma once

#include "dw4/model_player_start.hpp"

namespace dw4 {
// Ordered original map-name tables at 804E7C80 and 804E7C94. Synthetic callers
// provide explicit names; the native port does not embed original table bytes.
class ModelPlayerMapRules {
public:
    ModelPlayerMapRules(std::array<std::string,5> metal,std::array<std::string,4> main_lobby);
    const auto& metal() const noexcept { return metal_; }
    const auto& main_lobby() const noexcept { return main_lobby_; }
private:
    std::array<std::string,5> metal_;
    std::array<std::string,4> main_lobby_;
};
ModelPlayerMapRules read_model_player_map_rules(const std::filesystem::path& main_dol);
float original_player_hp_percent(std::int32_t hp,std::int32_t maximum);
float original_player_mp_percent(std::int32_t mp,std::int32_t maximum);

// Read the actual prepared manager/context map-name pointer when the caller
// reaches 8034DEAC. Retain that allocation through SETMETAL; replacing the live
// field afterward must not replace the captured name. Null is a checked fault.
using ModelPlayerMapReader=std::function<std::shared_ptr<const std::string>()>;
enum class ModelPlayerStartupPhase { prefix,level,hp_meter,hp_number,mp_meter,
    mp_number,map_capture,metal,main_lobby,completed };
enum class ModelPlayerStartupStop { service_required,map_context_required,completed };
struct ModelPlayerStartupResult {
    ModelPlayerStartupStop stop=ModelPlayerStartupStop::service_required;
    std::string diagnostic;
};

// Prepared original 8034D990..8034DF3C, after a real GETPLAYER has returned.
// Own the existing prefix, all five UI sends and the map-dependent tail.
// Completion means this caller returned; full player construction, graphics
// and original outer scheduling remain separate requirements.
class ModelPlayerStartup {
public:
    ModelPlayerStartup(ModelPlayerStartOwner,std::shared_ptr<GameDataList>,
        ModelPlayerStartDefaults,ModelPlayerMapRules);
    ModelPlayerStartup(ModelPlayerStartCapture,std::shared_ptr<GameDataList>,
        ModelPlayerStartDefaults,ModelPlayerMapRules);
    ~ModelPlayerStartup();
    ModelPlayerStartup(ModelPlayerStartup&&) noexcept;
    ModelPlayerStartup& operator=(ModelPlayerStartup&&) noexcept;
    ModelPlayerStartup(const ModelPlayerStartup&)=delete;
    ModelPlayerStartup& operator=(const ModelPlayerStartup&)=delete;
    ModelPlayerStartupResult resume(NativeMessenger&,ActionEventRuntime&,
        const ModelPlayerClock&,const ModelPlayerMapReader& = {});
    ModelPlayerStartupPhase phase() const;
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const ModelPlayerStart& prefix() const;
    const std::shared_ptr<const std::string>& captured_map() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
