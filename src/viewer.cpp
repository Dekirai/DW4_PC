#include "dw4/viewer.hpp"

namespace dw4 {
ViewerParameters::ViewerParameters():projection_matrix{},view_matrix{} {}

std::shared_ptr<ViewerParameters> make_viewer_parameters() {
    // Real metadata allocation initializes both matrix fields from zero-filled
    // defaults. The identity writes in the class-layout probe are not copied
    // into allocated instances. See VIEWER.md for the reached factory chain.
    return std::shared_ptr<ViewerParameters>(new ViewerParameters);
}

std::shared_ptr<AnimationMatrix> capture_view_matrix(
    const std::shared_ptr<ViewerSceneInfoManager>& manager) {
    if(!manager)throw ViewerError("Player input dereferences its represented null viewer manager.");
    const auto parameters=manager->parameters;
    if(!parameters)throw ViewerError("Player input dereferences its viewer manager's null parameters owner.");
    return std::shared_ptr<AnimationMatrix>(parameters,&parameters->view_matrix);
}
} // namespace dw4
