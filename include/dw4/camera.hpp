#pragma once

#include <array>

namespace dw4 {

// Orbit controls for a mesh normalized to a unit bounding sphere by the viewer.
// This is a host inspection camera, not the original game's camera behavior.
class OrbitCamera {
public:
    void reset() noexcept;
    void orbit(float yaw_delta, float pitch_delta);
    void zoom(float wheel_steps);
    // Displacements are fractions of the viewport; positive is right/up.
    void pan(float right, float up);
    // OpenGL column-major matrix, right-handed view, exploratory +Y up.
    std::array<float, 16> view_projection(float aspect) const;
    float distance() const noexcept { return static_cast<float>(distance_); }

private:
    double yaw_ = 0.65;
    double pitch_ = 0.35;
    double distance_ = 3.2;
    std::array<double, 3> target_{};
};

} // namespace dw4
