#pragma once

#include "dw4/model_subcommands.hpp"

namespace dw4 {
class ModelSceneRam;
class ModelSceneInfo;
class ModelSceneOne;
class ModelPlayerWork;
struct ModelSceneManager;

// Captures belong to one reached virtual helper, not the whole wrapper pass.
// The manager is weak because it can own the enclosing frame continuation.
struct ModelSubcommandUpdateScope {
    std::shared_ptr<ModelSceneRam> ram;
    std::weak_ptr<const ModelSceneManager> manager;
    std::shared_ptr<ModelSceneInfo> parent;
    std::shared_ptr<ModelSceneOne> one;
    std::shared_ptr<ModelPlayerWork> work;
};
struct ModelSubcommandUpdateServices {
    // Providers execute the actual effects through the captured manager. An
    // unavailable result must retain any internal continuation without replay.
    std::function<ModelSoundPlayResult(const ModelSubcommandUpdateScope&,
        const std::optional<std::string>&)> play_sound;
    std::function<ModelEffectSpaceResult(const ModelSubcommandUpdateScope&,
        const std::optional<std::string>&)> find_effect_space;
    std::function<bool(const ModelSubcommandUpdateScope&,
        const ModelEffectGenerateRequest&)> generate_effect;
    // Owns the remaining 8030FEEC geometry/debug/CHKBULLET/BHit operation.
    // Its context strings are not prepared: use the actual scope for the
    // original late source/attachment reads, after geometry and debug effects.
    std::function<bool(const ModelSubcommandUpdateScope&,
        const ModelHitboxCheckRequest&)> check_hitbox;
};

// One actual 803107E0(wrapper, RAM) invocation. The caller supplies exactly
// the wrapper captured from work+E4; this never looks up a replacement wrapper.
// Per-entry 80310754 reads current RAM.manager -> system -> frame_time_f.
class ModelSubcommandUpdateInvocation {
public:
    ModelSubcommandUpdateInvocation(std::shared_ptr<ModelSceneRam>,
        std::shared_ptr<ModelSubcommandWrapper> captured_wrapper);
    ~ModelSubcommandUpdateInvocation();
    ModelSubcommandUpdateInvocation(const ModelSubcommandUpdateInvocation&)=delete;
    ModelSubcommandUpdateInvocation& operator=(const ModelSubcommandUpdateInvocation&)=delete;
    ModelSubcommandResult resume(std::shared_ptr<ModelSubcommandUpdateServices> services={});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const ModelSubcommandUpdateScope& captured_scope() const noexcept;
    std::shared_ptr<ModelSubcommandWrapper> captured_wrapper() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace dw4
