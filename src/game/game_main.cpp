// Native Digimon World 4 runtime: SDL3 window, OpenGL renderer and the
// reconstructed engine in dw4::engine. Original data is read from --data.
#include "gl_renderer.hpp"
#include "interpolation.hpp"
#include "movie_decoder.hpp"
#include "settings.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/disc.hpp"
#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <optional>
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
                fields >> g.duration >> g.target;
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
    // SEEK may name a model prefix to go for first (e.g. g_b01cpole).
    struct Goto { int frame; float x, z; int duration; bool seek = false; std::string target; };
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

// The supported game data: the GDJEB2 disc 0 revision 0 image or a Dolphin
// extraction of it. Returns a problem description, empty when usable.
std::string check_game_data(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::is_directory(path, error)) {
        if (std::filesystem::is_regular_file(path / "files" / "ndmw.igb", error) ||
            std::filesystem::is_regular_file(path / "ndmw.igb", error))
            return {};
        return "This folder is not a Digimon World 4 extraction (no files/ndmw.igb):\n" + path.string();
    }
    if (!dw4::DiscImage::looks_like_disc(path))
        return "This is not an uncompressed GameCube disc image:\n" + path.string() +
               "\n\nCompressed images (RVZ, WIA, CISO, NKit) can be converted back to ISO in Dolphin "
               "(right-click the game > Convert File...).";
    try {
        const auto disc = dw4::DiscImage::open(path);
        if (disc.game_id() != "GDJEB2" || disc.disc_number() != 0 || disc.revision() != 0)
            return "This disc is " + disc.game_id() + " (revision " + std::to_string(disc.revision()) +
                   "), not Digimon World 4 USA (GDJEB2, revision 0).";
        if (!disc.find("ndmw.igb")) return "The disc image has no ndmw.igb.";
    } catch (const std::exception& problem) {
        return problem.what();
    }
    return {};
}

// First start without game data: ask for the disc image or extraction folder.
std::filesystem::path choose_game_data() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return {};
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Disc image..."},
        {0, 2, "Extracted folder..."},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
    };
    const SDL_MessageBoxData box{SDL_MESSAGEBOX_INFORMATION, nullptr, "Digimon World 4 PC",
                                 "Choose your Digimon World 4 (USA) game data: an uncompressed disc image "
                                 "(.iso / .gcm) or a Dolphin extraction folder. The choice is saved in settings.ini.",
                                 3, buttons, nullptr};
    int choice = 0;
    if (!SDL_ShowMessageBox(&box, &choice) || choice == 0) return {};
    // The dialog may answer from another thread.
    struct Result {
        std::atomic<bool> done{false};
        std::filesystem::path path;
    } result;
    const SDL_DialogFileCallback callback = [](void* user, const char* const* files, int) {
        auto* r = static_cast<Result*>(user);
        if (files && files[0]) r->path = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(files[0])));
        r->done.store(true);
    };
    static const SDL_DialogFileFilter filters[] = {{"GameCube disc image", "iso;gcm"}, {"All files", "*"}};
    if (choice == 1) SDL_ShowOpenFileDialog(callback, &result, nullptr, filters, 2, nullptr, false);
    else SDL_ShowOpenFolderDialog(callback, &result, nullptr, nullptr, false);
    while (!result.done.load()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        SDL_Delay(10);
    }
    return result.path;
}

// A key button of the controls table waiting for the next key press.
struct KeyCapture {
    std::size_t button{};
    std::size_t slot{};
};

int run(const Options& options, dw4::game::Settings& settings) {
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
    // Multisampling is chosen with the window (settings.ini msaa, read at
    // start-up); without support the window opens without it.
    const int msaa = options.smoke_frames ? 0 : settings.msaa;
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa > 1 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa > 1 ? msaa : 0);
    // Test runs ignore settings.ini: 1280x960 unless DW4_WINDOW=WxH, 4:3
    // unless DW4_WIDESCREEN=1.
    int window_width = options.smoke_frames ? 1280 : settings.width;
    int window_height = options.smoke_frames ? 960 : settings.height;
    if (options.smoke_frames) {
        if (const char* size = std::getenv("DW4_WINDOW")) std::sscanf(size, "%dx%d", &window_width, &window_height);
        settings.widescreen = std::getenv("DW4_WIDESCREEN") != nullptr;
    }
    SDL_Window* window = SDL_CreateWindow("Digimon World 4 PC", window_width, window_height, flags);
    SDL_GLContext context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context && msaa > 1) {
        if (window) SDL_DestroyWindow(window);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        window = SDL_CreateWindow("Digimon World 4 PC", window_width, window_height, flags);
        context = window ? SDL_GL_CreateContext(window) : nullptr;
    }
    if (!window || !context) throw std::runtime_error(SDL_GetError());
    SDL_GL_MakeCurrent(window, context);
    SDL_GL_SetSwapInterval(options.smoke_frames || !settings.vsync ? 0 : 1);
    if (!options.smoke_frames && settings.fullscreen) SDL_SetWindowFullscreen(window, true);
    int samples = 0;
    SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &samples);
    if (samples > 1) glEnable(GL_MULTISAMPLE);

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
        // Master volume (settings.ini [audio] volume, percent).
        if (settings.volume < 100)
            for (auto& sample : pcm) sample = std::int16_t(int(sample) * settings.volume / 100);
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
        // DW4_OPEN_MENU=1 opens the F1 menu at start (menu screenshots).
        bool running = true, show_menu = std::getenv("DW4_OPEN_MENU") != nullptr;
        bool settings_changed = false;
        std::optional<KeyCapture> capture;
        auto next_present = std::chrono::steady_clock::now();
        // Smooth motion above 60 presents per second (settings.ini smooth_motion).
        dw4::game::MotionInterpolator interpolator;
        double present_interval = 1.0 / 60.0;
        auto last_present = std::chrono::steady_clock::now();
        int frame = 0;
        const int trace_player = std::getenv("DW4_TRACE_PLAYER") ? std::atoi(std::getenv("DW4_TRACE_PLAYER")) : 0;
        auto last = std::chrono::steady_clock::now();
        double accumulator = 0;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                // A key binding being edited takes the next key (F1 still
                // closes the menu).
                if (capture && event.type == SDL_EVENT_KEY_DOWN && event.key.scancode != SDL_SCANCODE_F1) {
                    settings.keys[capture->button][capture->slot] = event.key.scancode;
                    // A key stands for one button only.
                    for (std::size_t b = 0; b < settings.keys.size(); ++b)
                        for (std::size_t k = 0; k < settings.keys[b].size(); ++k)
                            if ((b != capture->button || k != capture->slot) && settings.keys[b][k] == event.key.scancode)
                                settings.keys[b][k] = SDL_SCANCODE_UNKNOWN;
                    capture.reset();
                    settings_changed = true;
                    continue;
                }
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_WINDOW_RESIZED && !options.smoke_frames &&
                    !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)) {
                    settings.width = event.window.data1;
                    settings.height = event.window.data2;
                    settings_changed = true;
                }
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
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1) {
                    show_menu = !show_menu;
                    capture.reset();
                    if (!show_menu && settings_changed && settings.save()) settings_changed = false;
                }
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11 && !event.key.repeat) {
                    settings.fullscreen = !settings.fullscreen;
                    SDL_SetWindowFullscreen(window, settings.fullscreen);
                    settings_changed = true;
                }
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
                // Keyboard: the bound keys of settings.ini [keyboard]; the
                // direction keys also push the stick fully.
                const bool* keys = SDL_GetKeyboardState(nullptr);
                for (std::size_t b = 0; b < settings.keys.size(); ++b)
                    for (const SDL_Scancode code : settings.keys[b])
                        if (code != SDL_SCANCODE_UNKNOWN && keys[code]) held |= dw4::game::pad_key_bit(dw4::game::PadKey(b));
                if (held & dw4::pad_button::up) sy = 1;
                if (held & dw4::pad_button::down) sy = -1;
                if (held & dw4::pad_button::left) sx = -1;
                if (held & dw4::pad_button::right) sx = 1;
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
            const bool paused = show_menu && settings.pause_in_menu && !options.smoke_frames;
            if (paused) accumulator = 0;
            if (!options.smoke_frames && !paused) {
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
                            // Named targets, then pickups, then the nearest live enemy.
                            float best = 1e12f, target_y = 0.0f;
                            for (int pass = g->target.empty() ? 0 : -1; pass < 2 && best == 1e12f; ++pass)
                                for (const auto& e : models->rams()) {
                                    if (!e->generated || !e->node) continue;
                                    if (pass < 0 ? (e->info_name.rfind(g->target, 0) != 0 || e->hp <= 0)
                                        : pass == 0 ? (e->category != "ITEM" || e->item_table < 0)
                                                    : (e->category != "ENEMY" || e->hp <= 0))
                                        continue;
                                    const float ex = e->node->matrix(3, 0) - p->node->matrix(3, 0);
                                    const float ez = e->node->matrix(3, 2) - p->node->matrix(3, 2);
                                    if (ex * ex + ez * ez < best) {
                                        best = ex * ex + ez * ez;
                                        tx = e->node->matrix(3, 0);
                                        tz = e->node->matrix(3, 2);
                                        target_y = e->node->matrix(3, 1);
                                        reach = pass < 0 ? 64.0f : pass == 0 ? 9.0f : 400.0f;
                                    }
                                }
                            // A large boss body keeps the player beyond the reach:
                            // attack anyway once the walk stalls nearby.
                            static float last_x = 0, last_z = 0;
                            static int stalled = 0;
                            const float mx = p->node->matrix(3, 0) - last_x, mz = p->node->matrix(3, 2) - last_z;
                            stalled = mx * mx + mz * mz < 0.01f ? stalled + 1 : 0;
                            last_x = p->node->matrix(3, 0);
                            last_z = p->node->matrix(3, 2);
                            if (best == 1e12f) { tx = p->node->matrix(3, 0); tz = p->node->matrix(3, 2); }
                            else if (best <= reach || (best <= 6400.0f && stalled > 20)) {
                                // A target floating above the player is attacked from jumps.
                                if (target_y > p->node->matrix(3, 1) + 8.0f) {
                                    if (frame % 40 < 3) input |= dw4::pad_button::x;
                                    else if (frame % 40 >= 10 && frame % 40 < 13) input |= dw4::pad_button::a;
                                } else if (frame % 20 < 3) {
                                    input |= dw4::pad_button::a;
                                }
                            }
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
            if (steps > 0) interpolator.advance();
            if (rendered_stage != engine.stage_name()) {
                renderer.clear_cache();
                rendered_stage = engine.stage_name();
                interpolator.reset();
            }
            engine.render(items, camera, clear, &shadow);
            if (!interpolator.captured()) interpolator.capture(items, camera, shadow);
            if (!options.smoke_frames && settings.smooth_motion && present_interval < 1.0 / 75.0)
                interpolator.blend(items, camera, shadow, float(accumulator * 60.0));
            renderer.set_shadow(&shadow);
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window, &width, &height);
            dw4::game::Viewport screen;
            if (width * 3 > height * 4) { screen.height = height; screen.width = height * 4 / 3; }
            else { screen.width = width; screen.height = width * 3 / 4; }
            screen.x = (width - screen.width) / 2;
            screen.y = (height - screen.height) / 2;
            // Widescreen: the 3D scene uses the whole window width.
            dw4::game::Viewport wide = screen;
            if (settings.widescreen && width * 3 > height * 4) {
                wide.x = 0;
                wide.width = width;
            }
            renderer.render(items, camera, clear, screen, wide, width, height);
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
                ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_FirstUseEver);
                ImGui::Begin("DW4 PC (F1)");
                if (ImGui::BeginTabBar("menu")) {
                if (ImGui::BeginTabItem("Settings")) {
                    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::Checkbox("Fullscreen (F11)", &settings.fullscreen)) {
                            SDL_SetWindowFullscreen(window, settings.fullscreen);
                            settings_changed = true;
                        }
                        if (ImGui::Checkbox("Widescreen (the 3D view fills wide windows, 2D stays 4:3)", &settings.widescreen))
                            settings_changed = true;
                        if (ImGui::Checkbox("VSync", &settings.vsync)) {
                            SDL_GL_SetSwapInterval(settings.vsync ? 1 : 0);
                            settings_changed = true;
                        }
                        if (ImGui::SliderInt("Frame limit", &settings.fps_limit, 0, 300, settings.fps_limit ? "%d fps" : "off"))
                            settings_changed = true;
                        if (ImGui::Checkbox("Smooth motion above 60 fps", &settings.smooth_motion)) settings_changed = true;
                        ImGui::SameLine();
                        ImGui::TextDisabled("(now %.0f fps)", present_interval > 0 ? 1.0 / present_interval : 0.0);
                        ImGui::TextDisabled("The game itself always runs at 60 updates per second.");
                        static const int sample_choices[] = {0, 2, 4, 8};
                        static const char* sample_names[] = {"Off", "2x", "4x", "8x"};
                        int choice = 0;
                        for (int i = 0; i < 4; ++i)
                            if (settings.msaa >= sample_choices[i]) choice = i;
                        if (ImGui::Combo("Anti-aliasing (MSAA)", &choice, sample_names, 4)) {
                            settings.msaa = sample_choices[choice];
                            settings_changed = true;
                        }
                        ImGui::TextDisabled("Anti-aliasing applies at the next start (now %dx).", samples > 1 ? samples : 0);
                        static const int sizes[][2] = {{1280, 960}, {1440, 1080}, {1920, 1440}, {1280, 720}, {1920, 1080}, {2560, 1440}};
                        if (!settings.fullscreen) {
                            ImGui::TextUnformatted("Window size:");
                            for (const auto& size : sizes) {
                                ImGui::SameLine();
                                const std::string label = std::to_string(size[0]) + "x" + std::to_string(size[1]);
                                if (ImGui::SmallButton(label.c_str())) {
                                    SDL_SetWindowSize(window, size[0], size[1]);
                                    settings.width = size[0];
                                    settings.height = size[1];
                                    settings_changed = true;
                                }
                            }
                        }
                    }
                    if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (ImGui::SliderInt("Volume", &settings.volume, 0, 100, "%d%%")) settings_changed = true;
                    }
                    if (ImGui::CollapsingHeader("Keyboard (player 1)", ImGuiTreeNodeFlags_DefaultOpen)) {
                        ImGui::TextDisabled("Click a key to change it, right-click to clear it.");
                        if (ImGui::BeginTable("keys", 4, ImGuiTableFlags_SizingStretchSame)) {
                            for (std::size_t b = 0; b < settings.keys.size(); ++b) {
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::TextUnformatted(dw4::game::pad_key_name(dw4::game::PadKey(b)));
                                for (std::size_t k = 0; k < settings.keys[b].size(); ++k) {
                                    ImGui::TableNextColumn();
                                    const bool waiting = capture && capture->button == b && capture->slot == k;
                                    const SDL_Scancode code = settings.keys[b][k];
                                    std::string label = waiting ? "press a key..."
                                                      : code == SDL_SCANCODE_UNKNOWN ? "-"
                                                                                     : SDL_GetScancodeName(code);
                                    label += "##" + std::to_string(b * 8 + k);
                                    if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) capture = KeyCapture{b, k};
                                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                                        settings.keys[b][k] = SDL_SCANCODE_UNKNOWN;
                                        capture.reset();
                                        settings_changed = true;
                                    }
                                }
                            }
                            ImGui::EndTable();
                        }
                        if (ImGui::Button("Default keys")) {
                            settings.reset_keys();
                            capture.reset();
                            settings_changed = true;
                        }
                    }
                    if (ImGui::CollapsingHeader("Controllers", ImGuiTreeNodeFlags_DefaultOpen)) {
                        if (gamepads.empty()) ImGui::TextDisabled("No controller connected.");
                        for (std::size_t i = 0; i < gamepads.size() && i < 4; ++i) {
                            const char* name = SDL_GetGamepadName(gamepads[i]);
                            ImGui::BulletText("Player %zu: %s", i + 1, name ? name : "controller");
                        }
                        ImGui::TextDisabled("A = south, B = east, X = west, Y = north, L = left trigger or shoulder,");
                        ImGui::TextDisabled("R = right trigger, Z = right shoulder, Start = start.");
                    }
                    if (ImGui::Checkbox("Pause the game while this menu is open", &settings.pause_in_menu)) settings_changed = true;
                    ImGui::TextDisabled("Settings file: %s", settings.file.string().c_str());
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Cheats")) {
                    ImGui::TextWrapped("Optional cheats for this session. They change the original game; what they "
                                       "give (levels, bits) stays in saves.");
                    auto& cheats = engine.cheats();
                    ImGui::Checkbox("God mode (players lose no HP)", &cheats.god);
                    ImGui::Checkbox("Infinite MP", &cheats.infinite_mp);
                    ImGui::Checkbox("One-hit defeats (player hits)", &cheats.one_hit);
                    if (auto* sys = engine.get<dw4::engine::GameSystem>()) {
                        for (int slot = 0; slot < 4; ++slot) {
                            auto& player = sys->players[std::size_t(slot)];
                            if (!player.joined || !player.stats.ready) continue;
                            ImGui::PushID(slot);
                            ImGui::SeparatorText(("Player " + std::to_string(slot + 1)).c_str());
                            ImGui::Text("Level %d  HP %d/%d  MP %d/%d  Bits %d", player.stats.level, player.hp, player.max_hp,
                                        player.mp, player.max_mp, player.bits);
                            if (ImGui::Button("Refill HP/MP")) sys->cheat_refill(engine, slot);
                            ImGui::SameLine();
                            if (ImGui::Button("+1 level")) sys->cheat_levels(engine, slot, 1);
                            ImGui::SameLine();
                            if (ImGui::Button("+10 levels")) sys->cheat_levels(engine, slot, 10);
                            ImGui::SameLine();
                            if (ImGui::Button("+100,000 bits")) sys->cheat_bits(slot, 100000);
                            ImGui::PopID();
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Debug")) {
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
                ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
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
            {
                const auto now = std::chrono::steady_clock::now();
                const double seconds = std::chrono::duration<double>(now - last_present).count();
                last_present = now;
                present_interval += (std::min(seconds, 0.1) - present_interval) * 0.1;
            }
            // Frame limit (settings.ini fps_limit): presents are spaced out;
            // the simulation keeps its fixed 60 Hz steps.
            if (!options.smoke_frames && settings.fps_limit > 0) {
                const auto interval = std::chrono::nanoseconds(1'000'000'000LL / settings.fps_limit);
                next_present += interval;
                const auto now = std::chrono::steady_clock::now();
                if (next_present > now)
                    SDL_DelayPrecise(Uint64(std::chrono::duration_cast<std::chrono::nanoseconds>(next_present - now).count()));
                else if (now - next_present > interval * 4)
                    next_present = now;
            }
            if (options.smoke_frames && frame >= options.smoke_frames) running = false;
        }
        if (settings_changed && !options.smoke_frames) settings.save();
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
        auto settings = dw4::game::Settings::load();
        if (options.data.empty()) options.data = settings.data;
        // First start: pick and check the disc image or extraction, then
        // remember it.
        if (options.data.empty() && !options.smoke_frames) {
            for (;;) {
                const auto chosen = choose_game_data();
                if (chosen.empty()) return 1;
                const auto problem = check_game_data(chosen);
                if (problem.empty()) {
                    settings.data = chosen;
                    settings.save();
                    options.data = chosen;
                    break;
                }
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 4 PC", problem.c_str(), nullptr);
            }
        }
        if (options.data.empty())
            throw std::runtime_error("no --data given and " + settings.file.string() + " has no [data] path");
        if (const auto problem = check_game_data(options.data); !problem.empty()) throw std::runtime_error(problem);
        return run(options, settings);
    } catch (const std::exception& error) {
        std::cerr << "dw4_game: " << error.what() << '\n';
        // Without a console the reason shows in a message box.
        if (!std::getenv("DW4_NO_ERROR_BOX"))
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 4 PC", error.what(), nullptr);
        return 1;
    }
}
