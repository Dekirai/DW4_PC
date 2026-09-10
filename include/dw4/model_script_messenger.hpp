#pragma once

#include "dw4/lua4.hpp"
#include "dw4/model_script_context.hpp"

#include <memory>

namespace dw4 {
class ActionEventRuntime;

// The recovered BLPcalcKoukaMagic/BLTcalcSkillUp wrappers on the current shared
// MODELCTRL context and the actual action-event messenger. This owns retained
// delivery, not the GAMERAM FIGHTCALC effects or a private argument buffer.
class ModelScriptMessenger {
public:
    explicit ModelScriptMessenger(ActionEventRuntime& runtime);
    ~ModelScriptMessenger();
    ModelScriptMessenger(const ModelScriptMessenger&)=delete;
    ModelScriptMessenger& operator=(const ModelScriptMessenger&)=delete;
    ModelScriptMessenger(ModelScriptMessenger&&)=delete;
    ModelScriptMessenger& operator=(ModelScriptMessenger&&)=delete;
    // Install through ModelScriptRuntime::set_host_service. The runtime and
    // this bridge must outlive every retained call. Nested calls use the same
    // context/bridge and resume through their still-owning parent callback.
    Lua4HostResult handle(ModelScriptContextRuntime& context,const Lua4HostCall& call);
    bool pending() const noexcept;
    bool faulted() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
