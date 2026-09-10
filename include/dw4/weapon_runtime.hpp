#pragma once

#include "dw4/actor_scene.hpp"
#include "dw4/controllers.hpp"
#include "dw4/weapon_resource.hpp"

#include <array>
#include <memory>
#include <vector>

namespace dw4 {
class ActionEventRuntime;
class WeaponRuntimeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
struct WeaponRamState {
    ControllerObjectId id,manager,info;
    std::int32_t index{};
    std::uint8_t active{};
    std::array<std::int32_t,4> words{};
    std::shared_ptr<const WeaponProgram> program;
    std::size_t info_index{};
    const WeaponInfoDefinition& definition() const {return program->infos().at(info_index);}
};
struct WeaponAttachment {
    std::shared_ptr<const ActorBoneSelector> selector;
    std::shared_ptr<ActorSceneAppearanceList> appearances;
};

// Prepared beWeapon manager using the existing messenger. Registration retains
// the manager and its four tracking slots beyond this inspection facade.
// It does not construct the PLAYER/ACTION providers or execute a renderer.
class WeaponRuntime {
public:
    explicit WeaponRuntime(ActionEventRuntime& runtime);
    ~WeaponRuntime();
    WeaponRuntime(const WeaponRuntime&)=delete;
    WeaponRuntime& operator=(const WeaponRuntime&)=delete;
    WeaponRuntime(WeaponRuntime&&)=delete;
    WeaponRuntime& operator=(WeaponRuntime&&)=delete;
    void register_source(WeaponProgram program);
    ControllerObjectId manager_id() const noexcept;
    const std::vector<WeaponProgram>& sources() const noexcept;
    const WeaponRamState& ram(ControllerObjectId id) const;
    const std::vector<WeaponAttachment>& attachments(std::size_t ordinal) const;
    // Explicit prepared live fields; neither setter infers original activation.
    void set_active(ControllerObjectId id,std::uint8_t active);
    void set_index(ControllerObjectId id,std::int32_t index);
    bool faulted() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    ActionEventRuntime& runtime_;
};
} // namespace dw4
