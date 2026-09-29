// Native Digimon World 4 runtime: SDL3 window, OpenGL renderer and the
// reconstructed engine in dw4::engine. Original data is read from --data.
#include "gl_renderer.hpp"

#include "dw4/engine/engine.hpp"
#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

struct Options {
    std::filesystem::path data;
    std::string stage = "stagis08";
    int smoke_frames = 0;
    std::string screenshot;
    std::vector<int> screenshot_frames;
    std::string input_script;
    bool overlay = true;
    std::vector<float> debug_camera; // eye xyz, target xyz
};

std::filesystem::path default_data_path() {
    for (const auto& candidate : {std::filesystem::path("local/settings.ini"), std::filesystem::path("settings.ini")}) {
        std::ifstream in(candidate);
        std::string line;
        while (std::getline(in, line)) {
            if (line.rfind("path=", 0) == 0) return line.substr(5);
        }
    }
    return {};
}

void write_bmp(const std::string& path, int width, int height, const std::vector<std::uint8_t>& rgba) {
    std::ofstream out(path, std::ios::binary);
    const int row = width * 3;
    const int pad = (4 - row % 4) % 4;
    const std::uint32_t size = 54 + std::uint32_t((row + pad) * height);
    auto u32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    out.write("BM", 2);
    u32(size); u32(0); u32(54); u32(40); u32(std::uint32_t(width)); u32(std::uint32_t(height));
    u16(1); u16(24); u32(0); u32(size - 54); u32(2835); u32(2835); u32(0); u32(0);
    const char zeros[3]{};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto* p = &rgba[std::size_t((y * width + x) * 4)];
            const char bgr[3]{char(p[2]), char(p[1]), char(p[0])};
            out.write(bgr, 3);
        }
        out.write(zeros, pad);
    }
}

// Scripted input for automated runs: lines "frame button" (button: A,B,X,Y,START,UP,DOWN,LEFT,RIGHT,Z,L,R).
struct ScriptedInput {
    std::vector<std::pair<int, std::uint32_t>> presses;
    void load(const std::string& path) {
        std::ifstream in(path);
        int frame;
        std::string name;
        while (in >> frame >> name) {
            std::uint32_t bit = 0;
            if (name == "A") bit = dw4::engine::pad_a;
            else if (name == "B") bit = dw4::engine::pad_b;
            else if (name == "X") bit = dw4::engine::pad_x;
            else if (name == "Y") bit = dw4::engine::pad_y;
            else if (name == "START") bit = dw4::engine::pad_start;
            else if (name == "UP") bit = dw4::engine::pad_up;
            else if (name == "DOWN") bit = dw4::engine::pad_down;
            else if (name == "LEFT") bit = dw4::engine::pad_left;
            else if (name == "RIGHT") bit = dw4::engine::pad_right;
            else if (name == "Z") bit = dw4::engine::pad_z;
            else if (name == "L") bit = dw4::engine::pad_l;
            else if (name == "R") bit = dw4::engine::pad_r;
            presses.emplace_back(frame, bit);
        }
    }
    std::uint32_t held(int frame) const {
        std::uint32_t bits = 0;
        for (const auto& [f, b] : presses) if (frame >= f && frame < f + 6) bits |= b;
        return bits;
    }
};

int run(const Options& options) {
    dw4::engine::Engine engine(options.data);
    engine.request_stage(options.stage);
    auto make_camera = [](dw4::engine::Vec3f eye, dw4::engine::Vec3f target) {
        dw4::engine::FrameCamera c;
        c.view = dw4::engine::look_at(eye, target, {0, 1, 0});
        c.projection = dw4::engine::perspective(54.43f * 3.14159265f / 180.0f, 4.0f / 3.0f, 1.0f, 4000.0f);
        return c;
    };
    if (options.debug_camera.size() >= 6)
        engine.debug_camera = make_camera({options.debug_camera[0], options.debug_camera[1], options.debug_camera[2]},
                                          {options.debug_camera[3], options.debug_camera[4], options.debug_camera[5]});
    // Interactive free camera (F2): position + yaw/pitch.
    bool free_camera = false;
    dw4::engine::Vec3f free_eye{0, 60, 200};
    float free_yaw = 3.14159265f, free_pitch = -0.2f;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    Uint64 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if (options.smoke_frames) flags |= SDL_WINDOW_HIDDEN;
    SDL_Window* window = SDL_CreateWindow("Digimon World 4 PC", 1280, 960, flags);
    if (!window) throw std::runtime_error(SDL_GetError());
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) throw std::runtime_error(SDL_GetError());
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(options.smoke_frames ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, context);
    ImGui_ImplOpenGL3_Init("#version 330");

    {
        dw4::game::GlRenderer renderer;
        std::vector<SDL_Gamepad*> gamepads;
        ScriptedInput scripted;
        if (!options.input_script.empty()) scripted.load(options.input_script);
        std::vector<dw4::engine::DrawItem> items;
        dw4::engine::FrameCamera camera;
        dw4::engine::Vec4f clear{};
        std::array<dw4::engine::PadState, 4> pads{};
        std::uint32_t previous_held = 0;
        std::string rendered_stage;
        bool running = true, show_menu = false;
        int frame = 0;
        auto last = std::chrono::steady_clock::now();
        double accumulator = 0;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
                    if (auto* pad = SDL_OpenGamepad(event.gdevice.which)) gamepads.push_back(pad);
                }
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1) show_menu = !show_menu;
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F2) {
                    free_camera = !free_camera;
                    if (!free_camera) engine.debug_camera.reset();
                }
                if (free_camera && event.type == SDL_EVENT_MOUSE_MOTION && (event.motion.state & SDL_BUTTON_RMASK)) {
                    free_yaw -= event.motion.xrel * 0.005f;
                    free_pitch = std::clamp(free_pitch - event.motion.yrel * 0.005f, -1.5f, 1.5f);
                }
            }
            // Input: keyboard + first gamepad -> player 1.
            std::uint32_t held = 0;
            float sx = 0, sy = 0;
            if (free_camera) {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                const dw4::engine::Vec3f forward{std::sin(free_yaw) * std::cos(free_pitch), std::sin(free_pitch), std::cos(free_yaw) * std::cos(free_pitch)};
                const dw4::engine::Vec3f right = dw4::engine::normalize(dw4::engine::cross(forward, {0, 1, 0}));
                const float speed = keys[SDL_SCANCODE_LSHIFT] ? 8.0f : 2.0f;
                if (keys[SDL_SCANCODE_W]) free_eye = free_eye + forward * speed;
                if (keys[SDL_SCANCODE_S]) free_eye = free_eye - forward * speed;
                if (keys[SDL_SCANCODE_D]) free_eye = free_eye + right * speed;
                if (keys[SDL_SCANCODE_A]) free_eye = free_eye - right * speed;
                if (keys[SDL_SCANCODE_E]) free_eye.y += speed;
                if (keys[SDL_SCANCODE_Q]) free_eye.y -= speed;
                engine.debug_camera = make_camera(free_eye, free_eye + forward);
            } else if (!show_menu) {
                const bool* keys = SDL_GetKeyboardState(nullptr);
                if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) { held |= dw4::engine::pad_up; sy = 1; }
                if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) { held |= dw4::engine::pad_down; sy = -1; }
                if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) { held |= dw4::engine::pad_left; sx = -1; }
                if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) { held |= dw4::engine::pad_right; sx = 1; }
                if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_K]) held |= dw4::engine::pad_a;
                if (keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_L]) held |= dw4::engine::pad_b;
                if (keys[SDL_SCANCODE_J]) held |= dw4::engine::pad_x;
                if (keys[SDL_SCANCODE_I]) held |= dw4::engine::pad_y;
                if (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_TAB]) held |= dw4::engine::pad_start;
                if (keys[SDL_SCANCODE_Q]) held |= dw4::engine::pad_l;
                if (keys[SDL_SCANCODE_E]) held |= dw4::engine::pad_r;
                if (keys[SDL_SCANCODE_Z]) held |= dw4::engine::pad_z;
                for (auto* pad : gamepads) {
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) held |= dw4::engine::pad_a;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST)) held |= dw4::engine::pad_b;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST)) held |= dw4::engine::pad_x;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH)) held |= dw4::engine::pad_y;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START)) held |= dw4::engine::pad_start;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) held |= dw4::engine::pad_up;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) held |= dw4::engine::pad_down;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) held |= dw4::engine::pad_left;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) held |= dw4::engine::pad_right;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) held |= dw4::engine::pad_l;
                    if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) held |= dw4::engine::pad_r;
                    const float ax = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
                    const float ay = -SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
                    if (std::abs(ax) > 0.2f) sx = ax;
                    if (std::abs(ay) > 0.2f) sy = ay;
                    break;
                }
            }
            // Fixed 60 Hz simulation.
            const auto now = std::chrono::steady_clock::now();
            accumulator += std::chrono::duration<double>(now - last).count();
            last = now;
            int steps = options.smoke_frames ? 1 : 0;
            if (!options.smoke_frames) {
                while (accumulator >= 1.0 / 60.0 && steps < 4) { accumulator -= 1.0 / 60.0; ++steps; }
                if (steps == 4) accumulator = 0;
            }
            for (int i = 0; i < steps; ++i) {
                std::uint32_t input = held | scripted.held(frame);
                pads[0].connected = true;
                pads[0].held = input;
                pads[0].pressed = input & ~previous_held;
                pads[0].released = previous_held & ~input;
                pads[0].stick_x = sx;
                pads[0].stick_y = sy;
                previous_held = input;
                engine.step(pads);
                ++frame;
            }
            if (rendered_stage != engine.stage_name()) {
                renderer.clear_cache();
                rendered_stage = engine.stage_name();
            }
            engine.render(items, camera, clear);
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window, &width, &height);
            dw4::game::Viewport screen;
            if (width * 3 > height * 4) { screen.height = height; screen.width = height * 4 / 3; }
            else { screen.width = width; screen.height = width * 3 / 4; }
            screen.x = (width - screen.width) / 2;
            screen.y = (height - screen.height) / 2;
            renderer.render(items, camera, clear, screen, width, height);

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (show_menu) {
                ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
                ImGui::Begin("DW4 PC (F1)");
                ImGui::Text("Stage: %s  (%s)", engine.stage_name().c_str(), engine.loader_state().c_str());
                ImGui::Text("Frame %llu  draws %zu  meshes %zu  textures %zu", (unsigned long long)engine.frame(), items.size(),
                            renderer.mesh_count(), renderer.texture_count());
                static char stage[64] = "stagis10";
                ImGui::InputText("##stage", stage, sizeof stage);
                ImGui::SameLine();
                if (ImGui::Button("Load stage")) engine.request_stage(stage);
                if (ImGui::CollapsingHeader("Unimplemented original behavior", ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (const auto& note : engine.notes()) ImGui::BulletText("%s (x%zu)", note.text.c_str(), note.count);
                }
                if (ImGui::CollapsingHeader("Log")) {
                    for (const auto& line : engine.log_lines()) ImGui::TextUnformatted(line.c_str());
                }
                ImGui::End();
            }
            ImGui::Render();
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            const bool shoot = !options.screenshot.empty() &&
                ((options.screenshot_frames.empty() && frame == options.smoke_frames) ||
                 std::find(options.screenshot_frames.begin(), options.screenshot_frames.end(), frame) != options.screenshot_frames.end());
            if (shoot) {
                std::vector<std::uint8_t> pixels(std::size_t(width * height * 4));
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                std::string path = options.screenshot;
                if (!options.screenshot_frames.empty()) {
                    const auto dot = path.rfind('.');
                    path = path.substr(0, dot) + "_" + std::to_string(frame) + (dot == std::string::npos ? ".bmp" : path.substr(dot));
                }
                write_bmp(path, width, height, pixels);
                std::cout << "screenshot " << path << " at frame " << frame << " stage " << engine.stage_name() << '\n';
            }
            SDL_GL_SwapWindow(window);
            if (options.smoke_frames && frame >= options.smoke_frames) running = false;
        }
        std::cout << "stage " << engine.stage_name() << ", frame " << engine.frame() << ", draws " << items.size() << '\n';
        if (std::getenv("DW4_SCENE_REPORT")) for (const auto& line : engine.scene_report()) std::cout << line << '\n';
        if (std::getenv("DW4_DRAW_DUMP")) {
            for (const auto& item : items) {
                const auto& s = item.state;
                const std::string image = s.texture && s.texture->image ? std::string(s.texture->image.str(22)) : "-";
                std::printf("draw node='%s' verts=%zu tris=%zu tex=%d img=%s blend=%d(%d,%d) alpha=%d(%d,%.2f) vcol=%d "
                            "color=(%.2f,%.2f,%.2f,%.2f) light=%d depth=%d/%d cull=%d fog=%d\n",
                            item.node ? item.node->name.c_str() : "?", item.geometry->positions.size(),
                            item.geometry->indices.size() / 3, s.texture_enabled ? 1 : 0, image.c_str(), s.blend_enabled ? 1 : 0,
                            s.blend_src, s.blend_dst, s.alpha_test ? 1 : 0, s.alpha_func, s.alpha_ref,
                            item.geometry->colors.empty() ? 0 : 1, s.color.x, s.color.y, s.color.z, s.color.w, s.lighting ? 1 : 0,
                            s.depth_test ? 1 : 0, s.depth_write ? 1 : 0, s.cull_enabled ? 1 : 0, s.fog ? 1 : 0);
            }
        }
        std::cout << "unimplemented:\n";
        for (const auto& note : engine.notes()) std::cout << "  " << note.text << " x" << note.count << '\n';
        for (auto* pad : gamepads) SDL_CloseGamepad(pad);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error("missing value for " + arg);
                return argv[++i];
            };
            if (arg == "--data") options.data = next();
            else if (arg == "--stage") options.stage = next();
            else if (arg == "--smoke-test") options.smoke_frames = std::stoi(next());
            else if (arg == "--screenshot") options.screenshot = next();
            else if (arg == "--screenshot-frames") {
                std::string list = next();
                std::size_t start = 0;
                while (start < list.size()) {
                    const auto comma = list.find(',', start);
                    options.screenshot_frames.push_back(std::stoi(list.substr(start, comma - start)));
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            } else if (arg == "--input") options.input_script = next();
            else if (arg == "--debug-camera") {
                std::string list = next();
                std::size_t start = 0;
                while (start < list.size()) {
                    const auto comma = list.find(',', start);
                    options.debug_camera.push_back(std::stof(list.substr(start, comma - start)));
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }
            else if (arg == "--help" || arg == "-h") {
                std::cout << "dw4_game [--data EXTRACTED_ROOT] [--stage NAME] [--smoke-test FRAMES] [--screenshot FILE.bmp]\n"
                             "         [--screenshot-frames N,M,...] [--input SCRIPT]\n";
                return 0;
            } else throw std::runtime_error("unknown option " + arg);
        }
        if (options.data.empty()) options.data = default_data_path();
        if (options.data.empty()) throw std::runtime_error("no --data given and local/settings.ini has no path");
        return run(options);
    } catch (const std::exception& error) {
        std::cerr << "dw4_game: " << error.what() << '\n';
        return 1;
    }
}
