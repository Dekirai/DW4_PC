#pragma once

#include "dw4/model_player_create.hpp"

namespace dw4 {
struct ModelPlayerCreateDefinitions {
    ModelPlayerStartDefaults defaults;
    ModelPlayerMapRules maps;
};
struct ModelPlayerCreateServices {
    std::shared_ptr<const ModelPlayerCreateDefinitions> definitions;
    ModelPlayerClock clock;
    ModelPlayerMapReader map;
};

// One original 8030D910 call. The base's RAM/parent/ONE/work captures precede
// the independently resolved player +C4 capture. A retry preserves both, even
// through replacement of RAM.data, ONE.work or the RAM's current parent.
class ModelBaseInvocation {
public:
    ModelBaseInvocation(std::shared_ptr<ModelSceneRam>,std::optional<std::string> command);
    ~ModelBaseInvocation();
    ModelBaseInvocation(const ModelBaseInvocation&)=delete;
    ModelBaseInvocation& operator=(const ModelBaseInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&,const ModelPlayerCreateServices& = {});
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const std::shared_ptr<ModelSceneOne>& captured_one() const;
    const std::shared_ptr<ModelPlayerWork>& captured_work() const;
    const std::shared_ptr<ModelPlayerWork>& status_work() const;
    const std::shared_ptr<ModelPlayerWork>& outer_work() const;
    const ModelPlayerCreate* player_create() const noexcept;
    const ModelBaseInvocation* direct_entry() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
