#pragma once

#include "dw4/model_script_context.hpp"
#include "dw4/messenger.hpp"

namespace dw4 {
class ActionEventRuntime;

// Canonical beNDMWMdlPEBaseInfoWork fields. Lists initially are actual null
// references; status preparation allocates them later in original order.
// No facade, node, messenger or generation owner is stored back in this group.
struct ModelStatusFields {
    std::uint8_t is_player{};
    std::shared_ptr<std::vector<std::int32_t>> display_ram_numbers,display_positions,timers;
    std::shared_ptr<std::vector<std::uint8_t>> first_flags;
    std::int32_t poison_damage{},poison_endurance{}; // Work+100/+104.
    std::shared_ptr<std::vector<std::int32_t>> down_data,up_data;
};

// Explicit prepared beNDMWMdlPEBaseInfoWork fields on the same actual ONE work
// used by MODELCTRL. Null lists are real null fields, not empty-list defaults.
// Full construction and the status update loop are separate requirements.
class ModelStatusWork {
private:
    std::shared_ptr<ModelStatusFields> fields_;
public:
    // A captured canonical status group needs no script identity or node read.
    // actual_work() is null for this original owner-graph profile.
    explicit ModelStatusWork(std::shared_ptr<ModelStatusFields> actual_fields);
    ModelStatusWork(std::shared_ptr<ModelScriptWork> actual_work,std::uint8_t is_player);
    ModelStatusWork(std::shared_ptr<ModelScriptWork> actual_work,std::shared_ptr<ModelStatusFields> actual_fields);
    ModelStatusWork(const ModelStatusWork&)=delete;
    ModelStatusWork& operator=(const ModelStatusWork&)=delete;
    ModelStatusWork(ModelStatusWork&&)=delete;
    ModelStatusWork& operator=(ModelStatusWork&&)=delete;
    const std::shared_ptr<ModelScriptWork>& actual_work() noexcept { return actual_work_; }
    std::shared_ptr<const ModelScriptWork> actual_work() const noexcept { return actual_work_; }
    const std::shared_ptr<ModelStatusFields>& fields_owner() noexcept { return fields_; }
    std::shared_ptr<const ModelStatusFields> fields_owner() const noexcept { return fields_; }
    std::uint8_t& is_player() noexcept { return fields_->is_player; }
    const std::uint8_t& is_player() const noexcept { return fields_->is_player; }
    std::shared_ptr<std::vector<std::int32_t>>& display_ram_numbers() noexcept { return fields_->display_ram_numbers; }
    std::shared_ptr<const std::vector<std::int32_t>> display_ram_numbers() const noexcept { return fields_->display_ram_numbers; }
    std::shared_ptr<std::vector<std::int32_t>>& display_positions() noexcept { return fields_->display_positions; }
    std::shared_ptr<const std::vector<std::int32_t>> display_positions() const noexcept { return fields_->display_positions; }
    std::shared_ptr<std::vector<std::int32_t>>& timers() noexcept { return fields_->timers; }
    std::shared_ptr<const std::vector<std::int32_t>> timers() const noexcept { return fields_->timers; }
    std::shared_ptr<std::vector<std::uint8_t>>& first_flags() noexcept { return fields_->first_flags; }
    std::shared_ptr<const std::vector<std::uint8_t>> first_flags() const noexcept { return fields_->first_flags; }
    std::int32_t& poison_damage() noexcept { return fields_->poison_damage; }
    const std::int32_t& poison_damage() const noexcept { return fields_->poison_damage; }
    std::int32_t& poison_endurance() noexcept { return fields_->poison_endurance; }
    const std::int32_t& poison_endurance() const noexcept { return fields_->poison_endurance; }
    std::shared_ptr<std::vector<std::int32_t>>& down_data() noexcept { return fields_->down_data; }
    std::shared_ptr<const std::vector<std::int32_t>> down_data() const noexcept { return fields_->down_data; }
    std::shared_ptr<std::vector<std::int32_t>>& up_data() noexcept { return fields_->up_data; }
    std::shared_ptr<const std::vector<std::int32_t>> up_data() const noexcept { return fields_->up_data; }
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
