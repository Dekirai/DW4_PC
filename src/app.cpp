#include "dw4/assets.hpp"
#include "dw4/camera.hpp"
#include "dw4/igb.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/meshes.hpp"
#include "dw4/resources.hpp"
#include "dw4/settings.hpp"
#include "dw4/textures.hpp"
#include "mesh_renderer.hpp"

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
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
    std::string archive_name;
    std::string resource;
    std::optional<int> entry;
    int texture = 0;
    std::optional<int> mesh;
    bool mesh_wireframe = false;
    int max_entry_mib = 64;
    bool help = false;
};

int parse_integer_option(const std::string& value, const std::string& option, int minimum, int maximum) {
    int number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || number < minimum || number > maximum)
        throw std::runtime_error(option + " requires " + std::to_string(minimum) + " to " + std::to_string(maximum) + ".");
    return number;
}

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
    bool texture_requested = false;
    bool mesh_mode_requested = false;
    bool page_requested = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--help" || args[i] == "-h") { out.help = true; continue; }
        if (args[i] != "--data" && args[i] != "--config" && args[i] != "--smoke-test" && args[i] != "--screenshot" && args[i] != "--page" &&
            args[i] != "--archive" && args[i] != "--entry" && args[i] != "--texture" && args[i] != "--max-entry-mib" && args[i] != "--resource" &&
            args[i] != "--mesh" && args[i] != "--mesh-mode")
            throw std::runtime_error("Unknown option: " + args[i]);
        const auto option = args[i];
        if (++i == args.size()) throw std::runtime_error("Missing value for " + option);
        if (option == "--data") out.data = dw4::utf8_path(args[i]);
        else if (option == "--config") out.config = dw4::utf8_path(args[i]);
        else if (option == "--screenshot") out.screenshot = dw4::utf8_path(args[i]);
        else if (option == "--archive") out.archive_name = args[i];
        else if (option == "--resource") out.resource = args[i];
        else if (option == "--entry") out.entry = parse_integer_option(args[i], option, 0, 1000000);
        else if (option == "--texture") { out.texture = parse_integer_option(args[i], option, 0, 1000000); texture_requested = true; }
        else if (option == "--mesh") out.mesh = parse_integer_option(args[i], option, 0, 1000000);
        else if (option == "--mesh-mode") {
            if (args[i] != "solid" && args[i] != "wireframe") throw std::runtime_error("--mesh-mode requires solid or wireframe.");
            out.mesh_wireframe = args[i] == "wireframe";
            mesh_mode_requested = true;
        }
        else if (option == "--max-entry-mib") out.max_entry_mib = parse_integer_option(args[i], option, 1, 256);
        else if (option == "--page") {
            page_requested = true;
            const std::array<std::string,7> pages{"overview", "graphics", "controls", "assets", "textures", "meshes", "roadmap"};
            const auto found = std::find(pages.begin(), pages.end(), args[i]);
            if (found == pages.end()) throw std::runtime_error("--page requires overview, graphics, controls, assets, textures, meshes or roadmap.");
            out.initial_page = static_cast<int>(found - pages.begin());
        }
        else {
            out.smoke_frames = parse_integer_option(args[i], option, 1, 10000);
        }
    }
    if (!out.screenshot.empty() && !out.smoke_frames)
        throw std::runtime_error("--screenshot requires --smoke-test FRAMES.");
    if (out.entry.has_value() != !out.archive_name.empty())
        throw std::runtime_error("Use --archive NAME and --entry INDEX together.");
    if (texture_requested && !out.entry && out.resource.empty())
        throw std::runtime_error("--texture requires --resource, or --archive and --entry.");
    if (!out.resource.empty() && out.entry)
        throw std::runtime_error("Choose --resource or --archive with --entry; these source options cannot be combined.");
    if (out.mesh && !out.entry && out.resource.empty())
        throw std::runtime_error("--mesh requires --resource, or --archive and --entry.");
    if (out.mesh && texture_requested) throw std::runtime_error("Choose --mesh or --texture, not both.");
    if (out.mesh && page_requested && out.initial_page != 5)
        throw std::runtime_error("--mesh requires --page meshes when --page is specified.");
    if (texture_requested && out.initial_page == 5)
        throw std::runtime_error("Use --mesh for the Meshes page.");
    if (!page_requested && (out.entry || !out.resource.empty())) out.initial_page = out.mesh ? 5 : 4;
    if (mesh_mode_requested && out.initial_page != 5)
        throw std::runtime_error("--mesh-mode requires the Meshes page.");
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
        mesh_renderer_.shutdown();
        for (auto* pad : gamepads_) SDL_CloseGamepad(pad);
        if (banner_texture_) glDeleteTextures(1, &banner_texture_);
        if (asset_texture_) glDeleteTextures(1, &asset_texture_);
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
            if ((options_.entry || !options_.resource.empty()) && options_.initial_page == 5) {
                if (!mesh_renderer_.has_mesh() || mesh_render_failed_ || mesh_renderer_.rendered_frames() <= mesh_render_start_frame_) {
                    std::cerr << "Mesh upload/render failed: " << (mesh_error_.empty() ? "No geometry frame rendered." : mesh_error_) << '\n';
                    return 3;
                }
                const auto& mesh = meshes_.at(static_cast<std::size_t>(selected_mesh_));
                std::cout << "Mesh: " << mesh_source_ << " / mesh " << selected_mesh_ << " / reference " << mesh.reference_index
                          << " / " << mesh_data_->positions.size() << " vertices / " << mesh_renderer_.triangle_count() << " triangles\n"
                          << "Mesh render: " << mesh_target_width_ << 'x' << mesh_target_height_ << " / "
                          << (mesh_wireframe_ ? "wireframe" : "solid") << " / " << mesh_renderer_.samples() << " samples\n";
                if (!loaded_mesh_resource_.empty()) std::cout << "Logical resource: " << loaded_mesh_resource_ << '\n';
            } else if (options_.entry || !options_.resource.empty()) {
                if (!asset_texture_) {
                    std::cerr << "Texture load failed: " << texture_error_ << '\n';
                    return 3;
                }
                const auto& texture = textures_.at(static_cast<std::size_t>(selected_texture_));
                std::cout << "Texture: " << texture_source_ << " / texture " << selected_texture_
                          << " / reference " << texture.reference_index << " / " << texture.width << 'x' << texture.height
                          << " / " << texture.format_name << '\n';
                if (!loaded_resource_.empty()) std::cout << "Logical resource: " << loaded_resource_ << '\n';
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
    GLuint asset_texture_ = 0;
    std::optional<dw4::IgbDocument> texture_document_;
    std::optional<dw4::MountedResources> mounted_resources_;
    std::string catalog_error_;
    std::array<char,256> resource_filter_{};
    std::string loaded_resource_;
    std::vector<dw4::TextureInfo> textures_;
    std::string texture_source_;
    std::string texture_error_;
    int selected_texture_ = -1;
    int texture_entry_input_ = 0;
    int max_entry_mib_ = 64;
    float texture_zoom_ = 1.0f;
    ImVec2 texture_pan_{0,0};
    bool texture_fit_pending_ = true;
    bool texture_checkerboard_ = true;
    bool texture_linear_filter_ = false;
    dw4::viewer::MeshRenderer mesh_renderer_;
    dw4::OrbitCamera mesh_camera_;
    std::optional<dw4::assets::AfsArchive> mesh_archive_;
    std::optional<dw4::IgbDocument> mesh_document_;
    std::optional<dw4::MeshData> mesh_data_;
    std::vector<dw4::MeshInfo> meshes_;
    std::array<char,256> mesh_resource_filter_{};
    std::string loaded_mesh_resource_;
    std::string mesh_source_;
    std::string mesh_error_;
    int selected_mesh_archive_ = -1;
    int mesh_entry_input_ = 0;
    int selected_mesh_ = -1;
    int mesh_target_width_ = 0;
    int mesh_target_height_ = 0;
    int mesh_entry_limit_mib_ = 64;
    std::uint64_t mesh_render_start_frame_ = 0;
    bool mesh_wireframe_ = false;
    bool mesh_render_failed_ = false;
    bool mesh_scroll_pending_ = false;
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
        max_entry_mib_ = options_.max_entry_mib;
        mesh_entry_limit_mib_ = options_.max_entry_mib;
        mesh_wireframe_ = options_.mesh_wireframe;
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
        if (options_.initial_page == 5 && !options_.resource.empty()) load_mesh_resource(options_.resource, options_.mesh.value_or(0));
        else if (options_.initial_page == 5 && options_.entry) {
            bool found = false;
            if (disc_) for (std::size_t i = 0; i < disc_->archives.size(); ++i) {
                if (filename(disc_->archives[i].path) == options_.archive_name) {
                    select_mesh_archive(static_cast<int>(i));
                    found = true;
                    break;
                }
            }
            if (!found) mesh_error_ = "Archive was not found in the inspected game directory: " + options_.archive_name;
            else { mesh_entry_input_ = *options_.entry; load_mesh_entry(*options_.entry, options_.mesh.value_or(0)); }
        }
        else if (!options_.resource.empty()) load_resource(options_.resource, options_.texture);
        else if (options_.entry) {
            bool found = false;
            if (disc_) {
                for (std::size_t i = 0; i < disc_->archives.size(); ++i) {
                    if (filename(disc_->archives[i].path) == options_.archive_name) {
                        select_archive(static_cast<int>(i));
                        found = true;
                        break;
                    }
                }
            }
            if (!found) texture_error_ = "Archive was not found in the inspected game directory: " + options_.archive_name;
            else {
                texture_entry_input_ = *options_.entry;
                load_texture_entry(*options_.entry, options_.texture);
            }
        }
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
        clear_texture_document();
        clear_mesh_document();
        mesh_archive_.reset();
        selected_mesh_archive_ = -1;
        mounted_resources_.reset();
        catalog_error_.clear();
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
            try { mounted_resources_ = dw4::MountedResources::open(settings_.data_path); }
            catch (const std::exception& error) { catalog_error_ = error.what(); }
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
            if (!disc_->archives.empty()) select_mesh_archive(0);
        } catch (const std::exception& error) {
            asset_error_ = error.what();
            notice_ = asset_error_;
            notice_error_ = true;
        }
    }

    void select_archive(int index) {
        clear_texture_document();
        archive_.reset();
        selected_archive_ = index;
        selected_entry_ = -1;
        entry_preview_.clear();
        asset_error_.clear();
        try { archive_ = dw4::assets::AfsArchive::open(disc_->archives.at(static_cast<std::size_t>(index)).path); }
        catch (const std::exception& error) { asset_error_ = error.what(); }
    }

    void clear_texture_document() {
        if (asset_texture_) { glDeleteTextures(1, &asset_texture_); asset_texture_ = 0; }
        texture_document_.reset();
        textures_.clear();
        selected_texture_ = -1;
        texture_source_.clear();
        loaded_resource_.clear();
        texture_error_.clear();
        texture_pan_ = ImVec2(0,0);
        texture_fit_pending_ = true;
    }

    void load_texture_entry(int entry, int texture_index = 0) {
        clear_texture_document();
        try {
            if (!archive_) throw std::runtime_error("Choose a valid AFS archive first.");
            if (entry < 0 || static_cast<std::size_t>(entry) >= archive_->entries().size())
                throw std::runtime_error("Entry index is outside this archive.");
            max_entry_mib_ = std::clamp(max_entry_mib_, 1, 256);
            auto bytes = archive_->read(static_cast<std::size_t>(entry),
                static_cast<std::size_t>(max_entry_mib_) * 1024u * 1024u);
            texture_document_ = dw4::read_igb_document(std::move(bytes));
            texture_source_ = filename(archive_->path()) + " / entry " + std::to_string(entry);
            textures_ = dw4::list_igb_textures(*texture_document_);
            if (textures_.empty()) throw std::runtime_error("This IGB document contains no igImage textures.");
            if (texture_index < 0 || static_cast<std::size_t>(texture_index) >= textures_.size())
                throw std::runtime_error("Texture index is outside this document's texture list.");
            select_texture(texture_index);
        } catch (const std::exception& error) { texture_error_ = error.what(); }
    }

    void load_resource(const std::string& logical_path, int texture_index = 0) {
        clear_texture_document();
        try {
            if (!mounted_resources_) throw std::runtime_error("The logical resource catalog is unavailable: " + catalog_error_);
            const auto& catalog = mounted_resources_->catalog();
            const auto* resource = catalog.find(logical_path);
            if (!resource) throw std::runtime_error("Resource was not found in the catalog: " + logical_path);
            if (!resource->location) throw std::runtime_error("This catalog resource has no readable AFS entry.");
            const auto& source_archive = catalog.archives.at(resource->location->archive_index).filename;
            int archive_index = -1;
            for (std::size_t i = 0; i < disc_->archives.size(); ++i)
                if (filename(disc_->archives[i].path) == source_archive) archive_index = static_cast<int>(i);
            if (archive_index < 0) throw std::runtime_error("The catalog archive is not present in the inspected directory: " + source_archive);
            select_archive(archive_index);
            texture_entry_input_ = static_cast<int>(resource->location->entry_index);
            load_texture_entry(texture_entry_input_, texture_index);
            if (texture_document_) loaded_resource_ = logical_path;
        } catch (const std::exception& error) { texture_error_ = error.what(); }
    }

    void select_texture(int index) {
        if (asset_texture_) { glDeleteTextures(1, &asset_texture_); asset_texture_ = 0; }
        texture_error_.clear();
        selected_texture_ = index;
        texture_pan_ = ImVec2(0,0);
        texture_fit_pending_ = true;
        try {
            const auto& info = textures_.at(static_cast<std::size_t>(index));
            if (!info.supported) throw std::runtime_error(info.diagnostic.empty() ? "This texture storage format is not supported yet." : info.diagnostic);
            const auto pixels = dw4::decode_igb_texture(*texture_document_, info.reference_index);
            GLint max_size = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
            if (pixels.width > static_cast<std::uint32_t>(max_size) || pixels.height > static_cast<std::uint32_t>(max_size))
                throw std::runtime_error("Texture dimensions exceed this graphics driver's limit.");
            while (glGetError() != GL_NO_ERROR) {}
            glGenTextures(1, &asset_texture_);
            glBindTexture(GL_TEXTURE_2D, asset_texture_);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            const auto filter = texture_linear_filter_ ? GL_LINEAR : GL_NEAREST;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(pixels.width),
                static_cast<GLsizei>(pixels.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
            if (glGetError() != GL_NO_ERROR) {
                glDeleteTextures(1, &asset_texture_);
                asset_texture_ = 0;
                throw std::runtime_error("The graphics driver rejected the decoded texture upload.");
            }
        } catch (const std::exception& error) { texture_error_ = error.what(); }
    }

    void refresh_gamepads() {
        for (auto* pad : gamepads_) SDL_CloseGamepad(pad);
        gamepads_.clear();
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count; ++i) if (auto* pad = SDL_OpenGamepad(ids[i])) gamepads_.push_back(pad);
        SDL_free(ids);
    }

    void clear_mesh_document() {
        mesh_renderer_.clear();
        mesh_document_.reset();
        mesh_data_.reset();
        meshes_.clear();
        loaded_mesh_resource_.clear();
        mesh_source_.clear();
        mesh_error_.clear();
        selected_mesh_ = -1;
        mesh_scroll_pending_ = false;
        mesh_render_failed_ = false;
        mesh_target_width_ = mesh_target_height_ = 0;
        mesh_camera_.reset();
    }

    void select_mesh_archive(int index) {
        clear_mesh_document();
        mesh_archive_.reset();
        selected_mesh_archive_ = index;
        try { mesh_archive_ = dw4::assets::AfsArchive::open(disc_->archives.at(static_cast<std::size_t>(index)).path); }
        catch (const std::exception& error) { mesh_error_ = error.what(); }
    }

    void load_mesh_entry(int entry, int mesh_index = 0) {
        clear_mesh_document();
        try {
            if (!mesh_archive_) throw std::runtime_error("Choose a valid AFS archive first.");
            if (entry < 0 || static_cast<std::size_t>(entry) >= mesh_archive_->entries().size())
                throw std::runtime_error("Entry index is outside this archive.");
            mesh_entry_limit_mib_ = std::clamp(mesh_entry_limit_mib_, 1, 256);
            auto bytes = mesh_archive_->read(static_cast<std::size_t>(entry), static_cast<std::size_t>(mesh_entry_limit_mib_) * 1024u * 1024u);
            mesh_document_ = dw4::read_igb_document(std::move(bytes));
            mesh_source_ = filename(mesh_archive_->path()) + " / entry " + std::to_string(entry);
            meshes_ = dw4::list_igb_meshes(*mesh_document_);
            if (meshes_.empty()) throw std::runtime_error("This IGB document contains no geometry objects.");
            if (mesh_index < 0 || static_cast<std::size_t>(mesh_index) >= meshes_.size())
                throw std::runtime_error("Mesh index is outside this document's geometry list.");
            select_mesh(mesh_index);
        } catch (const std::exception& error) { mesh_error_ = error.what(); }
    }

    void load_mesh_resource(const std::string& logical_path, int mesh_index = 0) {
        clear_mesh_document();
        try {
            if (!mounted_resources_) throw std::runtime_error("The logical resource catalog is unavailable: " + catalog_error_);
            const auto& catalog = mounted_resources_->catalog();
            const auto* resource = catalog.find(logical_path);
            if (!resource) throw std::runtime_error("Resource was not found in the catalog: " + logical_path);
            if (!resource->location) throw std::runtime_error("This catalog resource has no readable AFS entry.");
            const auto& source_archive = catalog.archives.at(resource->location->archive_index).filename;
            int archive_index = -1;
            for (std::size_t i = 0; i < disc_->archives.size(); ++i)
                if (filename(disc_->archives[i].path) == source_archive) archive_index = static_cast<int>(i);
            if (archive_index < 0) throw std::runtime_error("The catalog archive is not present in the inspected directory: " + source_archive);
            select_mesh_archive(archive_index);
            mesh_entry_input_ = static_cast<int>(resource->location->entry_index);
            load_mesh_entry(mesh_entry_input_, mesh_index);
            if (mesh_document_) loaded_mesh_resource_ = logical_path;
        } catch (const std::exception& error) { mesh_error_ = error.what(); }
    }

    void select_mesh(int index) {
        mesh_renderer_.clear();
        mesh_data_.reset();
        mesh_error_.clear();
        mesh_render_failed_ = false;
        selected_mesh_ = index;
        mesh_scroll_pending_ = true;
        mesh_camera_.reset();
        try {
            const auto& info = meshes_.at(static_cast<std::size_t>(index));
            if (!info.supported) throw std::runtime_error(info.diagnostic.empty() ? "This geometry profile is not supported yet." : info.diagnostic);
            mesh_data_ = dw4::decode_igb_mesh(*mesh_document_, info.reference_index);
            mesh_renderer_.upload(*mesh_data_);
            mesh_render_start_frame_ = mesh_renderer_.rendered_frames();
        } catch (const std::exception& error) { mesh_error_ = error.what(); }
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
        const std::array<const char*, 7> pages{"Overview", "Graphics", "Controls", "Original data", "Textures", "Meshes", "Roadmap"};
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
        if (page_ == 4) draw_textures();
        if (page_ == 5) draw_meshes();
        if (page_ == 6) draw_roadmap();
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
        ImGui::TextColored(teal, "  Texture viewer");
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
            ImGui::SameLine();
            if (ImGui::Button("Open texture viewer", ImVec2(220, 42))) page_ = 4;
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
            paragraph("This panel shows header and type metadata. The asset viewers read supported texture pixels and local geometry. Scene reconstruction and gameplay are not implemented.");
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
                if (ImGui::SmallButton("View textures")) {
                    texture_entry_input_ = selected_entry_;
                    load_texture_entry(selected_entry_);
                    page_ = 4;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("View meshes")) {
                    select_mesh_archive(selected_archive_);
                    mesh_entry_input_ = selected_entry_;
                    load_mesh_entry(selected_entry_);
                    page_ = 5;
                }
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

    void draw_texture_canvas() {
        const auto& info = textures_.at(static_cast<std::size_t>(selected_texture_));
        const auto size = ImGui::GetContentRegionAvail();
        const ImVec2 canvas_size(std::max(1.0f, size.x), std::max(120.0f, size.y));
        const auto canvas_min = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_max(canvas_min.x + canvas_size.x, canvas_min.y + canvas_size.y);
        const ImVec2 center(canvas_min.x + canvas_size.x * 0.5f, canvas_min.y + canvas_size.y * 0.5f);
        if (texture_fit_pending_) {
            texture_zoom_ = std::clamp(std::min((canvas_size.x - 32) / static_cast<float>(info.width),
                (canvas_size.y - 32) / static_cast<float>(info.height)), 0.0625f, 32.0f);
            texture_pan_ = ImVec2(0,0);
            texture_fit_pending_ = false;
        }
        ImGui::InvisibleButton("texture_canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        auto& io = ImGui::GetIO();
        if (ImGui::IsItemHovered()) {
            if (ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY) && io.MouseWheel != 0) {
                const float old_zoom = texture_zoom_;
                texture_zoom_ = std::clamp(texture_zoom_ * std::pow(1.2f, io.MouseWheel), 0.0625f, 32.0f);
                const float ratio = texture_zoom_ / old_zoom;
                texture_pan_.x = io.MousePos.x + (center.x + texture_pan_.x - io.MousePos.x) * ratio - center.x;
                texture_pan_.y = io.MousePos.y + (center.y + texture_pan_.y - io.MousePos.y) * ratio - center.y;
            }
        }
        if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
            texture_pan_.x += io.MouseDelta.x;
            texture_pan_.y += io.MouseDelta.y;
        }
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(10,15,22,255), 6);
        const ImVec2 image_size(info.width * texture_zoom_, info.height * texture_zoom_);
        const ImVec2 image_min(center.x + texture_pan_.x - image_size.x * 0.5f,
            center.y + texture_pan_.y - image_size.y * 0.5f);
        const ImVec2 image_max(image_min.x + image_size.x, image_min.y + image_size.y);
        draw->PushClipRect(canvas_min, canvas_max, true);
        if (texture_checkerboard_) {
            const ImVec2 visible_min(std::max(canvas_min.x,image_min.x), std::max(canvas_min.y,image_min.y));
            const ImVec2 visible_max(std::min(canvas_max.x,image_max.x), std::min(canvas_max.y,image_max.y));
            if (visible_min.x < visible_max.x && visible_min.y < visible_max.y) {
                draw->PushClipRect(visible_min, visible_max, true);
                constexpr float tile = 16.0f;
                const int start_x = static_cast<int>(std::floor((visible_min.x - canvas_min.x) / tile));
                const int start_y = static_cast<int>(std::floor((visible_min.y - canvas_min.y) / tile));
                for (int y = start_y; canvas_min.y + y * tile < visible_max.y; ++y)
                    for (int x = start_x; canvas_min.x + x * tile < visible_max.x; ++x) {
                        const ImVec2 p(canvas_min.x + x * tile, canvas_min.y + y * tile);
                        const auto color = (x + y) % 2 ? IM_COL32(53,62,73,255) : IM_COL32(34,42,53,255);
                        draw->AddRectFilled(p, ImVec2(p.x+tile,p.y+tile), color);
                    }
                draw->PopClipRect();
            }
        }
        draw->AddImage(static_cast<ImTextureID>(asset_texture_), image_min, image_max);
        draw->AddRect(image_min, image_max, IM_COL32(109,132,148,150));
        draw->PopClipRect();
    }

    void draw_textures() {
        title("Texture viewer", "Original IGB texture pixels decoded natively from your game data. Final scene appearance and gameplay are not implemented yet.");
        if (!disc_) {
            paragraph("Connect your extracted USA copy before loading an AFS entry.");
            if (ImGui::Button("Connect original data")) page_ = 3;
            if (!texture_error_.empty()) paragraph(texture_error_.c_str(), amber);
            return;
        }
        if (mounted_resources_) {
            ImGui::SetNextItemWidth(210);
            ImGui::InputTextWithHint("##resource_filter", "Filter logical resource path", resource_filter_.data(), resource_filter_.size());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            const auto* preview = loaded_resource_.empty() ? "Choose a logical resource from ndmw.igb" : loaded_resource_.c_str();
            if (ImGui::BeginCombo("##logical_resource", preview, ImGuiComboFlags_HeightLarge)) {
                const auto& resources = mounted_resources_->catalog().resources;
                std::vector<std::size_t> visible;
                for (std::size_t i = 0; i < resources.size(); ++i)
                    if (!resource_filter_[0] || resources[i].logical_path.find(resource_filter_.data()) != std::string::npos)
                        visible.push_back(i);
                ImGuiListClipper clip;
                clip.Begin(static_cast<int>(visible.size()));
                while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                    const auto& resource = resources[visible[static_cast<std::size_t>(row)]];
                    ImGui::BeginDisabled(!resource.location);
                    if (ImGui::Selectable(resource.logical_path.c_str(), loaded_resource_ == resource.logical_path))
                        load_resource(resource.logical_path);
                    ImGui::EndDisabled();
                }
                if (visible.empty()) ImGui::TextDisabled("No matching catalog resources.");
                ImGui::EndCombo();
            }
        } else if (!catalog_error_.empty()) paragraph(("Resource catalog unavailable: " + catalog_error_).c_str(), amber);
        ImGui::SetNextItemWidth(170);
        const auto archive_name = archive_ ? filename(archive_->path()) : "Choose archive";
        if (ImGui::BeginCombo("Archive", archive_name.c_str())) {
            for (std::size_t i = 0; i < disc_->archives.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(filename(disc_->archives[i].path).c_str(), selected_archive_ == static_cast<int>(i)))
                    select_archive(static_cast<int>(i));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95);
        ImGui::InputInt("Entry", &texture_entry_input_, 0, 0);
        ImGui::SameLine();
        if (ImGui::Button("Load entry")) load_texture_entry(texture_entry_input_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::InputInt("Limit (MiB)", &max_entry_mib_, 0, 0);
        max_entry_mib_ = std::clamp(max_entry_mib_, 1, 256);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum AFS entry read: 1-256 MiB.\nDecoded RGBA is independently limited to 64 MiB per texture.");
        if (!texture_source_.empty())
            ImGui::TextColored(teal, "%s  |  %zu textures  |  %zu serialized objects", texture_source_.c_str(),
                textures_.size(), texture_document_->objects.size());
        else paragraph("Select an IGB entry here, or use View textures in the Original data archive browser.");
        if (!texture_error_.empty()) paragraph(texture_error_.c_str(), amber);
        if (textures_.empty()) {
            paragraph("Only supported base-image layouts are decoded. Unsupported data produces an explicit diagnostic; original files are never modified.");
            return;
        }
        const float panel_height = std::max(270.0f, ImGui::GetContentRegionAvail().y - 67.0f);
        ImGui::BeginChild("texture_list", ImVec2(235,panel_height), true);
        ImGui::TextColored(muted, "TEXTURES");
        ImGui::Separator();
        ImGui::BeginChild("texture_names", ImVec2(0,std::max(80.0f,ImGui::GetContentRegionAvail().y - 153)), false);
        ImGuiListClipper texture_clip;
        texture_clip.Begin(static_cast<int>(textures_.size()));
        while (texture_clip.Step()) for (int row = texture_clip.DisplayStart; row < texture_clip.DisplayEnd; ++row) {
            const auto i = static_cast<std::size_t>(row);
            const auto& info = textures_[i];
            ImGui::PushID(static_cast<int>(i));
            const auto label = std::to_string(i) + "  " + (info.name.empty() ? "Unnamed image" : info.name);
            if (!info.supported) ImGui::PushStyleColor(ImGuiCol_Text, muted);
            if (ImGui::Selectable(label.c_str(), selected_texture_ == static_cast<int>(i), 0, ImVec2(0,28))) select_texture(static_cast<int>(i));
            if (!info.supported) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%u x %u / %s\nIGB reference %u", info.name.c_str(),
                info.width, info.height, info.format_name.c_str(), info.reference_index);
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (selected_texture_ >= 0) {
            const auto& info = textures_[static_cast<std::size_t>(selected_texture_)];
            ImGui::Separator();
            ImGui::Text("%u x %u / ref %u", info.width, info.height, info.reference_index);
            paragraph(info.format_name.c_str());
            ImGui::TextColored(muted, "%s encoded", byte_size(info.encoded_bytes).c_str());
            ImGui::TextColored(muted, "Base image only");
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("texture_detail", ImVec2(0,panel_height), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (selected_texture_ >= 0) {
            if (asset_texture_) {
                if (ImGui::SmallButton("Fit")) texture_fit_pending_ = true;
                ImGui::SameLine();
                if (ImGui::SmallButton("1:1")) { texture_zoom_ = 1; texture_pan_ = ImVec2(0,0); texture_fit_pending_ = false; }
                ImGui::SameLine();
                ImGui::TextColored(muted, "%.0f%%", texture_zoom_ * 100);
                ImGui::SameLine();
                ImGui::Checkbox("Alpha grid", &texture_checkerboard_);
                ImGui::SameLine();
                if (ImGui::Checkbox("Linear", &texture_linear_filter_)) {
                    glBindTexture(GL_TEXTURE_2D, asset_texture_);
                    const auto filter = texture_linear_filter_ ? GL_LINEAR : GL_NEAREST;
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
                }
                if (ImGui::GetWindowWidth() > 700) {
                    ImGui::SameLine();
                    ImGui::TextColored(muted, "Wheel: zoom / drag: pan");
                }
                draw_texture_canvas();
            } else {
                ImGui::Spacing();
                paragraph("This image could not be decoded.", amber);
                paragraph(texture_error_.c_str());
            }
        }
        ImGui::EndChild();
    }

    void draw_mesh_canvas() {
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 canvas_size(std::max(1.0f,available.x),std::max(120.0f,available.y));
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("mesh_canvas", canvas_size, ImGuiButtonFlags_MouseButtonLeft |
            ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        auto& io = ImGui::GetIO();
        if (ImGui::IsItemHovered() && ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY) && io.MouseWheel != 0)
            mesh_camera_.zoom(io.MouseWheel);
        if (ImGui::IsItemActive()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                (io.KeyShift && ImGui::IsMouseDragging(ImGuiMouseButton_Left)))
                mesh_camera_.pan(-io.MouseDelta.x / canvas_size.y, io.MouseDelta.y / canvas_size.y);
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                mesh_camera_.orbit(-io.MouseDelta.x * 0.008f, -io.MouseDelta.y * 0.008f);
        }
        try {
            double width = std::max(1.0, static_cast<double>(canvas_size.x * io.DisplayFramebufferScale.x));
            double height = std::max(1.0, static_cast<double>(canvas_size.y * io.DisplayFramebufferScale.y));
            // Bound the native inspection target independently of the desktop size.
            const double reduction = std::min({1.0,4096.0 / width,4096.0 / height,std::sqrt(8388608.0 / (width * height))});
            mesh_target_width_ = std::max(1,static_cast<int>(width * reduction));
            mesh_target_height_ = std::max(1,static_cast<int>(height * reduction));
            const auto target = mesh_renderer_.render(mesh_target_width_, mesh_target_height_, mesh_camera_,
                mesh_wireframe_ ? dw4::viewer::MeshDrawMode::wireframe : dw4::viewer::MeshDrawMode::solid, settings_.msaa);
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 end(origin.x + canvas_size.x,origin.y + canvas_size.y);
            draw->AddImage(static_cast<ImTextureID>(target), origin, end, ImVec2(0,1), ImVec2(1,0));
            draw->AddRect(origin,end,IM_COL32(53,72,88,255),6);
        } catch (const std::exception& error) {
            mesh_error_ = error.what();
            mesh_render_failed_ = true;
        }
    }

    void draw_meshes() {
        title("Mesh viewer", "Original object-space mesh in its stored pose. Materials, scene transforms, skinning and gameplay are not implemented.");
        if (!disc_) {
            paragraph("Connect your extracted game copy before loading an IGB mesh.");
            if (ImGui::Button("Connect original data")) page_ = 3;
            if (!mesh_error_.empty()) paragraph(mesh_error_.c_str(),amber);
            return;
        }
        if (mounted_resources_) {
            ImGui::SetNextItemWidth(210);
            ImGui::InputTextWithHint("##mesh_resource_filter","Filter logical resource path",mesh_resource_filter_.data(),mesh_resource_filter_.size());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            const auto* preview = loaded_mesh_resource_.empty() ? "Choose a logical resource from ndmw.igb" : loaded_mesh_resource_.c_str();
            if (ImGui::BeginCombo("##mesh_logical_resource",preview,ImGuiComboFlags_HeightLarge)) {
                const auto& resources = mounted_resources_->catalog().resources;
                std::vector<std::size_t> visible;
                for (std::size_t i = 0; i < resources.size(); ++i)
                    if (!mesh_resource_filter_[0] || resources[i].logical_path.find(mesh_resource_filter_.data()) != std::string::npos) visible.push_back(i);
                ImGuiListClipper clip;
                clip.Begin(static_cast<int>(visible.size()));
                while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                    const auto& resource = resources[visible[static_cast<std::size_t>(row)]];
                    ImGui::BeginDisabled(!resource.location);
                    if (ImGui::Selectable(resource.logical_path.c_str(),loaded_mesh_resource_ == resource.logical_path)) load_mesh_resource(resource.logical_path);
                    ImGui::EndDisabled();
                }
                if (visible.empty()) ImGui::TextDisabled("No matching catalog resources.");
                ImGui::EndCombo();
            }
        } else if (!catalog_error_.empty()) paragraph(("Resource catalog unavailable: " + catalog_error_).c_str(),amber);
        ImGui::SetNextItemWidth(170);
        const auto archive_name = mesh_archive_ ? filename(mesh_archive_->path()) : "Choose archive";
        if (ImGui::BeginCombo("Archive",archive_name.c_str())) {
            for (std::size_t i = 0; i < disc_->archives.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(filename(disc_->archives[i].path).c_str(),selected_mesh_archive_ == static_cast<int>(i))) select_mesh_archive(static_cast<int>(i));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95);
        ImGui::InputInt("Entry",&mesh_entry_input_,0,0);
        ImGui::SameLine();
        if (ImGui::Button("Load entry")) load_mesh_entry(mesh_entry_input_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::InputInt("Limit (MiB)",&mesh_entry_limit_mib_,0,0);
        mesh_entry_limit_mib_ = std::clamp(mesh_entry_limit_mib_,1,256);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum AFS entry read: 1-256 MiB.\nThe mesh decoder and graphics target have independent limits.");
        if (!mesh_source_.empty()) ImGui::TextColored(teal,"%s  |  %zu geometry objects  |  target %d x %d / MSAA %dx",
            mesh_source_.c_str(),meshes_.size(),mesh_target_width_,mesh_target_height_,mesh_renderer_.samples());
        else paragraph("Select an IGB entry here, or use View meshes in the Original data archive browser.");
        if (!mesh_error_.empty()) paragraph(mesh_error_.c_str(),amber);
        if (meshes_.empty()) {
            paragraph("Supported geometry is decoded from its serialized position and topology data. Unsupported layouts remain explicit diagnostics.");
            return;
        }
        const float height = std::max(300.0f,ImGui::GetContentRegionAvail().y - 67.0f);
        ImGui::BeginChild("mesh_list",ImVec2(235,height),true);
        ImGui::TextColored(muted,"GEOMETRY");
        ImGui::Separator();
        ImGui::BeginChild("mesh_names",ImVec2(0,std::max(80.0f,ImGui::GetContentRegionAvail().y - 188)),false);
        ImGuiListClipper clip;
        clip.Begin(static_cast<int>(meshes_.size()));
        if (mesh_scroll_pending_ && selected_mesh_ >= 0) clip.IncludeItemByIndex(selected_mesh_);
        while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
            const auto& info = meshes_[static_cast<std::size_t>(row)];
            ImGui::PushID(row);
            const auto label = std::to_string(row) + "  " + (info.name.empty() ? "Geometry " + std::to_string(info.reference_index) : info.name);
            if (!info.supported) ImGui::PushStyleColor(ImGuiCol_Text,muted);
            if (ImGui::Selectable(label.c_str(),selected_mesh_ == row,0,ImVec2(0,28))) select_mesh(row);
            if (mesh_scroll_pending_ && selected_mesh_ == row) { ImGui::SetScrollHereY(0.5f); mesh_scroll_pending_ = false; }
            if (!info.supported) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nIGB reference %u / %s\n%s",info.name.c_str(),info.reference_index,info.topology_name.c_str(),info.diagnostic.c_str());
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (selected_mesh_ >= 0) {
            const auto& info = meshes_[static_cast<std::size_t>(selected_mesh_)];
            ImGui::Separator();
            ImGui::Text("Reference %u",info.reference_index);
            ImGui::Text("%u vertices / %u triangles",info.vertex_count,info.triangle_count);
            paragraph(info.topology_name.c_str());
            if (mesh_data_) {
                const auto& bounds = mesh_data_->bounds;
                ImGui::TextColored(muted,"Min %.3g, %.3g, %.3g",bounds.min.x,bounds.min.y,bounds.min.z);
                ImGui::TextColored(muted,"Max %.3g, %.3g, %.3g",bounds.max.x,bounds.max.y,bounds.max.z);
            }
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("mesh_detail",ImVec2(0,height),true,ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (mesh_renderer_.has_mesh() && !mesh_render_failed_) {
            if (ImGui::Button("Fit")) mesh_camera_.reset();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);
            int mode = mesh_wireframe_ ? 1 : 0;
            if (ImGui::Combo("##mesh_draw_mode",&mode,"Solid\0Wireframe\0")) mesh_wireframe_ = mode == 1;
            if (ImGui::GetWindowWidth() > 650) { ImGui::SameLine(); ImGui::TextColored(muted,"Drag: orbit / right: pan / wheel: zoom"); }
            draw_mesh_canvas();
        } else {
            paragraph("This geometry could not be rendered.",amber);
            paragraph(mesh_error_.c_str());
        }
        ImGui::EndChild();
    }

    void draw_roadmap() {
        title("The path to a playable port", "A staged development plan. Features below are not available until their implementation is verified.");
        struct Stage { const char* state; const char* heading; const char* body; bool ready; };
        const std::array<Stage,6> stages{{
            {"AVAILABLE", "01  Native workbench & data inspection", "Windows x64 interface, host settings, SDL input diagnostics, GameCube / AFS inspection, IGB object parsing and logical resource lookup.", true},
            {"IN PROGRESS", "02  Native original-asset viewer", "Native texture and local mesh viewers are available. Scene transforms, materials, animation and comparison to the original renderer remain.", false},
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
                         "--page         Open overview, graphics, controls, assets, textures, meshes or roadmap.\n"
                         "--archive      AFS filename inside the game directory; requires --entry.\n"
                         "--entry        Zero-based AFS entry for the selected asset viewer.\n"
                         "--resource     Logical catalog path; alternative to --archive/--entry.\n"
                         "--texture      Zero-based texture-list index (default 0).\n"
                         "--mesh         Zero-based geometry-list index; opens the Meshes page.\n"
                         "--mesh-mode    Solid or wireframe mesh inspection (default solid).\n"
                         "--max-entry-mib Limit an AFS entry read to 1-256 MiB (default 64).\n";
            return 0;
        }
        return Workbench(std::move(options)).run();
    } catch (const std::exception& error) {
        std::cerr << "dw4_pc: " << error.what() << '\n';
        return 1;
    }
}
