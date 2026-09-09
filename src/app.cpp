#include "dw4/assets.hpp"
#include "dw4/animations.hpp"
#include "dw4/camera.hpp"
#include "dw4/igb.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/materials.hpp"
#include "dw4/meshes.hpp"
#include "dw4/packages.hpp"
#include "dw4/player_placement.hpp"
#include "dw4/player_visual.hpp"
#include "dw4/resources.hpp"
#include "dw4/scenes.hpp"
#include "dw4/scene_controls.hpp"
#include "dw4/settings.hpp"
#include "dw4/startup.hpp"
#include "dw4/startup_loading.hpp"
#include "dw4/skinning.hpp"
#include "dw4/textures.hpp"
#include "dw4/walking.hpp"
#include "dw4/walking_collision.hpp"
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
#include <map>
#include <optional>
#include <set>
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
    std::string package;
    std::optional<int> package_member;
    std::optional<int> entry;
    int texture = 0;
    std::optional<int> mesh;
    dw4::viewer::MeshDrawMode mesh_mode = dw4::viewer::MeshDrawMode::solid;
    std::optional<int> scene_root;
    std::optional<int> scene_inspect_child;
    std::optional<int> scene_switch_control;
    std::optional<dw4::SwitchControlCommand> scene_switch_command;
    dw4::viewer::MeshDrawMode scene_mode = dw4::viewer::MeshDrawMode::solid;
    std::string scene_player_resource;
    std::optional<int> scene_player_model, scene_player_slot, scene_player_branch, scene_player_point, scene_player_group;
    std::string scene_player_animation;
    std::optional<double> scene_player_time;
    bool scene_player_play = false;
    bool play = false, play_smoke = false;
    std::string boot_package;
    dw4::BootInputs boot_inputs;
    bool rig = false;
    std::optional<int> bone;
    dw4::Vec3 rotation{};
    std::string animation;
    std::optional<double> animation_time;
    bool animation_play = false;
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

float parse_rotation_option(const std::string& value, const std::string& option) {
    float number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !std::isfinite(number) || number < -180 || number > 180)
        throw std::runtime_error(option + " requires a finite angle from -180 to 180 degrees.");
    return number;
}

double parse_animation_time(const std::string& value) {
    double seconds = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), seconds);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !std::isfinite(seconds) || seconds < 0)
        throw std::runtime_error("--animation-time requires finite, nonnegative seconds.");
    return seconds;
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
    if (std::find(args.begin(),args.end(),"--play") != args.end()) {
        for (std::size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--play" || args[i] == "--play-smoke" || args[i] == "--help" || args[i] == "-h") continue;
            if (args[i] == "--data" || args[i] == "--config" || args[i] == "--smoke-test" || args[i] == "--screenshot") { ++i; continue; }
            if (args[i].starts_with("--play-")) throw std::runtime_error("Unknown option: "+args[i]);
            throw std::runtime_error("--play cannot be combined with viewer, page or boot options: "+args[i]);
        }
    }
    bool texture_requested = false;
    bool mesh_mode_requested = false;
    bool scene_mode_requested = false;
    bool page_requested = false;
    bool rotation_requested = false;
    bool entry_limit_requested = false;
    std::set<std::string> player_options;
    std::set<std::string> boot_options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i].starts_with("--scene-player-") && !player_options.insert(args[i]).second)
            throw std::runtime_error("Duplicate player inspection option: " + args[i]);
        if (args[i] == "--help" || args[i] == "-h") { out.help = true; continue; }
        if (args[i] == "--play" || args[i] == "--play-smoke") {
            auto& flag = args[i] == "--play" ? out.play : out.play_smoke;
            if (flag) throw std::runtime_error("Duplicate play option: " + args[i]);
            flag = true; continue;
        }
        if (args[i] == "--rig") { out.rig = true; continue; }
        if (args[i] == "--animation-play") { out.animation_play = true; continue; }
        if (args[i] == "--scene-player-play") { out.scene_player_play = true; continue; }
        if (args[i].starts_with("--boot-")) {
            const auto option = args[i];
            if (!boot_options.insert(option).second) throw std::runtime_error("Duplicate boot option: "+option);
            if (++i == args.size()) throw std::runtime_error("Missing value for "+option);
            if (option == "--boot-package") {
                if (args[i].empty()) throw std::runtime_error("--boot-package requires a named package.");
                out.boot_package = args[i];
            } else if (!dw4::set_boot_input_option(out.boot_inputs,option,args[i])) throw std::runtime_error("Unknown option: "+option);
            continue;
        }
        if (args[i].starts_with("--scene-player-")) {
            const auto option = args[i];
            if (++i == args.size()) throw std::runtime_error("Missing value for " + option);
            if (option == "--scene-player-resource") out.scene_player_resource = args[i];
            else if (option == "--scene-player-model") out.scene_player_model = parse_integer_option(args[i],option,0,1048575);
            else if (option == "--scene-player-slot") out.scene_player_slot = parse_integer_option(args[i],option,0,3);
            else if (option == "--scene-player-branch") out.scene_player_branch = parse_integer_option(args[i],option,0,4095);
            else if (option == "--scene-player-point") out.scene_player_point = parse_integer_option(args[i],option,0,1048575);
            else if (option == "--scene-player-group") out.scene_player_group = parse_integer_option(args[i],option,0,4095);
            else if (option == "--scene-player-animation") out.scene_player_animation = args[i];
            else if (option == "--scene-player-time") out.scene_player_time = parse_animation_time(args[i]);
            else throw std::runtime_error("Unknown option: " + option);
            continue;
        }
        if (args[i] != "--data" && args[i] != "--config" && args[i] != "--smoke-test" && args[i] != "--screenshot" && args[i] != "--page" &&
            args[i] != "--archive" && args[i] != "--entry" && args[i] != "--texture" && args[i] != "--max-entry-mib" && args[i] != "--resource" &&
            args[i] != "--package" && args[i] != "--package-member" &&
            args[i] != "--mesh" && args[i] != "--mesh-mode" && args[i] != "--scene-root" && args[i] != "--scene-mode" &&
            args[i] != "--scene-inspect-child" && args[i] != "--scene-switch-control" && args[i] != "--scene-switch-command" && args[i] != "--bone" &&
            args[i] != "--rotate-x" && args[i] != "--rotate-y" && args[i] != "--rotate-z" &&
            args[i] != "--animation" && args[i] != "--animation-time")
            throw std::runtime_error("Unknown option: " + args[i]);
        const auto option = args[i];
        if (++i == args.size()) throw std::runtime_error("Missing value for " + option);
        if (option == "--data") out.data = dw4::utf8_path(args[i]);
        else if (option == "--config") out.config = dw4::utf8_path(args[i]);
        else if (option == "--screenshot") out.screenshot = dw4::utf8_path(args[i]);
        else if (option == "--archive") out.archive_name = args[i];
        else if (option == "--resource") out.resource = args[i];
        else if (option == "--package") {
            if (args[i].empty()) throw std::runtime_error("--package requires an original descriptor resource name.");
            out.package = args[i];
        }
        else if (option == "--package-member") out.package_member = parse_integer_option(args[i],option,0,1000000);
        else if (option == "--entry") out.entry = parse_integer_option(args[i], option, 0, 1000000);
        else if (option == "--texture") { out.texture = parse_integer_option(args[i], option, 0, 1000000); texture_requested = true; }
        else if (option == "--mesh") out.mesh = parse_integer_option(args[i], option, 0, 1000000);
        else if (option == "--scene-root") out.scene_root = parse_integer_option(args[i], option, 0, 1000000);
        else if (option == "--scene-inspect-child") out.scene_inspect_child = parse_integer_option(args[i], option, 0, 1000000);
        else if (option == "--scene-switch-control") out.scene_switch_control = parse_integer_option(args[i],option,0,1048575);
        else if (option == "--scene-switch-command") {
            if (args[i] == "all-on") out.scene_switch_command = dw4::SwitchControlCommand::all_on;
            else if (args[i] == "all-off") out.scene_switch_command = dw4::SwitchControlCommand::all_off;
            else throw std::runtime_error("--scene-switch-command requires all-on or all-off.");
        }
        else if (option == "--bone") out.bone = parse_integer_option(args[i], option, 0, 1000000);
        else if (option == "--animation") {
            if (args[i].empty()) throw std::runtime_error("--animation requires an original clip name.");
            out.animation = args[i];
        }
        else if (option == "--animation-time") out.animation_time = parse_animation_time(args[i]);
        else if (option == "--rotate-x" || option == "--rotate-y" || option == "--rotate-z") {
            const auto angle = parse_rotation_option(args[i], option);
            if (option == "--rotate-x") out.rotation.x = angle;
            if (option == "--rotate-y") out.rotation.y = angle;
            if (option == "--rotate-z") out.rotation.z = angle;
            rotation_requested = true;
        }
        else if (option == "--mesh-mode" || option == "--scene-mode") {
            auto& mode = option == "--mesh-mode" ? out.mesh_mode : out.scene_mode;
            if (args[i] == "solid") mode = dw4::viewer::MeshDrawMode::solid;
            else if (args[i] == "wireframe") mode = dw4::viewer::MeshDrawMode::wireframe;
            else if (args[i] == "textured") mode = dw4::viewer::MeshDrawMode::textured;
            else throw std::runtime_error(option + " requires solid, wireframe or textured.");
            if (option == "--mesh-mode") mesh_mode_requested = true;
            else scene_mode_requested = true;
        }
        else if (option == "--max-entry-mib") { out.max_entry_mib = parse_integer_option(args[i], option, 1, 256); entry_limit_requested = true; }
        else if (option == "--page") {
            page_requested = true;
            const std::array<std::string,9> pages{"overview", "graphics", "controls", "assets", "textures", "meshes", "scenes", "roadmap", "startup"};
            const auto found = std::find(pages.begin(), pages.end(), args[i]);
            if (found == pages.end()) throw std::runtime_error("--page requires overview, graphics, controls, assets, textures, meshes, scenes, roadmap or startup.");
            out.initial_page = static_cast<int>(found - pages.begin());
        }
        else {
            out.smoke_frames = parse_integer_option(args[i], option, 1, 10000);
        }
    }
    if (!out.screenshot.empty() && !out.smoke_frames)
        throw std::runtime_error("--screenshot requires --smoke-test FRAMES.");
    if (!player_options.empty()) {
        if (out.scene_player_resource.empty() || !out.scene_player_model || !out.scene_player_slot ||
            !out.scene_player_branch || !out.scene_player_point || !out.scene_player_group || !out.scene_root)
            throw std::runtime_error("Player inspection requires --scene-root and explicit --scene-player-resource, -model, -slot, -branch, -point and -group selectors.");
        if ((out.scene_player_time || out.scene_player_play || player_options.contains("--scene-player-animation")) && out.scene_player_animation.empty())
            throw std::runtime_error("Player animation options require a nonempty --scene-player-animation NAME.");
        if (out.scene_player_play && out.smoke_frames == 1)
            throw std::runtime_error("Player playback smoke testing requires at least two frames.");
    }
    if (out.entry.has_value() != !out.archive_name.empty())
        throw std::runtime_error("Use --archive NAME and --entry INDEX together.");
    if (texture_requested && !out.entry && out.resource.empty())
        throw std::runtime_error("--texture requires --resource, or --archive and --entry.");
    if (!out.resource.empty() && out.entry)
        throw std::runtime_error("Choose --resource or --archive with --entry; these source options cannot be combined.");
    if (!out.package.empty() && (!out.resource.empty() || out.entry))
        throw std::runtime_error("Choose --package, --resource or --archive with --entry; these source options cannot be combined.");
    if (out.package_member && out.package.empty()) throw std::runtime_error("--package-member requires --package NAME.");
    if (out.package_member && !out.scene_root) throw std::runtime_error("--package-member requires an explicit --scene-root INDEX. Use --package NAME alone to inspect the member list.");
    if (!out.package.empty() && !out.package_member && (out.scene_root || out.scene_inspect_child || out.scene_switch_control || out.scene_switch_command))
        throw std::runtime_error("Package scene selection requires an explicit --package-member INDEX.");
    if (out.scene_switch_control.has_value() != out.scene_switch_command.has_value())
        throw std::runtime_error("Use --scene-switch-control REFERENCE and --scene-switch-command all-on|all-off together.");
    if (out.scene_switch_control && out.scene_inspect_child)
        throw std::runtime_error("Choose an original switch command or a root-child inspection override; they cannot be combined.");
    if (!out.animation.empty()) {
        if (out.bone || rotation_requested) throw std::runtime_error("Choose an animation clip or manual bone rotation; these pose inputs cannot be combined.");
        out.rig = true;
    }
    if (out.animation_time && out.animation.empty()) throw std::runtime_error("--animation-time requires --animation NAME.");
    if (out.animation_play && out.animation.empty()) throw std::runtime_error("--animation-play requires --animation NAME.");
    if (out.animation_play && out.smoke_frames == 1) throw std::runtime_error("Animation playback smoke testing requires at least two frames.");
    if (out.rig && !out.entry && out.resource.empty())
        throw std::runtime_error("--rig requires --resource, or --archive and --entry.");
    if ((out.bone || rotation_requested) && !out.rig)
        throw std::runtime_error("--bone and rotation controls require --rig.");
    if (rotation_requested && !out.bone)
        throw std::runtime_error("Rotation controls require an explicit --bone INDEX.");
    if (out.rig && !out.mesh) out.mesh = 0;
    const bool scene_requested = !out.package.empty() || out.scene_root || out.scene_inspect_child || out.scene_switch_control || scene_mode_requested || out.initial_page == 6;
    if (scene_requested && (out.mesh || texture_requested || mesh_mode_requested))
        throw std::runtime_error("Scene inspection cannot be combined with mesh, rig, animation or texture selection.");
    if ((!out.package.empty() || out.scene_root || out.scene_inspect_child || out.scene_switch_control || scene_mode_requested) && page_requested && out.initial_page != 6)
        throw std::runtime_error("Scene options require --page scenes when --page is specified.");
    if ((out.scene_root || out.scene_inspect_child || out.scene_switch_control) && !out.entry && out.resource.empty() && out.package.empty())
        throw std::runtime_error("Scene-root and child selection require --package, --resource, or --archive and --entry.");
    if (!page_requested && scene_requested) out.initial_page = 6;
    if (out.mesh && !out.entry && out.resource.empty())
        throw std::runtime_error("--mesh requires --resource, or --archive and --entry.");
    if (out.mesh && texture_requested) throw std::runtime_error("Choose --mesh or --texture, not both.");
    if (out.mesh && page_requested && out.initial_page != 5)
        throw std::runtime_error("--mesh requires --page meshes when --page is specified.");
    if (texture_requested && out.initial_page == 5)
        throw std::runtime_error("Use --mesh for the Meshes page.");
    if (!page_requested && !scene_requested && (out.entry || !out.resource.empty())) out.initial_page = out.mesh ? 5 : 4;
    if (mesh_mode_requested && out.initial_page != 5)
        throw std::runtime_error("--mesh-mode requires the Meshes page.");
    if (out.initial_page == 8 && (!out.package.empty() || !out.resource.empty() || out.entry || out.mesh || out.rig || texture_requested || scene_mode_requested))
        throw std::runtime_error("The Startup page requires --boot-package rather than viewer source selectors.");
    if (!boot_options.empty()) {
        if (out.boot_package.empty()) throw std::runtime_error("Boot inspection state requires --boot-package NAME.");
        if (entry_limit_requested) throw std::runtime_error("--max-entry-mib applies to asset viewers; boot execution uses its documented fixed member and aggregate limits.");
        if (!out.package.empty() || !out.resource.empty() || out.entry || out.mesh || out.rig || texture_requested ||
            scene_requested || mesh_mode_requested || !player_options.empty())
            throw std::runtime_error("Boot execution cannot be combined with scene, mesh or texture inspection sources.");
        if (page_requested && out.initial_page != 8) throw std::runtime_error("Boot options require --page startup when --page is specified.");
        dw4::validate_boot_inputs(out.boot_inputs); out.initial_page = 8;
    }
    if (out.play_smoke && (!out.play || out.smoke_frames < 120))
        throw std::runtime_error("--play-smoke requires --play and --smoke-test of at least 120 frames.");
    if (out.play && (scene_requested || page_requested || !out.resource.empty() || out.entry || out.mesh || out.rig ||
        texture_requested || mesh_mode_requested || entry_limit_requested || !boot_options.empty() || !player_options.empty()))
        throw std::runtime_error("--play uses its verified original scene profile and cannot be combined with viewer, page or boot options.");
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

const char* mesh_mode_name(dw4::viewer::MeshDrawMode mode) {
    if (mode == dw4::viewer::MeshDrawMode::textured) return "textured";
    if (mode == dw4::viewer::MeshDrawMode::wireframe) return "wireframe";
    return "solid";
}

dw4::viewer::MeshTextureWrap texture_wrap(dw4::MaterialWrap wrap) {
    switch (wrap) {
    case dw4::MaterialWrap::repeat: return dw4::viewer::MeshTextureWrap::repeat;
    case dw4::MaterialWrap::clamp_to_edge: return dw4::viewer::MeshTextureWrap::clamp_to_edge;
    case dw4::MaterialWrap::mirrored_repeat: return dw4::viewer::MeshTextureWrap::mirrored_repeat;
    }
    throw std::runtime_error("Unsupported material wrap mode.");
}

dw4::viewer::MeshTextureFilter texture_filter(dw4::MaterialFilter filter) {
    switch (filter) {
    case dw4::MaterialFilter::nearest: return dw4::viewer::MeshTextureFilter::nearest;
    case dw4::MaterialFilter::linear: return dw4::viewer::MeshTextureFilter::linear;
    }
    throw std::runtime_error("Unsupported material filter mode.");
}

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

struct ScenePlayerState {
    std::optional<dw4::IgbDocument> document;
    std::vector<dw4::PlayerModelInfo> models;
    std::optional<dw4::PlayerVisualData> visual;
    std::optional<dw4::PlayerPlacement> placement;
    std::vector<dw4::AnimationInfo> clips;
    std::optional<dw4::AnimationClip> clip;
    dw4::MeshData mesh;
    std::optional<dw4::MaterialInfo> material;
    std::optional<dw4::RgbaImage> image;
    std::string resource, error;
    bool playing = false, loop = true, uploaded = false;
    double time = 0;
    std::uint64_t updates = 0;
    std::chrono::steady_clock::time_point tick = std::chrono::steady_clock::now();
};

dw4::MeshBounds position_bounds(std::span<const dw4::Vec3> positions) {
    if (positions.empty()) throw std::runtime_error("Player geometry is empty.");
    dw4::MeshBounds bounds{positions.front(),positions.front()};
    for (const auto& p : positions) {
        bounds.min.x = std::min(bounds.min.x,p.x); bounds.max.x = std::max(bounds.max.x,p.x);
        bounds.min.y = std::min(bounds.min.y,p.y); bounds.max.y = std::max(bounds.max.y,p.y);
        bounds.min.z = std::min(bounds.min.z,p.z); bounds.max.z = std::max(bounds.max.z,p.z);
    }
    return bounds;
}

class Workbench {
public:
    explicit Workbench(Options options) : options_(std::move(options)) {}
    ~Workbench() {
        scene_renderer_.shutdown();
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
            if (options_.play_smoke) inject_play_smoke(frames);
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                handle_play_event(event);
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
            advance_animation_clock();
            advance_scene_player_clock();
            advance_play();
            draw();
            if (quit_requested_) running = false;
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
            if (options_.play) {
                if (!play_active_ || !play_world_ || !scene_player_.uploaded || !play_error_.empty() ||
                    !scene_player_.error.empty() || !scene_renderer_.has_texture() || scene_renderer_.rendered_frames() == 0) {
                    std::cerr << "Walking preview failed: " << play_error_ << '\n'; return 3;
                }
                const auto p = play_walker_.position();
                std::cout << "Native walking preview: original dng101b1/BG02, Agumon, floor info 1768\n"
                    << "Walking floor triangles: " << play_world_->triangle_count() << " / wall triangles " << play_world_->wall_triangle_count()
                    << " / position " << p.x << ',' << p.y << ',' << p.z << '\n'
                    << "Walking simulation: " << play_steps_ << " steps / distance " << play_distance_ << " / idle " << play_idle_samples_
                    << " / run " << play_run_samples_ << " / pose uploads " << scene_player_.updates << '\n'
                    << "Walking render: " << scene_target_width_ << 'x' << scene_target_height_ << " / " << scene_renderer_.samples() << " samples\n";
                if (options_.play_smoke) {
                    std::cout << "SDL keyboard replay: pause " << play_pause_verified_ << " / restart " << play_restart_verified_ << " / focus loss " << play_focus_verified_ << '\n';
                    if (play_distance_ < 20 || !play_run_samples_ || !play_idle_samples_ || !play_pause_verified_ || !play_restart_verified_ || !play_focus_verified_ ||
                        std::hypot(p.x-play_spawn_.matrix[12],p.z-play_spawn_.matrix[14]) < 1) {
                        std::cerr << "Walking input replay did not satisfy movement, idle/run, pause and restart checks.\n"; return 3;
                    }
                }
            }
            if (!options_.boot_package.empty()) {
                if (!boot_session_ || !boot_session_->files_loaded || !boot_session_->info_routing_complete ||
                    !boot_report_ || !boot_report_->boot_scripts_completed || !boot_report_->resources_resolved || !boot_error_.empty()) {
                    std::cerr << "Startup loading failed: " << (!boot_error_.empty() ? boot_error_ : boot_session_ ? boot_session_->diagnostic : "No session.") << '\n';
                    return 3;
                }
                std::cout << "Original boot scripts executed: " << boot_report_->scripts.size() << " / " << boot_report_->instructions
                    << " instructions / " << boot_report_->host_calls << " native calls / " << boot_report_->resources.size() << " unique common resources\n"
                    << "Boot folder: " << boot_report_->folder << " / explicit difficulty " << boot_report_->inputs.difficulty
                    << " / camera " << boot_report_->inputs.camera_mode << '\n'
                    << "Native startup payloads loaded: " << boot_session_->files.size() << " files / " << boot_session_->total_bytes
                    << " bytes / " << boot_export_count() << " exported info occurrences\n"
                    << "Info routing: " << boot_session_->infos.lua_infos.size() << " immediate Lua / "
                    << boot_session_->infos.held_scenes.size() << " held scenes / " << boot_session_->infos.deferred_infos.size()
                    << " deferred candidates (including " << boot_session_->infos.late_scenes.size() << " late scenes)\n"
                    << "Next native barrier: " << boot_session_->next_barrier << '\n';
            }
            if (!options_.package.empty()) {
                if (!scene_package_ || !scene_package_error_.empty()) {
                    std::cerr << "Package inspection failed: " << (scene_package_error_.empty() ? "No package descriptor was decoded." : scene_package_error_) << '\n';
                    return 3;
                }
                std::cout << "Package: " << scene_package_->logical_path << " / descriptor " << scene_package_->reference_index
                          << " / " << scene_package_->members.size() << " original members\n"
                          << "Package provenance: " << scene_package_->provenance << '\n';
                if (options_.package_member) {
                    const auto* member = selected_package_member();
                    if (!member || !scene_document_) {
                        std::cerr << "Package member inspection failed: no selected member document was decoded.\n";
                        return 3;
                    }
                    std::cout << "Package member " << member->index << ": " << member->specifier << " / file reference " << member->file_reference
                              << " / " << scene_package_exports_.size() << " original exports\n"
                              << "Member provenance: " << member->provenance << '\n';
                } else {
                    for (std::size_t i = 0; i < std::min<std::size_t>(64,scene_package_->members.size()); ++i) {
                        const auto& member = scene_package_->members[i];
                        std::cout << "Member " << member.index << ": " << member.specifier << " / "
                                  << (member.supported ? "supported" : member.diagnostic) << '\n';
                    }
                    if (scene_package_->members.size() > 64) std::cout << "Additional members remain available in the package selector.\n";
                    std::cout << "Descriptor inspection only; no member or scene was automatically selected or rendered.\n";
                }
            }
            if ((options_.entry || !options_.resource.empty() || options_.package_member) && options_.initial_page == 6) {
                if (options_.scene_mode == dw4::viewer::MeshDrawMode::textured && !scene_texture_error_.empty()) {
                    std::cerr << "Textured scene unavailable: " << scene_texture_error_ << '\n';
                    return 3;
                }
                if (!scene_data_ || !scene_data_->complete || scene_data_->instances.empty() ||
                    !scene_renderer_.has_mesh() || scene_render_failed_ || !scene_error_.empty() ||
                    scene_renderer_.rendered_frames() <= scene_render_start_frame_) {
                    std::cerr << "Static scene inspection failed: " << (scene_error_.empty() ?
                        "No complete static scene was uploaded and rendered." : scene_error_) << '\n';
                    if (scene_data_) for (std::size_t i = 0; i < std::min<std::size_t>(8,scene_data_->diagnostics.size()); ++i) {
                        const auto& diagnostic = scene_data_->diagnostics[i];
                        std::cerr << "Scene node " << diagnostic.node_reference << ": " << diagnostic.reason << '\n';
                    }
                    return 3;
                }
                if (options_.scene_mode == dw4::viewer::MeshDrawMode::textured &&
                    (scene_renderer_.textured_batch_count() != scene_data_->instances.size() + (scene_player_.uploaded ? 1u : 0u) || !scene_texture_error_.empty())) {
                    std::cerr << "Textured scene unavailable: " << (scene_texture_error_.empty() ?
                        "Not every scene instance has a verified GPU texture." : scene_texture_error_) << '\n';
                    return 3;
                }
                std::cout << "Static scene: " << scene_source_ << " / root index " << selected_scene_
                          << " / " << scene_data_->instances.size() << " instances / " << scene_data_->triangle_count << " triangles\n"
                          << "Scene export: " << scene_display_name(scene_data_->info) << " / info " << scene_data_->info.info_reference
                          << " / root " << scene_data_->info.root_reference << " / " << scene_data_->info.root_type << '\n'
                          << "Scene traversal: " << scene_data_->visited_nodes << " nodes / " << scene_data_->transform_nodes
                          << " transforms / " << scene_data_->vertex_count << " placed vertices\n"
                          << "Scene render: " << scene_target_width_ << 'x' << scene_target_height_ << " / "
                          << mesh_mode_name(scene_mode_) << " / " << scene_renderer_.samples() << " samples\n"
                          << "Scene GPU batches: " << scene_renderer_.batch_count() << " / textured " << scene_renderer_.textured_batch_count()
                          << " / unique textures " << scene_renderer_.texture_count() << '\n';
                if (!loaded_scene_resource_.empty()) std::cout << "Logical resource: " << loaded_scene_resource_ << '\n';
                if (scene_data_->root_control) std::cout << "Original switch command: "
                    << (scene_data_->root_control->command == dw4::SwitchControlCommand::all_on ? "ALLON" : "ALLOF")
                    << " / controller " << scene_data_->root_control->controller_reference << ". Game startup is not running.\n";
                else if (scene_data_->inspected_root_child) std::cout << "Inspection override: root child " << *scene_data_->inspected_root_child
                    << "; runtime activation unverified.\n";
                else std::cout << "Stored root selection; no activation override.\n";
                std::cout << "Static exported-root inspection only. Gameplay and original-frame comparison remain pending.\n";
                if (!options_.scene_player_resource.empty()) {
                    if (!scene_player_.visual || !scene_player_.placement || !scene_player_.uploaded || !scene_player_.error.empty()) {
                        std::cerr << "Scene player inspection failed: " << scene_player_.error << '\n'; return 3;
                    }
                    if (options_.scene_player_play && scene_player_.updates <= 2) {
                        std::cerr << "Scene player playback did not upload a pose after placement and initial clip selection.\n"; return 3;
                    }
                    const auto& visual = scene_player_.visual->info;
                    const auto& point = *scene_player_.placement;
                    std::cout << "Player visual: " << scene_player_.resource << " / model " << visual.request.model_reference
                        << " / slot " << visual.request.instance_index << " / branch " << visual.request.branch_index
                        << " / actor " << visual.actor_reference << " / geometry " << visual.geometry_reference << '\n'
                        << "Player placement: info " << point.info_reference << " / group " << point.group_index
                        << " / player " << point.player_index << " / transform " << point.transform_reference
                        << " / position " << point.matrix[12] << ',' << point.matrix[13] << ',' << point.matrix[14] << '\n'
                        << "Player pose: " << (scene_player_.clip ? scene_player_.clip->info.name : "stored skin vertices")
                        << " / time " << std::setprecision(9) << scene_player_.time << " / updates " << scene_player_.updates
                        << " / batch " << scene_data_->instances.size() << '\n'
                        << "Explicit player visual placement; startup, input, collision and combat are not running.\n";
                }
            } else if ((options_.entry || !options_.resource.empty()) && options_.initial_page == 5) {
                if (!options_.animation.empty() && (!mesh_animation_ || !mesh_animation_uploaded_ || !mesh_animation_error_.empty())) {
                    const auto reason = !mesh_animation_error_.empty() ? mesh_animation_error_ :
                        !mesh_rig_error_.empty() ? mesh_rig_error_ : !mesh_error_.empty() ? mesh_error_ : "No decoded animation pose was uploaded.";
                    std::cerr << "Animation inspection failed: " << reason << '\n';
                    return 3;
                }
                if (options_.animation_play && mesh_animation_upload_count_ < 2) {
                    std::cerr << "Animation playback failed: no additional pose was sampled and uploaded.\n";
                    return 3;
                }
                if (options_.rig && (!mesh_skin_ || !mesh_pose_ || !mesh_pose_uploaded_ || !mesh_rig_error_.empty() ||
                    mesh_renderer_.rendered_frames() <= mesh_pose_start_frame_)) {
                    const auto reason = !mesh_rig_error_.empty() ? mesh_rig_error_ :
                        !mesh_error_.empty() ? mesh_error_ : "No evaluated pose was uploaded and rendered.";
                    std::cerr << "Rig inspection failed: " << reason << '\n';
                    return 3;
                }
                if (options_.mesh_mode == dw4::viewer::MeshDrawMode::textured && !mesh_renderer_.has_texture()) {
                    const auto reason = !mesh_material_error_.empty() ? mesh_material_error_ :
                        !mesh_error_.empty() ? mesh_error_ : "No verified material binding and GPU texture are available.";
                    std::cerr << "Textured mesh unavailable: " << reason << '\n';
                    return 3;
                }
                if (!mesh_renderer_.has_mesh() || mesh_render_failed_ || mesh_renderer_.rendered_frames() <= mesh_render_start_frame_) {
                    std::cerr << "Mesh upload/render failed: " << (mesh_error_.empty() ? "No geometry frame rendered." : mesh_error_) << '\n';
                    return 3;
                }
                const auto& mesh = meshes_.at(static_cast<std::size_t>(selected_mesh_));
                std::cout << "Mesh: " << mesh_source_ << " / mesh " << selected_mesh_ << " / reference " << mesh.reference_index
                          << " / " << mesh_data_->positions.size() << " vertices / " << mesh_renderer_.triangle_count() << " triangles\n"
                          << "Mesh render: " << mesh_target_width_ << 'x' << mesh_target_height_ << " / "
                          << mesh_mode_name(mesh_mode_) << " / " << mesh_renderer_.samples() << " samples\n";
                if (!loaded_mesh_resource_.empty()) std::cout << "Logical resource: " << loaded_mesh_resource_ << '\n';
                if (mesh_mode_ == dw4::viewer::MeshDrawMode::textured && mesh_material_) {
                    std::cout << "Bound image: " << mesh_material_->image_name << " / reference " << *mesh_material_->image_reference
                              << " / " << mesh_texture_width_ << 'x' << mesh_texture_height_ << " / " << mesh_data_->texcoords.size() << " UV pairs\n"
                              << "Material provenance: " << mesh_material_->provenance << '\n';
                }
                if (options_.rig && mesh_skin_) {
                    const auto& skin = *mesh_skin_;
                    std::cout << "Rig: " << skin.info.skeleton_name << " / skeleton " << *skin.info.skeleton_reference
                              << " / " << skin.bones.size() << " bones / " << skin.joints.size() << " joints / " << skin.influences.size() << " influences\n"
                              << "Rig provenance: " << skin.info.provenance << '\n'
                              << "Pose inspection: " << (mesh_animation_mode_ && mesh_animation_uploaded_ ? "decoded animation" : manual_pose_active() ? "manual joint delta" : "inverse-bind reconstruction")
                              << " / changed vertices " << mesh_pose_changed_vertices_ << " / max displacement " << mesh_pose_max_displacement_ << '\n';
                    if (options_.bone) std::cout << "Manual bone: " << selected_bone_ << " / " << skin.bones.at(static_cast<std::size_t>(selected_bone_)).name
                        << " / XYZ degrees " << mesh_rotation_.x << ',' << mesh_rotation_.y << ',' << mesh_rotation_.z << '\n';
                    if (!options_.animation.empty() && mesh_animation_) {
                        const auto& animation = mesh_animation_->info;
                        std::cout << "Animation: " << animation.name << " / reference " << animation.reference_index << " / source " << animation.source_reference
                            << " / " << animation.track_count << " tracks / time " << std::setprecision(9) << mesh_animation_time_
                            << " / duration " << animation.duration_seconds << " seconds\n"
                            << "Animation provenance: " << animation.provenance << '\n';
                        std::cout << "Animation pose uploads: " << mesh_animation_upload_count_ << " / "
                            << (mesh_animation_playing_ ? "playing" : "paused") << '\n';
                    }
                    std::cout << "Animation inspection has not been compared to a recorded game frame.\n";
                }
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
    bool play_active_ = false, play_paused_ = false, quit_requested_ = false;
    bool play_pad_start_ = false, play_running_clip_ = false;
    std::array<bool, SDL_SCANCODE_COUNT> play_keys_{};
    std::optional<dw4::WalkingWorld> play_world_;
    dw4::Walker play_walker_;
    dw4::PlayerPlacement play_spawn_;
    dw4::Vec3 play_grounded_spawn_{};
    float play_spawn_facing_ = 0;
    std::optional<dw4::AnimationClip> play_idle_clip_, play_run_clip_;
    std::string play_error_;
    double play_accumulator_ = 0, play_distance_ = 0;
    std::uint64_t play_steps_ = 0, play_run_samples_ = 0, play_idle_samples_ = 0;
    float play_zoom_ = 1;
    bool play_pause_verified_ = false, play_restart_verified_ = false, play_focus_verified_ = false;
    dw4::Vec3 play_pause_position_{};
    double play_pause_time_ = 0;
    std::chrono::steady_clock::time_point play_tick_ = std::chrono::steady_clock::now();
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
    dw4::viewer::MeshDrawMode mesh_mode_ = dw4::viewer::MeshDrawMode::solid;
    std::string mesh_material_error_;
    std::optional<dw4::MaterialInfo> mesh_material_;
    std::uint32_t mesh_texture_width_ = 0;
    std::uint32_t mesh_texture_height_ = 0;
    bool mesh_render_failed_ = false;
    bool mesh_scroll_pending_ = false;
    bool mesh_rig_enabled_ = false;
    bool mesh_skeleton_overlay_ = true;
    bool mesh_pose_uploaded_ = false;
    bool mesh_rig_options_consumed_ = false;
    bool mesh_rig_tab_pending_ = false;
    std::optional<dw4::SkinningData> mesh_skin_;
    std::optional<dw4::SkinPose> mesh_pose_;
    std::string mesh_rig_error_;
    int selected_bone_ = -1;
    dw4::Vec3 mesh_rotation_{};
    std::uint64_t mesh_pose_start_frame_ = 0;
    std::size_t mesh_pose_changed_vertices_ = 0;
    double mesh_pose_max_displacement_ = 0;
    bool mesh_animation_mode_ = false;
    bool mesh_animation_catalog_ready_ = false;
    bool mesh_animation_catalog_attempted_ = false;
    bool mesh_animation_uploaded_ = false;
    bool mesh_animation_playing_ = false;
    bool mesh_animation_loop_ = true;
    std::vector<dw4::AnimationInfo> mesh_animations_;
    std::optional<dw4::AnimationClip> mesh_animation_;
    std::string mesh_animation_error_;
    int selected_animation_ = -1;
    double mesh_animation_time_ = 0;
    std::uint64_t mesh_animation_upload_count_ = 0;
    std::chrono::steady_clock::time_point mesh_animation_tick_ = std::chrono::steady_clock::now();
    dw4::viewer::MeshRenderer scene_renderer_;
    dw4::OrbitCamera scene_camera_;
    bool scene_package_mode_ = false;
    std::optional<dw4::PackageDescriptor> scene_package_;
    std::vector<dw4::PackageExport> scene_package_exports_;
    std::string scene_package_path_;
    std::string scene_package_error_;
    int selected_package_member_ = -1;
    std::optional<dw4::assets::AfsArchive> scene_archive_;
    std::optional<dw4::IgbDocument> scene_document_;
    std::optional<dw4::SceneData> scene_data_;
    std::vector<dw4::SceneInfo> scenes_;
    std::vector<dw4::MaterialInfo> scene_materials_;
    std::array<char,256> scene_resource_filter_{};
    std::string loaded_scene_resource_;
    std::string scene_source_;
    std::string scene_error_;
    std::string scene_texture_error_;
    int selected_scene_archive_ = -1;
    int selected_scene_ = -1;
    int selected_scene_instance_ = -1;
    int scene_entry_input_ = 0;
    int scene_entry_limit_mib_ = 64;
    int scene_target_width_ = 0;
    int scene_target_height_ = 0;
    std::uint64_t scene_render_start_frame_ = 0;
    bool scene_render_failed_ = false;
    bool scene_fit_pending_ = true;
    bool scene_status_tab_pending_ = false;
    bool scene_child_enabled_ = false;
    int scene_child_input_ = 0;
    std::vector<dw4::SwitchControlInfo> scene_switch_controls_;
    int selected_scene_switch_control_ = -1;
    std::string scene_switch_error_;
    bool scene_state_tab_pending_ = false;
    ScenePlayerState scene_player_;
    std::array<char,256> scene_player_resource_input_{};
    std::vector<dw4::PlayerPlacementInfo> scene_player_points_;
    int scene_player_model_input_ = -1, scene_player_slot_input_ = 0, scene_player_branch_input_ = 0;
    int scene_player_point_input_ = -1, scene_player_group_input_ = 0;
    bool scene_player_tab_pending_ = false, scene_player_focus_pending_ = false;
    dw4::viewer::MeshDrawMode scene_mode_ = dw4::viewer::MeshDrawMode::solid;
    std::vector<SDL_Gamepad*> gamepads_;
    std::optional<dw4::StartupSession> boot_session_;
    std::optional<dw4::BootReport> boot_report_;
    std::string boot_error_;
    int boot_tab_to_select_ = -1;
    int boot_route_view_ = 0;
    std::array<char,256> boot_package_input_{};
    dw4::BootInputs boot_inputs_;
    std::array<std::array<char,129>,4> boot_player_names_{};
    std::array<std::array<std::array<char,129>,3>,4> boot_weapon_names_{};

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
        mesh_mode_ = options_.mesh_mode;
        scene_entry_limit_mib_ = options_.max_entry_mib;
        scene_mode_ = options_.scene_mode;
        scene_package_mode_ = !options_.package.empty();
        mesh_rig_enabled_ = options_.rig;
        mesh_animation_mode_ = !options_.animation.empty();
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
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
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
        std::optional<dw4::SwitchControlRequest> scene_command;
        if (options_.scene_switch_control) scene_command = dw4::SwitchControlRequest{
            static_cast<std::uint32_t>(*options_.scene_switch_control),*options_.scene_switch_command};
        if (!options_.package.empty()) {
            load_scene_package(options_.package);
            if (options_.package_member && scene_package_) select_package_member(*options_.package_member,options_.scene_root,options_.scene_inspect_child,scene_command);
        }
        else if (options_.initial_page == 6 && !options_.resource.empty()) load_scene_resource(options_.resource, options_.scene_root.value_or(0),options_.scene_inspect_child,scene_command);
        else if (options_.initial_page == 6 && options_.entry) {
            bool found = false;
            if (disc_) for (std::size_t i = 0; i < disc_->archives.size(); ++i) {
                if (filename(disc_->archives[i].path) == options_.archive_name) {
                    select_scene_archive(static_cast<int>(i));
                    found = true;
                    break;
                }
            }
            if (!found) scene_error_ = "Archive was not found in the inspected game directory: " + options_.archive_name;
            else { scene_entry_input_ = *options_.entry; load_scene_entry(*options_.entry, options_.scene_root.value_or(0),options_.scene_inspect_child,scene_command); }
        }
        else if (options_.initial_page == 5 && !options_.resource.empty()) load_mesh_resource(options_.resource, options_.mesh.value_or(0));
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
        if (!options_.scene_player_resource.empty()) {
            std::snprintf(scene_player_resource_input_.data(),scene_player_resource_input_.size(),"%s",options_.scene_player_resource.c_str());
            load_scene_player_resource(options_.scene_player_resource);
            if (scene_player_.document && scene_player_.error.empty()) {
                scene_player_model_input_ = *options_.scene_player_model;
                scene_player_slot_input_ = *options_.scene_player_slot;
                scene_player_branch_input_ = *options_.scene_player_branch;
                scene_player_point_input_ = *options_.scene_player_point;
                scene_player_group_input_ = *options_.scene_player_group;
                place_scene_player();
                if (scene_player_.uploaded && !options_.scene_player_animation.empty()) {
                    int found = -1;
                    for (std::size_t i = 0; i < scene_player_.clips.size(); ++i) {
                        if (scene_player_.clips[i].name != options_.scene_player_animation) continue;
                        if (found >= 0) { found = -2; break; }
                        found = static_cast<int>(i);
                    }
                    if (found < 0) scene_player_.error = "Player animation name is missing or ambiguous: " + options_.scene_player_animation;
                    else select_scene_player_animation(found,options_.scene_player_time.value_or(0));
                    scene_player_.playing = options_.scene_player_play && scene_player_.error.empty();
                }
                scene_state_tab_pending_ = scene_status_tab_pending_ = false;
                scene_player_tab_pending_ = true;
            }
        }
        if (!options_.boot_package.empty()) {
            std::snprintf(boot_package_input_.data(),boot_package_input_.size(),"%s",options_.boot_package.c_str());
            boot_inputs_ = options_.boot_inputs;
            for (std::size_t p = 0; p < 4; ++p) {
                std::snprintf(boot_player_names_[p].data(),boot_player_names_[p].size(),"%s",boot_inputs_.players[p].model_name.c_str());
                for (std::size_t s = 0; s < 3; ++s) if (boot_inputs_.players[p].weapon_names[s])
                    std::snprintf(boot_weapon_names_[p][s].data(),boot_weapon_names_[p][s].size(),"%s",boot_inputs_.players[p].weapon_names[s]->c_str());
            }
            run_boot_inspection(options_.boot_package);
        }
        if (options_.play) start_play();
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
        clear_boot_inspection();
        clear_texture_document();
        clear_mesh_document();
        clear_scene_document();
        clear_scene_package();
        scene_archive_.reset();
        selected_scene_archive_ = -1;
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
            if (!disc_->archives.empty()) select_scene_archive(0);
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

    void reset_scene_camera() {
        scene_camera_.reset();
        // A higher inspection angle makes broad static layouts easier to read.
        // This remains a host camera; it does not reproduce the game camera.
        scene_camera_.orbit(0,0.15f);
        scene_camera_.zoom(6);
        scene_fit_pending_ = true;
    }

    void fit_scene_bounds(float aspect) {
        if (!scene_data_ || !scene_renderer_.has_mesh()) return;
        const auto& bounds = scene_data_->bounds;
        auto projected_bounds = [&](const dw4::OrbitCamera& camera) -> std::optional<std::array<float,4>> {
            std::array<float,4> rectangle{1,1,0,0};
            for (int corner = 0; corner < 8; ++corner) {
                const dw4::Vec3 point{corner & 1 ? bounds.max.x : bounds.min.x,
                    corner & 2 ? bounds.max.y : bounds.min.y,corner & 4 ? bounds.max.z : bounds.min.z};
                const auto projected = scene_renderer_.project_to_canvas(point,camera,aspect);
                if (!projected) return std::nullopt;
                rectangle[0] = std::min(rectangle[0],projected->x); rectangle[1] = std::min(rectangle[1],projected->y);
                rectangle[2] = std::max(rectangle[2],projected->x); rectangle[3] = std::max(rectangle[3],projected->y);
            }
            return rectangle;
        };
        auto fits = [&](const dw4::OrbitCamera& camera) {
            const auto rectangle = projected_bounds(camera);
            return rectangle && (*rectangle)[0] >= 0.025f && (*rectangle)[1] >= 0.025f && (*rectangle)[2] <= 0.975f && (*rectangle)[3] <= 0.975f;
        };
        auto center = [&](dw4::OrbitCamera& camera) {
            for (int pass = 0; pass < 4; ++pass) {
                const auto rectangle = projected_bounds(camera);
                if (!rectangle) break;
                auto candidate = camera;
                candidate.pan((((*rectangle)[0]+(*rectangle)[2])*0.5f-0.5f)*aspect,
                    0.5f-((*rectangle)[1]+(*rectangle)[3])*0.5f);
                if (!fits(candidate)) break;
                camera = candidate;
            }
        };
        const float nearest_distance = scene_camera_.distance();
        for (int step = 0; step < 40 && !fits(scene_camera_); ++step)
            scene_camera_.zoom(-0.25f);
        if (!fits(scene_camera_)) throw std::runtime_error("The scene inspection camera could not fit the original bounds.");
        center(scene_camera_);
        for (int step = 0; step < 80; ++step) {
            auto candidate = scene_camera_;
            candidate.zoom(0.125f);
            if (candidate.distance() < nearest_distance || !fits(candidate)) break;
            center(candidate);
            scene_camera_ = candidate;
        }
        scene_fit_pending_ = false;
    }

    std::string scene_node_name(std::uint32_t reference) const {
        try {
            if (scene_document_) {
                const auto& node = scene_document_->object(reference);
                if (scene_document_->is_a(node,"igNode")) return std::string(scene_document_->field_string(node,2));
            }
        } catch (const std::exception&) { /* A missing display name does not change graph identity. */ }
        return {};
    }

    std::string scene_display_name(const dw4::SceneInfo& info) const {
        if (!info.name.empty()) return info.name;
        const auto name = scene_node_name(info.root_reference);
        return name.empty() ? "Root " + std::to_string(info.root_reference) : name;
    }

    void clear_scene_document() {
        clear_scene_player();
        scene_renderer_.clear();
        scene_document_.reset();
        scene_package_exports_.clear();
        scene_data_.reset();
        scenes_.clear();
        scene_materials_.clear();
        loaded_scene_resource_.clear();
        scene_source_.clear();
        scene_error_.clear();
        scene_texture_error_.clear();
        selected_scene_ = selected_scene_instance_ = -1;
        scene_target_width_ = scene_target_height_ = 0;
        scene_render_failed_ = false;
        scene_status_tab_pending_ = false;
        scene_child_enabled_ = false;
        scene_child_input_ = 0;
        scene_switch_controls_.clear();
        selected_scene_switch_control_ = -1;
        scene_switch_error_.clear();
        scene_state_tab_pending_ = false;
        reset_scene_camera();
    }

    void clear_scene_package() {
        scene_package_.reset();
        scene_package_exports_.clear();
        scene_package_path_.clear();
        scene_package_error_.clear();
        selected_package_member_ = -1;
    }

    const dw4::PackageMember* selected_package_member() const {
        if (!scene_package_ || selected_package_member_ < 0) return nullptr;
        for (const auto& member : scene_package_->members)
            if (member.index == static_cast<std::uint32_t>(selected_package_member_)) return &member;
        return nullptr;
    }

    std::string package_member_source(const dw4::PackageMember& member) const {
        if (!member.location || !mounted_resources_) return "Unresolved archive location";
        const auto& archives = mounted_resources_->catalog().archives;
        if (member.location->archive_index >= archives.size()) return "Invalid archive index";
        return archives[member.location->archive_index].filename + " / entry " + std::to_string(member.location->entry_index);
    }

    void load_scene_package(const std::string& logical_path) {
        clear_scene_document();
        clear_scene_package();
        scene_archive_.reset();
        selected_scene_archive_ = -1;
        scene_package_mode_ = true;
        scene_package_path_ = logical_path;
        try {
            if (!mounted_resources_) throw std::runtime_error("The logical resource catalog is unavailable: " + catalog_error_);
            scene_entry_limit_mib_ = std::clamp(scene_entry_limit_mib_,1,256);
            scene_package_ = dw4::load_package_descriptor(*mounted_resources_,logical_path,
                static_cast<std::size_t>(scene_entry_limit_mib_) * 1024u * 1024u);
        } catch (const std::exception& error) { scene_package_error_ = error.what(); }
    }

    void select_package_member(int index, std::optional<int> root_index = std::nullopt,
        std::optional<int> inspect_child = std::nullopt, std::optional<dw4::SwitchControlRequest> control = std::nullopt) {
        clear_scene_document();
        scene_package_error_.clear();
        selected_package_member_ = index;
        try {
            if (!scene_package_ || !mounted_resources_) throw std::runtime_error("Choose a decoded package descriptor first.");
            const auto* member = selected_package_member();
            if (!member) throw std::runtime_error("Package member index is outside the original filename list.");
            if (!member->supported) throw std::runtime_error(member->diagnostic.empty() ? "This package member form is unsupported." : member->diagnostic);
            scene_entry_limit_mib_ = std::clamp(scene_entry_limit_mib_,1,256);
            scene_document_ = dw4::read_package_member(*mounted_resources_,*member,
                static_cast<std::size_t>(scene_entry_limit_mib_) * 1024u * 1024u);
            scene_source_ = package_member_source(*member);
            scene_package_exports_ = dw4::list_package_exports(*scene_document_);
            const auto candidates = dw4::list_igb_scenes(*scene_document_);
            // Package scene choices follow explicit export references in source
            // order. Unexported descriptors never become a guessed scene root.
            for (const auto& exported : scene_package_exports_) {
                const auto found = std::find_if(candidates.begin(),candidates.end(),[&](const auto& info) { return info.info_reference == exported.reference_index; });
                if (found != candidates.end()) scenes_.push_back(*found);
            }
            if (scenes_.empty()) scene_error_ = "This member has no igSceneInfo export in its original info list.";
            else if (root_index) select_scene(*root_index,inspect_child,control);
        } catch (const std::exception& error) { scene_package_error_ = error.what(); }
    }

    void select_scene_archive(int index) {
        clear_scene_document();
        scene_archive_.reset();
        selected_scene_archive_ = index;
        try { scene_archive_ = dw4::assets::AfsArchive::open(disc_->archives.at(static_cast<std::size_t>(index)).path); }
        catch (const std::exception& error) { scene_error_ = error.what(); }
    }

    void load_scene_entry(int entry, int root_index = 0, std::optional<int> inspect_child = std::nullopt,
        std::optional<dw4::SwitchControlRequest> control = std::nullopt) {
        clear_scene_package();
        scene_package_mode_ = false;
        clear_scene_document();
        try {
            if (!scene_archive_) throw std::runtime_error("Choose a valid AFS archive first.");
            if (entry < 0 || static_cast<std::size_t>(entry) >= scene_archive_->entries().size())
                throw std::runtime_error("Entry index is outside this archive.");
            scene_entry_limit_mib_ = std::clamp(scene_entry_limit_mib_,1,256);
            auto bytes = scene_archive_->read(static_cast<std::size_t>(entry),static_cast<std::size_t>(scene_entry_limit_mib_) * 1024u * 1024u);
            scene_document_ = dw4::read_igb_document(std::move(bytes));
            scene_source_ = filename(scene_archive_->path()) + " / entry " + std::to_string(entry);
            scenes_ = dw4::list_igb_scenes(*scene_document_);
            if (scenes_.empty()) throw std::runtime_error("This IGB document contains no exported scene roots.");
            select_scene(root_index,inspect_child,control);
        } catch (const std::exception& error) { scene_error_ = error.what(); }
    }

    void load_scene_resource(const std::string& logical_path, int root_index = 0, std::optional<int> inspect_child = std::nullopt,
        std::optional<dw4::SwitchControlRequest> control = std::nullopt) {
        clear_scene_package();
        scene_package_mode_ = false;
        clear_scene_document();
        try {
            if (!mounted_resources_) throw std::runtime_error("The logical resource catalog is unavailable: " + catalog_error_);
            const auto& catalog = mounted_resources_->catalog();
            const auto* resource = catalog.find(logical_path);
            if (!resource) throw std::runtime_error("Resource was not found in the catalog: " + logical_path);
            if (!resource->location) throw std::runtime_error("This catalog resource has no readable AFS entry.");
            const auto& source_archive = catalog.archives.at(resource->location->archive_index).filename;
            int archive_index = -1;
            if (disc_) for (std::size_t i = 0; i < disc_->archives.size(); ++i)
                if (filename(disc_->archives[i].path) == source_archive) archive_index = static_cast<int>(i);
            if (archive_index < 0) throw std::runtime_error("The catalog archive is not present in the inspected directory: " + source_archive);
            select_scene_archive(archive_index);
            scene_entry_input_ = static_cast<int>(resource->location->entry_index);
            load_scene_entry(scene_entry_input_,root_index,inspect_child,control);
            if (scene_document_) loaded_scene_resource_ = logical_path;
        } catch (const std::exception& error) { scene_error_ = error.what(); }
    }

    void upload_scene_batches(bool textured) {
        scene_player_.uploaded = false;
        try {
            if (!scene_data_ || !scene_data_->complete || scene_data_->instances.empty() || !scene_document_)
                throw std::runtime_error("A complete, nonempty static scene is required for rendering.");
            std::vector<dw4::viewer::MeshBatch> batches;
            std::map<std::uint32_t,dw4::RgbaImage> images;
            std::size_t image_bytes = 0;
            batches.reserve(scene_data_->instances.size()+1);
            for (std::size_t i = 0; i < scene_data_->instances.size(); ++i) {
                auto& instance = scene_data_->instances[i];
                dw4::viewer::MeshBatch batch;
                batch.mesh = &instance.mesh;
                if (textured) {
                    const auto& material = scene_materials_.at(i);
                    if (!material.supported || !material.image_reference || material.geometry_reference != instance.geometry_reference)
                        throw std::runtime_error("Instance " + std::to_string(i) + " (node " + std::to_string(instance.node_reference) + "): " +
                            (material.diagnostic.empty() ? "No verified base-image binding is available." : material.diagnostic));
                    if (instance.mesh.texcoords.empty()) instance.mesh.texcoords = dw4::decode_igb_texcoords(*scene_document_,instance.geometry_reference);
                    if (instance.mesh.texcoords.size() != instance.mesh.positions.size())
                        throw std::runtime_error("Instance " + std::to_string(i) + " has a mismatched UV count.");
                    const auto image_reference = *material.image_reference;
                    if (!images.contains(image_reference)) {
                        auto image = dw4::decode_igb_texture(*scene_document_,image_reference);
                        constexpr std::size_t image_limit = 256u * 1024u * 1024u;
                        if (image.rgba.size() > image_limit - image_bytes)
                            throw std::runtime_error("The scene's unique decoded images exceed the 256 MiB inspection limit.");
                        image_bytes += image.rgba.size();
                        images.emplace(image_reference,std::move(image));
                    }
                    batch.texture = &images.at(image_reference);
                    batch.texture_options.wrap_u = texture_wrap(material.wrap_s);
                    batch.texture_options.wrap_v = texture_wrap(material.wrap_t);
                    batch.texture_options.min_filter = texture_filter(material.min_filter);
                    batch.texture_options.mag_filter = texture_filter(material.mag_filter);
                }
                batches.push_back(batch);
            }
            const bool has_player = scene_player_.visual && scene_player_.placement;
            if (has_player) {
                dw4::viewer::MeshBatch batch;
                batch.mesh = &scene_player_.mesh;
                if (textured) {
                    if (!scene_player_.document || !scene_player_.material || !scene_player_.material->supported ||
                        !scene_player_.material->image_reference)
                        throw std::runtime_error("Player appearance has no verified base-image material.");
                    const auto& material = *scene_player_.material;
                    if (scene_player_.mesh.texcoords.empty()) scene_player_.mesh.texcoords =
                        dw4::decode_igb_texcoords(*scene_player_.document,scene_player_.visual->info.geometry_reference);
                    if (!scene_player_.image) scene_player_.image = dw4::decode_igb_texture(*scene_player_.document,*material.image_reference);
                    if (scene_player_.image->rgba.size() > 256u*1024u*1024u-image_bytes)
                        throw std::runtime_error("Map and player decoded images exceed the 256 MiB inspection limit.");
                    // Reference numbers are local to each document; never share this image by numeric ID with the map.
                    batch.texture = &*scene_player_.image;
                    batch.texture_options.wrap_u = texture_wrap(material.wrap_s);
                    batch.texture_options.wrap_v = texture_wrap(material.wrap_t);
                    batch.texture_options.min_filter = texture_filter(material.min_filter);
                    batch.texture_options.mag_filter = texture_filter(material.mag_filter);
                }
                batches.push_back(batch);
            }
            scene_renderer_.upload_batches(batches);
            if (scene_renderer_.batch_count() != batches.size() ||
                (textured && scene_renderer_.textured_batch_count() != batches.size()))
                throw std::runtime_error("The renderer did not retain every requested scene instance.");
            scene_player_.uploaded = has_player;
            scene_render_start_frame_ = scene_renderer_.rendered_frames();
            scene_render_failed_ = false;
            scene_error_.clear();
            if (textured) scene_texture_error_.clear();
        } catch (const std::exception& error) {
            scene_renderer_.clear();
            scene_render_failed_ = true;
            scene_player_.playing = false;
            if (textured) scene_texture_error_ = error.what();
            else scene_error_ = error.what();
        }
    }

    void select_scene(int index, std::optional<int> inspect_child = std::nullopt,
        std::optional<dw4::SwitchControlRequest> control = std::nullopt) {
        clear_scene_player();
        scene_renderer_.clear();
        scene_data_.reset();
        scene_materials_.clear();
        scene_error_.clear();
        scene_texture_error_.clear();
        scene_render_failed_ = false;
        scene_target_width_ = scene_target_height_ = 0;
        selected_scene_ = index;
        selected_scene_instance_ = -1;
        scene_status_tab_pending_ = false;
        scene_child_enabled_ = inspect_child.has_value();
        scene_child_input_ = inspect_child.value_or(0);
        scene_switch_controls_.clear();
        scene_switch_error_.clear();
        selected_scene_switch_control_ = control ? static_cast<int>(control->controller_reference) : -1;
        scene_state_tab_pending_ = control.has_value();
        reset_scene_camera();
        try {
            if (!scene_document_) throw std::runtime_error("Load an IGB document before selecting a scene root.");
            if (index < 0 || static_cast<std::size_t>(index) >= scenes_.size())
                throw std::runtime_error("Scene-root index is outside this document's exported root list.");
            const auto& info = scenes_[static_cast<std::size_t>(index)];
            if (!info.supported) throw std::runtime_error(info.diagnostic.empty() ? "This exported scene descriptor is unsupported." : info.diagnostic);
            try { scene_player_points_ = dw4::list_igb_player_placements(*scene_document_); }
            catch (const std::exception& error) { scene_player_.error = error.what(); }
            try { scene_switch_controls_ = dw4::list_igb_switch_controls(*scene_document_); }
            catch (const std::exception& error) { scene_switch_error_ = error.what(); }
            dw4::SceneOptions scene_options;
            if (inspect_child) {
                if (*inspect_child < 0 || *inspect_child > 1000000) throw std::runtime_error("Inspection child index must be from 0 to 1000000.");
                scene_options.inspect_root_child = static_cast<std::uint32_t>(*inspect_child);
            }
            scene_options.root_control = control;
            scene_data_ = dw4::decode_igb_scene(*scene_document_,info.info_reference,scene_options);
            if (!scene_data_->complete) {
                scene_error_ = "This root contains unsupported static behavior. Inspect its diagnostics; partial geometry is not rendered.";
                scene_status_tab_pending_ = true;
                return;
            }
            if (scene_data_->instances.empty()) throw std::runtime_error(control ?
                "This original switch command selects no visible static geometry." : inspect_child ?
                "This inspected child has no visible static geometry." :
                "The stored selection has no visible static geometry. Runtime activation has not been reproduced.");
            selected_scene_instance_ = 0;
            scene_materials_.reserve(scene_data_->instances.size());
            for (const auto& instance : scene_data_->instances) {
                try { scene_materials_.push_back(dw4::resolve_igb_material_on_path(*scene_document_,instance.geometry_reference,instance.path)); }
                catch (const std::exception& error) {
                    dw4::MaterialInfo material;
                    material.geometry_reference = instance.geometry_reference;
                    material.diagnostic = error.what();
                    scene_materials_.push_back(std::move(material));
                }
            }
            upload_scene_batches(false);
            if (scene_mode_ == dw4::viewer::MeshDrawMode::textured && scene_renderer_.has_mesh()) upload_scene_batches(true);
        } catch (const std::exception& error) {
            scene_error_ = error.what();
            scene_status_tab_pending_ = !scene_state_tab_pending_;
        }
    }

    void clear_scene_player() {
        stop_play();
        scene_player_ = {};
        scene_player_points_.clear();
        scene_player_model_input_ = scene_player_point_input_ = -1;
        scene_player_focus_pending_ = scene_player_tab_pending_ = false;
    }

    void remove_scene_player() {
        stop_play();
        scene_player_.playing = scene_player_.uploaded = false;
        scene_player_.visual.reset(); scene_player_.placement.reset();
        scene_player_.clip.reset(); scene_player_.clips.clear();
        scene_player_.mesh = {}; scene_player_.image.reset(); scene_player_.material.reset();
        scene_player_.time = 0; scene_player_.updates = 0;
        scene_player_focus_pending_ = false;
        // Clear the old dynamic batch even if reloading static materials fails.
        scene_renderer_.clear();
        if (scene_data_ && scene_data_->complete && !scene_data_->instances.empty())
            upload_scene_batches(scene_mode_ == dw4::viewer::MeshDrawMode::textured);
    }

    void load_scene_player_resource(const std::string& path) {
        remove_scene_player();
        scene_player_.document.reset(); scene_player_.models.clear();
        scene_player_.resource = path; scene_player_.error.clear();
        scene_player_model_input_ = -1;
        scene_player_tab_pending_ = true;
        try {
            if (!mounted_resources_) throw std::runtime_error("Mount the game's logical resource catalog first.");
            scene_player_.document = dw4::read_igb_document(mounted_resources_->read(path,
                static_cast<std::size_t>(std::clamp(scene_entry_limit_mib_,1,256))*1024u*1024u));
            scene_player_.models = dw4::list_igb_player_models(*scene_player_.document);
            if (scene_player_.models.empty()) throw std::runtime_error("This resource exports no player model descriptors.");
        } catch (const std::exception& error) { scene_player_.error = error.what(); }
    }

    void update_scene_player_pose(double seconds) {
        try {
            if (!scene_player_.visual || !scene_player_.placement || !scene_player_.uploaded || !scene_data_)
                throw std::runtime_error("Place a supported player visual in the scene first.");
            const auto local = scene_player_.clip ? dw4::sample_animation_pose(*scene_player_.clip,scene_player_.visual->skin,seconds).positions :
                scene_player_.visual->mesh.positions;
            auto world = dw4::apply_player_placement(*scene_player_.placement,local);
            const auto bounds = position_bounds(world);
            scene_renderer_.update_batch_positions(scene_data_->instances.size(),world);
            scene_player_.mesh.positions = std::move(world);
            scene_player_.mesh.bounds = bounds;
            scene_player_.time = seconds;
            ++scene_player_.updates;
            scene_player_.error.clear();
        } catch (const std::exception& error) { scene_player_.error = error.what(); scene_player_.playing = false; }
    }

    void select_scene_player_animation(int index, double seconds = 0) {
        scene_player_.playing = false;
        try {
            if (!scene_player_.document || !scene_player_.visual || !scene_player_.uploaded)
                throw std::runtime_error("Place a supported player visual before selecting its clip.");
            std::optional<dw4::AnimationClip> candidate;
            if (index >= 0) {
                const auto& info = scene_player_.clips.at(static_cast<std::size_t>(index));
                if (!info.supported) throw std::runtime_error(info.diagnostic);
                if (!std::isfinite(seconds) || seconds < 0 || seconds > info.duration_seconds)
                    throw std::runtime_error("Player animation time is outside this clip's duration.");
                candidate = dw4::decode_igb_animation(*scene_player_.document,scene_player_.visual->skin,info.reference_index);
            }
            const auto local = candidate ? dw4::sample_animation_pose(*candidate,scene_player_.visual->skin,seconds).positions :
                scene_player_.visual->mesh.positions;
            auto world = dw4::apply_player_placement(*scene_player_.placement,local);
            const auto bounds = position_bounds(world);
            scene_renderer_.update_batch_positions(scene_data_->instances.size(),world);
            scene_player_.clip = std::move(candidate);
            scene_player_.mesh.positions = std::move(world); scene_player_.mesh.bounds = bounds;
            scene_player_.time = seconds; ++scene_player_.updates; scene_player_.error.clear();
            scene_player_.tick = std::chrono::steady_clock::now();
        } catch (const std::exception& error) { scene_player_.error = error.what(); }
    }

    void place_scene_player() {
        remove_scene_player();
        scene_player_.error.clear();
        try {
            if (!scene_data_ || !scene_data_->complete || scene_data_->instances.empty() || !scene_document_)
                throw std::runtime_error("Select a complete, nonempty map scene before placing a player visual.");
            if (!scene_player_.document) throw std::runtime_error("Load a player model resource first.");
            if (scene_player_model_input_ < 0 || scene_player_slot_input_ < 0 || scene_player_slot_input_ > 3 ||
                scene_player_branch_input_ < 0 || scene_player_point_input_ < 0 || scene_player_group_input_ < 0)
                throw std::runtime_error("Select an exported model and point descriptor, a player slot from 0 to 3, and nonnegative branch/group indices.");
            auto visual = dw4::decode_igb_player_visual(*scene_player_.document,{
                static_cast<std::uint32_t>(scene_player_model_input_),static_cast<std::uint32_t>(scene_player_slot_input_),
                static_cast<std::uint32_t>(scene_player_branch_input_)});
            auto placement = dw4::decode_igb_player_placement(*scene_document_,static_cast<std::uint32_t>(scene_player_point_input_),
                static_cast<std::uint32_t>(scene_player_group_input_),static_cast<std::uint32_t>(scene_player_slot_input_));
            auto mesh = visual.mesh;
            mesh.positions = dw4::apply_player_placement(placement,visual.mesh.positions);
            mesh.bounds = position_bounds(mesh.positions);
            auto material = dw4::resolve_igb_material(*scene_player_.document,visual.info.geometry_reference);
            auto clips = dw4::list_igb_animations(*scene_player_.document,visual.skin);
            scene_player_.visual = std::move(visual); scene_player_.placement = std::move(placement);
            scene_player_.mesh = std::move(mesh); scene_player_.material = std::move(material); scene_player_.clips = std::move(clips);
            upload_scene_batches(scene_mode_ == dw4::viewer::MeshDrawMode::textured);
            if (!scene_player_.uploaded) throw std::runtime_error(scene_mode_ == dw4::viewer::MeshDrawMode::textured ? scene_texture_error_ : scene_error_);
            scene_player_.updates = 1;
            // An elevated host inspection angle reduces nearby terrain occlusion.
            // This is not an original gameplay camera or collision-aware camera.
            scene_camera_.reset();
            scene_camera_.orbit(0,0.75f);
            scene_fit_pending_ = false;
            scene_player_focus_pending_ = true;
        } catch (const std::exception& error) {
            const std::string message = error.what();
            remove_scene_player();
            scene_player_.error = message;
        }
    }

    void advance_scene_player_clock() {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now-scene_player_.tick).count();
        scene_player_.tick = now;
        if (play_active_ || page_ != 6 || !scene_player_.playing || !scene_player_.clip || !scene_player_.uploaded || elapsed <= 0) return;
        const double duration = scene_player_.clip->info.duration_seconds;
        if (!std::isfinite(duration) || duration <= 0) { scene_player_.playing = false; return; }
        double seconds = scene_player_.time + elapsed;
        if (seconds >= duration) {
            if (scene_player_.loop) seconds = std::fmod(seconds,duration);
            else { seconds = duration; scene_player_.playing = false; }
        }
        update_scene_player_pose(seconds);
    }

    void stop_play() {
        play_active_ = false; play_paused_ = false;
        play_keys_.fill(false); play_accumulator_ = 0;
        play_world_.reset(); play_idle_clip_.reset(); play_run_clip_.reset();
    }

    void start_play() {
        stop_play(); play_error_.clear();
        try {
            if (!disc_ || !disc_->header.is_dw4_usa() || !mounted_resources_)
                throw std::runtime_error("Mount the extracted USA GameCube game before starting the walking preview.");
            // Explicit verified asset profile; this does not bypass a partially executed game startup.
            load_scene_package("dng101b1.igb");
            if (!scene_package_) throw std::runtime_error(scene_package_error_);
            select_package_member(1,1,std::nullopt,dw4::SwitchControlRequest{3471,dw4::SwitchControlCommand::all_on});
            if (!scene_data_ || !scene_data_->complete || !scene_document_) throw std::runtime_error(scene_error_);
            scene_mode_ = dw4::viewer::MeshDrawMode::textured;
            load_scene_player_resource("_common/p_agumon.igb");
            scene_player_model_input_ = 1386; scene_player_slot_input_ = 0; scene_player_branch_input_ = 1;
            scene_player_point_input_ = 115; scene_player_group_input_ = 0;
            place_scene_player();
            if (!scene_player_.uploaded || !scene_player_.visual || !scene_player_.error.empty())
                throw std::runtime_error(scene_player_.error);
            const auto floor_meshes = dw4::decode_walking_collision(*scene_document_,1768);
            auto walls = dw4::decode_walking_walls(*scene_document_,1953);
            auto doors = dw4::decode_walking_walls(*scene_document_,192);
            for (auto& door : doors) walls.push_back(std::move(door));
            play_world_.emplace(floor_meshes,walls);
            play_spawn_ = *scene_player_.placement;
            const auto clip = [&](const char* name) {
                const dw4::AnimationInfo* selected = nullptr;
                for (const auto& item : scene_player_.clips) if (item.name == name) {
                    if (selected) throw std::runtime_error("Ambiguous walking animation.");
                    selected = &item;
                }
                if (!selected || !selected->supported || selected->duration_seconds <= 0)
                    throw std::runtime_error(std::string("Walking animation is unavailable: ")+name);
                return dw4::decode_igb_animation(*scene_player_.document,scene_player_.visual->skin,selected->reference_index);
            };
            play_idle_clip_ = clip("waitS"); play_run_clip_ = clip("runD");
            play_active_ = true; play_paused_ = false; play_pad_start_ = false;
            play_distance_ = 0; play_steps_ = play_idle_samples_ = play_run_samples_ = 0;
            play_pause_verified_ = play_restart_verified_ = play_focus_verified_ = false;
            restart_play();
            play_grounded_spawn_ = play_walker_.position(); play_spawn_facing_ = play_walker_.facing();
            page_ = 0;
        } catch (const std::exception& error) {
            play_error_ = error.what(); stop_play();
        }
    }

    void restart_play() {
        if (!play_world_ || !scene_player_.placement || !play_idle_clip_) return;
        play_walker_.reset({play_spawn_.matrix[12],play_spawn_.matrix[13],play_spawn_.matrix[14]},*play_world_,
            std::atan2(play_spawn_.matrix[8],play_spawn_.matrix[10]));
        *scene_player_.placement = play_spawn_;
        scene_player_.clip = *play_idle_clip_; scene_player_.playing = false;
        scene_player_.time = 0; play_running_clip_ = false;
        play_keys_.fill(false); play_accumulator_ = 0; play_zoom_ = 1;
        scene_camera_.reset(); scene_camera_.orbit(-0.65f,0.9f);
        scene_fit_pending_ = scene_player_focus_pending_ = false;
        apply_walk_pose();
        play_tick_ = std::chrono::steady_clock::now();
    }

    void apply_walk_pose() {
        const auto p = play_walker_.position();
        const float c = std::cos(play_walker_.facing()), s = std::sin(play_walker_.facing());
        // Host controller yaw replaces the actor's outer orientation; source skin remains unchanged.
        scene_player_.placement->matrix = {c,0,-s,0, 0,1,0,0, s,0,c,0, p.x,p.y,p.z,1};
        update_scene_player_pose(scene_player_.time);
        if (!scene_player_.error.empty()) throw std::runtime_error(scene_player_.error);
    }

    void pause_play(bool pause) {
        play_paused_ = pause; play_keys_.fill(false); play_accumulator_ = 0;
        play_tick_ = std::chrono::steady_clock::now();
    }

    void handle_play_event(const SDL_Event& event) {
        if (!play_active_) return;
        if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == SDL_GetWindowID(window_)) {
            pause_play(true); return;
        }
        if ((event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) || event.key.windowID != SDL_GetWindowID(window_)) return;
        const auto key = event.key.scancode;
        if (key < 0 || key >= SDL_SCANCODE_COUNT) return;
        if (event.type == SDL_EVENT_KEY_UP) { play_keys_[key] = false; return; }
        if (event.key.repeat) return;
        if (key == SDL_SCANCODE_ESCAPE) pause_play(!play_paused_);
        else if (key == SDL_SCANCODE_R && !play_paused_) restart_play();
        else if (!play_paused_) play_keys_[key] = true;
    }

    void inject_play_smoke(int frame) {
        if (!play_active_) return;
        const auto key = [&](SDL_Scancode code, bool down) {
            SDL_Event event{}; event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
            event.key.windowID = SDL_GetWindowID(window_); event.key.scancode = code;
            event.key.down = down;
            if (!SDL_PushEvent(&event)) throw std::runtime_error(SDL_GetError());
        };
        if (frame == 4) key(SDL_SCANCODE_W,true);
        if (frame == 44) { key(SDL_SCANCODE_W,false); key(SDL_SCANCODE_D,true); }
        if (frame == 74) key(SDL_SCANCODE_D,false);
        if (frame == 80) {
            play_pause_position_ = play_walker_.position(); play_pause_time_ = scene_player_.time;
            key(SDL_SCANCODE_ESCAPE,true); key(SDL_SCANCODE_ESCAPE,false);
        }
        if (frame == 90) key(SDL_SCANCODE_W,true);
        if (frame == 100) {
            const auto p = play_walker_.position();
            play_pause_verified_ = play_paused_ && p.x == play_pause_position_.x && p.y == play_pause_position_.y &&
                p.z == play_pause_position_.z && scene_player_.time == play_pause_time_;
            key(SDL_SCANCODE_ESCAPE,true); key(SDL_SCANCODE_ESCAPE,false);
        }
        if (frame == 102) {
            play_pause_position_ = play_walker_.position(); play_pause_time_ = scene_player_.time;
            SDL_Event lost{}; lost.type = SDL_EVENT_WINDOW_FOCUS_LOST; lost.window.windowID = SDL_GetWindowID(window_);
            if (!SDL_PushEvent(&lost)) throw std::runtime_error(SDL_GetError());
            key(SDL_SCANCODE_W,true);
        }
        if (frame == 103) {
            const auto p = play_walker_.position();
            play_focus_verified_ = play_paused_ && p.x == play_pause_position_.x && p.y == play_pause_position_.y &&
                p.z == play_pause_position_.z && scene_player_.time == play_pause_time_ && !play_keys_[SDL_SCANCODE_W];
            key(SDL_SCANCODE_ESCAPE,true); key(SDL_SCANCODE_ESCAPE,false);
        }
        if (frame == 105) { key(SDL_SCANCODE_R,true); key(SDL_SCANCODE_R,false); }
        if (frame == 106) {
            const auto p = play_walker_.position();
            play_restart_verified_ = p.x == play_grounded_spawn_.x && p.y == play_grounded_spawn_.y && p.z == play_grounded_spawn_.z &&
                play_walker_.facing() == play_spawn_facing_ && !play_walker_.moving() && !play_running_clip_ &&
                scene_player_.clip && scene_player_.clip->info.name == "waitS" && scene_player_.time < 0.02;
        }
        if (frame == 110) key(SDL_SCANCODE_D,true);
        if (frame == 119) key(SDL_SCANCODE_D,false);
    }

    void advance_play() {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now-play_tick_).count(); play_tick_ = now;
        if (!play_active_ || !play_world_) return;
        try {
            bool start = false;
            if (!options_.smoke_frames) for (auto* pad : gamepads_) start |= SDL_GetGamepadButton(pad,SDL_GAMEPAD_BUTTON_START);
            if (start && !play_pad_start_) pause_play(!play_paused_);
            play_pad_start_ = start;
            if (play_paused_ || (!options_.smoke_frames && SDL_GetKeyboardFocus() != window_)) return;
            float right = float(play_keys_[SDL_SCANCODE_D] || play_keys_[SDL_SCANCODE_RIGHT]) - float(play_keys_[SDL_SCANCODE_A] || play_keys_[SDL_SCANCODE_LEFT]);
            float forward = float(play_keys_[SDL_SCANCODE_W] || play_keys_[SDL_SCANCODE_UP]) - float(play_keys_[SDL_SCANCODE_S] || play_keys_[SDL_SCANCODE_DOWN]);
            if (!options_.smoke_frames) for (auto* pad : gamepads_) {
                const float x = SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTX)/32768.0f;
                const float z = -SDL_GetGamepadAxis(pad,SDL_GAMEPAD_AXIS_LEFTY)/32768.0f;
                const float length = std::hypot(x,z);
                if (length > 0.2f && std::hypot(right,forward) < (length-0.2f)/0.8f) {
                    right = x/length*std::min(1.0f,(length-0.2f)/0.8f);
                    forward = z/length*std::min(1.0f,(length-0.2f)/0.8f);
                }
            }
            const double dt = options_.play_smoke ? 1.0/60.0 : std::clamp(elapsed,0.0,0.1);
            scene_camera_.orbit(float((int(play_keys_[SDL_SCANCODE_E])-int(play_keys_[SDL_SCANCODE_Q]))*dt*1.5),0);
            const float yaw = scene_camera_.yaw();
            const float x = right*std::cos(yaw)-forward*std::sin(yaw);
            const float z = -right*std::sin(yaw)-forward*std::cos(yaw);
            // Provisional host simulation, independent of the chosen presentation limit.
            constexpr double step = 1.0/120.0;
            play_accumulator_ = std::min(play_accumulator_+dt,0.1);
            while (play_accumulator_+1e-12 >= step) {
                const auto before = play_walker_.position();
                play_walker_.advance(x,z,step,*play_world_);
                const auto after = play_walker_.position();
                play_distance_ += std::hypot(double(after.x-before.x),double(after.z-before.z));
                const bool moving = play_walker_.moving();
                if (moving != play_running_clip_) {
                    scene_player_.clip = moving ? *play_run_clip_ : *play_idle_clip_;
                    scene_player_.time = 0; play_running_clip_ = moving;
                }
                scene_player_.time = std::fmod(scene_player_.time+step,scene_player_.clip->info.duration_seconds);
                ++play_steps_; if (moving) ++play_run_samples_; else ++play_idle_samples_;
                play_accumulator_ -= step;
            }
            apply_walk_pose();
        } catch (const std::exception& error) { play_error_ = error.what(); pause_play(true); }
    }

    void draw_play() {
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos); ImGui::SetNextWindowSize(viewport->Size);
        ImGui::Begin("Walking preview",nullptr,ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::TextColored(teal,"DIGIMON WORLD 4 / PC"); ImGui::SameLine();
        ImGui::TextColored(muted,"  Walking preview"); ImGui::SameLine();
        if (ImGui::Button(play_paused_ ? "Resume" : "Pause")) pause_play(!play_paused_);
        ImGui::TextUnformatted("WASD / Arrows / Left stick: move    Right-drag / Q E: camera    Wheel: zoom    R: restart    Esc / Start: pause");
        const auto size = ImGui::GetContentRegionAvail(); const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("play_canvas",size,ImGuiButtonFlags_MouseButtonRight);
        auto& io = ImGui::GetIO();
        if (!play_paused_ && ImGui::IsItemHovered()) {
            play_zoom_ = std::clamp(play_zoom_*std::exp(-0.12f*io.MouseWheel),0.55f,2.5f);
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) scene_camera_.orbit(-io.MouseDelta.x*0.008f,
                std::clamp(scene_camera_.pitch()-io.MouseDelta.y*0.008f,0.7f,1.45f)-scene_camera_.pitch());
        }
        try {
            const auto p = play_walker_.position(); const float radius = 15*play_zoom_;
            scene_renderer_.focus_camera(scene_camera_,{{p.x-radius,p.y+3-radius,p.z-radius},{p.x+radius,p.y+3+radius,p.z+radius}});
            const double width = std::max(1.0,double(size.x*io.DisplayFramebufferScale.x));
            const double height = std::max(1.0,double(size.y*io.DisplayFramebufferScale.y));
            const double reduction = std::min({1.0,4096.0/width,4096.0/height,std::sqrt(8388608.0/(width*height))});
            scene_target_width_ = std::max(1,int(width*reduction)); scene_target_height_ = std::max(1,int(height*reduction));
            const auto texture = scene_renderer_.render(scene_target_width_,scene_target_height_,scene_camera_,scene_mode_,settings_.msaa,scene_data_->instances.size());
            ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(texture),origin,{origin.x+size.x,origin.y+size.y},{0,1},{1,0});
        } catch (const std::exception& error) { play_error_ = error.what(); pause_play(true); }
        ImGui::End();
        if (play_paused_) {
            ImGui::SetNextWindowPos({viewport->Pos.x+viewport->Size.x*0.5f,viewport->Pos.y+viewport->Size.y*0.5f},ImGuiCond_Always,{0.5f,0.5f});
            ImGui::SetNextWindowSize({420,0});
            ImGui::Begin("Paused",nullptr,ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize);
            paragraph("Walking preview",teal);
            if (!play_error_.empty()) paragraph(play_error_.c_str(),amber);
            if (ImGui::Button("Resume",{-1,36})) pause_play(false);
            if (ImGui::Button("Restart at map entrance",{-1,36})) { restart_play(); pause_play(false); }
            if (ImGui::Button("Return to workbench",{-1,36})) { stop_play(); page_ = 0; }
            if (ImGui::Button("Exit",{-1,36})) quit_requested_ = true;
            ImGui::End();
        }
    }

    void clear_mesh_document() {
        mesh_renderer_.clear();
        clear_mesh_rig();
        mesh_document_.reset();
        mesh_data_.reset();
        mesh_material_.reset();
        mesh_material_error_.clear();
        mesh_texture_width_ = mesh_texture_height_ = 0;
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
        clear_mesh_rig();
        mesh_data_.reset();
        mesh_material_.reset();
        mesh_material_error_.clear();
        mesh_texture_width_ = mesh_texture_height_ = 0;
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
        if (mesh_data_ && mesh_renderer_.has_mesh()) {
            try {
                mesh_material_ = dw4::resolve_igb_material(*mesh_document_, meshes_.at(static_cast<std::size_t>(index)).reference_index);
                if (!mesh_material_->supported) mesh_material_error_ = mesh_material_->diagnostic;
            } catch (const std::exception& error) { mesh_material_error_ = error.what(); }
            if (mesh_mode_ == dw4::viewer::MeshDrawMode::textured) prepare_mesh_texture();
            if (mesh_rig_enabled_) {
                const bool initial_request = options_.rig && !mesh_rig_options_consumed_;
                mesh_rig_options_consumed_ = true;
                prepare_mesh_rig(initial_request ? options_.bone : std::nullopt,
                    initial_request ? options_.rotation : dw4::Vec3{});
                if (mesh_animation_mode_ && mesh_skin_) {
                    prepare_animation_catalog();
                    if (initial_request && !options_.animation.empty()) select_animation_name(options_.animation,options_.animation_time.value_or(0));
                    if (initial_request && options_.animation_play && mesh_animation_uploaded_ && mesh_animation_error_.empty()) {
                        mesh_animation_playing_ = true;
                        mesh_animation_tick_ = std::chrono::steady_clock::now();
                    }
                }
            }
        }
    }

    void clear_mesh_rig() {
        mesh_animations_.clear();
        mesh_animation_.reset();
        mesh_animation_catalog_ready_ = false;
        mesh_animation_catalog_attempted_ = false;
        mesh_animation_uploaded_ = false;
        mesh_animation_playing_ = false;
        mesh_animation_error_.clear();
        selected_animation_ = -1;
        mesh_animation_time_ = 0;
        mesh_animation_upload_count_ = 0;
        mesh_skin_.reset();
        mesh_pose_.reset();
        mesh_rig_error_.clear();
        selected_bone_ = -1;
        mesh_rotation_ = {};
        mesh_pose_uploaded_ = false;
        mesh_pose_changed_vertices_ = 0;
        mesh_pose_max_displacement_ = 0;
    }

    bool manual_pose_active() const {
        return mesh_rotation_.x != 0 || mesh_rotation_.y != 0 || mesh_rotation_.z != 0;
    }

    void upload_mesh_pose(dw4::SkinPose pose) {
        if (!mesh_skin_ || !mesh_renderer_.has_mesh()) throw std::runtime_error("No decoded rig and GPU geometry are available.");
        if (pose.positions.size() != mesh_skin_->source_positions.size() || pose.joint_positions.size() != mesh_skin_->joints.size())
            throw std::runtime_error("Evaluated pose counts do not match the selected rig.");
        std::size_t changed = 0;
        double largest = 0;
        for (std::size_t i = 0; i < pose.positions.size(); ++i) {
            const auto& before = mesh_skin_->source_positions[i];
            const auto& after = pose.positions[i];
            const auto delta = std::hypot(static_cast<double>(after.x)-before.x,
                static_cast<double>(after.y)-before.y,static_cast<double>(after.z)-before.z);
            if (delta > 0.00001) ++changed;
            largest = std::max(largest,delta);
        }
        mesh_renderer_.update_positions(pose.positions);
        mesh_pose_ = std::move(pose);
        mesh_pose_uploaded_ = true;
        mesh_pose_start_frame_ = mesh_renderer_.rendered_frames();
        mesh_pose_changed_vertices_ = changed;
        mesh_pose_max_displacement_ = largest;
        mesh_rig_error_.clear();
    }

    void update_mesh_pose() {
        try {
            if (!mesh_skin_) throw std::runtime_error("No decoded rig is available.");
            std::optional<dw4::BoneRotation> rotation;
            if (selected_bone_ >= 0 && manual_pose_active()) rotation = dw4::BoneRotation{static_cast<std::uint32_t>(selected_bone_), mesh_rotation_};
            upload_mesh_pose(dw4::evaluate_skinning(*mesh_skin_, rotation));
        } catch (const std::exception& error) { mesh_rig_error_ = error.what(); }
    }

    void prepare_mesh_rig(std::optional<int> bone = std::nullopt, dw4::Vec3 rotation = {}) {
        mesh_rig_tab_pending_ = true;
        try {
            if (!mesh_document_ || !mesh_data_ || selected_mesh_ < 0 || !mesh_renderer_.has_mesh())
                throw std::runtime_error("Select decoded geometry before inspecting its rig.");
            if (!mesh_skin_) {
                auto skin = dw4::decode_igb_skinning(*mesh_document_, meshes_.at(static_cast<std::size_t>(selected_mesh_)).reference_index);
                if (skin.source_positions.size() != mesh_data_->positions.size()) throw std::runtime_error("Rig vertex count does not match the selected mesh.");
                mesh_skin_ = std::move(skin);
            }
            selected_bone_ = -1;
            if (bone) {
                if (*bone < 0 || static_cast<std::size_t>(*bone) >= mesh_skin_->bones.size())
                    throw std::runtime_error("Bone index is outside this skeleton.");
                if (mesh_skin_->bones[static_cast<std::size_t>(*bone)].joint_index < 0)
                    throw std::runtime_error("This helper bone has no recovered joint frame and cannot be rotated.");
                selected_bone_ = *bone;
            } else {
                for (std::size_t i = 0; i < mesh_skin_->bones.size(); ++i)
                    if (mesh_skin_->bones[i].joint_index >= 0) { selected_bone_ = static_cast<int>(i); break; }
            }
            mesh_rotation_ = rotation;
            update_mesh_pose();
        } catch (const std::exception& error) { mesh_rig_error_ = error.what(); }
    }

    void prepare_animation_catalog() {
        if (mesh_animation_catalog_attempted_) return;
        mesh_animation_catalog_attempted_ = true;
        try {
            if (!mesh_document_ || !mesh_skin_) throw std::runtime_error("Decode the selected mesh rig before listing its animations.");
            mesh_animations_ = dw4::list_igb_animations(*mesh_document_,*mesh_skin_);
            mesh_animation_catalog_ready_ = true;
            mesh_animation_error_ = mesh_animations_.empty() ? "This rig's owning database contains no animation clips." : "";
        } catch (const std::exception& error) { mesh_animation_error_ = error.what(); }
    }

    void update_animation_pose(double seconds) {
        try {
            if (!mesh_animation_ || !mesh_skin_) throw std::runtime_error("Select a decoded animation clip first.");
            if (!std::isfinite(seconds) || seconds < 0 || seconds > mesh_animation_->info.duration_seconds)
                throw std::runtime_error("Animation time is outside this clip's duration.");
            upload_mesh_pose(dw4::sample_animation_pose(*mesh_animation_,*mesh_skin_,seconds));
            mesh_animation_time_ = seconds;
            mesh_animation_uploaded_ = true;
            ++mesh_animation_upload_count_;
            mesh_animation_error_.clear();
        } catch (const std::exception& error) {
            mesh_animation_error_ = error.what();
            mesh_animation_playing_ = false;
        }
    }

    void select_animation(int index, double seconds = 0) {
        mesh_animation_playing_ = false;
        mesh_animation_uploaded_ = false;
        mesh_animation_.reset();
        mesh_animation_error_.clear();
        mesh_animation_time_ = 0;
        mesh_animation_upload_count_ = 0;
        selected_animation_ = index;
        try {
            if (!mesh_document_ || !mesh_skin_) throw std::runtime_error("No decoded rig is available for animation inspection.");
            upload_mesh_pose(dw4::evaluate_skinning(*mesh_skin_));
            if (index < 0 || static_cast<std::size_t>(index) >= mesh_animations_.size()) throw std::runtime_error("Animation index is outside the owning database's clip list.");
            const auto& info = mesh_animations_[static_cast<std::size_t>(index)];
            if (!info.supported) throw std::runtime_error(info.diagnostic.empty() ? "This animation binding profile is unsupported." : info.diagnostic);
            if (!std::isfinite(seconds) || seconds < 0 || seconds > info.duration_seconds)
                throw std::runtime_error("Animation time is outside this clip's duration.");
            mesh_animation_ = dw4::decode_igb_animation(*mesh_document_,*mesh_skin_,info.reference_index);
            update_animation_pose(seconds);
        } catch (const std::exception& error) { mesh_animation_error_ = error.what(); }
    }

    void select_animation_name(const std::string& name, double seconds) {
        try {
            if (!mesh_animation_catalog_ready_) throw std::runtime_error(mesh_animation_error_.empty() ? "The animation list is unavailable." : mesh_animation_error_);
            int found = -1;
            for (std::size_t i = 0; i < mesh_animations_.size(); ++i) {
                if (mesh_animations_[i].name != name) continue;
                if (found >= 0) throw std::runtime_error("The animation name is ambiguous in this database: " + name);
                found = static_cast<int>(i);
            }
            if (found < 0) throw std::runtime_error("Animation was not found in the selected rig's owning database: " + name);
            select_animation(found,seconds);
        } catch (const std::exception& error) { mesh_animation_error_ = error.what(); }
    }

    void advance_animation_clock() {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now-mesh_animation_tick_).count();
        mesh_animation_tick_ = now;
        if (page_ != 5 || !mesh_rig_enabled_ || !mesh_animation_mode_ || !mesh_animation_playing_ || !mesh_animation_) return;
        if (elapsed <= 0) return;
        const double duration = mesh_animation_->info.duration_seconds;
        if (!std::isfinite(duration) || duration <= 0) { mesh_animation_playing_ = false; return; }
        double seconds = mesh_animation_time_ + elapsed;
        if (seconds >= duration) {
            if (mesh_animation_loop_) seconds = std::fmod(seconds,duration);
            else { seconds = duration; mesh_animation_playing_ = false; }
        }
        update_animation_pose(seconds);
    }

    void prepare_mesh_texture() {
        if (mesh_renderer_.has_texture()) return;
        try {
            if (!mesh_data_ || !mesh_document_ || selected_mesh_ < 0) throw std::runtime_error("No decoded geometry is selected.");
            if (!mesh_material_) throw std::runtime_error(mesh_material_error_.empty() ? "No verified material binding was resolved." : mesh_material_error_);
            if (!mesh_material_->supported) throw std::runtime_error(mesh_material_->diagnostic.empty() ? "This material profile is unsupported." : mesh_material_->diagnostic);
            const auto reference = meshes_.at(static_cast<std::size_t>(selected_mesh_)).reference_index;
            if (mesh_material_->geometry_reference != reference || !mesh_material_->image_reference)
                throw std::runtime_error("The material binding does not identify this geometry and an image.");
            auto uv = dw4::decode_igb_texcoords(*mesh_document_, reference);
            if (uv.size() != mesh_data_->positions.size()) throw std::runtime_error("Texture-coordinate count does not match the selected geometry.");
            const auto pixels = dw4::decode_igb_texture(*mesh_document_, *mesh_material_->image_reference);
            dw4::viewer::MeshTextureOptions sampler;
            sampler.wrap_u = texture_wrap(mesh_material_->wrap_s);
            sampler.wrap_v = texture_wrap(mesh_material_->wrap_t);
            sampler.min_filter = texture_filter(mesh_material_->min_filter);
            sampler.mag_filter = texture_filter(mesh_material_->mag_filter);
            mesh_data_->texcoords = std::move(uv);
            mesh_pose_uploaded_ = false;
            mesh_renderer_.upload(*mesh_data_);
            mesh_renderer_.set_texture(pixels, sampler);
            if (mesh_rig_enabled_ && mesh_pose_) {
                mesh_renderer_.update_positions(mesh_pose_->positions);
                mesh_pose_uploaded_ = true;
                mesh_pose_start_frame_ = mesh_renderer_.rendered_frames();
            }
            if (!mesh_renderer_.has_texture()) throw std::runtime_error("The graphics renderer did not retain the verified texture.");
            mesh_texture_width_ = pixels.width;
            mesh_texture_height_ = pixels.height;
            mesh_render_start_frame_ = mesh_renderer_.rendered_frames();
            mesh_material_error_.clear();
        } catch (const std::exception& error) {
            mesh_renderer_.clear_texture();
            mesh_texture_width_ = mesh_texture_height_ = 0;
            mesh_material_error_ = error.what();
            if (!mesh_renderer_.has_mesh()) mesh_error_ = mesh_material_error_;
        }
    }

    void draw() {
        if (play_active_) { draw_play(); return; }
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
        const std::array<const char*, 9> pages{"Overview", "Graphics", "Controls", "Original data", "Textures", "Meshes", "Scenes", "Roadmap", "Startup"};
        for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(pages[static_cast<std::size_t>(i)], page_ == i, 0, ImVec2(0, 32))) page_ = i;
            ImGui::PopID();
        }
        const float bottom = ImGui::GetWindowHeight() - 146;
        if (ImGui::GetCursorPosY() < bottom) ImGui::SetCursorPosY(bottom);
        ImGui::Separator();
        ImGui::TextColored(teal, "VERSION 0.14");
        paragraph("Development workbench");
        paragraph("Walking preview available.");
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("content", ImVec2(0, 0), false);
        if (page_ == 0) draw_overview();
        if (page_ == 1) draw_graphics();
        if (page_ == 2) draw_controls();
        if (page_ == 3) draw_assets();
        if (page_ == 4) draw_textures();
        if (page_ == 5) draw_meshes();
        if (page_ == 6) draw_scenes();
        if (page_ == 7) draw_roadmap();
        if (page_ == 8) draw_boot();
        if (!notice_.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            paragraph(notice_.c_str(), notice_error_ ? amber : teal);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void draw_overview() {
        title("Digimon World 4 PC", "Native walking preview  /  Original GameCube assets");
        ImGui::BeginChild("status", ImVec2(0, 155), true);
        ImGui::TextColored(teal, "EXPLORE THE ORIGINAL MAP");
        paragraph("Control Agumon with WASD or a gamepad. Original map, model, floor geometry and animations; provisional native movement. Combat, progression and saves are still in development.", ImVec4(0.86f,0.90f,0.96f,1));
        ImGui::BeginDisabled(!mounted_resources_);
        if (ImGui::Button("Play walking preview", ImVec2(240,36))) start_play();
        ImGui::EndDisabled();
        if (!play_error_.empty()) paragraph(play_error_.c_str(),amber);
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
        title("Graphics", "Window and rendering settings for the native walking preview and asset viewers.");
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
        paragraph("The walking preview supports WASD, arrows, mouse camera and SDL-mapped gamepads. Rebinding and four-player input routing remain in development.");
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
            paragraph("This panel shows header and type metadata. Use the walking preview to explore the supported original map.");
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
                if (ImGui::SmallButton("View scenes")) {
                    select_scene_archive(selected_archive_);
                    scene_entry_input_ = selected_entry_;
                    load_scene_entry(selected_entry_);
                    page_ = 6;
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

    void draw_animation_controls() {
        prepare_animation_catalog();
        ImGui::SetNextItemWidth(-1);
        const auto* selected_name = selected_animation_ >= 0 && static_cast<std::size_t>(selected_animation_) < mesh_animations_.size()
            ? mesh_animations_[static_cast<std::size_t>(selected_animation_)].name.c_str() : "Choose an original clip";
        if (ImGui::BeginCombo("##animation_clip",selected_name,ImGuiComboFlags_HeightLarge)) {
            for (std::size_t i = 0; i < mesh_animations_.size(); ++i) {
                const auto& clip = mesh_animations_[i];
                ImGui::PushID(static_cast<int>(i));
                if (!clip.supported) ImGui::PushStyleColor(ImGuiCol_Text,muted);
                const auto label = (clip.name.empty() ? std::string("Unnamed clip") : clip.name) + " / ref " + std::to_string(clip.reference_index);
                if (ImGui::Selectable(label.c_str(),selected_animation_ == static_cast<int>(i))) select_animation(static_cast<int>(i));
                if (!clip.supported) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%.6g seconds / %u tracks\n%s\n%s",clip.duration_seconds,clip.track_count,
                    clip.provenance.c_str(),clip.diagnostic.c_str());
                ImGui::PopID();
            }
            if (mesh_animations_.empty()) ImGui::TextDisabled("No animation clips available.");
            ImGui::EndCombo();
        }
        if (mesh_animation_) {
            const double duration = mesh_animation_->info.duration_seconds;
            ImGui::BeginDisabled(!mesh_animation_uploaded_ || !mesh_animation_error_.empty() || duration <= 0);
            if (ImGui::Button(mesh_animation_playing_ ? "Pause" : "Play")) {
                if (mesh_animation_playing_) mesh_animation_playing_ = false;
                else {
                    if (mesh_animation_time_ >= duration) update_animation_pose(0);
                    mesh_animation_playing_ = mesh_animation_uploaded_ && mesh_animation_error_.empty();
                }
                mesh_animation_tick_ = std::chrono::steady_clock::now();
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset")) { mesh_animation_playing_ = false; update_animation_pose(0); }
            ImGui::SameLine();
            ImGui::Checkbox("Loop",&mesh_animation_loop_);
            ImGui::EndDisabled();
            ImGui::TextColored(muted,"Time %.3f / %.3f seconds",mesh_animation_time_,duration);
            double seconds = mesh_animation_time_;
            const double start = 0;
            ImGui::SetNextItemWidth(-1);
            ImGui::BeginDisabled(duration <= 0);
            if (ImGui::SliderScalar("##animation_time",ImGuiDataType_Double,&seconds,&start,&duration,"%.3f s",ImGuiSliderFlags_AlwaysClamp)) {
                mesh_animation_playing_ = false;
                update_animation_pose(seconds);
            }
            ImGui::EndDisabled();
        }
        ImGui::Checkbox("X-ray joints",&mesh_skeleton_overlay_);
        ImGui::SameLine();
        if (ImGui::Button("Details")) ImGui::OpenPopup("rig_provenance");
        if (!mesh_animation_error_.empty()) paragraph(mesh_animation_error_.c_str(),amber);
        else if (!mesh_animation_uploaded_) paragraph("Choose a clip to apply decoded animation. The stored mesh is shown.");
        else paragraph("Animation inspection. Game-frame comparison pending.");
    }

    void draw_mesh_rig_controls() {
        if (!mesh_rig_enabled_) return;
        if (!mesh_rig_error_.empty()) paragraph(("Rig inspection: " + mesh_rig_error_).c_str(),amber);
        if (!mesh_skin_) {
            paragraph("The stored geometry remains available. No verified joint pose is applied.");
            return;
        }
        const auto& skin = *mesh_skin_;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(8,5));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8,4));
        int pose_mode = mesh_animation_mode_ ? 1 : 0;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##pose_source",&pose_mode,"Manual joints\0Animation\0")) {
            mesh_animation_mode_ = pose_mode == 1;
            mesh_animation_playing_ = false;
            if (mesh_animation_mode_) {
                prepare_animation_catalog();
                if (mesh_animation_) update_animation_pose(mesh_animation_time_);
                else {
                    try { upload_mesh_pose(dw4::evaluate_skinning(skin)); }
                    catch (const std::exception& error) { mesh_rig_error_ = error.what(); }
                }
            } else { mesh_animation_uploaded_ = false; update_mesh_pose(); }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s / %zu palette entries\n%s",skin.info.skeleton_name.c_str(),skin.palette_to_joint.size(),skin.info.provenance.c_str());
        if (mesh_animation_mode_) draw_animation_controls();
        else {
        ImGui::SetNextItemWidth(-1);
        const auto selected_name = selected_bone_ >= 0 ? std::to_string(selected_bone_) + "  " + skin.bones[static_cast<std::size_t>(selected_bone_)].name : "Choose a joint-bearing bone";
        if (ImGui::BeginCombo("##manual_bone",selected_name.c_str(),ImGuiComboFlags_HeightLarge)) {
            for (std::size_t i = 0; i < skin.bones.size(); ++i) {
                const auto& bone = skin.bones[i];
                ImGui::BeginDisabled(bone.joint_index < 0);
                const auto label = std::to_string(i) + "  " + (bone.name.empty() ? "Unnamed helper" : bone.name) + (bone.joint_index < 0 ? " (no joint frame)" : "");
                if (ImGui::Selectable(label.c_str(),selected_bone_ == static_cast<int>(i))) {
                    selected_bone_ = static_cast<int>(i);
                    mesh_rotation_ = {};
                    update_mesh_pose();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Bone reference %u / parent bone %d / joint %d",
                    bone.reference_index,bone.parent,bone.joint_index);
                ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        ImGui::TextColored(muted,"Manual X / Y / Z degrees");
        float rotation[3]{mesh_rotation_.x,mesh_rotation_.y,mesh_rotation_.z};
        ImGui::SetNextItemWidth(-1);
        ImGui::BeginDisabled(selected_bone_ < 0);
        if (ImGui::SliderFloat3("##manual_rotation",rotation,-180.0f,180.0f,"%.1f",ImGuiSliderFlags_AlwaysClamp)) {
            mesh_rotation_ = {rotation[0],rotation[1],rotation[2]};
            update_mesh_pose();
        }
        ImGui::EndDisabled();
        if (ImGui::Button("Reset")) { mesh_rotation_ = {}; update_mesh_pose(); }
        ImGui::SameLine();
        ImGui::Checkbox("X-ray joints",&mesh_skeleton_overlay_);
        ImGui::SameLine();
        if (ImGui::Button("Details")) ImGui::OpenPopup("rig_provenance");
        paragraph(manual_pose_active() ? "Manual joint delta." : "Inverse-bind inspection.");
        }
        if (ImGui::BeginPopup("rig_provenance")) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+480);
            ImGui::Text("%s / %zu palette entries",skin.info.skeleton_name.c_str(),skin.palette_to_joint.size());
            paragraph(skin.info.provenance.c_str());
            ImGui::Text("%zu changed vertices / max displacement %.5g",mesh_pose_changed_vertices_,mesh_pose_max_displacement_);
            if (mesh_animation_mode_ && mesh_animation_) {
                const auto& clip = *mesh_animation_;
                paragraph(clip.info.provenance.c_str());
                ImGui::Text("%u original tracks / %u samples per second / %zu decoded frames",clip.info.track_count,
                    clip.channels.sample_rate(),clip.channels.frame_count());
                paragraph("Decoded source channels drive the checked skeleton mapping. Scene placement, full game materials and recorded-frame comparison remain incomplete.");
            } else paragraph("Joint frames come from inverse-bind matrices. Manual deltas are inspection controls, not original animation samples.");
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar(2);
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
                mesh_mode_, settings_.msaa);
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 end(origin.x + canvas_size.x,origin.y + canvas_size.y);
            draw->AddImage(static_cast<ImTextureID>(target), origin, end, ImVec2(0,1), ImVec2(1,0));
            draw->AddRect(origin,end,IM_COL32(53,72,88,255),6);
            if (mesh_rig_enabled_ && mesh_skeleton_overlay_ && mesh_pose_uploaded_ && mesh_skin_ && mesh_pose_) {
                const float aspect = static_cast<float>(mesh_target_width_) / mesh_target_height_;
                std::vector<std::optional<ImVec2>> points;
                points.reserve(mesh_pose_->joint_positions.size());
                for (const auto& position : mesh_pose_->joint_positions) {
                    const auto projected = mesh_renderer_.project_to_canvas(position,mesh_camera_,aspect);
                    if (projected) points.push_back(ImVec2(origin.x+projected->x*canvas_size.x,origin.y+projected->y*canvas_size.y));
                    else points.push_back(std::nullopt);
                }
                draw->PushClipRect(origin,end,true);
                for (std::size_t i = 0; i < points.size(); ++i) {
                    if (!points[i]) continue;
                    const auto& joint = mesh_skin_->joints.at(i);
                    const bool selected = !mesh_animation_mode_ && selected_bone_ >= 0 && joint.bone_index == static_cast<std::uint32_t>(selected_bone_);
                    const auto color = selected ? IM_COL32(255,190,77,255) : IM_COL32(90,240,225,225);
                    if (joint.parent_joint >= 0 && static_cast<std::size_t>(joint.parent_joint) < points.size() && points[static_cast<std::size_t>(joint.parent_joint)])
                        draw->AddLine(*points[static_cast<std::size_t>(joint.parent_joint)],*points[i],color,2.0f);
                    draw->AddCircleFilled(*points[i],selected ? 5.0f : 3.0f,color);
                    if (selected) draw->AddText(ImVec2(points[i]->x+8,points[i]->y-17),color,mesh_skin_->bones[joint.bone_index].name.c_str());
                }
                draw->PopClipRect();
            }
        } catch (const std::exception& error) {
            mesh_error_ = error.what();
            mesh_render_failed_ = true;
        }
    }

    void draw_meshes() {
        title("Mesh viewer", "Original assets with manual posing or animation inspection. Full materials, actor behavior and gameplay remain pending.");
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
        if (mesh_material_ && mesh_material_->supported && mesh_material_->image_reference) {
            const auto& material = *mesh_material_;
            const auto name = material.image_name.empty() ? std::string("Unnamed image") : material.image_name;
            ImGui::TextColored(teal,"Bound image: %s / reference %u%s",name.c_str(),*material.image_reference,
                mesh_renderer_.has_texture() ? " / uploaded" : " / not uploaded");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",material.provenance.c_str());
            if (ImGui::CollapsingHeader("Material binding provenance / partial profile")) {
                paragraph(material.provenance.c_str());
                auto reference_text = [](const auto& reference) { return reference ? std::to_string(*reference) : "-"; };
                ImGui::TextWrapped("Geometry node %s -> scope %s -> attributes %s -> texture bind %s -> texture %s -> image %s",
                    reference_text(material.geometry_node_reference).c_str(),reference_text(material.scope_node_reference).c_str(),
                    reference_text(material.attribute_list_reference).c_str(),reference_text(material.texture_bind_reference).c_str(),
                    reference_text(material.texture_reference).c_str(),reference_text(material.image_reference).c_str());
                if (material.inherited) {
                    std::string path;
                    for (const auto reference : material.inheritance_path) { if (!path.empty()) path += " -> "; path += std::to_string(reference); }
                    ImGui::TextWrapped("Inherited binding path: %s",path.c_str());
                }
                ImGui::Text("Stored sampler values: wrap S/T %u/%u, min/mag %u/%u",material.raw_wrap_s,material.raw_wrap_t,
                    material.raw_min_filter,material.raw_mag_filter);
                if (mesh_renderer_.has_texture()) ImGui::Text("Decoded image %u x %u / %zu UV pairs",mesh_texture_width_,mesh_texture_height_,mesh_data_->texcoords.size());
                paragraph("Only the verified base-color profile is applied. Full game materials, mip selection and scene appearance remain separate work.");
            }
            if (!mesh_material_error_.empty()) paragraph(("Textured mode unavailable: " + mesh_material_error_).c_str(),amber);
        } else if (!mesh_material_error_.empty() && mesh_mode_ != dw4::viewer::MeshDrawMode::textured) {
            paragraph(("Textured mode unavailable: " + mesh_material_error_).c_str(),amber);
        }
        if (meshes_.empty()) {
            paragraph("Supported geometry is decoded from its serialized position and topology data. Unsupported layouts remain explicit diagnostics.");
            return;
        }
        const float height = std::max(300.0f,ImGui::GetContentRegionAvail().y - 67.0f);
        ImGui::BeginChild("mesh_list",ImVec2(mesh_rig_enabled_ ? 300.0f : 235.0f,height),true);
        if (ImGui::BeginTabBar("mesh_inspector_tabs")) {
        if (ImGui::BeginTabItem("Geometry")) {
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
        ImGui::EndTabItem();
        }
        if (mesh_rig_enabled_ && ImGui::BeginTabItem("Rig",nullptr,mesh_rig_tab_pending_ ? ImGuiTabItemFlags_SetSelected : 0)) {
            mesh_rig_tab_pending_ = false;
            draw_mesh_rig_controls();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        }
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("mesh_detail",ImVec2(0,height),true,ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (mesh_renderer_.has_mesh() && !mesh_render_failed_) {
            if (ImGui::Button("Fit")) mesh_camera_.reset();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);
            int mode = mesh_mode_ == dw4::viewer::MeshDrawMode::textured ? 2 : mesh_mode_ == dw4::viewer::MeshDrawMode::wireframe ? 1 : 0;
            if (ImGui::Combo("##mesh_draw_mode",&mode,"Solid\0Wireframe\0Textured\0")) {
                mesh_mode_ = mode == 2 ? dw4::viewer::MeshDrawMode::textured : mode == 1 ? dw4::viewer::MeshDrawMode::wireframe : dw4::viewer::MeshDrawMode::solid;
                if (mesh_mode_ == dw4::viewer::MeshDrawMode::textured) prepare_mesh_texture();
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Inspect rig",&mesh_rig_enabled_)) {
                if (mesh_rig_enabled_) prepare_mesh_rig();
                else {
                    try { mesh_renderer_.update_positions(mesh_data_->positions); clear_mesh_rig(); }
                    catch (const std::exception& error) { mesh_rig_enabled_ = true; mesh_rig_error_ = error.what(); }
                }
            }
            if (ImGui::GetWindowWidth() > 900) { ImGui::SameLine(); ImGui::TextColored(muted,"Drag: orbit / right: pan / wheel: zoom"); }
            if (mesh_mode_ != dw4::viewer::MeshDrawMode::textured || mesh_renderer_.has_texture()) draw_mesh_canvas();
            else {
                paragraph("Textured rendering is unavailable for this geometry.",amber);
                paragraph(mesh_material_error_.c_str());
                paragraph("Solid and wireframe modes remain available for geometry inspection.");
            }
        } else {
            paragraph("This geometry could not be rendered.",amber);
            paragraph(mesh_error_.c_str());
        }
        ImGui::EndChild();
    }

    void draw_scene_package_members() {
        const auto* selected = selected_package_member();
        std::string preview = "Choose an original member; no member is selected automatically";
        if (selected) preview = std::to_string(selected->index) + "  " + selected->specifier + "  /  " + package_member_source(*selected);
        ImGui::SetNextItemWidth(std::max(160.0f,ImGui::GetContentRegionAvail().x-385));
        if (ImGui::BeginCombo("Member",preview.c_str(),ImGuiComboFlags_HeightLarge)) {
            if (scene_package_) {
                ImGuiListClipper clip;
                clip.Begin(static_cast<int>(scene_package_->members.size()));
                while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                    const auto& member = scene_package_->members[static_cast<std::size_t>(row)];
                    ImGui::PushID(row);
                    const auto label = std::to_string(member.index) + "  " + member.specifier + "  /  " + package_member_source(member);
                    ImGui::BeginDisabled(!member.supported);
                    if (ImGui::Selectable(label.c_str(),selected_package_member_ == static_cast<int>(member.index))) select_package_member(static_cast<int>(member.index));
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s\n%s",member.provenance.c_str(),member.diagnostic.c_str());
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                if (scene_package_->members.empty()) ImGui::TextDisabled("This descriptor contains no member records.");
            } else ImGui::TextDisabled("Choose a package descriptor first.");
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Package info")) ImGui::OpenPopup("package_provenance");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::InputInt("Limit (MiB)",&scene_entry_limit_mib_,0,0);
        scene_entry_limit_mib_ = std::clamp(scene_entry_limit_mib_,1,256);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Member read limit: 1-256 MiB.\nDescriptor reads also have an independent 4 MiB cap.");
        if (ImGui::BeginPopup("package_provenance")) {
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+540);
            if (scene_package_) {
                const auto& package = *scene_package_;
                ImGui::Text("Descriptor %u / %zu original members",package.reference_index,package.members.size());
                paragraph(package.logical_path.c_str(),teal);
                paragraph(package.provenance.c_str());
                ImGui::Text("Stored resolve flag: %s",package.resolve_state ? "true" : "false");
                if (!package.name.empty()) paragraph(package.name.c_str());
                if (!package.section.empty()) paragraph(package.section.c_str());
                if (const auto* member = selected_package_member()) {
                    ImGui::Separator();
                    ImGui::Text("Member %u / file reference %u",member->index,member->file_reference);
                    paragraph(member->specifier.c_str(),teal);
                    paragraph(member->provenance.c_str());
                    if (!member->diagnostic.empty()) paragraph(member->diagnostic.c_str(),amber);
                    for (const auto& alias : member->catalog_paths) ImGui::TextWrapped("Catalog alias: %s",alias.c_str());
                    ImGui::Text("%zu original exports / %zu scene choices",scene_package_exports_.size(),scenes_.size());
                    if (!scene_package_exports_.empty()) {
                        ImGui::BeginChild("package_exports",ImVec2(540,160),true);
                        ImGuiListClipper clip;
                        clip.Begin(static_cast<int>(scene_package_exports_.size()));
                        while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                            const auto& exported = scene_package_exports_[static_cast<std::size_t>(row)];
                            ImGui::Text("%d / ref %u / %s / %s",row,exported.reference_index,exported.type.c_str(),exported.name.c_str());
                        }
                        ImGui::EndChild();
                    }
                }
                paragraph("Member and export references are resolved from original data. No game startup or automatic scene activation is performed.");
            } else paragraph(scene_package_error_.empty() ? "No decoded package descriptor is selected." : scene_package_error_.c_str(),amber);
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
        }
        if (!scene_package_error_.empty()) paragraph(scene_package_error_.c_str(),amber);
    }

    void draw_scenes() {
        title("Scene inspector", "Original map geometry and explicitly placed animated visuals. Game cameras, startup, collision and gameplay remain pending.");
        if (!disc_) {
            paragraph("Connect your extracted game copy before loading an IGB scene.");
            if (ImGui::Button("Connect original data")) page_ = 3;
            if (!scene_error_.empty()) paragraph(scene_error_.c_str(),amber);
            if (!scene_package_error_.empty()) paragraph(scene_package_error_.c_str(),amber);
            return;
        }
        int source_mode = scene_package_mode_ ? 1 : 0;
        ImGui::SetNextItemWidth(110);
        if (ImGui::Combo("##scene_source_mode",&source_mode,"Resource\0Package\0")) {
            clear_scene_document();
            clear_scene_package();
            scene_package_mode_ = source_mode == 1;
            scene_archive_.reset();
            selected_scene_archive_ = -1;
            scene_entry_input_ = 0;
            if (!scene_package_mode_ && !disc_->archives.empty()) select_scene_archive(0);
        }
        if (mounted_resources_) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(170);
            ImGui::InputTextWithHint("##scene_resource_filter","Filter logical resource path",scene_resource_filter_.data(),scene_resource_filter_.size());
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            const auto* preview = scene_package_mode_ ? (scene_package_path_.empty() ? "Choose a package descriptor" : scene_package_path_.c_str()) :
                (loaded_scene_resource_.empty() ? "Choose a logical resource from ndmw.igb" : loaded_scene_resource_.c_str());
            if (ImGui::BeginCombo("##scene_logical_resource",preview,ImGuiComboFlags_HeightLarge)) {
                const auto& resources = mounted_resources_->catalog().resources;
                std::vector<std::size_t> visible;
                for (std::size_t i = 0; i < resources.size(); ++i)
                    if (!scene_resource_filter_[0] || resources[i].logical_path.find(scene_resource_filter_.data()) != std::string::npos) visible.push_back(i);
                ImGuiListClipper clip;
                clip.Begin(static_cast<int>(visible.size()));
                while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                    const auto& resource = resources[visible[static_cast<std::size_t>(row)]];
                    ImGui::BeginDisabled(!resource.location);
                    const bool selected = (scene_package_mode_ ? scene_package_path_ : loaded_scene_resource_) == resource.logical_path;
                    if (ImGui::Selectable(resource.logical_path.c_str(),selected)) {
                        if (scene_package_mode_) load_scene_package(resource.logical_path);
                        else load_scene_resource(resource.logical_path);
                    }
                    ImGui::EndDisabled();
                }
                if (visible.empty()) ImGui::TextDisabled("No matching catalog resources.");
                ImGui::EndCombo();
            }
        } else if (!catalog_error_.empty()) paragraph(("Resource catalog unavailable: " + catalog_error_).c_str(),amber);
        if (scene_package_mode_) draw_scene_package_members();
        else {
        ImGui::SetNextItemWidth(170);
        const auto archive_name = scene_archive_ ? filename(scene_archive_->path()) : "Choose archive";
        if (ImGui::BeginCombo("Archive",archive_name.c_str())) {
            for (std::size_t i = 0; i < disc_->archives.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(filename(disc_->archives[i].path).c_str(),selected_scene_archive_ == static_cast<int>(i))) select_scene_archive(static_cast<int>(i));
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95);
        ImGui::InputInt("Entry",&scene_entry_input_,0,0);
        ImGui::SameLine();
        if (ImGui::Button("Load entry")) load_scene_entry(scene_entry_input_);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70);
        ImGui::InputInt("Limit (MiB)",&scene_entry_limit_mib_,0,0);
        scene_entry_limit_mib_ = std::clamp(scene_entry_limit_mib_,1,256);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum AFS entry read: 1-256 MiB.\nTraversal, geometry, images and graphics targets have independent limits.");
        }
        if (scene_source_.empty()) paragraph(scene_package_mode_ ? "Choose an original package member to inspect its exported scene roots." :
            "Choose an original resource to list its explicit scene exports.");
        else ImGui::TextColored(teal,"%s  |  %zu roots  |  %zu instances  |  target %d x %d / MSAA %dx",scene_source_.c_str(),scenes_.size(),
            scene_renderer_.batch_count(),scene_target_width_,scene_target_height_,scene_renderer_.samples());
        if (scene_data_ && scene_data_->root_control)
            ImGui::TextColored(teal,"Original switch command: %s / controller %u. Game startup is not running.",
                scene_data_->root_control->command == dw4::SwitchControlCommand::all_on ? "ALLON" : "ALLOF",
                scene_data_->root_control->controller_reference);
        else if (scene_data_ && scene_data_->inspected_root_child)
            ImGui::TextColored(amber,"Inspection override: root child %u; runtime activation unverified.",*scene_data_->inspected_root_child);
        else if (!scenes_.empty()) ImGui::TextColored(muted,"Stored root selection / no activation override.");
        if (scenes_.empty()) {
            if (!scene_error_.empty()) paragraph(scene_error_.c_str(),amber);
            paragraph("Unsupported and dynamic graph behavior is retained as a diagnostic, not guessed into static scenery.");
            return;
        }
        const float height = std::max(300.0f,ImGui::GetContentRegionAvail().y - 67.0f);
        ImGui::BeginChild("scene_inspector",ImVec2(300,height),true);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(8,5));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(8,4));
        ImGui::SetNextItemWidth(-1);
        const bool valid_root = selected_scene_ >= 0 && static_cast<std::size_t>(selected_scene_) < scenes_.size();
        std::string root_label = "Choose an exported root";
        if (valid_root) {
            const auto& info = scenes_[static_cast<std::size_t>(selected_scene_)];
            root_label = std::to_string(selected_scene_) + "  " + scene_display_name(info);
        }
        if (ImGui::BeginCombo("##scene_root",root_label.c_str(),ImGuiComboFlags_HeightLarge)) {
            for (std::size_t i = 0; i < scenes_.size(); ++i) {
                const auto& info = scenes_[i];
                ImGui::PushID(static_cast<int>(i));
                const auto label = std::to_string(i) + "  " + scene_display_name(info);
                if (!info.supported) ImGui::PushStyleColor(ImGuiCol_Text,muted);
                if (ImGui::Selectable(label.c_str(),selected_scene_ == static_cast<int>(i))) select_scene(static_cast<int>(i));
                if (!info.supported) ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Export %u / root %u / %s\n%s",info.info_reference,info.root_reference,info.root_type.c_str(),info.diagnostic.c_str());
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(!valid_root || scenes_[static_cast<std::size_t>(selected_scene_)].root_type != "igSwitch");
        if (ImGui::Checkbox("Inspect root child",&scene_child_enabled_))
            select_scene(selected_scene_,scene_child_enabled_ ? std::optional<int>(scene_child_input_) : std::nullopt);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Explicit inspection for an exported igSwitch root.\nThis overrides only its stored mask; runtime activation is unverified.\nOther switch masks remain stored.");
        if (scene_child_enabled_) {
            ImGui::SetNextItemWidth(80);
            ImGui::InputInt("Child",&scene_child_input_,0,0);
            ImGui::SameLine();
            if (ImGui::Button("Apply")) select_scene(selected_scene_,scene_child_input_);
        }
        ImGui::EndDisabled();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(4,5));
        if (ImGui::BeginTabBar("scene_tabs",ImGuiTabBarFlags_FittingPolicyShrink)) {
            if (ImGui::BeginTabItem("Items")) {
                if (scene_data_ && scene_data_->complete) {
                    ImGui::TextColored(muted,"%zu instances / %zu transforms",scene_data_->instances.size(),scene_data_->transform_nodes);
                    ImGuiListClipper clip;
                    clip.Begin(static_cast<int>(scene_data_->instances.size()));
                    while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                        const auto& instance = scene_data_->instances[static_cast<std::size_t>(row)];
                        ImGui::PushID(row);
                        auto name = instance.name.empty() ? scene_node_name(instance.node_reference) : instance.name;
                        if (name.empty()) name = "Node " + std::to_string(instance.node_reference);
                        const auto label = std::to_string(row) + "  " + name;
                        if (ImGui::Selectable(label.c_str(),selected_scene_instance_ == row,0,ImVec2(0,26))) selected_scene_instance_ = row;
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Node %u / geometry %u\n%zu vertices / %zu triangles",instance.node_reference,
                            instance.geometry_reference,instance.mesh.positions.size(),instance.mesh.indices.size()/3);
                        ImGui::PopID();
                    }
                } else paragraph("Instances are not displayed until this root's static traversal is complete.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Info")) {
                if (valid_root) {
                    const auto& info = scenes_[static_cast<std::size_t>(selected_scene_)];
                    ImGui::Text("Export %u / root %u",info.info_reference,info.root_reference);
                    paragraph(info.root_type.c_str());
                    ImGui::TextColored(muted,"Stored up %.3g, %.3g, %.3g",info.up_direction.x,info.up_direction.y,info.up_direction.z);
                }
                if (scene_data_) {
                    const auto& scene = *scene_data_;
                    ImGui::Text("%zu visited nodes / %zu transforms",scene.visited_nodes,scene.transform_nodes);
                    ImGui::Text("%zu vertices / %zu triangles",scene.vertex_count,scene.triangle_count);
                    if (scene.complete && !scene.instances.empty()) {
                        ImGui::TextColored(muted,"Min %.4g, %.4g, %.4g",scene.bounds.min.x,scene.bounds.min.y,scene.bounds.min.z);
                        ImGui::TextColored(muted,"Max %.4g, %.4g, %.4g",scene.bounds.max.x,scene.bounds.max.y,scene.bounds.max.z);
                    }
                    if (scene.complete && selected_scene_instance_ >= 0 && static_cast<std::size_t>(selected_scene_instance_) < scene.instances.size()) {
                        const auto& instance = scene.instances[static_cast<std::size_t>(selected_scene_instance_)];
                        ImGui::Separator();
                        ImGui::Text("Instance %d / node %u",selected_scene_instance_,instance.node_reference);
                        ImGui::Text("Geometry %u",instance.geometry_reference);
                        std::string path;
                        for (const auto reference : instance.path) { if (!path.empty()) path += " -> "; path += std::to_string(reference); }
                        paragraph(path.c_str());
                        ImGui::TextColored(muted,"Accumulated transform (rows)");
                        for (std::size_t row = 0; row < 4; ++row) ImGui::Text("%.4g  %.4g  %.4g  %.4g",instance.world_transform[row],
                            instance.world_transform[4+row],instance.world_transform[8+row],instance.world_transform[12+row]);
                        if (static_cast<std::size_t>(selected_scene_instance_) < scene_materials_.size()) {
                            const auto& material = scene_materials_[static_cast<std::size_t>(selected_scene_instance_)];
                            if (material.supported) { paragraph(material.image_name.c_str(),teal); paragraph(material.provenance.c_str()); }
                            else paragraph(material.diagnostic.c_str(),amber);
                        }
                    }
                }
                paragraph("Stored static placement. The inspection camera and base-image profile do not reproduce full game scene behavior.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("State",nullptr,scene_state_tab_pending_ ? ImGuiTabItemFlags_SetSelected : 0)) {
                scene_state_tab_pending_ = false;
                draw_scene_state();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Player",nullptr,scene_player_tab_pending_ ? ImGuiTabItemFlags_SetSelected : 0)) {
                scene_player_tab_pending_ = false;
                draw_scene_player();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Status",nullptr,scene_status_tab_pending_ ? ImGuiTabItemFlags_SetSelected : 0)) {
                scene_status_tab_pending_ = false;
                if (!scene_error_.empty()) paragraph(scene_error_.c_str(),amber);
                if (!scene_texture_error_.empty()) paragraph(scene_texture_error_.c_str(),amber);
                if (scene_data_) {
                    if (scene_data_->complete) paragraph("Static traversal complete for this exported root.",teal);
                    for (const auto& diagnostic : scene_data_->diagnostics) {
                        ImGui::Separator();
                        ImGui::Text("Node %u",diagnostic.node_reference);
                        paragraph(diagnostic.reason.c_str(),amber);
                        std::string path;
                        for (const auto reference : diagnostic.path) { if (!path.empty()) path += " -> "; path += std::to_string(reference); }
                        paragraph(path.c_str());
                    }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleVar(2);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("scene_view",ImVec2(0,height),true,ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::Button("Fit")) reset_scene_camera();
        ImGui::SameLine();
        ImGui::BeginDisabled(!scene_player_.uploaded);
        if (ImGui::Button("Focus player")) scene_player_focus_pending_ = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        int mode = scene_mode_ == dw4::viewer::MeshDrawMode::textured ? 2 : scene_mode_ == dw4::viewer::MeshDrawMode::wireframe ? 1 : 0;
        if (ImGui::Combo("##scene_draw_mode",&mode,"Solid\0Wireframe\0Textured\0")) {
            scene_mode_ = mode == 2 ? dw4::viewer::MeshDrawMode::textured : mode == 1 ? dw4::viewer::MeshDrawMode::wireframe : dw4::viewer::MeshDrawMode::solid;
            if (scene_mode_ == dw4::viewer::MeshDrawMode::textured) upload_scene_batches(true);
            else if (scene_data_ && scene_data_->complete && (!scene_renderer_.has_mesh() || scene_render_failed_)) upload_scene_batches(false);
        }
        if (ImGui::GetWindowWidth() > 900) { ImGui::SameLine(); ImGui::TextColored(muted,"Drag: orbit / right: pan / wheel: zoom"); }
        if (!scene_error_.empty()) paragraph(scene_error_.c_str(),amber);
        else if (scene_mode_ == dw4::viewer::MeshDrawMode::textured && !scene_texture_error_.empty()) {
            paragraph("Textured inspection requires a verified image and UVs for every instance.",amber);
            paragraph(scene_texture_error_.c_str());
            paragraph("Solid and wireframe modes remain available for complete static geometry.");
        } else if (scene_renderer_.has_mesh() && !scene_render_failed_) draw_scene_canvas();
        else paragraph("Select a supported exported root to render its static instances.");
        ImGui::EndChild();
    }

    void draw_scene_player() {
        if (scene_player_.uploaded && scene_player_.visual && scene_player_.placement) {
            paragraph(scene_player_.visual->info.model_name.c_str(),teal);
            const auto& point = *scene_player_.placement;
            ImGui::Text("Group %u / player %u",point.group_index,point.player_index);
            ImGui::TextColored(muted,"Position %.3g, %.3g, %.3g",point.matrix[12],point.matrix[13],point.matrix[14]);
            ImGui::SetNextItemWidth(-1);
            const char* name = scene_player_.clip ? scene_player_.clip->info.name.c_str() : "Stored skin vertices";
            if (ImGui::BeginCombo("##scene_player_clip",name)) {
                if (ImGui::Selectable("Stored skin vertices",!scene_player_.clip)) select_scene_player_animation(-1);
                for (std::size_t i = 0; i < scene_player_.clips.size(); ++i) {
                    const auto& clip = scene_player_.clips[i];
                    ImGui::PushID(static_cast<int>(i));
                    ImGui::BeginDisabled(!clip.supported);
                    if (ImGui::Selectable(clip.name.c_str(),scene_player_.clip && scene_player_.clip->info.reference_index == clip.reference_index))
                        select_scene_player_animation(static_cast<int>(i));
                    ImGui::EndDisabled(); ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (scene_player_.clip) {
                if (ImGui::Button(scene_player_.playing ? "Pause" : "Play")) {
                    scene_player_.playing = !scene_player_.playing;
                    scene_player_.tick = std::chrono::steady_clock::now();
                }
                ImGui::SameLine(); ImGui::Checkbox("Loop",&scene_player_.loop);
                const double minimum = 0, maximum = scene_player_.clip->info.duration_seconds;
                double seconds = scene_player_.time;
                ImGui::SetNextItemWidth(-1);
                if (ImGui::SliderScalar("##scene_player_time",ImGuiDataType_Double,&seconds,&minimum,&maximum,"%.3f s")) {
                    scene_player_.playing = false; update_scene_player_pose(seconds);
                }
            }
        }
        if (ImGui::CollapsingHeader("Placement selection",scene_player_.uploaded ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextWithHint("##player_resource","Logical player resource",scene_player_resource_input_.data(),scene_player_resource_input_.size());
            if (ImGui::Button("Load resource")) load_scene_player_resource(scene_player_resource_input_.data());
            ImGui::SetNextItemWidth(-1);
            const auto model_name = scene_player_model_input_ < 0 ? std::string("Choose a player model") : "Model " + std::to_string(scene_player_model_input_);
            if (ImGui::BeginCombo("##player_model",model_name.c_str())) {
                for (const auto& model : scene_player_.models) {
                    ImGui::BeginDisabled(!model.supported);
                    const auto label = std::to_string(model.reference_index)+"  "+model.name;
                    if (ImGui::Selectable(label.c_str(),scene_player_model_input_ == static_cast<int>(model.reference_index)))
                        scene_player_model_input_ = static_cast<int>(model.reference_index);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",model.diagnostic.c_str());
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(-1);
            const auto point_name = scene_player_point_input_ < 0 ? std::string("Choose map entry points") : "Points " + std::to_string(scene_player_point_input_);
            if (ImGui::BeginCombo("##player_points",point_name.c_str())) {
                for (const auto& point : scene_player_points_) {
                    ImGui::BeginDisabled(!point.supported);
                    const auto label = std::to_string(point.reference_index)+"  "+point.name;
                    if (ImGui::Selectable(label.c_str(),scene_player_point_input_ == static_cast<int>(point.reference_index)))
                        scene_player_point_input_ = static_cast<int>(point.reference_index);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%u groups\n%s",point.group_count,point.diagnostic.c_str());
                    ImGui::EndDisabled();
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(72); ImGui::InputInt("Player slot",&scene_player_slot_input_,0,0);
            ImGui::SetNextItemWidth(72); ImGui::InputInt("Entry group",&scene_player_group_input_,0,0);
            ImGui::SetNextItemWidth(72); ImGui::InputInt("LOD branch",&scene_player_branch_input_,0,0);
            if (ImGui::Button("Place visual")) place_scene_player();
            if (scene_player_.uploaded) { ImGui::SameLine(); if (ImGui::Button("Remove")) remove_scene_player(); }
        }
        if (!scene_player_.error.empty()) paragraph(scene_player_.error.c_str(),amber);
        paragraph("Explicit visual inspection. Startup, movement, collision and combat are not running.");
        if (scene_player_.visual && ImGui::CollapsingHeader("Visual provenance")) paragraph(scene_player_.visual->info.provenance.c_str());
    }

    void draw_scene_state() {
        if (selected_scene_ < 0 || static_cast<std::size_t>(selected_scene_) >= scenes_.size()) {
            paragraph("Select an exported scene root to inspect its original switch controls.");
            return;
        }
        const auto root_reference = scenes_[static_cast<std::size_t>(selected_scene_)].root_reference;
        std::optional<dw4::SwitchControlInfo> selected_control;
        std::size_t matching_controls = 0;
        for (const auto& control : scene_switch_controls_) if (control.target_reference == root_reference) {
            ++matching_controls;
            if (static_cast<int>(control.reference_index) == selected_scene_switch_control_) selected_control = control;
        }
        paragraph("Explicit original switch commands",teal);
        if (!scene_switch_error_.empty()) paragraph(scene_switch_error_.c_str(),amber);
        if (!matching_controls) {
            paragraph("No exported controller targets this exact scene root.");
        } else {
            const auto preview = selected_control ? std::to_string(selected_control->reference_index) + "  " + selected_control->name :
                "Choose an exported controller";
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##scene_switch_control",preview.c_str())) {
                for (const auto& control : scene_switch_controls_) if (control.target_reference == root_reference) {
                    ImGui::PushID(static_cast<int>(control.reference_index));
                    ImGui::BeginDisabled(!control.supported);
                    const auto label = std::to_string(control.reference_index) + "  " + control.name;
                    if (ImGui::Selectable(label.c_str(),selected_scene_switch_control_ == static_cast<int>(control.reference_index))) {
                        selected_scene_switch_control_ = static_cast<int>(control.reference_index);
                        selected_control = control;
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Target %u / %u children / %u mask bits\n%s",control.target_reference,
                            control.child_count,control.mask_bit_count,control.diagnostic.c_str());
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (selected_control) {
                ImGui::Text("Target %u / %u children",selected_control->target_reference,selected_control->child_count);
                if (!selected_control->diagnostic.empty()) paragraph(selected_control->diagnostic.c_str(),amber);
            }
            std::optional<dw4::SwitchControlCommand> command;
            ImGui::BeginDisabled(!selected_control || !selected_control->supported);
            if (ImGui::Button("ALLON")) command = dw4::SwitchControlCommand::all_on;
            ImGui::SameLine();
            if (ImGui::Button("ALLOF")) command = dw4::SwitchControlCommand::all_off;
            ImGui::EndDisabled();
            if (command && selected_control) select_scene(selected_scene_,std::nullopt,
                dw4::SwitchControlRequest{selected_control->reference_index,*command});
        }
        if (ImGui::Button("Stored selection")) select_scene(selected_scene_);
        paragraph("Game startup, scripts and staged loading are not running.");
        if (!scene_error_.empty()) paragraph(scene_error_.c_str(),amber);
    }

    void draw_scene_canvas() {
        const auto available = ImGui::GetContentRegionAvail();
        const ImVec2 size(std::max(1.0f,available.x),std::max(120.0f,available.y));
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("scene_canvas",size,ImGuiButtonFlags_MouseButtonLeft |
            ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const auto& io = ImGui::GetIO();
        if (ImGui::IsItemHovered() && ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY) && io.MouseWheel != 0)
            scene_camera_.zoom(io.MouseWheel);
        if (ImGui::IsItemActive()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                (io.KeyShift && ImGui::IsMouseDragging(ImGuiMouseButton_Left)))
                scene_camera_.pan(-io.MouseDelta.x / size.y,io.MouseDelta.y / size.y);
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                scene_camera_.orbit(-io.MouseDelta.x * 0.008f,-io.MouseDelta.y * 0.008f);
        }
        try {
            const double width = std::max(1.0,static_cast<double>(size.x * io.DisplayFramebufferScale.x));
            const double height = std::max(1.0,static_cast<double>(size.y * io.DisplayFramebufferScale.y));
            const double reduction = std::min({1.0,4096.0 / width,4096.0 / height,std::sqrt(8388608.0 / (width * height))});
            scene_target_width_ = std::max(1,static_cast<int>(width * reduction));
            scene_target_height_ = std::max(1,static_cast<int>(height * reduction));
            if (scene_fit_pending_) fit_scene_bounds(static_cast<float>(scene_target_width_) / scene_target_height_);
            if (scene_player_focus_pending_ && scene_player_.uploaded) {
                scene_renderer_.focus_camera(scene_camera_,scene_player_.mesh.bounds);
                scene_player_focus_pending_ = false;
            }
            const auto target = scene_renderer_.render(scene_target_width_,scene_target_height_,scene_camera_,scene_mode_,settings_.msaa);
            auto* draw = ImGui::GetWindowDrawList();
            const ImVec2 end(origin.x+size.x,origin.y+size.y);
            draw->AddImage(static_cast<ImTextureID>(target),origin,end,ImVec2(0,1),ImVec2(1,0));
            draw->AddRect(origin,end,IM_COL32(53,72,88,255),6);
        } catch (const std::exception& error) {
            scene_error_ = error.what();
            scene_render_failed_ = true;
        }
    }

    void clear_boot_inspection() {
        boot_session_.reset();
        boot_report_.reset();
        boot_error_.clear();
        boot_tab_to_select_ = -1;
        boot_route_view_ = 0;
    }

    std::size_t boot_export_count() const {
        std::size_t count = 0;
        if (boot_session_) for (const auto& file : boot_session_->files) count += file.exports.size();
        return count;
    }

    void run_boot_inspection(const std::string& path) {
        clear_boot_inspection();
        try {
            if (!mounted_resources_) throw std::runtime_error("Mount the original game data before running startup loading.");
            boot_session_ = dw4::load_startup_session(*mounted_resources_,path,boot_inputs_);
            boot_report_ = boot_session_->boot.report();
            if (!boot_report_->boot_scripts_completed) boot_error_ = boot_report_->diagnostic;
            else if (!boot_report_->resources_resolved) boot_error_ = "Some requested resources do not resolve to readable original catalog entries.";
            if (!boot_session_->files_loaded || !boot_session_->info_routing_complete) {
                if (!boot_session_->diagnostic.empty()) boot_error_ = boot_session_->diagnostic;
                else if (boot_error_.empty()) boot_error_ = "Original payload loading or info routing did not complete.";
            }
            boot_tab_to_select_ = 1;
        } catch (const std::exception& error) { boot_error_ = error.what(); }
    }

    void draw_boot_resources(float height) {
        const auto& report = *boot_report_;
        if (ImGui::BeginTable("boot_queue",3,ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,ImVec2(0,height))) {
            ImGui::TableSetupColumn("Order",ImGuiTableColumnFlags_WidthFixed,55);
            ImGui::TableSetupColumn("Original resource request");
            ImGui::TableSetupColumn("Catalog location",ImGuiTableColumnFlags_WidthFixed,170);
            ImGui::TableSetupScrollFreeze(0,1); ImGui::TableHeadersRow();
            ImGuiListClipper clip; clip.Begin(static_cast<int>(report.resources.size()));
            while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                const auto& request = report.resources[static_cast<std::size_t>(row)];
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%d",row);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(request.logical_path.c_str());
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s / PC %zu",request.source.c_str(),request.prototype.c_str(),request.pc);
                ImGui::TableNextColumn();
                if (request.location) ImGui::Text("Archive %u / %u",request.location->archive_index,request.location->entry_index);
                else { ImGui::TextColored(amber,"Unresolved"); if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",request.diagnostic.c_str()); }
            }
            ImGui::EndTable();
        }
    }

    void boot_file_tooltip(const dw4::StartupLoadedFile& file, std::size_t index) const {
        if (!ImGui::IsItemHovered()) return;
        ImGui::SetTooltip("File occurrence %zu / original ordinal %u\nRequest: %s\nCatalog: %s\nArchive %u / entry %u\n%zu bytes / %zu exports / %zu external-directory metadata names",
            index,file.ordinal,file.requested_path.c_str(),file.catalog_path.c_str(),file.location.archive_index,file.location.entry_index,
            file.document.bytes().size(),file.exports.size(),file.document.external_directories.size());
    }

    void draw_boot_files(float height) {
        const auto& files = boot_session_->files;
        if (ImGui::BeginTable("boot_files",5,ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,ImVec2(0,height))) {
            ImGui::TableSetupColumn("Order",ImGuiTableColumnFlags_WidthFixed,45);
            ImGui::TableSetupColumn("Stage",ImGuiTableColumnFlags_WidthFixed,105);
            ImGui::TableSetupColumn("Loaded original file");
            ImGui::TableSetupColumn("Bytes",ImGuiTableColumnFlags_WidthFixed,85);
            ImGui::TableSetupColumn("Exports",ImGuiTableColumnFlags_WidthFixed,65);
            ImGui::TableSetupScrollFreeze(0,1); ImGui::TableHeadersRow();
            ImGuiListClipper clip; clip.Begin(static_cast<int>(files.size()));
            while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                const auto& file = files[static_cast<std::size_t>(row)];
                const char* stage = file.stage == dw4::StartupFileStage::preliminary ? "Preliminary" :
                    file.stage == dw4::StartupFileStage::package ? "Package" : "Common";
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%d",row);
                ImGui::TableNextColumn(); ImGui::Text("%s %u",stage,file.ordinal);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(file.requested_path.c_str());
                boot_file_tooltip(file,static_cast<std::size_t>(row));
                ImGui::TableNextColumn(); ImGui::Text("%zu",file.document.bytes().size());
                ImGui::TableNextColumn(); ImGui::Text("%zu",file.exports.size());
            }
            ImGui::EndTable();
        }
    }

    void draw_boot_routes(float height) {
        const auto& session = *boot_session_;
        ImGui::SetNextItemWidth(235);
        const char* views[]{"Deferred manager candidates", "Immediate Lua route", "Held scenes", "Late scenes (deferred tail)"};
        ImGui::Combo("##boot_route",&boot_route_view_,views,4);
        const std::array<const std::vector<dw4::StartupInfoAddress>*,4> lists{
            &session.infos.deferred_infos,&session.infos.lua_infos,&session.infos.held_scenes,&session.infos.late_scenes};
        const auto& routes = *lists.at(static_cast<std::size_t>(boot_route_view_));
        ImGui::SameLine(); ImGui::TextColored(muted,"%zu occurrences",routes.size());
        const char* descriptions[]{
            "Ordered candidates only. Runtime manager callbacks have not executed.",
            "Lua info occurrences routed immediately while loading, in source order.",
            "Ordinary scenes are held separately for later layer setup.",
            "Camera and ROOTATTR scenes are also included at the end of the deferred list."};
        paragraph(descriptions[boot_route_view_],muted);
        height = std::max(110.0f,height - ImGui::GetFrameHeightWithSpacing() - 35.0f);
        if (ImGui::BeginTable("boot_routes",5,ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,ImVec2(0,height))) {
            ImGui::TableSetupColumn("Order",ImGuiTableColumnFlags_WidthFixed,45);
            ImGui::TableSetupColumn("Source",ImGuiTableColumnFlags_WidthFixed,100);
            ImGui::TableSetupColumn("Reference",ImGuiTableColumnFlags_WidthFixed,75);
            ImGui::TableSetupColumn("Original info type",ImGuiTableColumnFlags_WidthFixed,210);
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupScrollFreeze(0,1); ImGui::TableHeadersRow();
            ImGuiListClipper clip; clip.Begin(static_cast<int>(routes.size()));
            while (clip.Step()) for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                const auto& address = routes[static_cast<std::size_t>(row)];
                const auto& file = session.files.at(address.file_index);
                const auto& info = file.exports.at(address.export_index);
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%d",row);
                ImGui::TableNextColumn(); ImGui::Text("%u / %u",address.file_index,address.export_index);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("File occurrence %u / export ordinal %u\n%s\nArchive %u / entry %u / reference %u",
                    address.file_index,address.export_index,file.requested_path.c_str(),file.location.archive_index,file.location.entry_index,address.reference_index);
                ImGui::TableNextColumn(); ImGui::Text("%u",address.reference_index);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(info.type.c_str());
                ImGui::TableNextColumn(); ImGui::TextUnformatted(info.name.empty() ? "(unnamed)" : info.name.c_str());
                boot_file_tooltip(file,address.file_index);
            }
            ImGui::EndTable();
        }
    }

    void draw_boot() {
        title("Startup inspection", "Execute func/boot and inspect loaded resources. Original preliminary initialization and sound gates are bypassed here.");
        bool changed = false;
        ImGui::SetNextItemWidth(std::max(200.0f,ImGui::GetContentRegionAvail().x-230.0f));
        changed |= ImGui::InputTextWithHint("##boot_package","Logical package descriptor",boot_package_input_.data(),boot_package_input_.size());
        ImGui::SameLine();
        const bool run = ImGui::Button("Run startup loading");
        if (ImGui::CollapsingHeader("Inspection inputs",boot_report_ ? 0 : ImGuiTreeNodeFlags_DefaultOpen)) {
            paragraph("These values are explicit test state, not an imported save. USA country, area and pack-only getters retain their original constant 1.");
            ImGui::SetNextItemWidth(95); changed |= ImGui::InputInt("Difficulty (0-2)",&boot_inputs_.difficulty,0,0);
            ImGui::SameLine(); ImGui::SetNextItemWidth(95); changed |= ImGui::InputInt("Camera (0-1)",&boot_inputs_.camera_mode,0,0);
            for (std::size_t p = 0; p < 4; ++p) {
                ImGui::PushID(static_cast<int>(p));
                const auto label = "Player "+std::to_string(p);
                changed |= ImGui::Checkbox(label.c_str(),&boot_inputs_.players[p].joined);
                ImGui::SameLine(); ImGui::SetNextItemWidth(170);
                changed |= ImGui::InputTextWithHint("##boot_model","Model name",boot_player_names_[p].data(),boot_player_names_[p].size());
                if (boot_inputs_.players[p].joined && ImGui::TreeNode("Weapon names (empty = nil)")) {
                    for (std::size_t s = 0; s < 3; ++s) {
                        ImGui::SetNextItemWidth(170); const auto slot = "Slot "+std::to_string(s);
                        changed |= ImGui::InputText(slot.c_str(),boot_weapon_names_[p][s].data(),boot_weapon_names_[p][s].size());
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }
        if (changed) clear_boot_inspection();
        if (run) {
            for (std::size_t p = 0; p < 4; ++p) {
                auto& player = boot_inputs_.players[p];
                player.model_name = player.joined ? boot_player_names_[p].data() : "";
                for (std::size_t s = 0; s < 3; ++s) player.weapon_names[s] = player.joined && boot_weapon_names_[p][s][0] ?
                    std::optional<std::string>(boot_weapon_names_[p][s].data()) : std::nullopt;
            }
            run_boot_inspection(boot_package_input_.data());
        }
        if (!boot_error_.empty()) paragraph(boot_error_.c_str(),amber);
        if (!boot_report_) { paragraph("Choose a package and run startup loading. Unknown native effects stop with a diagnostic."); return; }
        const auto& report = *boot_report_;
        ImGui::Spacing();
        ImGui::TextColored(report.boot_scripts_completed ? teal : amber,"%s  |  %zu scripts  |  %llu instructions  |  %zu resources",
            report.folder.c_str(),report.scripts.size(),static_cast<unsigned long long>(report.instructions),report.resources.size());
        if (boot_session_) {
            const auto& session = *boot_session_;
            ImGui::TextColored(session.files_loaded && session.info_routing_complete ? teal : amber,
                "%zu files / %.2f MiB / %zu exports  |  %zu deferred / %zu held scenes / %zu Lua infos",
                session.files.size(),static_cast<double>(session.total_bytes)/(1024.0*1024.0),boot_export_count(),
                session.infos.deferred_infos.size(),session.infos.held_scenes.size(),session.infos.lua_infos.size());
            paragraph(session.next_barrier.c_str(),amber);
        }
        for (const auto& script : report.scripts) {
            ImGui::TextColored(script.completed ? teal : amber,"%s: %s / %llu instructions / %llu native calls",script.name.c_str(),
                script.completed ? "executed" : "stopped",static_cast<unsigned long long>(script.instructions),static_cast<unsigned long long>(script.host_calls));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nInfo %u / script index %u\n%s",script.source.c_str(),script.info_reference,script.data_index,script.diagnostic.c_str());
        }
        if (ImGui::BeginTabBar("boot_results")) {
            const float height = std::max(130.0f,ImGui::GetContentRegionAvail().y-55.0f);
            if (ImGui::BeginTabItem("Resources",nullptr,boot_tab_to_select_ == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
                draw_boot_resources(height); ImGui::EndTabItem();
            }
            if (boot_session_ && ImGui::BeginTabItem("Loaded files",nullptr,boot_tab_to_select_ == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                draw_boot_files(height); ImGui::EndTabItem();
            }
            if (boot_session_ && ImGui::BeginTabItem("Info routing",nullptr,boot_tab_to_select_ == 2 ? ImGuiTabItemFlags_SetSelected : 0)) {
                draw_boot_routes(height); ImGui::EndTabItem();
            }
            boot_tab_to_select_ = -1;
            ImGui::EndTabBar();
        }
    }

    void draw_roadmap() {
        title("The path to a playable port", "A staged development plan. Features below are not available until their implementation is verified.");
        struct Stage { const char* state; const char* heading; const char* body; bool ready; };
        const std::array<Stage,6> stages{{
            {"AVAILABLE", "01  Native workbench & data inspection", "Windows x64 interface, host settings, SDL input diagnostics, GameCube / AFS inspection, IGB object parsing and logical resource lookup.", true},
            {"IN PROGRESS", "02  Native original-asset viewer", "Checked meshes, textures, rigs, clips and static exported scene roots render natively. Full materials, actor state and original-frame comparison remain.", false},
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
                         "Native walking preview and asset workbench. Original game data is read only.\n\n"
                         "Usage: dw4_pc [--data EXTRACTED_DIRECTORY] [--config SETTINGS_FILE]\n"
                         "  --play                       Control Agumon in the original map\n"
                         "  --play-smoke                 Replay movement/pause/restart; requires --play --smoke-test 120 or more\n"
                         "              [--smoke-test FRAMES]\n\n"
                         "--data         Inspect this extracted game directory (not an ISO).\n"
                         "--config       Use this INI file; independent of the game directory.\n"
                         "--smoke-test   Render 1-10000 hidden native frames, then exit.\n"
                         "               Does not write settings or launch any emulator.\n"
                         "--screenshot   Save the final smoke-test frame to a BMP path.\n"
                         "--page         Open overview, graphics, controls, assets, textures, meshes, scenes, roadmap or startup.\n"
                         "--archive      AFS filename inside the game directory; requires --entry.\n"
                         "--entry        Zero-based AFS entry for the selected asset viewer.\n"
                         "--resource     Logical catalog path; alternative to --archive/--entry.\n"
                         "--package      Exact package descriptor catalog path; lists original members.\n"
                         "--package-member Original member ordinal; requires --package and --scene-root.\n"
                         "--texture      Zero-based texture-list index (default 0).\n"
                         "--mesh         Zero-based geometry-list index; opens the Meshes page.\n"
                         "--mesh-mode    Solid, wireframe or textured mesh inspection (default solid).\n"
                         "--scene-root   Zero-based exported scene-root index; opens the Scenes page.\n"
                         "--scene-mode   Solid, wireframe or strictly textured static scene inspection.\n"
                         "--scene-inspect-child Explicitly inspect one root-switch child; runtime activation unverified.\n"
                         "--scene-switch-control Exported controller reference; requires --scene-switch-command.\n"
                         "--scene-switch-command all-on | all-off; explicit original command, no game startup.\n"
                         "--scene-player-resource Logical player resource; requires explicit --scene-root and all selectors below.\n"
                         "--scene-player-model Exported player model reference in the player resource.\n"
                         "--scene-player-slot Player/model instance index from 0 to 3.\n"
                         "--scene-player-branch Explicit appearance LOD child index.\n"
                         "--scene-player-point Exported entry-point descriptor reference in the map member.\n"
                         "--scene-player-group Entry-point group index. Placement replaces the outer model transform.\n"
                         "--scene-player-animation Original clip name; optional --scene-player-time in seconds.\n"
                         "--scene-player-play Start the selected player clip; smoke tests require 2+ frames.\n"
                         "--boot-package Execute original func/boot and load requested assets; opens Startup.\n"
                         "--boot-difficulty 0..2 and --boot-camera 0..1: explicit inspection inputs, default 0.\n"
                         "--boot-player-N NAME Join slot N=0..3 with this model; default no joined slots.\n"
                         "--boot-weapon-N-S NAME Weapon for player N, slot S=0..2; omitted slots return nil.\n"
                         "--rig          Decode the selected mesh rig and inspect inverse-bind joint frames.\n"
                         "--bone         Zero-based skeleton bone index for manual inspection; requires --rig.\n"
                         "--rotate-x/y/z Manual joint angles from -180 to 180 degrees; require --rig and --bone.\n"
                         "--animation    Original clip name in the selected rig's database; implies --rig.\n"
                         "--animation-time Paused clip time in seconds (default 0); requires --animation.\n"
                         "--animation-play Start playback at the selected time; smoke tests require 2+ frames.\n"
                         "--max-entry-mib Limit an AFS entry read to 1-256 MiB (default 64).\n";
            return 0;
        }
        return Workbench(std::move(options)).run();
    } catch (const std::exception& error) {
        std::cerr << "dw4_pc: " << error.what() << '\n';
        return 1;
    }
}
