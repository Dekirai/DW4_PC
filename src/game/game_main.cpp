// Native Digimon World 4 runtime: SDL3 window, OpenGL renderer and the
// reconstructed engine in dw4::engine. Original data is read from --data.
#include "gl_renderer.hpp"
#include "movie_decoder.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
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
#include <sstream>
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
    std::vector<std::pair<int, int>> flags;
    std::filesystem::path card_dir; // memory card GCI folders (Card A / Card B)
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

// Scripted input for automated runs: lines "frame button [duration [player]]"
// (button: A,B,X,Y,START,UP,DOWN,LEFT,RIGHT,Z,L,R; player 1-4, default 1) and "frame GOTO x z [duration]",
// which steers player 1's stick toward world (x, z) relative to the camera, and
// "frame SEEK duration", which walks to the nearest pickup or live enemy and
// presses A next to it, and "frame SEND CATEGORY COMMAND [argument]".
struct ScriptedInput {
    void load(const std::string& path) {
        std::ifstream in(path);
        int frame;
        std::string name;
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream fields(line);
            int duration = 6;
            if (!(fields >> frame >> name)) continue;
            if (name == "GOTO") {
                Goto g{frame, 0, 0, 600};
                fields >> g.x >> g.z >> g.duration;
                gotos.push_back(g);
                continue;
            }
            if (name == "SEEK") {
                Goto g{frame, 0, 0, 600, true};
                fields >> g.duration;
                gotos.push_back(g);
                continue;
            }
            if (name == "SEND") {
                // "frame SEND CATEGORY COMMAND [a/b/I<n> [ROW]]": a messenger
                // command like the stage scripts' DMsetSD (row -1 unless given,
                // "-" for no arguments), e.g. opening a shop.
                Send send{frame};
                fields >> send.category >> send.command >> send.argument >> send.row;
                if (send.argument == "-") send.argument.clear();
                sends.push_back(send);
                continue;
            }
            int player = 1;
            fields >> duration >> player;
            std::uint32_t bit = 0;
            namespace b = dw4::pad_button;
            if (name == "A") bit = b::a;
            else if (name == "B") bit = b::b;
            else if (name == "X") bit = b::x;
            else if (name == "Y") bit = b::y;
            else if (name == "START") bit = b::start;
            else if (name == "UP") bit = b::up;
            else if (name == "DOWN") bit = b::down;
            else if (name == "LEFT") bit = b::left;
            else if (name == "RIGHT") bit = b::right;
            else if (name == "Z") bit = b::z;
            else if (name == "L") bit = b::l;
            else if (name == "R") bit = b::r;
            presses.push_back({frame, bit, duration, std::clamp(player - 1, 0, 3)});
        }
    }
    struct Send { int frame; std::string category, command, argument; int row = -1; };
    std::vector<Send> sends;
    struct Press { int frame; std::uint32_t bit; int duration; int slot; };
    std::vector<Press> presses;
    struct Goto { int frame; float x, z; int duration; bool seek = false; };
    std::vector<Goto> gotos;
    const Goto* active_goto(int frame) const {
        for (const auto& g : gotos) if (frame >= g.frame && frame < g.frame + g.duration) return &g;
        return nullptr;
    }
    std::uint32_t held(int frame, int slot = 0) const {
        std::uint32_t bits = 0;
        for (const auto& p : presses)
            if (p.slot == slot && frame >= p.frame && frame < p.frame + p.duration) bits |= p.bit;
        return bits;
    }
    // A controller slot the script presses buttons on counts as connected.
    bool uses_slot(int slot) const {
        for (const auto& p : presses) if (p.slot == slot) return true;
        return false;
    }
};

int run(const Options& options) {
    dw4::engine::Engine engine(options.data);
    // Memory cards: --card-dir, DW4_CARD_DIR, else saves/GC/USA next to the
    // program. Dolphin's GC/USA folder works too (same GCI files).
    if (!options.card_dir.empty()) engine.set_card_root(options.card_dir);
    else if (const char* dir = std::getenv("DW4_CARD_DIR")) engine.set_card_root(dir);
    else if (const char* base = SDL_GetBasePath()) engine.set_card_root(std::filesystem::path(base) / "saves" / "GC" / "USA");
    for (const auto& [flag, value] : options.flags) engine.flags()[flag] = static_cast<std::uint8_t>(value);
    // DW4_PLAYERS=n: test runs start with n joined players.
    if (const char* count = std::getenv("DW4_PLAYERS"))
        if (auto* sys = engine.get<dw4::engine::GameSystem>())
            for (int i = 0; i < std::clamp(std::atoi(count), 0, 4); ++i) sys->players[std::size_t(i)].joined = true;
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

    // Audio: the SoundManager's streams go to the default playback device
    // (not in smoke tests). DW4_AUDIO_DUMP=FILE.wav records what was mixed.
    SDL_AudioStream* audio = nullptr;
    if (!options.smoke_frames && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, dw4::engine::SoundManager::kOutputRate};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (audio) SDL_ResumeAudioStreamDevice(audio);
        else std::cerr << "audio: " << SDL_GetError() << '\n';
    }
    const char* audio_dump_path = std::getenv("DW4_AUDIO_DUMP");
    std::vector<std::int16_t> audio_dump;
    constexpr int kAudioFrameSamples = dw4::engine::SoundManager::kOutputRate / 60;
    auto mix_audio = [&](int frames) {
        auto* sound = engine.get<dw4::engine::SoundManager>();
        if (!sound || (!audio && !audio_dump_path)) return;
        std::vector<std::int16_t> pcm(std::size_t(frames) * 2);
        sound->render(pcm.data(), std::size_t(frames));
        if (audio) SDL_PutAudioStreamData(audio, pcm.data(), int(pcm.size() * sizeof(std::int16_t)));
        if (audio_dump_path) audio_dump.insert(audio_dump.end(), pcm.begin(), pcm.end());
    };

    {
        dw4::game::GlRenderer renderer;
        std::vector<SDL_Gamepad*> gamepads;
        ScriptedInput scripted;
        if (!options.input_script.empty()) scripted.load(options.input_script);
        std::vector<dw4::engine::DrawItem> items;
        dw4::engine::ShadowFrame shadow;
        dw4::engine::FrameCamera camera;
        dw4::engine::Vec4f clear{};
        std::array<dw4::engine::PadSample, 4> pads{};
        std::string rendered_stage;
        std::unique_ptr<dw4::game::MovieDecoder> movie_decoder;
        std::uint32_t movie_serial = 0;
        bool running = true, show_menu = false;
        int frame = 0;
        const int trace_player = std::getenv("DW4_TRACE_PLAYER") ? std::atoi(std::getenv("DW4_TRACE_PLAYER")) : 0;
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
                if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
                    for (auto it = gamepads.begin(); it != gamepads.end(); ++it)
                        if (SDL_GetGamepadID(*it) == event.gdevice.which) {
                            SDL_CloseGamepad(*it);
                            gamepads.erase(it);
                            break;
                        }
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
            // Input: player 1 is the keyboard and the first gamepad, players 2-4
            // the further gamepads, each on its own controller slot.
            std::uint32_t held = 0;
            float sx = 0, sy = 0;
            std::array<std::uint32_t, 4> slot_held{};
            std::array<dw4::PadStick, 4> slot_left{}, slot_right{};
            // GameCube layout: L / R on the triggers (and L on the left
            // shoulder), Z on the right shoulder, the C stick on the right stick.
            auto read_gamepad = [](SDL_Gamepad* pad, std::uint32_t& buttons, dw4::PadStick& left, dw4::PadStick& right) {
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_SOUTH)) buttons |= dw4::pad_button::a;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_EAST)) buttons |= dw4::pad_button::b;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_WEST)) buttons |= dw4::pad_button::x;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_NORTH)) buttons |= dw4::pad_button::y;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_START)) buttons |= dw4::pad_button::start;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP)) buttons |= dw4::pad_button::up;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) buttons |= dw4::pad_button::down;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) buttons |= dw4::pad_button::left;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) buttons |= dw4::pad_button::right;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) ||
                    SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000)
                    buttons |= dw4::pad_button::l;
                if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) buttons |= dw4::pad_button::r;
                if (SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) buttons |= dw4::pad_button::z;
                auto axis = [&](SDL_GamepadAxis a, float sign) {
                    const float v = sign * SDL_GetGamepadAxis(pad, a) / 32767.0f;
                    return std::abs(v) > 0.2f ? v : 0.0f;
                };
                left = {axis(SDL_GAMEPAD_AXIS_LEFTX, 1.0f), axis(SDL_GAMEPAD_AXIS_LEFTY, -1.0f)};
                right = {axis(SDL_GAMEPAD_AXIS_RIGHTX, 1.0f), axis(SDL_GAMEPAD_AXIS_RIGHTY, -1.0f)};
            };
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
                if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) { held |= dw4::pad_button::up; sy = 1; }
                if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) { held |= dw4::pad_button::down; sy = -1; }
                if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) { held |= dw4::pad_button::left; sx = -1; }
                if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) { held |= dw4::pad_button::right; sx = 1; }
                if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_K]) held |= dw4::pad_button::a;
                if (keys[SDL_SCANCODE_BACKSPACE] || keys[SDL_SCANCODE_L]) held |= dw4::pad_button::b;
                if (keys[SDL_SCANCODE_J]) held |= dw4::pad_button::x;
                if (keys[SDL_SCANCODE_I]) held |= dw4::pad_button::y;
                if (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_TAB]) held |= dw4::pad_button::start;
                if (keys[SDL_SCANCODE_Q]) held |= dw4::pad_button::l;
                if (keys[SDL_SCANCODE_E]) held |= dw4::pad_button::r;
                if (keys[SDL_SCANCODE_Z]) held |= dw4::pad_button::z;
                for (std::size_t i = 0; i < gamepads.size() && i < 4; ++i)
                    read_gamepad(gamepads[i], slot_held[i], slot_left[i], slot_right[i]);
                held |= slot_held[0];
                if (slot_left[0].x != 0.0f) sx = slot_left[0].x;
                if (slot_left[0].y != 0.0f) sy = slot_left[0].y;
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
                for (const auto& send : scripted.sends) {
                    if (send.frame != frame) continue;
                    dw4::engine::Packet packet;
                    if (!send.argument.empty()) dw4::engine::parse_packet_arguments(send.argument, packet);
                    engine.messenger().send(engine, send.category, std::nullopt, send.row, send.command, packet);
                }
                std::uint32_t input = held | scripted.held(frame);
                float stick_x = sx, stick_y = sy;
                if (const auto* g = scripted.active_goto(frame); g && stick_x == 0 && stick_y == 0) {
                    auto* models = engine.get<dw4::engine::ModelCtrlManager>();
                    auto* cameras = engine.get<dw4::engine::CameraManager>();
                    if (const auto* p = models ? models->player(0) : nullptr; p && p->node && cameras) {
                        float tx = g->x, tz = g->z, reach = 4.0f;
                        if (g->seek) {
                            // Pickups first, then the nearest live enemy.
                            float best = 1e12f;
                            for (int pass = 0; pass < 2 && best == 1e12f; ++pass)
                                for (const auto& e : models->rams()) {
                                    if (!e->generated || !e->node) continue;
                                    if (pass == 0 ? (e->category != "ITEM" || e->item_table < 0)
                                                  : (e->category != "ENEMY" || e->hp <= 0))
                                        continue;
                                    const float ex = e->node->matrix(3, 0) - p->node->matrix(3, 0);
                                    const float ez = e->node->matrix(3, 2) - p->node->matrix(3, 2);
                                    if (ex * ex + ez * ez < best) {
                                        best = ex * ex + ez * ez;
                                        tx = e->node->matrix(3, 0);
                                        tz = e->node->matrix(3, 2);
                                        reach = pass == 0 ? 9.0f : 400.0f;
                                    }
                                }
                            if (best == 1e12f) { tx = p->node->matrix(3, 0); tz = p->node->matrix(3, 2); }
                            else if (best <= reach && frame % 20 < 3) input |= dw4::pad_button::a;
                        }
                        const float dx = tx - p->node->matrix(3, 0), dz = tz - p->node->matrix(3, 2);
                        if (dx * dx + dz * dz > reach) {
                            dw4::engine::FrameCamera camera;
                            cameras->frame_camera(engine, camera);
                            dw4::engine::Mat4 inverse;
                            if (dw4::engine::invert(camera.view, inverse)) {
                                const float heading = std::atan2(inverse(2, 0), inverse(2, 2));
                                const float relative = std::atan2(dx, dz) - heading;
                                stick_x = std::sin(relative);
                                stick_y = -std::cos(relative);
                            }
                        }
                    }
                }
                if (stick_x == 0 && stick_y == 0) {
                    if (input & dw4::pad_button::left) stick_x = -1;
                    if (input & dw4::pad_button::right) stick_x = 1;
                    if (input & dw4::pad_button::up) stick_y = 1;
                    if (input & dw4::pad_button::down) stick_y = -1;
                }
                // Original device: full deflection is 72/74 after the SDK clamp.
                auto scale = [](dw4::PadStick stick) {
                    return dw4::PadStick{std::clamp(stick.x, -1.0f, 1.0f) * (72.0f / 74.0f),
                                                 std::clamp(stick.y, -1.0f, 1.0f) * (72.0f / 74.0f)};
                };
                pads[0].connected = true;
                pads[0].buttons = input;
                pads[0].left = scale({stick_x, stick_y});
                pads[0].right = scale(slot_right[0]);
                for (std::size_t i = 1; i < 4; ++i) {
                    pads[i].connected = i < gamepads.size() || scripted.uses_slot(int(i));
                    dw4::PadStick left = slot_left[i];
                    const std::uint32_t buttons = slot_held[i] | scripted.held(frame, int(i));
                    if (left.x == 0.0f && left.y == 0.0f) {
                        if (buttons & dw4::pad_button::left) left.x = -1;
                        if (buttons & dw4::pad_button::right) left.x = 1;
                        if (buttons & dw4::pad_button::up) left.y = 1;
                        if (buttons & dw4::pad_button::down) left.y = -1;
                    }
                    pads[i].buttons = buttons;
                    pads[i].left = scale(left);
                    pads[i].right = scale(slot_right[i]);
                }
                engine.step(pads);
                mix_audio(kAudioFrameSamples);
                ++frame;
                // DW4_TRACE_PLAYER=N prints player 1's sequence and position every N frames.
                if (trace_player > 0 && frame % trace_player == 0)
                    if (auto* models = engine.get<dw4::engine::ModelCtrlManager>())
                        if (const auto* p = models->player(0); p && p->node) {
                            std::printf("[%d] player0 %s hp=%d (%.2f, %.2f, %.2f)\n", frame, p->seq_name.c_str(), p->hp,
                                        p->node->matrix(3, 0), p->node->matrix(3, 1), p->node->matrix(3, 2));
                            for (const auto& e : models->rams())
                                if (e->generated && (e->category == "ENEMY" || e->category == "ITEM") && e->node)
                                    std::printf("      %s#%d %s hp=%d (%.1f, %.1f, %.1f)\n", e->info_name.c_str(), e->index,
                                                e->seq_name.c_str(), e->hp, e->node->matrix(3, 0), e->node->matrix(3, 1),
                                                e->node->matrix(3, 2));
                        }
            }
            if (rendered_stage != engine.stage_name()) {
                renderer.clear_cache();
                rendered_stage = engine.stage_name();
            }
            engine.render(items, camera, clear, &shadow);
            renderer.set_shadow(&shadow);
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window, &width, &height);
            dw4::game::Viewport screen;
            if (width * 3 > height * 4) { screen.height = height; screen.width = height * 4 / 3; }
            else { screen.width = width; screen.height = width * 3 / 4; }
            screen.x = (width - screen.width) / 2;
            screen.y = (height - screen.height) / 2;
            renderer.render(items, camera, clear, screen, width, height);
            // A playing movie covers the screen with its current picture.
            if (const auto* sound = engine.get<dw4::engine::SoundManager>(); sound && sound->movie()) {
                const auto* movie = sound->movie();
                if (!movie_decoder || movie_serial != movie->serial) {
                    movie_decoder = std::make_unique<dw4::game::MovieDecoder>(movie->video);
                    movie_serial = movie->serial;
                }
                const double seconds = double(engine.time() - movie->start) / 1e9;
                const auto picture = static_cast<std::size_t>(std::max(0.0, seconds * movie->frame_rate));
                if (movie_decoder->seek(picture))
                    renderer.draw_movie(movie_decoder->rgba().data(), movie_decoder->width(), movie_decoder->height(), screen,
                                        width, height);
            } else {
                movie_decoder.reset();
            }

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
                if (auto* models = engine.get<dw4::engine::ModelCtrlManager>()) {
                    if (const auto* p = models->player(0); p && p->node) {
                        const auto& m = p->node->matrix;
                        ImGui::Text("Player 0: %s seq=%s pos=(%.1f, %.1f, %.1f) ctrl=%d mode=%d ground=%d", p->info_name.c_str(),
                                    p->seq_name.c_str(), m(3, 0), m(3, 1), m(3, 2), p->control_enabled ? 1 : 0, p->ctrl_mode,
                                    p->on_ground ? 1 : 0);
                    }
                }
                if (ImGui::CollapsingHeader("Unimplemented original behavior", ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (const auto& note : engine.notes()) ImGui::BulletText("%s (x%zu)", note.text.c_str(), note.count);
                }
                if (ImGui::CollapsingHeader("Demo scripts")) {
                    for (const auto& line : engine.demo().describe()) ImGui::TextUnformatted(line.c_str());
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
            // Keep about three frames of audio queued across frame hitches.
            if (audio) {
                const int queued = SDL_GetAudioStreamQueued(audio) / int(2 * sizeof(std::int16_t));
                if (queued < kAudioFrameSamples * 2) mix_audio(kAudioFrameSamples * 3 - queued);
            }
            SDL_GL_SwapWindow(window);
            if (options.smoke_frames && frame >= options.smoke_frames) running = false;
        }
        std::cout << "stage " << engine.stage_name() << ", frame " << engine.frame() << ", draws " << items.size() << '\n';
        if (auto* models = engine.get<dw4::engine::ModelCtrlManager>())
            if (const auto* p = models->player(0); p && p->node)
                std::printf("player0 seq=%s pos=(%.2f, %.2f, %.2f) ctrl=%d mode=%d ground=%d\n", p->seq_name.c_str(), p->node->matrix(3, 0),
                            p->node->matrix(3, 1), p->node->matrix(3, 2), p->control_enabled ? 1 : 0, p->ctrl_mode, p->on_ground ? 1 : 0);
        for (const auto& line : engine.demo().describe()) std::cout << "demo " << line << '\n';
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
                if (!item.geometry->positions.empty()) {
                    auto lo = item.geometry->positions[0], hi = lo;
                    for (const auto& p : item.geometry->positions) {
                        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
                        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
                    }
                    const auto& m = item.model;
                    std::printf("     layer=%u bounds=(%.2f,%.2f,%.2f)-(%.2f,%.2f,%.2f) model.t=(%.2f,%.2f,%.2f) diag=(%.2f,%.2f,%.2f)\n",
                                unsigned(item.layer), lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, m(3, 0), m(3, 1), m(3, 2), m(0, 0), m(1, 1),
                                m(2, 2));
                }
            }
        }
        if (audio_dump_path) {
            // 16-bit stereo PCM WAV of everything mixed.
            std::ofstream wav(audio_dump_path, std::ios::binary);
            const std::uint32_t rate = dw4::engine::SoundManager::kOutputRate;
            const std::uint32_t bytes = std::uint32_t(audio_dump.size() * sizeof(std::int16_t));
            auto u32 = [&](std::uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
            auto u16 = [&](std::uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
            wav.write("RIFF", 4);
            u32(36 + bytes);
            wav.write("WAVEfmt ", 8);
            u32(16);
            u16(1);
            u16(2);
            u32(rate);
            u32(rate * 4);
            u16(4);
            u16(16);
            wav.write("data", 4);
            u32(bytes);
            wav.write(reinterpret_cast<const char*>(audio_dump.data()), std::streamsize(bytes));
            std::cout << "audio dump " << audio_dump_path << " (" << audio_dump.size() / 2 << " frames)\n";
        }
        std::cout << "unimplemented:\n";
        for (const auto& note : engine.notes()) std::cout << "  " << note.text << " x" << note.count << '\n';
        for (auto* pad : gamepads) SDL_CloseGamepad(pad);
    }
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (audio) SDL_DestroyAudioStream(audio);
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
            else if (arg == "--card-dir") options.card_dir = next();
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
            else if (arg == "--flag") {
                const std::string value = next();
                const auto eq = value.find('=');
                options.flags.emplace_back(std::stoi(value.substr(0, eq)), eq == std::string::npos ? 1 : std::stoi(value.substr(eq + 1)));
            }
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
                             "         [--screenshot-frames N,M,...] [--input SCRIPT] [--card-dir GCI_ROOT]\n";
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
