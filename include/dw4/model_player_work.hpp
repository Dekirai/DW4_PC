#pragma once

#include "dw4/model_sequence.hpp"
#include "dw4/model_status.hpp"

namespace dw4 {
class NativeMessenger;
// These non-null concrete owners still require their original implementations.
// Fresh work stores null references; it allocates no stand-in scene objects.
class ActorSceneNode;
using ModelWorkNode=ActorSceneNode;
class ModelPositionTrace;
class ModelRideData;
class ModelHitBodyWork;

struct ModelWorkAi {
    std::optional<std::string> sequence_name;
    std::int32_t map_number{};
    std::array<float,3> world_position{};
    std::int32_t status{},count{};
};

// One beNDMWMdlPlayerInfoWork allocation. Every reflected field has exactly one
// storage location, grouped by the algorithms that consume it. Views retain
// aliasing pointers to these groups; work never caches those retaining views.
// Node matrices and the RAM's named KEIKATIMER are deliberately separate.
class ModelPlayerWork {
public:
    ModelPlayerWork(const ModelPlayerWork&)=delete;
    ModelPlayerWork& operator=(const ModelPlayerWork&)=delete;
    ModelPlayerWork(ModelPlayerWork&&)=delete;
    ModelPlayerWork& operator=(ModelPlayerWork&&)=delete;

    ModelScriptFields script;          // +08/+0C/+10/+125
    ModelSequenceFields sequence;      // +14..+2C, +44/+4C/+128/+12C
    ModelControlFields control;        // +30..+40/+48/+7C/+C8/+D8..+DA
    ModelMotionFields motion;          // +78/+80/+8C/+98/+A8
    ModelStatusFields status;          // +EC..+10C

    std::uint8_t frozen{};             // +45
    std::shared_ptr<ModelWorkAi> ai;    // +50
    float ai_height_offset{};          // +54
    std::optional<std::string> target_mode,target_kind,target_special; // +58/+5C/+60
    std::array<float,3> target_position{}; // +64
    std::uint8_t key_input{};           // +70
    std::optional<std::string> key_event_name; // +74
    float saved_lod_high_range=-1.0f;   // +A4
    std::shared_ptr<ModelWorkNode> position_lock_node; // +AC
    std::shared_ptr<ModelPositionTrace> position_lock_trace; // +B0
    std::optional<std::string> position_lock_model_name,position_lock_bone_name; // +B4/+B8
    std::array<float,3> position_lock_offset{}; // +BC
    std::shared_ptr<ModelRideData> ride_data; // +CC
    std::shared_ptr<ModelWorkNode> debug_hitbox_node; // +D0
    std::shared_ptr<ModelHitBodyWork> hit_body; // +D4
    std::optional<std::string> hit_parent_name; // +DC
    std::int32_t hit_parent_ram{};      // +E0
    std::shared_ptr<ModelSubcommandWrapper> subcommands; // +E4
    std::shared_ptr<CollisionBox> base_box; // +E8
    NativeMessenger* messenger{};      // +110, non-owning original reference
    std::shared_ptr<std::vector<std::int32_t>> turn_attack_timers; // +114
    std::int32_t body_attack_state{},body_attack_pad{}; // +118/+11C
    float body_attack_timer{};         // +120
    std::uint8_t panel{};              // +124

private:
    ModelPlayerWork();
    ModelPlayerWork(ModelMotionFields,ModelControlFields);
    friend std::shared_ptr<ModelPlayerWork> make_model_player_work();
    friend std::shared_ptr<ModelPlayerWork> prepare_model_player_work(const ModelControlState&);
};

// Normal built-in factory field/default result (80338F60/800667F4). It creates
// distinct zero HP/MHP wrappers, empty pad/movement lists and subcommand
// wrapper/list. No node/timer, player callback, registration or GENERATE tail.
std::shared_ptr<ModelPlayerWork> make_model_player_work();

// Explicit prepared compatibility profile: retain supplied actual list owners
// and copy supplied scalar fields. Lobby initialization remains unknown and
// wait control retains the former prepared default zero. This is not GENERATE.
std::shared_ptr<ModelPlayerWork> prepare_model_player_work(const ModelControlState&);
ModelControlState bind_model_player_control(const std::shared_ptr<ModelPlayerWork>&,
    std::shared_ptr<ModelMotionPose::Matrix> actual_node_matrix,
    std::shared_ptr<ModelRamTimerSlot> actual_ram_timer);
std::shared_ptr<ModelScriptWork> bind_model_player_script(const std::shared_ptr<ModelPlayerWork>&,
    ControllerObjectId target,std::string source,std::string name,std::int32_t ram_no,
    std::shared_ptr<AnimationMatrix> actual_node_matrix);
ModelSequenceRuntime bind_model_player_sequence(const std::shared_ptr<ModelPlayerWork>&,
    std::shared_ptr<const ModelSequenceProgram>,std::string name,std::int32_t ram_no,
    std::shared_ptr<ModelMotionPose::Matrix> actual_node_matrix,
    std::shared_ptr<ModelRamTimerSlot> actual_ram_timer);
} // namespace dw4
