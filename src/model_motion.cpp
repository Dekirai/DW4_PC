#include "dw4/model_motion.hpp"

#include <cmath>
#include <cstddef>
#include <bit>
#include <cstdint>
#include <limits>

namespace dw4 {
ModelMotionPose::ModelMotionPose(Matrix value):ModelMotionPose(std::make_shared<Matrix>(value)) {}
ModelMotionPose::ModelMotionPose(std::shared_ptr<Matrix> node)
    :ModelMotionPose(std::move(node),std::make_shared<ModelMotionFields>()) {}
ModelMotionPose::ModelMotionPose(std::shared_ptr<Matrix> node,std::shared_ptr<ModelMotionFields> fields)
    :matrix_(std::move(node)),fields_(std::move(fields)) {
    if(!matrix_ || !fields_)throw MotionError("Model motion requires actual node matrix and work field owners.");
}
ModelMotionPose::ModelMotionPose(const ModelMotionPose& other):
    ModelMotionPose(std::make_shared<Matrix>(other.matrix()),std::make_shared<ModelMotionFields>(*other.fields_owner())) {}
ModelMotionPose& ModelMotionPose::operator=(const ModelMotionPose& other) {
    if(this==&other)return *this;
    const auto value=other.matrix();
    const auto fields=*other.fields_owner();
    auto matrix_owner=matrix_ ? matrix_ : std::make_shared<Matrix>();
    auto field_owner=fields_ ? fields_ : std::make_shared<ModelMotionFields>();
    *matrix_owner=value;*field_owner=fields;
    matrix_=std::move(matrix_owner);fields_=std::move(field_owner);
    return *this;
}
ModelMotionPose& ModelMotionPose::operator=(ModelMotionPose&& other) {
    // Existing publications retain the node owner even when committing a
    // temporary candidate. A moved-from destination may regain value ownership.
    return operator=(static_cast<const ModelMotionPose&>(other));
}
ModelMotionPose::Matrix& ModelMotionPose::matrix() {
    if(!matrix_)throw MotionError("This model pose has transferred its matrix owner.");
    return *matrix_;
}
const ModelMotionPose::Matrix& ModelMotionPose::matrix() const {
    if(!matrix_)throw MotionError("This model pose has transferred its matrix owner.");
    return *matrix_;
}
std::shared_ptr<ModelMotionPose::Matrix> ModelMotionPose::matrix_owner() {
    (void)matrix();return matrix_;
}
std::shared_ptr<const ModelMotionPose::Matrix> ModelMotionPose::matrix_owner() const {
    (void)matrix();return matrix_;
}
std::shared_ptr<ModelMotionFields> ModelMotionPose::fields_owner() {
    if(!fields_)throw MotionError("This model pose has transferred its work field owner.");
    return fields_;
}
std::shared_ptr<const ModelMotionFields> ModelMotionPose::fields_owner() const {
    if(!fields_)throw MotionError("This model pose has transferred its work field owner.");
    return fields_;
}
ModelTurn& ModelMotionPose::turn() { return fields_owner()->turn; }
const ModelTurn& ModelMotionPose::turn() const { return fields_owner()->turn; }
std::uint8_t& ModelMotionPose::translated() { return fields_owner()->translated; }
const std::uint8_t& ModelMotionPose::translated() const { return fields_owner()->translated; }
std::uint8_t& ModelMotionPose::position_locked() { return fields_owner()->position_locked; }
const std::uint8_t& ModelMotionPose::position_locked() const { return fields_owner()->position_locked; }
bool ModelMotionPose::operator==(const ModelMotionPose& other) const {
    return matrix()==other.matrix() && *fields_owner()==*other.fields_owner();
}
namespace {
constexpr float tau = 0x1.921fb6p+2f; // GDJEB2 804268DC.
constexpr float radians_per_degree = 0x1.1df46ap-6f; // 80426804.
void finite(float value) {
    if (!std::isfinite(value)) throw MotionError("Model motion cannot publish a non-finite pose.");
}
float euler_single(double value) {
    // Original frsp checks tininess before rounding the returned double trig
    // value. Trig itself remains the explicitly shared host-libm boundary.
    return std::abs(value)<std::numeric_limits<float>::min() ?
        std::copysign(0.0f,static_cast<float>(value)) : static_cast<float>(value);
}
float euler_product(float a,float b) {
    const float rounded=a*b;
    const float magnitude=std::abs(rounded),minimum=std::numeric_limits<float>::min();
    if (magnitude<minimum || (magnitude==minimum &&
        std::abs(static_cast<double>(a)*static_cast<double>(b))<minimum))
        return std::copysign(0.0f,rounded);
    return rounded;
}
float euler_fused(float a,float b,float addend) {
    const float rounded=std::fma(a,b,addend);
    const float magnitude=std::abs(rounded),minimum=std::numeric_limits<float>::min();
    if (magnitude<minimum) return std::copysign(0.0f,rounded);
    if (magnitude!=minimum) return rounded;
    // A binary32 product is exact in double. TwoSum preserves a deficit that
    // a double-only sum would lose at the pre-round min-normal boundary.
    const double product=static_cast<double>(a)*static_cast<double>(b);
    const double sum=product+static_cast<double>(addend);
    const double virtual_addend=sum-product;
    const double error=(product-(sum-virtual_addend))+(static_cast<double>(addend)-virtual_addend);
    if (std::abs(sum)<minimum || (std::abs(sum)==minimum &&
        ((sum>0 && error<0) || (sum<0 && error>0))))
        return std::copysign(0.0f,rounded);
    return rounded;
}
float wrap(float angle, std::size_t& remaining) {
    finite(angle);
    while (angle < 0 || angle >= tau) {
        if (remaining == 0) throw MotionError("Original model angle wrapping exceeded its native operation budget.");
        --remaining;
        const float next = angle < 0 ? angle + tau : angle - tau;
        if (next == angle) throw MotionError("Original model angle wrapping cannot advance at this float magnitude.");
        // Within a float exponent bin >= 4, subtracting this odd-mantissa tau
        // rounds to one fixed integer ULP decrement (no half-ULP ties). Batch
        // those identical steps with integer mantissas, leaving a full step
        // before the exponent boundary. The smaller/tie bins use actual adds.
        // This preserves repeated float rounding even after millions of turns;
        // replacing the loop with fmod would change the resulting angle.
        const auto current_bits = std::bit_cast<std::uint32_t>(angle);
        const auto next_bits = std::bit_cast<std::uint32_t>(next);
        const auto magnitude = current_bits & 0x7fffffffu;
        const auto next_magnitude = next_bits & 0x7fffffffu;
        const auto exponent = magnitude & 0x7f800000u;
        if (exponent >= 0x41800000u && exponent == (next_magnitude & 0x7f800000u) &&
            ((current_bits ^ next_bits) & 0x80000000u) == 0) {
            const auto decrement = magnitude - next_magnitude;
            const auto count = (magnitude - exponent) / decrement;
            if (count > 1) {
                angle = std::bit_cast<float>((current_bits & 0x80000000u) | (magnitude - (count-1)*decrement));
                continue;
            }
        }
        angle = next;
    }
    return angle;
}
float interpolate(float current, float desired, float step, std::size_t& remaining) {
    current = wrap(current,remaining); desired = wrap(desired,remaining);
    float forward, backward;
    if (current <= desired) {
        const float previous_target = desired - tau;
        forward = desired - current; backward = current - previous_target;
    } else {
        const float next_target = desired + tau;
        backward = current - desired; forward = next_target - current;
    }
    if (forward < backward) return forward > step ? current + step : desired;
    return backward > step ? current - step : desired;
}
void euler_basis(std::array<float,16>& matrix, const std::array<float,3>& angles) {
    for (const auto value : angles) finite(value);
    for (std::size_t i=12;i<15;++i) finite(matrix[i]);
    // 801292D4 special-cases exact zero before calling double sin/cos. Keep
    // those branches, their positive-zero products and the original FMA order.
    const float sx = angles[0] == 0 ? 0 : euler_single(std::sin(static_cast<double>(angles[0])));
    const float cx = angles[0] == 0 ? 1 : euler_single(std::cos(static_cast<double>(angles[0])));
    const float sy = angles[1] == 0 ? 0 : euler_single(std::sin(static_cast<double>(angles[1])));
    const float cy = angles[1] == 0 ? 1 : euler_single(std::cos(static_cast<double>(angles[1])));
    const float sxsy = angles[1] == 0 ? 0 : euler_product(sx,sy);
    const float sz = angles[2] == 0 ? 0 : euler_single(std::sin(static_cast<double>(angles[2])));
    const float cz = angles[2] == 0 ? 1 : euler_single(std::cos(static_cast<double>(angles[2])));
    const float cxsz = angles[2] == 0 ? 0 : euler_product(cx,sz);
    const float cxcz=euler_product(cx,cz),sxsz=euler_product(sx,sz),sxcz=euler_product(sx,cz);
    matrix[0]=euler_product(cy,cz);matrix[1]=euler_product(cy,sz);matrix[2]=-sy;
    matrix[4]=euler_fused(sxsy,cz,-cxsz);
    matrix[5]=euler_fused(sxsy,sz,cxcz);matrix[6]=euler_product(sx,cy);
    matrix[8]=euler_fused(cxcz,sy,sxsz);
    matrix[9]=euler_fused(cxsz,sy,-sxcz);matrix[10]=euler_product(cx,cy);
    // 80129134 resets these components from globals initialized at 8012F57C;
    // 8030C85C then restores only translation XYZ with 80128D90.
    matrix[3] = 0; matrix[7] = 0; matrix[11] = 0; matrix[15] = 1;
    for (const auto value : matrix) finite(value);
}
} // namespace

void set_player_turn_rates(ModelMotionPose& pose) {
    pose.turn().rate = {0,7,0}; // 80456F68 and 8045700C, already radians/frame.
}
void synchronize_model_heading(ModelMotionPose& pose) {
    // 8012D434 transforms (0,0,1) without translation. Retain zero products:
    // directly copying the Z column changes signed-zero and non-finite cases.
    std::array<float,3> forward;
    for (std::size_t row=0;row<3;++row) {
        const float y = 0.0f * pose.matrix()[4+row];
        const float xy = std::fma(0.0f,pose.matrix()[row],y);
        forward[row] = std::fma(1.0f,pose.matrix()[8+row],xy);
    }
    // The caller uses only X/Z; the original temporary Y is not published.
    finite(forward[0]); finite(forward[2]);
    const float heading = static_cast<float>(std::atan2(static_cast<double>(forward[0]),
        static_cast<double>(forward[2])));
    finite(heading);
    pose.turn().desired[1] = heading; pose.turn().applied[1] = heading;
}

bool turn_model(ModelMotionPose& pose, MotionTimer& timer, std::uint64_t world_time, bool force) {
    auto candidate_timer = timer;
    auto angles = pose.turn().desired;
    if (!force) {
        std::size_t remaining = 4096;
        for (std::size_t i=0;i<3;++i) {
            if (pose.turn().applied[i] == pose.turn().desired[i]) continue;
            if (pose.turn().rate[i] == 0) angles[i] = pose.turn().applied[i];
            else {
                finite(pose.turn().rate[i]);
                const float delta = motion_timer_frames(candidate_timer,world_time);
                const float step = pose.turn().rate[i] * delta;
                angles[i] = interpolate(pose.turn().applied[i],pose.turn().desired[i],step,remaining);
            }
        }
        if (angles == pose.turn().applied) { timer = candidate_timer; return false; }
    }
    auto matrix = pose.matrix(); euler_basis(matrix,angles);
    pose.matrix() = matrix; pose.turn().applied = angles; timer = candidate_timer;
    return true;
}

void force_model_turn(ModelMotionPose& pose) {
    auto matrix=pose.matrix();
    euler_basis(matrix,pose.turn().desired);
    pose.matrix()=matrix;pose.turn().applied=pose.turn().desired;
}

ModelMotionStep MotionChannels::advance_model(ModelMotionPose& pose, MotionTimer& timer, std::uint64_t world_time) {
    ModelMotionStep result;
    if (!allocated()) { pose.translated() = false; return result; }
    auto states = this->states(); auto candidate_pose = pose; auto candidate_timer = timer;
    candidate_pose.translated() = false;
    const float delta = motion_timer_frames(candidate_timer,world_time);
    if (states[3].active) {
        result.channels.rotation = advance_motion(states[3],delta);
        const float radians = radians_per_degree * result.channels.rotation;
        candidate_pose.turn().desired[1] = radians + candidate_pose.turn().desired[1];
        // Preserve the PPC ordered branch: -0 stays -0, unordered stays
        // unordered. MSVC folds the float comparison/negation into fabs even
        // with /fp:strict; expressing the IEEE classification in bits avoids it.
        auto magnitude_bits = std::bit_cast<std::uint32_t>(radians);
        if (magnitude_bits > 0x80000000u && magnitude_bits <= 0xff800000u)
            magnitude_bits ^= 0x80000000u;
        const float magnitude = std::bit_cast<float>(magnitude_bits);
        candidate_pose.turn().rate[1] = 1.25f * magnitude;
    }
    result.turned = turn_model(candidate_pose,candidate_timer,world_time);
    result.channels.translation_updated = states[0].active || states[1].active || states[2].active;
    if (result.channels.translation_updated) {
        for (std::size_t i=0;i<3;++i) result.channels.translation[i] = advance_motion(states[i],delta);
        translate_model_local(candidate_pose.matrix(),result.channels.translation);
        candidate_pose.translated() = true;
    }
    publish(states); pose = candidate_pose; timer = candidate_timer;
    return result;
}
} // namespace dw4
