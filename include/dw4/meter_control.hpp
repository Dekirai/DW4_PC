#pragma once

#include "dw4/actor_scene.hpp"
#include "dw4/database_runtime.hpp"
#include "dw4/game_data.hpp"

#include <array>
#include <memory>
#include <vector>

namespace dw4 {
class MeterControlError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct MeterNodeDefinition {
    std::shared_ptr<const ActorSceneNode> node;
    std::uint8_t meter_mode{};
    std::array<float,3> step_translation{};
    DatabaseListIdentity time_list;
    std::vector<std::int64_t> times;
};
struct MeterOneDefinition {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name;
    std::shared_ptr<const MeterNodeDefinition> node;
    float maximum{},time_scale{},step{},target{},current{};
    std::int64_t minimum_time{},maximum_time{};
    std::int32_t digit_count{};
    std::uint8_t dirty{},trim_zeroes{},integer_mode{};
    std::shared_ptr<GameDataInt> bound_value,bound_maximum;
};
struct MeterInfoDefinition {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name;
    std::uint8_t active{};
    std::optional<std::uint32_t> data_reference;
    DatabaseListIdentity ram_list,one_list;
    std::vector<DatabaseRamDefinition> rams;
    std::vector<MeterOneDefinition> ones;
};
class MeterProgram {
public:
    const std::shared_ptr<ActorSceneResource>& resource() const noexcept;
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    const std::vector<MeterInfoDefinition>& infos() const noexcept;
private:
    struct Data;
    explicit MeterProgram(std::shared_ptr<const Data> data):data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
    friend MeterProgram decode_meter_controls(std::shared_ptr<ActorSceneResource>);
};
MeterProgram decode_meter_controls(std::shared_ptr<ActorSceneResource>);

struct MeterNodeState {
    std::shared_ptr<const MeterNodeDefinition> definition;
    // Shared physical igLongList owner, mutated by the original update. This
    // feeds the meter node's later geometry/animation traversal, not HUD pixels.
    std::shared_ptr<std::vector<std::int64_t>> times;
};
struct MeterOneState {
    ControllerObjectId id;
    std::shared_ptr<MeterNodeState> node;
    float maximum{},time_scale{},step{},target{},current{};
    std::int64_t minimum_time{},maximum_time{};
    std::int32_t digit_count{};
    std::uint8_t dirty{},trim_zeroes{},integer_mode{};
    std::shared_ptr<GameDataInt> bound_value,bound_maximum;
};
struct MeterRamState {
    ControllerObjectId id,manager,info;
    std::int32_t index{};
    std::uint8_t active{};
    std::array<std::int32_t,4> words{};
    std::shared_ptr<const MeterProgram> program;
    std::size_t info_index{};
};
class MeterRuntime {
public:
    explicit MeterRuntime(ActionEventRuntime&);
    ~MeterRuntime();
    MeterRuntime(const MeterRuntime&)=delete;
    MeterRuntime& operator=(const MeterRuntime&)=delete;
    void register_source(MeterProgram);
    ControllerObjectId manager_id() const noexcept;
    const std::vector<MeterProgram>& sources() const noexcept;
    const MeterRamState& ram(ControllerObjectId) const;
    const MeterOneState& one(ControllerObjectId) const;
    const MeterOneState& selected_one(ControllerObjectId ram_id) const;
    void set_active(ControllerObjectId,std::uint8_t);
    void set_index(ControllerObjectId,std::int32_t);
    // Exact virtual update803071DC for a supplied RAM. This is not an inferred
    // global scheduler tick or visibility gate. First update initializes work.
    void update(ControllerObjectId);
    bool faulted() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    ActionEventRuntime& runtime_;
};
} // namespace dw4
