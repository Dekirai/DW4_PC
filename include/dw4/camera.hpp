#pragma once

#include <array>

namespace dw4 {

struct CameraMatrices {
    std::array<float,16> view, projection;
};

// Orbit controls for a mesh normalized to a unit bounding sphere by the viewer.
// This is a host inspection camera, not the original game's camera behavior.
class OrbitCamera {
public:
    void reset() noexcept;
    void orbit(float yaw_delta, float pitch_delta);
    void zoom(float wheel_steps);
    // Displacements are fractions of the viewport; positive is right/up.
    void pan(float right, float up);
    // Preserve the orbit angles while aiming at a normalized source-space point.
    // Target components must be within +/-10000; distance must be positive and
    // finite, then is clamped to the same 0.05-1000 range as zoom. Invalid input
    // leaves the previous camera unchanged.
    void focus(std::array<float,3> normalized_target, float distance);
    // OpenGL column-major matrix, right-handed view, exploratory +Y up.
    std::array<float, 16> view_projection(float aspect) const;
    // Separate stages for prepared actor draws. These retain the same host
    // inspection lens and aspect framing; they are not original game cameras.
    CameraMatrices matrices(float aspect) const;
    float distance() const noexcept { return static_cast<float>(distance_); }
    float yaw() const noexcept { return static_cast<float>(yaw_); }
    float pitch() const noexcept { return static_cast<float>(pitch_); }

private:
    double yaw_ = 0.65;
    double pitch_ = 0.35;
    double distance_ = 3.2;
    std::array<double, 3> target_{};
    std::array<std::array<double,16>,2> matrix_pair(float aspect) const;
};

} // namespace dw4
