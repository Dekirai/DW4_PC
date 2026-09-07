#include "dw4/assets.hpp"
#include "dw4/igb.hpp"
#include "dw4/settings.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
const ImVec4 muted{0.55f, 0.61f, 0.70f, 1.0f};
const ImVec4 teal{0.30f, 0.90f, 0.79f, 1.0f};
const ImVec4 amber{0.98f, 0.73f, 0.37f, 1.0f};
const ImVec4 soft_blue{0.40f, 0.69f, 1.0f, 1.0f};

struct Options {
    std::filesystem::path data;
    std::filesystem::path config;
    std::filesystem::path screenshot;
    int smoke_frames = 0;
    int initial_page = 0;
    bool help = false;
};

std::vector<std::string> arguments(int argc, char** argv) {
    std::vector<std::string> result;
#ifdef _WIN32
    int count = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) throw std::runtime_error("Cannot read command-line arguments.");
    for (int i = 1; i < count; ++i) result.push_back(dw4::path_utf8(std::filesystem::path(wide[i])));
    LocalFree(wide);
    (void)argc;
    (void)argv;
#else
    for (int i = 1; i < argc; ++i) result.emplace_back(argv[i]);
#endif
    return result;
}

Options parse_options(const std::vector<std::string>& args) {
    Options out;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") { out.help = true; continue; }
        if (args[i] != "--data" && args[i] != "--config" && args[i] != "--smoke-test" && args[i] != "--screenshot" && args[i] != "--page")
            throw std::runtime_error("Unknown option: " + args[i]);
        const auto option = args[i];
        if (++i == args.size()) throw std::runtime_error("Missing value for " + option);
        if (option == "--data") out.data = dw4::utf8_path(args[i]);
        else if (option == "--config") out.config = dw4::utf8_path(args[i]);
        else if (option == "--screenshot") out.screenshot = dw4::utf8_path(args[i]);
        else if (option == "--page") {
            const std::array<std::string,5> pages{"overview", "graphics", "controls", "assets", "roadmap"};
            const auto found = std::find(pages.begin(), pages.end(), args[i]);
            if (found == pages.end()) throw std::runtime_error("--page requires overview, graphics, controls, assets or roadmap.");
            out.initial_page = static_cast<int>(found - pages.begin());
        }
        else {
            auto parsed = std::from_chars(args[i].data(), args[i].data() + args[i].size(), out.smoke_frames);
            if (parsed.ec != std::errc{} || parsed.ptr != args[i].data() + args[i].size() ||
                out.smoke_frames < 1 || out.smoke_frames > 10000)
                throw std::runtime_error("--smoke-test requires 1 to 10000 frames.");
        }
    }
    if (!out.screenshot.empty() && !out.smoke_frames)
        throw std::runtime_error("--screenshot requires --smoke-test FRAMES.");
    return out;
}

std::string byte_size(std::uint64_t bytes) {
    std::ostringstream text;
    if (bytes >= 1024 * 1024) text << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / (1024 * 1024) << " MiB";
    else if (bytes >= 1024) text << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / 1024 << " KiB";
    else text << bytes << " B";
    return text.str();
}

std::string filename(const std::filesystem::path& path) { return dw4::path_utf8(path.filename()); }

void paragraph(const char* text, const ImVec4& color = muted) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void title(const char* text, const char* description) {
    ImGui::SetWindowFontScale(1.55f);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Spacing();
    paragraph(description);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
}

void label_value(const char* label, const std::string& value, const ImVec4& color = ImVec4(0.90f,0.93f,0.97f,1)) {
    ImGui::TextColored(muted, "%s", label);
    ImGui::SameLine(225.0f);
    ImGui::TextColored(color, "%s", value.c_str());
}

void configure_style() {
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 0;
    s.ChildRounding = 10;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 8;
    s.GrabRounding = 5;
    s.WindowPadding = ImVec2(22, 20);
    s.FramePadding = ImVec2(12, 9);
    s.ItemSpacing = ImVec2(14, 12);
    s.CellPadding = ImVec2(10, 9);
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.FrameBorderSize = 0;
    auto* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(0.90f,0.93f,0.97f,1);
    c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_WindowBg] = ImVec4(0.055f,0.071f,0.102f,1);
    c[ImGuiCol_ChildBg] = ImVec4(0.080f,0.098f,0.133f,1);
    c[ImGuiCol_PopupBg] = ImVec4(0.090f,0.112f,0.15f,1);
    c[ImGuiCol_Border] = ImVec4(0.16f,0.20f,0.27f,1);
    c[ImGuiCol_FrameBg] = ImVec4(0.125f,0.153f,0.20f,1);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.16f,0.23f,0.28f,1);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.18f,0.28f,0.32f,1);
    c[ImGuiCol_Button] = ImVec4(0.13f,0.24f,0.27f,1);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.16f,0.36f,0.37f,1);
    c[ImGuiCol_ButtonActive] = ImVec4(0.15f,0.43f,0.40f,1);
    c[ImGuiCol_Header] = ImVec4(0.12f,0.30f,0.31f,1);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.14f,0.29f,0.33f,1);
    c[ImGuiCol_HeaderActive] = ImVec4(0.16f,0.37f,0.37f,1);
    c[ImGuiCol_CheckMark] = teal;
    c[ImGuiCol_SliderGrab] = teal;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.4f,1,0.89f,1);
    c[ImGuiCol_Separator] = ImVec4(0.16f,0.20f,0.27f,1);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.105f,0.14f,0.185f,1);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1,1,1,0.018f);
    c[ImGuiCol_NavHighlight] = teal;
}

class Workbench {
public:
    explicit Workbench(Options options) : options_(std::move(options)) {}
    ~Workbench() {
        for (auto* pad : gamepads_) SDL_CloseGamepad(pad);
        if (banner_texture_) glDeleteTextures(1, &banner_texture_);
        if (imgui_gl_ready_) ImGui_ImplOpenGL3_Shutdown();
        if (imgui_sdl_ready_) ImGui_ImplSDL3_Shutdown();
        if (imgui_ready_) ImGui::DestroyContext();
        if (context_) SDL_GL_DestroyContext(context_);
        if (window_) SDL_DestroyWindow(window_);
        if (sdl_ready_) SDL_Quit();
    }

    int run() {
        initialize();
        bool running = true;
        int frames = 0;
        while (running) {
            const auto frame_start = std::chrono::steady_clock::now();
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                ImGui_ImplSDL3_ProcessEvent(&event);
                if (event.type == SDL_EVENT_QUIT ||
                    (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window_)))
                    running = false;
                if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED)
                    refresh_gamepads();
            }
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            draw();
            ImGui::Render();
            int width = 0, height = 0;
            SDL_GetWindowSizeInPixels(window_, &width, &height);
            glViewport(0, 0, width, height);
            glClearColor(0.055f, 0.071f, 0.102f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (!options_.screenshot.empty() && frames + 1 == options_.smoke_frames)
                capture_frame(width, height);
            if (!SDL_GL_SwapWindow(window_)) throw std::runtime_error(SDL_GetError());
            ++frames;
            if (options_.smoke_frames && frames >= options_.smoke_frames) running = false;
            if (!options_.smoke_frames && settings_.fps_limit > 0) {
                const auto target = frame_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(1.0 / settings_.fps_limit));
                std::this_thread::sleep_until(target);
            }
        }
        if (options_.smoke_frames) {
            std::cout << "Native workbench smoke test: " << frames << " frames rendered\n"
                      << "GL renderer: " << renderer_ << "\nMSAA samples: " << actual_msaa_ << '\n';
            if (disc_) std::cout << "Disc: " << disc_->header.game_id << ", " << disc_->archives.size() << " AFS archives indexed\n";
            if (!options_.data.empty() && !disc_) {
                std::cerr << "Data inspection failed: " << asset_error_ << '\n';
                return 2;
            }
        }
        return 0;
    }

private:
    Options options_;
    dw4::Settings settings_;
    std::filesystem::path config_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    bool sdl_ready_ = false, imgui_ready_ = false, imgui_sdl_ready_ = false, imgui_gl_ready_ = false;
    int page_ = 0;
    int actual_msaa_ = 0;
    int startup_msaa_ = 0;
    std::string renderer_;
    std::string notice_;
    bool notice_error_ = false;
    std::vector<std::string> startup_warnings_;
    std::array<char, 4096> data_edit_{};
    std::array<char, 128> archive_filter_{};
    std::optional<dw4::assets::DiscInspection> disc_;
    std::optional<dw4::assets::AfsArchive> archive_;
    std::optional<dw4::IgbMetadata> igb_;
    std::string igb_source_;
    std::string asset_error_;
    int selected_archive_ = -1;
    int selected_entry_ = -1;
    std::vector<std::uint8_t> entry_preview_;
    GLuint banner_texture_ = 0;
    std::vector<SDL_Gamepad*> gamepads_;

    void capture_frame(int width, int height) {
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
            throw std::runtime_error("Invalid screenshot dimensions.");
        const auto stride = static_cast<std::size_t>(width) * 4;
        std::vector<std::uint8_t> pixels(stride * static_cast<std::size_t>(height));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // OpenGL starts at the lower-left; BMP surfaces use top-to-bottom rows.
        for (int y = 0; y < height / 2; ++y)
            std::swap_ranges(pixels.begin() + static_cast<std::size_t>(y) * stride,
                pixels.begin() + static_cast<std::size_t>(y + 1) * stride,
                pixels.begin() + static_cast<std::size_t>(height - 1 - y) * stride);
        if (!options_.screenshot.parent_path().empty())
            std::filesystem::create_directories(options_.screenshot.parent_path());
        auto* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, pixels.data(), width * 4);
        if (!surface) throw std::runtime_error(SDL_GetError());
        const bool saved = SDL_SaveBMP(surface, dw4::path_utf8(options_.screenshot).c_str());
        SDL_DestroySurface(surface);
        if (!saved) throw std::runtime_error("Screenshot could not be saved: " + std::string(SDL_GetError()));
        std::cout << "Screenshot: " << dw4::path_utf8(options_.screenshot) << '\n';
    }

    void initialize() {
        page_ = options_.initial_page;
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
        sdl_ready_ = true;
        if (!options_.config.empty()) config_ = options_.config;
        else {
            char* pref = SDL_GetPrefPath("DW4-PC", "DW4_PC");
            if (!pref) throw std::runtime_error("Cannot locate settings directory: " + std::string(SDL_GetError()));
            config_ = dw4::utf8_path(pref) / "settings.ini";
            SDL_free(pref);
        }
        auto loaded = dw4::load_settings(config_);
        settings_ = std::move(loaded.value);
        startup_warnings_ = std::move(loaded.warnings);
        if (!options_.data.empty()) settings_.data_path = options_.data;
        const auto initial_path = dw4::path_utf8(settings_.data_path);
        std::snprintf(data_edit_.data(), data_edit_.size(), "%s", initial_path.c_str());
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
        startup_msaa_ = settings_.msaa;
        create_window(settings_.msaa);
        if (!context_ && settings_.msaa) {
            startup_warnings_.emplace_back("Requested MSAA unavailable; native window started without MSAA.");
            create_window(0);
        }
        if (!context_) throw std::runtime_error("OpenGL 3.3 window creation failed: " + std::string(SDL_GetError()));
        if (!SDL_GL_MakeCurrent(window_, context_)) throw std::runtime_error(SDL_GetError());
        int sample_buffers = 0;
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &sample_buffers);
        if (sample_buffers) SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &actual_msaa_);
        if (!SDL_GL_SetSwapInterval(settings_.vsync ? 1 : 0))
            startup_warnings_.emplace_back("VSync request failed: " + std::string(SDL_GetError()));
        renderer_ = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        if (settings_.borderless && !options_.smoke_frames) {
            if (!SDL_SetWindowFullscreen(window_, true)) startup_warnings_.emplace_back(SDL_GetError());
        }
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imgui_ready_ = true;
        auto& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.IniFilename = nullptr;
#ifdef _WIN32
        wchar_t windows_dir[MAX_PATH]{};
        if (GetWindowsDirectoryW(windows_dir, MAX_PATH)) {
            const auto font_path = std::filesystem::path(windows_dir) / "Fonts" / "segoeui.ttf";
            if (std::filesystem::exists(font_path))
                io.Fonts->AddFontFromFileTTF(dw4::path_utf8(font_path).c_str(), 19.0f);
        }
#endif
        if (io.Fonts->Fonts.empty()) io.Fonts->AddFontDefault();
        configure_style();
        if (!ImGui_ImplSDL3_InitForOpenGL(window_, context_)) throw std::runtime_error("ImGui SDL initialization failed.");
        imgui_sdl_ready_ = true;
        if (!ImGui_ImplOpenGL3_Init("#version 330 core")) throw std::runtime_error("ImGui OpenGL initialization failed.");
        imgui_gl_ready_ = true;
        refresh_gamepads();
        if (!settings_.data_path.empty()) mount_data();
    }

    void create_window(int msaa) {
        if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa);
        auto flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        if (options_.smoke_frames) flags |= SDL_WINDOW_HIDDEN;
        window_ = SDL_CreateWindow("Digimon World 4 PC - Development Workbench", settings_.width, settings_.height, flags);
        if (!window_) return;
        SDL_SetWindowMinimumSize(window_, 1024, 640);
        context_ = SDL_GL_CreateContext(window_);
    }

    void save() {
        try {
            dw4::save_settings(config_, settings_);
            notice_ = "Settings saved.";
            notice_error_ = false;
        } catch (const std::exception& error) { notice_ = error.what(); notice_error_ = true; }
    }

    void mount_data() {
        disc_.reset();
        archive_.reset();
        igb_.reset();
        igb_source_.clear();
        selected_archive_ = selected_entry_ = -1;
        entry_preview_.clear();
        asset_error_.clear();
        if (banner_texture_) { glDeleteTextures(1, &banner_texture_); banner_texture_ = 0; }
        try {
            settings_.data_path = dw4::utf8_path(data_edit_.data());
            if (settings_.data_path.empty()) throw std::runtime_error("Choose the extracted game directory first.");
            disc_ = dw4::assets::inspect_extracted(settings_.data_path);
            for (const auto& candidate : {disc_->root / "files" / "ndmw.igb", disc_->root / "ndmw.igb"}) {
                if (std::filesystem::exists(candidate)) {
                    try { igb_ = dw4::read_igb_metadata(candidate); igb_source_ = filename(candidate); }
                    catch (const std::exception& error) { disc_->warnings.emplace_back(error.what()); }
                    break;
                }
            }
            if (!disc_->banner_path.empty()) {
                try {
                    const auto banner = dw4::assets::decode_banner(disc_->banner_path);
                    glGenTextures(1, &banner_texture_);
                    glBindTexture(GL_TEXTURE_2D, banner_texture_);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(banner.width),
                        static_cast<GLsizei>(banner.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, banner.rgba.data());
                } catch (const std::exception& error) { disc_->warnings.emplace_back(error.what()); }
            }
            notice_ = "Original game data inspected successfully.";
            notice_error_ = false;
            if (!disc_->archives.empty()) select_archive(0);
        } catch (const std::exception& error) {
            asset_error_ = error.what();
            notice_ = asset_error_;
            notice_error_ = true;
        }
    }

    void select_archive(int index) {
        archive_.reset();
        selected_archive_ = index;
        selected_entry_ = -1;
        entry_preview_.clear();
        asset_error_.clear();
        try { archive_ = dw4::assets::AfsArchive::open(disc_->archives.at(static_cast<std::size_t>(index)).path); }
        catch (const std::exception& error) { asset_error_ = error.what(); }
    }

    void refresh_gamepads() {
        for (auto* pad : gamepads_) SDL_CloseGamepad(pad);
        gamepads_.clear();
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count; ++i) if (auto* pad = SDL_OpenGamepad(ids[i])) gamepads_.push_back(pad);
        SDL_free(ids);
    }

    void draw() {
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("DW4 PC", nullptr, flags);
        ImGui::BeginChild("navigation", ImVec2(214, 0), false);
        ImGui::TextColored(teal, "DW4 / PC");
        ImGui::Spacing();
        ImGui::SetWindowFontScale(1.35f);
        ImGui::TextUnformatted("Digimon\nWorld 4");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Spacing();
        paragraph("NATIVE PORT PROJECT");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        const std::array<const char*, 5> pages{"Overview", "Graphics", "Controls", "Original data", "Roadmap"};
        for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(pages[static_cast<std::size_t>(i)], page_ == i, 0, ImVec2(0, 37))) page_ = i;
            ImGui::PopID();
        }
        const float bottom = ImGui::GetWindowHeight() - 146;
        if (ImGui::GetCursorPosY() < bottom) ImGui::SetCursorPosY(bottom);
        ImGui::Separator();
        ImGui::TextColored(amber, "MILESTONE 01");
        paragraph("Development workbench");
        paragraph("Gameplay not ported yet.");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("content", ImVec2(0, 0), false);
        if (page_ == 0) draw_overview();
        if (page_ == 1) draw_graphics();
        if (page_ == 2) draw_controls();
        if (page_ == 3) draw_assets();
        if (page_ == 4) draw_roadmap();
        if (!notice_.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            paragraph(notice_.c_str(), notice_error_ ? amber : teal);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void draw_overview() {
        title("A native foundation.", "Digimon World 4 PC  /  Windows x64  /  Development workbench");
        ImGui::BeginChild("status", ImVec2(0, 155), true);
        ImGui::TextColored(amber, "GAMEPLAY NOT PORTED YET");
        paragraph("This executable renders its own native interface and reads your original GameCube data. The game engine, combat, maps and playable characters still need to be ported.", ImVec4(0.86f,0.90f,0.96f,1));
        ImGui::Spacing();
        ImGui::TextColored(teal, "Native window");
        ImGui::SameLine();
        ImGui::TextColored(teal, "  Archive inspector");
        ImGui::SameLine();
        ImGui::TextColored(teal, "  Input diagnostics");
        ImGui::EndChild();
        ImGui::Spacing();
        ImGui::TextUnformatted("Your game data");
        ImGui::Spacing();
        if (disc_) {
            if (banner_texture_) {
                ImGui::Image(static_cast<ImTextureID>(banner_texture_), ImVec2(288,96));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextUnformatted(disc_->header.title.c_str());
                ImGui::TextColored(muted, "%s  /  revision %u", disc_->header.game_id.c_str(), disc_->header.revision);
                ImGui::TextColored(disc_->header.is_dw4_usa() ? teal : amber,
                    "%s", disc_->header.is_dw4_usa() ? "USA game data recognized" : "Unexpected game or revision");
                ImGui::EndGroup();
            }
            label_value("Indexed archives", std::to_string(disc_->archives.size()));
            label_value("PowerPC executable", byte_size(disc_->dol.file_size) + " / inspected only");
            if (ImGui::Button("Explore original data", ImVec2(220, 42))) page_ = 3;
        } else {
            paragraph("Connect your extracted USA copy to inspect its executable, archives and original banner.");
            if (ImGui::Button("Connect game directory", ImVec2(250, 42))) page_ = 3;
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        label_value("Native renderer", renderer_);
        label_value("Workbench frame rate", std::to_string(static_cast<int>(ImGui::GetIO().Framerate + 0.5f)) + " FPS");
        label_value("Game runtime", "Not implemented", amber);
        for (const auto& warning : startup_warnings_) paragraph(warning.c_str(), amber);
    }

    void apply_window() {
        bool success = true;
        std::string failure;
        auto check = [&](bool ok) { if (!ok) { success = false; failure += std::string(SDL_GetError()) + " "; } };
        if (settings_.borderless) {
            check(SDL_SetWindowFullscreenMode(window_, nullptr));
            check(SDL_SetWindowFullscreen(window_, true));
        } else {
            check(SDL_SetWindowFullscreen(window_, false));
            check(SDL_SetWindowSize(window_, settings_.width, settings_.height));
            check(SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED));
        }
        check(SDL_GL_SetSwapInterval(settings_.vsync ? 1 : 0));
        if (success) save();
        else { notice_ = "Window setting could not be applied: " + failure; notice_error_ = true; }
    }

    void draw_graphics() {
        title("Graphics", "These settings control this native workbench window. A game renderer is not implemented yet.");
        ImGui::TextColored(teal, "ACTIVE WINDOW SETTINGS");
        const std::array<std::pair<int,int>,4> sizes{{{1280,720},{1920,1080},{2560,1440},{3840,2160}}};
        int resolution = 0;
        for (int i = 0; i < 4; ++i) if (settings_.width == sizes[i].first && settings_.height == sizes[i].second) resolution = i;
        ImGui::SetNextItemWidth(300);
        if (ImGui::Combo("Resolution", &resolution, "1280 x 720\0" "1920 x 1080\0" "2560 x 1440\0" "3840 x 2160\0")) {
            settings_.width = sizes[resolution].first;
            settings_.height = sizes[resolution].second;
        }
        int mode = settings_.borderless ? 1 : 0;
        ImGui::SetNextItemWidth(300);
        if (ImGui::Combo("Display mode", &mode, "Windowed\0Borderless desktop\0")) settings_.borderless = mode == 1;
        paragraph("Borderless uses the desktop's current resolution. Resolution above sets the windowed size.");
        const std::array<int,6> limits{30,60,120,144,240,0};
        int fps = 3;
        for (int i = 0; i < 6; ++i) if (settings_.fps_limit == limits[i]) fps = i;
        ImGui::SetNextItemWidth(300);
        if (ImGui::Combo("FPS limit", &fps, "30\0" "60\0" "120\0" "144\0" "240\0" "Unlimited\0")) settings_.fps_limit = limits[fps];
        ImGui::Checkbox("VSync", &settings_.vsync);
        const std::array<int,4> samples{0,2,4,8};
        int msaa = 0;
        for (int i = 0; i < 4; ++i) if (settings_.msaa == samples[i]) msaa = i;
        ImGui::SetNextItemWidth(300);
        if (ImGui::Combo("MSAA (restart required)", &msaa, "Off\0" "2x\0" "4x\0" "8x\0")) settings_.msaa = samples[msaa];
        if (settings_.msaa != startup_msaa_) paragraph("Save and restart the workbench to request the new MSAA level.", amber);
        int actual_width = 0, actual_height = 0, actual_vsync = 0;
        SDL_GetWindowSizeInPixels(window_, &actual_width, &actual_height);
        const bool queried_vsync = SDL_GL_GetSwapInterval(&actual_vsync);
        ImGui::TextColored(muted, "Current framebuffer: %d x %d  |  MSAA: %dx  |  VSync: %s", actual_width, actual_height,
            actual_msaa_, queried_vsync ? (actual_vsync ? "On" : "Off") : "Unknown");
        if (ImGui::Button("Apply and save", ImVec2(200, 42))) apply_window();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(amber, "GAME PREFERENCES / PLANNED");
        int aspect = settings_.aspect == "16:9" ? 1 : 0;
        ImGui::SetNextItemWidth(300);
        if (ImGui::Combo("Game aspect ratio", &aspect, "Original (4:3)\0" "16:9 widescreen\0")) settings_.aspect = aspect ? "16:9" : "original";
        int gameplay = settings_.gameplay == "modernized" ? 1 : 0;
        ImGui::SetNextItemWidth(300);
        if (ImGui::Combo("Gameplay", &gameplay, "Original\0Modernized\0")) settings_.gameplay = gameplay ? "modernized" : "original";
        paragraph("Saved preferences only. Widescreen cameras, high frame rate presentation and modernized gameplay require game-engine work.");
        if (ImGui::Button("Save preferences")) save();
    }

    void draw_stick(const char* label, float x, float y) {
        ImGui::BeginGroup();
        ImGui::TextColored(muted, "%s", label);
        auto pos = ImGui::GetCursorScreenPos();
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 center(pos.x + 42, pos.y + 42);
        draw->AddCircleFilled(center, 40, IM_COL32(24,36,49,255), 40);
        draw->AddCircle(center, 40, IM_COL32(66,88,107,255), 40);
        draw->AddLine(ImVec2(center.x-40,center.y), ImVec2(center.x+40,center.y), IM_COL32(50,69,87,255));
        draw->AddLine(ImVec2(center.x,center.y-40), ImVec2(center.x,center.y+40), IM_COL32(50,69,87,255));
        draw->AddCircleFilled(ImVec2(center.x+x*34,center.y+y*34), 6, IM_COL32(77,230,201,255));
        ImGui::Dummy(ImVec2(110,90));
        ImGui::TextColored(muted, "%+.2f  %+.2f", x, y);
        ImGui::EndGroup();
    }

    void draw_controls() {
        title("Controls", "Inspect mapped controllers live. Workbench keyboard and mouse navigation is available now.");
        ImGui::TextColored(teal, "LIVE SDL GAMEPAD INPUT");
        if (ImGui::Button("Refresh controllers")) refresh_gamepads();
        if (gamepads_.empty()) {
            ImGui::BeginChild("no_controllers", ImVec2(0, 108), true);
            ImGui::TextUnformatted("No mapped gamepad connected");
            paragraph("Connect a controller supported by SDL. GameCube USB adapters need a compatible SDL mapping or a future dedicated adapter backend.");
            ImGui::EndChild();
        }
        for (std::size_t i = 0; i < gamepads_.size(); ++i) {
            auto* pad = gamepads_[i];
            ImGui::PushID(static_cast<int>(i));
            const char* name = SDL_GetGamepadName(pad);
            if (ImGui::CollapsingHeader(name ? name : "Mapped gamepad", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto type = SDL_GetGamepadType(pad);
                const char* family = "Generic SDL mapping";
                if (type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE) family = "Xbox mapping";
                if (type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5) family = "PlayStation mapping";
                ImGui::TextColored(teal, "%s  /  connected", family);
                auto axis = [pad](SDL_GamepadAxis a) { return std::clamp(SDL_GetGamepadAxis(pad, a) / 32767.0f, -1.0f, 1.0f); };
                draw_stick("Left stick", axis(SDL_GAMEPAD_AXIS_LEFTX), axis(SDL_GAMEPAD_AXIS_LEFTY));
                ImGui::SameLine();
                draw_stick("Right stick", axis(SDL_GAMEPAD_AXIS_RIGHTX), axis(SDL_GAMEPAD_AXIS_RIGHTY));
                ImGui::SameLine();
                ImGui::BeginGroup();
                ImGui::TextColored(muted, "Triggers");
                ImGui::ProgressBar(std::max(0.0f,axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER)), ImVec2(170,22), "Left");
                ImGui::ProgressBar(std::max(0.0f,axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)), ImVec2(170,22), "Right");
                ImGui::EndGroup();
                std::string pressed;
                for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
                    if (SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b))) {
                        const auto* label = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(b));
                        if (!pressed.empty()) pressed += ", ";
                        pressed += label ? label : "button";
                    }
                }
                ImGui::TextWrapped("Pressed: %s", pressed.empty() ? "none" : pressed.c_str());
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(amber, "GAME INPUT PREFERENCE / PLANNED");
        const std::array<std::string,4> modes{"keyboard_mouse","xbox","playstation","gamecube"};
        int mode = 0;
        for (int i = 0; i < 4; ++i) if (settings_.controls == modes[i]) mode = i;
        ImGui::SetNextItemWidth(320);
        if (ImGui::Combo("Preferred input", &mode, "Keyboard / Mouse\0Xbox Controller\0PlayStation Controller\0GameCube Controller\0")) settings_.controls = modes[mode];
        paragraph("This records your preference. Gameplay bindings, mouse camera, rebinding and four-player input routing are not implemented.");
        if (ImGui::Button("Save preference")) save();
    }

    void draw_assets() {
        title("Original data", "Read-only inspection of your extracted GameCube copy. Original assets remain in your game directory.");
        ImGui::SetNextItemWidth(std::max(200.0f, ImGui::GetContentRegionAvail().x - 194.0f));
        ImGui::InputTextWithHint("##data_directory", "Path to extracted game directory", data_edit_.data(), data_edit_.size());
        ImGui::SameLine();
        if (ImGui::Button("Inspect directory", ImVec2(175, 0))) mount_data();
        if (!asset_error_.empty()) paragraph(asset_error_.c_str(), amber);
        if (!disc_) {
            paragraph("Supported layout: sys/boot.bin, sys/main.dol and files/. Flat extracted directories are also supported. Use an extracted directory, not the ISO file.");
            return;
        }
        ImGui::TextColored(teal, "%s  |  %s  |  %zu archives", disc_->header.title.c_str(), disc_->header.game_id.c_str(), disc_->archives.size());
        if (ImGui::Button("Remember directory")) save();
        ImGui::SameLine();
        paragraph("Stored in your local settings.");
        if (ImGui::CollapsingHeader("Executable layout / main.dol")) {
            ImGui::TextColored(muted, "Entry point 0x%08X  |  BSS 0x%08X + %s", disc_->dol.entry_point,
                disc_->dol.bss_address, byte_size(disc_->dol.bss_size).c_str());
            paragraph("PowerPC sections are inspected as data. This workbench does not execute the DOL.");
            if (ImGui::BeginTable("sections", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("Section"); ImGui::TableSetupColumn("File offset");
                ImGui::TableSetupColumn("GC address"); ImGui::TableSetupColumn("Size"); ImGui::TableHeadersRow();
                for (const auto& section : disc_->dol.sections) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%s %zu", section.executable ? "Text" : "Data", section.index);
                    ImGui::TableNextColumn(); ImGui::Text("0x%08X", section.file_offset);
                    ImGui::TableNextColumn(); ImGui::Text("0x%08X", section.address);
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(byte_size(section.size).c_str());
                }
                ImGui::EndTable();
            }
        }
        for (const auto& warning : disc_->warnings) paragraph(warning.c_str(), amber);
        if (igb_ && ImGui::CollapsingHeader("Alchemy IGB / type metadata")) {
            ImGui::Text("%s / IGB v%u / %s endian", igb_source_.c_str(), igb_->header.version,
                igb_->header.byte_order == dw4::IgbByteOrder::little ? "little" : "big");
            ImGui::Text("%zu field types / %zu object types / %u declared object instances",
                igb_->field_types.size(), igb_->object_types.size(), igb_->header.object_count);
            paragraph("Only the header and type metadata are decoded. Meshes, textures, object instances and scene rendering are not implemented.");
            if (ImGui::BeginTable("igb_types", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 180))) {
                ImGui::TableSetupColumn("Object type"); ImGui::TableSetupColumn("Parent");
                ImGui::TableSetupColumn("Fields"); ImGui::TableHeadersRow();
                for (const auto& type : igb_->object_types) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(type.name.c_str());
                    ImGui::TableNextColumn();
                    if (type.parent_index >= 0 && static_cast<std::size_t>(type.parent_index) < igb_->object_types.size())
                        ImGui::TextUnformatted(igb_->object_types[static_cast<std::size_t>(type.parent_index)].name.c_str());
                    else ImGui::TextUnformatted("-");
                    ImGui::TableNextColumn(); ImGui::Text("%zu", type.fields.size());
                }
                ImGui::EndTable();
            }
        }
        ImGui::Spacing();
        ImGui::TextUnformatted("AFS archive browser");
        ImGui::SetNextItemWidth(300);
        const auto archive_label = selected_archive_ >= 0 ? filename(disc_->archives[static_cast<std::size_t>(selected_archive_)].path) : "Choose archive";
        if (ImGui::BeginCombo("Archive", archive_label.c_str())) {
            for (std::size_t i = 0; i < disc_->archives.size(); ++i) {
                auto name = filename(disc_->archives[i].path);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(name.c_str(), selected_archive_ == static_cast<int>(i))) select_archive(static_cast<int>(i));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        if (!archive_) return;
        ImGui::SetNextItemWidth(300);
        ImGui::InputTextWithHint("##archive_filter", "Filter filename or entry index", archive_filter_.data(), archive_filter_.size());
        ImGui::SameLine();
        ImGui::TextColored(muted, "%zu entries / %s", archive_->entries().size(), byte_size(archive_->file_size()).c_str());
        const float table_height = std::clamp(ImGui::GetContentRegionAvail().y - 190.0f, 150.0f, 330.0f);
        if (ImGui::BeginTable("archive_entries", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable, ImVec2(0, table_height))) {
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 70);
            ImGui::TableSetupColumn("Filename", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 130);
            ImGui::TableSetupScrollFreeze(0,1);
            ImGui::TableHeadersRow();
            std::vector<std::size_t> visible;
            visible.reserve(archive_->entries().size());
            for (std::size_t i = 0; i < archive_->entries().size(); ++i) {
                const auto& entry = archive_->entries()[i];
                if (!archive_filter_[0] || entry.name.find(archive_filter_.data()) != std::string::npos ||
                    std::to_string(i).find(archive_filter_.data()) != std::string::npos) visible.push_back(i);
            }
            ImGuiListClipper clip;
            clip.Begin(static_cast<int>(visible.size()));
            while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                const auto i = visible[static_cast<std::size_t>(row)];
                const auto& entry = archive_->entries()[i];
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(std::to_string(i).c_str(), selected_entry_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_entry_ = static_cast<int>(i);
                    try { entry_preview_ = archive_->read_prefix(i, 128); asset_error_.clear(); }
                    catch (const std::exception& error) { entry_preview_.clear(); asset_error_ = error.what(); }
                }
                ImGui::PopID();
                ImGui::TableNextColumn(); ImGui::TextUnformatted(entry.name.empty() ? "(unnamed)" : entry.name.c_str());
                ImGui::TableNextColumn(); ImGui::Text("0x%08X", entry.offset);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(byte_size(entry.size).c_str());
            }
            ImGui::EndTable();
        }
        if (selected_entry_ >= 0) {
            ImGui::TextColored(muted, "Entry %d / first %zu bytes", selected_entry_, entry_preview_.size());
            const auto& selected = archive_->entries()[static_cast<std::size_t>(selected_entry_)];
            const bool igb_cookie = entry_preview_.size() >= 44 &&
                ((entry_preview_[40] == 0xDA && entry_preview_[41] == 0xFA && entry_preview_[42] == 0 && entry_preview_[43] == 0) ||
                 (entry_preview_[40] == 0 && entry_preview_[41] == 0 && entry_preview_[42] == 0xFA && entry_preview_[43] == 0xDA));
            if (igb_cookie || selected.name.ends_with(".igb") || selected.name.ends_with(".IGB")) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Read IGB metadata")) {
                    try {
                        const auto prefix = archive_->read_prefix(static_cast<std::size_t>(selected_entry_), 16u * 1024u * 1024u + 64u);
                        igb_ = dw4::read_igb_metadata(prefix);
                        igb_source_ = filename(archive_->path()) + " [" +
                            std::to_string(selected_entry_) + "] / " +
                            (selected.name.empty() ? "unnamed" : selected.name);
                        notice_ = "IGB metadata decoded; open the Alchemy IGB section above.";
                        notice_error_ = false;
                    } catch (const std::exception& error) { notice_ = error.what(); notice_error_ = true; }
                }
            }
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.045f,0.063f,0.092f,1));
            ImGui::BeginChild("hex_preview", ImVec2(0, 152), true);
            for (std::size_t pos = 0; pos < entry_preview_.size(); pos += 16) {
                std::ostringstream row;
                row << std::hex << std::setfill('0') << std::setw(4) << pos << "  ";
                std::string ascii;
                for (std::size_t j = 0; j < 16; ++j) {
                    if (pos+j < entry_preview_.size()) {
                        const auto byte = entry_preview_[pos+j];
                        row << std::setw(2) << static_cast<unsigned>(byte) << ' ';
                        ascii += (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
                    } else row << "   ";
                }
                row << "  " << ascii;
                ImGui::TextColored(soft_blue, "%s", row.str().c_str());
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else paragraph("Select an entry to inspect its first 128 bytes. Payloads are read only on selection.");
    }

    void draw_roadmap() {
        title("The path to a playable port", "A staged development plan. Features below are not available until their implementation is verified.");
        struct Stage { const char* state; const char* heading; const char* body; bool ready; };
        const std::array<Stage,6> stages{{
            {"AVAILABLE", "01  Native workbench & data inspection", "Windows x64 interface, window settings, persistent preferences, SDL input diagnostics, GameCube header / DOL / AFS / banner inspection.", true},
            {"NEXT", "02  Runtime and first scene", "Recover game behavior, decode Alchemy IGB scenes and textures, implement native rendering and platform services; verify against the original.", false},
            {"PLANNED", "03  Playable original campaign", "Character movement, combat, maps, AI, progression, audio, saves and local four-player co-op with behavior tests.", false},
            {"PLANNED", "04  PC graphics and controls", "Game resolution, borderless mode, widescreen, high frame rate presentation, MSAA, keyboard / mouse and controller bindings.", false},
            {"PLANNED", "05  Online four-player co-op", "Define authority and synchronization, deterministic state where needed, lobby flow, disconnect handling and full-session testing.", false},
            {"PLANNED", "06  Mods and modernization", "HD texture packs, mod loader, custom maps / Digimon / weapons, randomizer, difficulty rebalancing and documented bug fixes.", false}
        }};
        for (const auto& stage : stages) {
            ImGui::PushID(stage.heading);
            ImGui::BeginChild("stage", ImVec2(0, 125), true);
            ImGui::TextColored(stage.ready ? teal : amber, "%s", stage.state);
            ImGui::SameLine(138);
            ImGui::TextUnformatted(stage.heading);
            paragraph(stage.body);
            ImGui::EndChild();
            ImGui::PopID();
        }
    }
};
} // namespace

int main(int argc, char** argv) {
    try {
        auto options = parse_options(arguments(argc, argv));
        if (options.help) {
            std::cout << "Digimon World 4 PC - native development workbench\n"
                         "Gameplay is not ported yet. Original game data is read only.\n\n"
                         "Usage: dw4_pc [--data EXTRACTED_DIRECTORY] [--config SETTINGS_FILE]\n"
                         "              [--smoke-test FRAMES]\n\n"
                         "--data         Inspect this extracted game directory (not an ISO).\n"
                         "--config       Use this INI file; independent of the game directory.\n"
                         "--smoke-test   Render 1-10000 hidden native frames, then exit.\n"
                         "               Does not write settings or launch any emulator.\n"
                         "--screenshot   Save the final smoke-test frame to a BMP path.\n"
                         "--page         Open overview, graphics, controls, assets or roadmap.\n";
            return 0;
        }
        return Workbench(std::move(options)).run();
    } catch (const std::exception& error) {
        std::cerr << "dw4_pc: " << error.what() << '\n';
        return 1;
    }
}
