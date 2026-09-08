#include "dw4/camera.hpp"

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void rejects(const std::function<void()>& action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid camera input was accepted");
}
std::array<float, 4> project(const std::array<float, 16>& matrix, float x, float y, float z) {
    std::array<float, 4> result{};
    for (std::size_t row = 0; row < 4; ++row)
        result[row] = matrix[row] * x + matrix[4 + row] * y + matrix[8 + row] * z + matrix[12 + row];
    return result;
}
}
int main() {
    try {
        dw4::OrbitCamera camera;
        const auto initial = camera.view_projection(16.0f / 9);
        for (float aspect : {0.2f, 1.0f, 16.0f / 9, 4.0f}) {
            const auto matrix = camera.view_projection(aspect);
            const auto center = project(matrix, 0, 0, 0);
            check(std::abs(center[0]) < 1e-5f && std::abs(center[1]) < 1e-5f && center[3] > 0,
                "Fit must aim at the origin with positive clip W");
            for (const auto point : {std::array<float, 3>{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}}) {
                const auto clip = project(matrix, point[0], point[1], point[2]);
                check(clip[3] > 0 && std::abs(clip[0]) < clip[3] && std::abs(clip[1]) < clip[3]
                    && std::abs(clip[2]) < clip[3], "Fitted unit sphere axis escaped the frustum");
            }
        }
        camera.orbit(0.5f, 0.2f);
        check(camera.view_projection(16.0f / 9) != initial, "Orbit must change the view");
        camera.zoom(3);
        check(camera.distance() < 3.2f, "Positive wheel input must approach the mesh");
        camera.pan(0.2f, 0.1f);
        const auto moved = project(camera.view_projection(1), 0, 0, 0);
        check(std::abs(moved[0]) > 0.1f || std::abs(moved[1]) > 0.1f, "Pan must move the target");
        camera.reset();
        check(camera.view_projection(16.0f / 9) == initial, "Reset must restore the fitted view");
        camera.orbit(.4f,-.2f);camera.pan(.3f,-.1f);
        const auto before_focus = camera.view_projection(1);
        const std::array<float,3> target{4,-3,8};
        camera.focus(target,3.7f);
        const auto focused = camera.view_projection(1);
        check(std::abs(camera.distance()-3.7f)<1e-6f,"Focus did not use the supplied distance");
        for (std::size_t column=0;column<3;++column)
            for (std::size_t row=0;row<2;++row)
                check(focused[column*4+row]==before_focus[column*4+row],"Focus changed the orbit angles");
        for (float aspect : {.2f,1.0f,16.0f/9,4.0f}) {
            const auto clip=project(camera.view_projection(aspect),target[0],target[1],target[2]);
            check(clip[3]>0 && std::abs(clip[0]/clip[3])<1e-5f && std::abs(clip[1]/clip[3])<1e-5f,
                "Focus target was not centered for the requested aspect ratio");
        }
        const auto focus_distance=camera.distance();
        const auto focus_state=camera.view_projection(1);
        for (const auto bad_target : {std::array<float,3>{0,0,std::numeric_limits<float>::quiet_NaN()},
            {std::numeric_limits<float>::infinity(),0,0},{0,10001,0},{-10001,0,0}}) {
            rejects([&]{camera.focus(bad_target,2);});
            check(camera.view_projection(1)==focus_state && camera.distance()==focus_distance,
                "Rejected focus target changed camera state");
        }
        for(float bad_distance : {0.0f,-1.0f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
            rejects([&]{camera.focus({0,0,0},bad_distance);});
            check(camera.view_projection(1)==focus_state && camera.distance()==focus_distance,
                "Rejected focus distance changed camera state");
        }
        camera.focus({0,0,0},.00001f);
        check(camera.distance()==.05f,"Focus did not preserve the minimum zoom distance");
        camera.focus({10000,-10000,10000},std::numeric_limits<float>::max());
        check(camera.distance()==1000,"Focus did not preserve the maximum zoom distance");
        for(auto value:camera.view_projection(.01f)) check(std::isfinite(value),"Bounded extreme focus produced a non-finite matrix");
        camera.reset();
        check(camera.view_projection(16.0f/9)==initial,"Reset after focus retained the changed target");
        camera.orbit(1e30f, 1e30f); camera.zoom(-1e30f); camera.pan(1e30f, -1e30f);
        for (auto value : camera.view_projection(0.01f)) check(std::isfinite(value), "Extreme finite controls produced an invalid matrix");
        camera.zoom(1e30f);
        check(camera.distance() >= 0.049f && camera.distance() <= 1000, "Zoom range is not bounded");
        const auto nan = std::numeric_limits<float>::quiet_NaN();
        const auto infinity = std::numeric_limits<float>::infinity();
        rejects([&] { camera.orbit(nan, 0); }); rejects([&] { camera.orbit(0, infinity); });
        rejects([&] { camera.pan(0, nan); }); rejects([&] { camera.zoom(infinity); });
        rejects([&] { camera.view_projection(0); }); rejects([&] { camera.view_projection(nan); });
        std::cout << "Camera projection, controls and atomic source-target focus tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Camera test failure: " << error.what() << '\n';
        return 1;
    }
}
