#include "dw4/model_placement.hpp"

#include "dw4/model_motion.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw ModelPlacementError(message); }
void finite(float value) {
    if(!std::isfinite(value))fail("Model placement requires finite copied values.");
}
AnimationMatrix matrix_copy(const AnimationMatrix& source) {
    AnimationMatrix result;
    for(std::size_t i=0;i<result.size();++i) {
        finite(source[i]);
        const auto word=std::bit_cast<std::uint32_t>(source[i]);
        // 80128CF4 uses GQR0 paired-single stores, including untouched entries.
        result[i]=(word&0x7f800000u)==0 ? std::bit_cast<float>(word&0x80000000u) : source[i];
    }
    return result;
}
float radians(float degrees) {
    finite(degrees);
    constexpr float conversion=std::bit_cast<float>(std::uint32_t{0x3c8efa35});
    const float rounded=conversion*degrees;
    constexpr float minimum=std::numeric_limits<float>::min();
    // Startup NI=1 tests tininess before rounding, including a result that
    // would otherwise round upward to the smallest normal binary32 value.
    if(std::abs(rounded)<minimum || (std::abs(rounded)==minimum &&
        std::abs(static_cast<double>(conversion)*static_cast<double>(degrees))<minimum))
        return std::copysign(0.0f,rounded);
    return rounded;
}
void rotation(AnimationMatrix& matrix,const std::array<float,3>& degrees) {
    ModelMotionPose temporary(matrix);
    for(std::size_t i=0;i<3;++i)temporary.turn.desired[i]=radians(degrees[i]);
    // Reuse the verified 801292D4 trig branches and scalar/FMA order. The
    // surrounding motion operation also resets a bottom row; placement does
    // not, so copy only the nine entries written by 801292D4 itself.
    force_model_turn(temporary);
    for(const std::size_t i:{0u,1u,2u,4u,5u,6u,8u,9u,10u})matrix[i]=temporary.matrix()[i];
}
}

std::shared_ptr<ModelPlacementNode> make_model_ctrl_node2_placement(
    std::shared_ptr<AnimationMatrix> actual_matrix) {
    if(!actual_matrix)fail("The concrete placement setter requires its actual node matrix owner.");
    auto result=std::make_shared<ModelPlacementNode>();
    result->matrix=actual_matrix;
    result->set_matrix=[matrix=std::move(actual_matrix)](const AnimationMatrix& value) {
        const auto copied=matrix_copy(value);
        *matrix=copied;
        return MessengerCallResult{true,1,{}};
    };
    return result;
}

struct ModelPlacementInvocation::State {
    ModelPlacementNodeReader live_node;
    enum class Phase { prepare,publish,completed,faulted } phase=Phase::prepare;
    AnimationMatrix candidate{};
    std::shared_ptr<ModelPlacementNode> setter_node;
    std::function<MessengerCallResult(const AnimationMatrix&)> setter;
    bool did_publish{},processing{};
};

ModelPlacementInvocation::ModelPlacementInvocation(ModelPlacementNodeReader live_node)
    :state_(std::make_unique<State>()) { state_->live_node=std::move(live_node); }
ModelPlacementInvocation::ModelPlacementInvocation(ModelPlacementInvocation&&) noexcept=default;
ModelPlacementInvocation& ModelPlacementInvocation::operator=(ModelPlacementInvocation&&) noexcept=default;
ModelPlacementInvocation::~ModelPlacementInvocation()=default;
bool ModelPlacementInvocation::completed() const noexcept {
    return state_ && state_->phase==State::Phase::completed;
}
bool ModelPlacementInvocation::faulted() const noexcept {
    return state_ && state_->phase==State::Phase::faulted;
}
bool ModelPlacementInvocation::published() const noexcept { return state_ && state_->did_publish; }

MessengerCallResult ModelPlacementInvocation::resume(const MessengerBuffer& packet) {
    if(!state_)fail("A moved placement invocation cannot resume.");
    auto& state=*state_;
    if(state.phase==State::Phase::faulted)fail("The placement invocation faulted; automatic replay is forbidden.");
    if(state.phase==State::Phase::completed)return {true,1,{}};
    if(state.processing) {
        state.phase=State::Phase::faulted;
        fail("The same placement invocation cannot recursively resume its active callback.");
    }
    struct ProcessingGuard {
        bool& value;
        explicit ProcessingGuard(bool& processing):value(processing) { value=true; }
        ~ProcessingGuard() { value=false; }
    } guard(state.processing);
    const auto require_healthy_callback=[&] {
        // A callback may catch the recursive-resume exception. It still must
        // not convert the faulted outer invocation into successful publication.
        if(state.phase==State::Phase::faulted)
            fail("A placement callback attempted to resume its own active invocation.");
    };
    try {
        if(state.phase==State::Phase::prepare) {
            // Match the helper's first vector-count test. The current node is
            // not read at all on the matrix-only or no-argument branch.
            if(!packet.vectors().empty()) {
                if(!state.live_node)return {false,0,"Model placement requires the actual ONE node resolver."};
                const auto source_node=state.live_node();
                require_healthy_callback();
                if(!source_node || !source_node->matrix)fail("Vector placement reached a null actual node matrix.");
                state.candidate=matrix_copy(*source_node->matrix); // D710, before vector0.
                if(packet.vectors().empty())fail("Vector placement lost vector0 before its original read.");
                const auto position=packet.vectors()[0];
                for(std::size_t i=0;i<3;++i) {
                    finite(position[i]);
                    state.candidate[12+i]=position[i]; // Scalar copy preserves tiny words here.
                }
                // D734 reads the count again; greater than two skips rotation.
                if(packet.vectors().size()==2)rotation(state.candidate,packet.vectors()[1]);
            } else if(!packet.matrices().empty()) {
                state.candidate=matrix_copy(packet.matrices()[0]);
            } else {
                state.phase=State::Phase::completed;
                state.live_node={};
                return {true,1,{}};
            }
            state.phase=State::Phase::publish;
        }
        if(!state.setter_node) {
            if(!state.live_node)return {false,0,"Model placement requires the actual ONE node resolver."};
            // D778 / D7C0 re-read the captured ONE's live node after computing
            // the local matrix; it need not be the vector branch's source node.
            state.setter_node=state.live_node();
            require_healthy_callback();
            if(!state.setter_node)fail("Placement publication reached a null actual node.");
            state.live_node={};
        }
        if(!state.setter) {
            if(!state.setter_node->set_matrix)
                return {false,0,"The actual model node requires its concrete virtual+94 placement setter."};
            state.setter=state.setter_node->set_matrix;
        }
        const auto result=state.setter(state.candidate);
        require_healthy_callback();
        if(!result.completed)return result;
        state.did_publish=true;
        state.phase=State::Phase::completed;
        state.setter={};state.setter_node.reset();
        // 8030D610 ignores the concrete setter's machine return value.
        return {true,1,{}};
    } catch(...) {
        state.phase=State::Phase::faulted;
        state.live_node={};state.setter={};state.setter_node.reset();
        throw;
    }
}
} // namespace dw4
