#include "dw4/model_player_work.hpp"

namespace dw4 {
namespace {
void require_work(const std::shared_ptr<ModelPlayerWork>& work) {
    if(!work)throw MotionError("A player-work view requires its actual captured work owner.");
}
}
ModelPlayerWork::ModelPlayerWork():subcommands(std::make_shared<ModelSubcommandWrapper>()) {
    sequence.wait.control_enabled=1;
    script.original_is_main_lobby=std::uint8_t{0};
}
ModelPlayerWork::ModelPlayerWork(ModelMotionFields prepared_motion,ModelControlFields prepared_control)
    :control(std::move(prepared_control)),motion(std::move(prepared_motion)),
    subcommands(std::make_shared<ModelSubcommandWrapper>()) {}
std::shared_ptr<ModelPlayerWork> make_model_player_work() {
    return std::shared_ptr<ModelPlayerWork>(new ModelPlayerWork());
}
std::shared_ptr<ModelPlayerWork> prepare_model_player_work(const ModelControlState& state) {
    return std::shared_ptr<ModelPlayerWork>(new ModelPlayerWork(*state.pose.fields_owner(),*state.fields_owner()));
}
ModelControlState bind_model_player_control(const std::shared_ptr<ModelPlayerWork>& work,
    std::shared_ptr<ModelMotionPose::Matrix> node,std::shared_ptr<ModelRamTimerSlot> timer) {
    require_work(work);
    ModelMotionPose pose(std::move(node),std::shared_ptr<ModelMotionFields>(work,&work->motion));
    return ModelControlState(std::move(pose),std::shared_ptr<ModelControlFields>(work,&work->control),std::move(timer));
}
std::shared_ptr<ModelScriptWork> bind_model_player_script(const std::shared_ptr<ModelPlayerWork>& work,
    ControllerObjectId target,std::string source,std::string name,std::int32_t ram_no,
    std::shared_ptr<AnimationMatrix> node) {
    require_work(work);
    return std::make_shared<ModelScriptWork>(target,std::move(source),std::move(name),ram_no,std::move(node),
        std::shared_ptr<ModelScriptFields>(work,&work->script),std::shared_ptr<ModelStatusFields>(work,&work->status));
}
ModelControlState bind_model_player_control(const std::shared_ptr<ModelPlayerWork>& work,
    std::shared_ptr<ModelMotionPose::Matrix> node,std::shared_ptr<ModelRamTimerAccess> timer) {
    require_work(work);
    ModelMotionPose pose(std::move(node),std::shared_ptr<ModelMotionFields>(work,&work->motion));
    return ModelControlState(std::move(pose),std::shared_ptr<ModelControlFields>(work,&work->control),std::move(timer));
}
ModelSequenceRuntime bind_model_player_sequence(const std::shared_ptr<ModelPlayerWork>& work,
    std::shared_ptr<const ModelSequenceProgram> program,std::string name,std::int32_t ram_no,
    std::shared_ptr<ModelMotionPose::Matrix> node,std::shared_ptr<ModelRamTimerSlot> timer) {
    require_work(work);
    return ModelSequenceRuntime(std::move(program),bind_model_player_control(work,std::move(node),std::move(timer)),
        std::move(name),ram_no,std::shared_ptr<ModelSequenceFields>(work,&work->sequence));
}
ModelSequenceRuntime bind_model_player_sequence(const std::shared_ptr<ModelPlayerWork>& work,
    std::shared_ptr<const ModelSequenceProgram> program,std::string name,std::int32_t ram_no,
    std::shared_ptr<ModelMotionPose::Matrix> node,std::shared_ptr<ModelRamTimerAccess> timer) {
    require_work(work);
    return ModelSequenceRuntime(std::move(program),bind_model_player_control(work,std::move(node),std::move(timer)),
        std::move(name),ram_no,std::shared_ptr<ModelSequenceFields>(work,&work->sequence));
}
} // namespace dw4
