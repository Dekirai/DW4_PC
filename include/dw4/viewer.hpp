#pragma once

#include "dw4/animation_math.hpp"

#include <memory>
#include <stdexcept>

namespace dw4 {
class ViewerError : public std::runtime_error {public: using std::runtime_error::runtime_error;};

// The two inline matrix fields of igViewerParameters. This owner is distinct
// from igViewModel and does not select a camera or evaluate a camera graph.
// Construction is explicit through make_viewer_parameters().
struct ViewerParameters {
    AnimationMatrix projection_matrix; // +2C.
    AnimationMatrix view_matrix;       // +6C.
private:
    ViewerParameters();
    friend std::shared_ptr<ViewerParameters> make_viewer_parameters();
};

// The reached +14 field of igViewerSceneInfoManager. A fresh represented
// manager has a null parameters owner; binding one is a separate startup step.
struct ViewerSceneInfoManager {
    std::shared_ptr<ViewerParameters> parameters;
};

// Allocate the represented parameters with the original zero matrix defaults.
// The class-layout probe's identity matrices are not runtime factory defaults.
// No manager lookup, camera selection, scene loading or frame update occurs.
std::shared_ptr<ViewerParameters> make_viewer_parameters();

// Original player input's controller+2C -> manager+14 -> parameters+6C.
// The returned matrix aliases and retains the selected parameters allocation.
// A represented null manager or parameters owner throws; no owner is created.
// An unimplemented controller binding must be handled separately by its caller.
std::shared_ptr<AnimationMatrix> capture_view_matrix(
    const std::shared_ptr<ViewerSceneInfoManager>& manager);
} // namespace dw4
