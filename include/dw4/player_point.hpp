#pragma once

#include "dw4/actor_scene.hpp"
#include "dw4/database_runtime.hpp"

#include <functional>

namespace dw4 {
class PlayerPointError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
struct PlayerPointNodeDefinition {
    std::shared_ptr<const ActorSceneNode> node;
    AnimationMatrix matrix;
};
struct PlayerPointGroupDefinition {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name;
    std::optional<DatabaseListIdentity> child_list;
    // Authored null children survive decoding; querying one is a checked error.
    std::vector<std::shared_ptr<const PlayerPointNodeDefinition>> points;
};
struct PlayerPointInfoDefinition {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name;
    std::uint8_t active{};
    std::optional<std::uint32_t> data_reference,polygon_switch_reference,polygon_switch_owner_reference;
    DatabaseListIdentity ram_list;
    std::vector<DatabaseRamDefinition> rams;
    std::optional<DatabaseListIdentity> group_list;
    std::vector<std::shared_ptr<const PlayerPointGroupDefinition>> groups;
};
class PlayerPointProgram {
public:
    const std::shared_ptr<ActorSceneResource>& resource() const noexcept;
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    const std::vector<PlayerPointInfoDefinition>& infos() const noexcept;
private:
    struct Data;
    explicit PlayerPointProgram(std::shared_ptr<const Data> data):data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
    friend PlayerPointProgram decode_player_points(std::shared_ptr<ActorSceneResource>);
};
// Exact exported bePoint01Info records on this actual loaded source. This query
// profile preserves null groups/children and accepts finite static igTransform
// matrices, without requiring four children or composing parent transforms.
PlayerPointProgram decode_player_points(std::shared_ptr<ActorSceneResource>);

struct PlayerPointNodeState {
    ControllerObjectId id;
    std::shared_ptr<const PlayerPointNodeDefinition> definition;
    AnimationMatrix matrix;
    std::uint64_t revision{};
};
struct PlayerPointGroupState {
    ControllerObjectId id;
    std::shared_ptr<const PlayerPointGroupDefinition> definition;
    std::vector<std::shared_ptr<const PlayerPointNodeState>> points;
};
struct PlayerPointInfoState {
    ControllerObjectId id;
    std::shared_ptr<const PlayerPointProgram> program;
    std::size_t info_index{};
    std::vector<std::shared_ptr<const PlayerPointGroupState>> groups;
};
struct PlayerPointRamState {
    ControllerObjectId id,manager,info;
    std::int32_t index{};
    std::uint8_t active{};
    std::array<std::int32_t,4> words{};
    std::shared_ptr<const PlayerPointInfoState> parent;
};
struct PlayerPointServices {
    // Actual igSwitch mask mutation (all bits on/off), not drawing. False leaves
    // this captured switch operation pending; the service owns any inner prefix.
    std::function<bool(const std::shared_ptr<ActorSceneResource>&,ControllerObjectId,bool)> polygon_switch;
};
class PlayerPointRuntime {
public:
    explicit PlayerPointRuntime(ActionEventRuntime&);
    ~PlayerPointRuntime();
    PlayerPointRuntime(const PlayerPointRuntime&)=delete;
    PlayerPointRuntime& operator=(const PlayerPointRuntime&)=delete;
    void register_source(PlayerPointProgram);
    ControllerObjectId manager_id() const noexcept;
    const std::vector<PlayerPointProgram>& sources() const noexcept;
    const PlayerPointRamState& ram(ControllerObjectId) const;
    const PlayerPointNodeState& point(ControllerObjectId) const;
    // One manager-wide signed word, initialized by the original integer field
    // default and changed only through SETGENERATEPOS delivery.
    std::int32_t saved_group() const noexcept;
    void set_active(ControllerObjectId,std::uint8_t);
    void set_matrix(ControllerObjectId,const AnimationMatrix&);
    void set_services(PlayerPointServices);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    ActionEventRuntime& runtime_;
};
} // namespace dw4
