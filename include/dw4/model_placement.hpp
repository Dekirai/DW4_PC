#pragma once

#include "dw4/messenger.hpp"

#include <functional>
#include <memory>
#include <stdexcept>

namespace dw4 {
class ModelPlacementError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Actual mutable node+20 storage and its concrete virtual+94 operation. The
// resolver below reads the captured ONE's live node reference. It does not
// create ONE, work, a transform, or a replacement scene node.
struct ModelPlacementNode {
    std::shared_ptr<AnimationMatrix> matrix;
    std::function<MessengerCallResult(const AnimationMatrix&)> set_matrix;
};
using ModelPlacementNodeReader=std::function<std::shared_ptr<ModelPlacementNode>()>;

// Only for a verified beModelCtrlNode2 / 801166B8 setter. Retains the supplied
// actual matrix owner and performs its original GQR0 matrix copy on publication.
std::shared_ptr<ModelPlacementNode> make_model_ctrl_node2_placement(
    std::shared_ptr<AnimationMatrix> actual_matrix);

// 8030D6F4 onward, after the caller has performed its live RAM->ONE lookup.
// Vectors take precedence. Vector0 replaces translation; exactly two vectors
// replace only the nine Euler basis entries using vector1 in degrees. Otherwise
// matrix0 replaces the matrix. No arguments means no node access/publication.
// The candidate and concrete setter node survive a required-service pause.
class ModelPlacementInvocation {
public:
    explicit ModelPlacementInvocation(ModelPlacementNodeReader live_node);
    ModelPlacementInvocation(ModelPlacementInvocation&&) noexcept;
    ModelPlacementInvocation& operator=(ModelPlacementInvocation&&) noexcept;
    ~ModelPlacementInvocation();
    ModelPlacementInvocation(const ModelPlacementInvocation&)=delete;
    ModelPlacementInvocation& operator=(const ModelPlacementInvocation&)=delete;
    MessengerCallResult resume(const MessengerBuffer& current_packet);
    bool completed() const noexcept;
    bool faulted() const noexcept;
    bool published() const noexcept;
private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace dw4
