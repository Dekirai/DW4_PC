#include "dw4/camera.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace dw4 {
namespace {
using Vector = std::array<double, 3>;
using Matrix = std::array<double, 16>;
constexpr double half_fov_tangent = 0.414213562373095; // Vertical field of view: 45 degrees.
void finite(double value) {
    if (!std::isfinite(value)) throw std::invalid_argument("Camera input must be finite");
}
double dot(const Vector& a, const Vector& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}
Vector outward(double yaw, double pitch) {
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
}
}

void OrbitCamera::reset() noexcept {
    yaw_ = 0.65;
    pitch_ = 0.35;
    distance_ = 3.2;
    target_ = {};
}
void OrbitCamera::orbit(float yaw_delta, float pitch_delta) {
    finite(yaw_delta); finite(pitch_delta);
    yaw_ = std::remainder(yaw_ + yaw_delta, 2.0 * std::numbers::pi);
    pitch_ = std::clamp(pitch_ + pitch_delta, -1.55, 1.55);
}
void OrbitCamera::zoom(float wheel_steps) {
    finite(wheel_steps);
    distance_ = std::clamp(distance_ * std::exp(-0.12 * std::clamp(double(wheel_steps), -1000.0, 1000.0)), 0.05, 1000.0);
}
void OrbitCamera::pan(float right, float up) {
    finite(right); finite(up);
    const Vector right_axis{std::cos(yaw_), 0, -std::sin(yaw_)};
    const Vector up_axis{-std::sin(yaw_) * std::sin(pitch_), std::cos(pitch_), -std::cos(yaw_) * std::sin(pitch_)};
    const auto scale = 2.0 * distance_ * half_fov_tangent;
    for (std::size_t i = 0; i < 3; ++i)
        target_[i] = std::clamp(target_[i] + scale * (right * right_axis[i] + up * up_axis[i]), -10000.0, 10000.0);
}
void OrbitCamera::focus(std::array<float,3> normalized_target, float distance) {
    finite(distance);
    if (distance <= 0) throw std::invalid_argument("Camera focus distance must be positive");
    for (const auto component : normalized_target) {
        finite(component);
        if (std::abs(component) > 10000.0f)
            throw std::invalid_argument("Camera focus target must be within +/-10000");
    }
    target_ = {normalized_target[0],normalized_target[1],normalized_target[2]};
    distance_ = std::clamp(double(distance),0.05,1000.0);
}
std::array<std::array<double,16>,2> OrbitCamera::matrix_pair(float aspect) const {
    finite(aspect);
    if (aspect < 0.01f || aspect > 100.0f) throw std::invalid_argument("Camera aspect must be between 0.01 and 100");
    const auto backward = outward(yaw_, pitch_);
    const Vector right{std::cos(yaw_), 0, -std::sin(yaw_)};
    const Vector up{-std::sin(yaw_) * std::sin(pitch_), std::cos(pitch_), -std::cos(yaw_) * std::sin(pitch_)};
    // Back away in narrow viewports to keep the fitted unit sphere visible.
    const double effective_distance = distance_ / std::min(double(aspect), 1.0);
    Vector eye{};
    for (std::size_t i = 0; i < 3; ++i) eye[i] = target_[i] + backward[i] * effective_distance;
    const Matrix view{right[0], up[0], backward[0], 0,
        right[1], up[1], backward[1], 0,
        right[2], up[2], backward[2], 0,
        -dot(right, eye), -dot(up, eye), -dot(backward, eye), 1};
    constexpr double near_plane = 0.01;
    const double far_plane = std::max(100.0, effective_distance + std::sqrt(dot(target_, target_)) + 10.0);
    const double scale = 1.0 / half_fov_tangent;
    const Matrix projection{scale / aspect, 0, 0, 0, 0, scale, 0, 0,
        0, 0, -(far_plane + near_plane) / (far_plane - near_plane), -1,
        0, 0, -(2 * far_plane * near_plane) / (far_plane - near_plane), 0};
    return {view,projection};
}
CameraMatrices OrbitCamera::matrices(float aspect) const {
    const auto pair=matrix_pair(aspect);
    CameraMatrices result;
    for (std::size_t i=0;i<16;++i) {
        result.view[i]=static_cast<float>(pair[0][i]);
        result.projection[i]=static_cast<float>(pair[1][i]);
    }
    return result;
}
std::array<float,16> OrbitCamera::view_projection(float aspect) const {
    const auto [view,projection]=matrix_pair(aspect);
    std::array<float, 16> result{};
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row) {
            double value = 0;
            for (std::size_t k = 0; k < 4; ++k) value += projection[k * 4 + row] * view[column * 4 + k];
            result[column * 4 + row] = static_cast<float>(value);
        }
    return result;
}
} // namespace dw4
