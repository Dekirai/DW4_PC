#include "mesh_renderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void rejects(const std::function<void()>& operation) {
    try { operation(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Invalid renderer operation was accepted");
}
struct Context {
    SDL_Window* window{};
    SDL_GLContext context{};
    ~Context() {
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
std::vector<std::uint8_t> pixels(std::uint32_t texture, int width, int height) {
    GLint previous = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    glBindTexture(GL_TEXTURE_2D, texture);
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width) * height * 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
    check(glGetError() == GL_NO_ERROR, "Reading the rendered texture failed");
    return image;
}
std::size_t coverage(const std::vector<std::uint8_t>& image) {
    std::size_t count = 0;
    for (std::size_t offset = 0; offset < image.size(); offset += 4) {
        check(image[offset + 3] == 255, "Offscreen viewer output must be opaque");
        if (image[offset] > 16 || image[offset + 1] > 24 || image[offset + 2] > 30) ++count;
    }
    return count;
}
void saved_state() {
    check(glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST)
        && !glIsEnabled(GL_DEPTH_TEST), "Renderer did not restore caller capabilities");
    GLboolean depth_mask = GL_TRUE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
    check(!depth_mask, "Renderer did not restore depth write mask");
    GLint viewport[4]{}; glGetIntegerv(GL_VIEWPORT, viewport);
    check(viewport[0] == 7 && viewport[1] == 9 && viewport[2] == 71 && viewport[3] == 53,
        "Renderer did not restore the caller viewport");
    GLdouble depth = 0; glGetDoublev(GL_DEPTH_CLEAR_VALUE, &depth);
    check(std::abs(depth - 0.4) < 1e-5, "Renderer did not restore depth clear value");
    GLint polygon[2]{}; glGetIntegerv(GL_POLYGON_MODE, polygon);
    // Core profile has one polygon mode; some drivers write only the first
    // value while compatibility implementations return front/back modes.
    check(polygon[0] == GL_LINE, "Renderer did not restore the core polygon mode");
    check(glGetError() == GL_NO_ERROR, "Renderer left an OpenGL error");
}
}
int main() {
    Context context;
    try {
        check(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        context.window = SDL_CreateWindow("DW4 native renderer validation", 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        check(context.window != nullptr, SDL_GetError());
        context.context = SDL_GL_CreateContext(context.window);
        check(context.context != nullptr, SDL_GetError());
        std::cout << "GPU: " << glGetString(GL_RENDERER) << '\n';
        {
            dw4::viewer::MeshRenderer renderer;
            dw4::OrbitCamera camera;
            dw4::MeshData tetra;
            tetra.positions = {{1,1,1}, {-1,-1,1}, {-1,1,-1}, {1,-1,-1}};
            tetra.indices = {0,1,2, 0,3,1, 0,2,3, 1,3,2};
            glViewport(7, 9, 71, 53);
            glEnable(GL_CULL_FACE); glEnable(GL_BLEND); glEnable(GL_SCISSOR_TEST);
            glScissor(0, 0, 1, 1); glDisable(GL_DEPTH_TEST); glDepthMask(GL_FALSE);
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE); glClearDepth(0.4);
            saved_state();
            renderer.upload(tetra); saved_state();
            check(renderer.has_mesh() && renderer.triangle_count() == 4, "Tetrahedron upload lost geometry");
            const auto texture = renderer.render(128, 128, camera, dw4::viewer::MeshDrawMode::solid);
            saved_state();
            const auto solid = pixels(texture, 128, 128);
            const auto solid_coverage = coverage(solid);
            check(solid_coverage > 500 && solid_coverage < 12000, "Solid triangles did not produce a fitted visible shape");
            camera.orbit(0.8f, -0.35f);
            const auto orbited = pixels(renderer.render(128, 128, camera, dw4::viewer::MeshDrawMode::solid), 128, 128);
            check(orbited != solid && coverage(orbited) > 200, "Camera orbit did not change rendered geometry");
            camera.reset();
            const auto wire = pixels(renderer.render(128, 128, camera, dw4::viewer::MeshDrawMode::wireframe), 128, 128);
            check(coverage(wire) > 40 && coverage(wire) < solid_coverage, "Wireframe coverage is not distinct from solid rendering");
            GLint maximum_samples = 0; glGetIntegerv(GL_MAX_SAMPLES, &maximum_samples);
            const auto target = renderer.render(96, 80, camera, dw4::viewer::MeshDrawMode::solid, 8);
            const auto minimum_samples = maximum_samples >= 8 ? 8 : maximum_samples >= 4 ? 4 : maximum_samples >= 2 ? 2 : 0;
            check(renderer.samples() >= minimum_samples && renderer.samples() <= maximum_samples,
                "Offscreen MSAA is outside the supported request range");
            check(coverage(pixels(target, 96, 80)) > 200, "Resized/MSAA-resolved target is blank");
            check(renderer.render(96, 80, camera, dw4::viewer::MeshDrawMode::solid, 8) == target,
                "Unchanged framebuffer request should reuse its color texture");
            saved_state();
            check(renderer.rendered_frames() == 5, "Successful render frame counter is incorrect");
            rejects([&] { renderer.render(4097, 128, camera, dw4::viewer::MeshDrawMode::solid); });
            rejects([&] { renderer.render(4096, 4096, camera, dw4::viewer::MeshDrawMode::solid); });
            auto malformed = tetra; malformed.indices[0] = 99;
            rejects([&] { renderer.upload(malformed); });
            check(!renderer.has_mesh(), "Failed upload retained stale geometry");
            malformed = tetra; malformed.positions[0].x = std::numeric_limits<float>::infinity();
            rejects([&] { renderer.upload(malformed); });
            renderer.upload(tetra); renderer.clear();
            check(!renderer.has_mesh() && renderer.triangle_count() == 0, "Clear retained geometry");
            rejects([&] { renderer.render(128, 128, camera, dw4::viewer::MeshDrawMode::solid); });
            renderer.shutdown(); renderer.shutdown();
            std::cout << "Renderer GPU checks passed: coverage, orbit, wireframe, resize, MSAA, state restoration, malformed upload and cleanup\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Renderer test failure: " << error.what() << '\n';
        return 1;
    }
}
