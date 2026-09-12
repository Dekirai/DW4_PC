#pragma once

#include "dw4/model_script_context.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
class ActionEventRuntime;

// Explicit prepared beNDMWMdlPEBaseInfoWork fields on the same actual ONE work
// used by MODELCTRL. Null lists are real null fields, not empty-list defaults.
// Full construction and the status update loop are separate requirements.
class ModelStatusWork {
public:
    ModelStatusWork(std::shared_ptr<ModelScriptWork> actual_work,std::uint8_t is_player);
    ModelStatusWork(const ModelStatusWork&)=delete;
    ModelStatusWork& operator=(const ModelStatusWork&)=delete;
    ModelStatusWork(ModelStatusWork&&)=delete;
    ModelStatusWork& operator=(ModelStatusWork&&)=delete;
    const std::shared_ptr<ModelScriptWork>& actual_work() const noexcept { return actual_work_; }
    std::uint8_t is_player;
    std::shared_ptr<std::vector<std::int32_t>> display_ram_numbers,display_positions,timers;
    std::shared_ptr<std::vector<std::uint8_t>> first_flags;
    std::shared_ptr<std::vector<std::int32_t>> down_data,up_data;
private:
    std::shared_ptr<ModelScriptWork> actual_work_;
};

struct ModelStatusOwner {
    // Captured actual base work; a null pointer is the original null-work case.
    std::shared_ptr<ModelStatusWork> work;
    // The actual RAM+08 field, read only when the DOWNDATA query is prepared.
    std::shared_ptr<std::int32_t> live_ram_no;
};

// One invocation of the player's virtual+C4 status handler803AEBB4, after the
// original ONE lookup. The root dispatcher retains one instance per delivery.
// Unknown commands decline before allocations. Other known status commands
// retain their original allocation prefix but require their actual service.
class ModelStatusInvocation {
public:
    ModelStatusInvocation(ModelStatusOwner,std::optional<std::string> command);
    ~ModelStatusInvocation();
    ModelStatusInvocation(ModelStatusInvocation&&) noexcept;
    ModelStatusInvocation& operator=(ModelStatusInvocation&&) noexcept;
    ModelStatusInvocation(const ModelStatusInvocation&)=delete;
    ModelStatusInvocation& operator=(const ModelStatusInvocation&)=delete;
    MessengerCallResult resume(NativeMessenger&,ActionEventRuntime&);
    bool completed() const noexcept;
    bool faulted() const noexcept;
    const ModelStatusOwner& owner() const;
    const std::shared_ptr<GameDataList>& captured_row() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
