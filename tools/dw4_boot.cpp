#include "dw4/startup.hpp"
#include "dw4/settings.hpp"

#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
int run(const std::vector<std::string>& args) {
    try {
        bool help = false;
        std::string root, package;
        dw4::BootInputs input;
        std::set<std::string> seen;
        for (std::size_t i = 0; i < args.size(); ++i) {
            const auto& option = args[i];
            if (option == "--help") { help = true; continue; }
            if (!option.starts_with("--")) {
                if (!root.empty()) throw std::runtime_error("Provide exactly one extracted game directory.");
                root = option; continue;
            }
            if (!seen.insert(option).second) throw std::runtime_error("Duplicate option: "+option);
            if (++i == args.size()) throw std::runtime_error("Missing value for "+option);
            if (option == "--package") {
                package = args[i];
                if (package.empty()) throw std::runtime_error("Package name is empty.");
            } else if (!dw4::set_boot_input_option(input,option,args[i])) throw std::runtime_error("Unknown option: "+option);
        }
        dw4::validate_boot_inputs(input);
        if (help || args.empty()) {
            std::cout << "DW4 native boot-script execution\n"
                "Usage: dw4_boot EXTRACTED_ROOT --package LOGICAL_PATH [inspection state]\n"
                "--boot-difficulty 0..2   Explicit DIFFICULTY input (default 0).\n"
                "--boot-camera 0..1       Explicit CAMERAMODE input (default 0).\n"
                "--boot-player-N NAME     Join slot N=0..3 with this model name; default no joined slots.\n"
                "--boot-weapon-N-S NAME   Weapon name for player N, slot S=0..2; default nil.\n"
                "USA country, area and pack-only getters retain their original constant 1.\n"
                "Runs func.lua and boot.lua, building the original common-resource queue.\n"
                "Stops before common loading/controller registration; start/first and gameplay do not run.\n"
                "No original files are modified and no host filesystem Lua API is installed.\n";
            return help ? 0 : 2;
        }
        if (root.empty() || package.empty()) throw std::runtime_error("An extracted game directory and --package are required.");
        const auto resources = dw4::MountedResources::open(dw4::utf8_path(root));
        const auto report = dw4::inspect_package_boot(resources,package,input);
        std::cout << "Package: " << report.package_path << " / folder " << report.folder << '\n'
            << "Inspection state: difficulty=" << input.difficulty << ", camera=" << input.camera_mode
            << ", USA country/area/pack-only=1\n";
        for (std::size_t i = 0; i < input.players.size(); ++i) {
            const auto& player = input.players[i];
            std::cout << "Player " << i << ": " << (player.joined ? player.model_name : "not joined") << " / weapons";
            for (const auto& weapon : player.weapon_names) std::cout << ' ' << (weapon ? *weapon : "nil");
            std::cout << '\n';
        }
        for (const auto& script : report.scripts) std::cout << script.name << ": " << (script.completed ? "executed" : "stopped")
            << " / " << script.instructions << " instructions / " << script.host_calls << " native calls / " << script.source
            << " / info " << script.info_reference << " / data index " << script.data_index << '\n';
        std::cout << "Common resource requests: " << report.resources.size() << '\n' << "index\trequest\tlocation\n";
        for (std::size_t i = 0; i < report.resources.size(); ++i) {
            const auto& request = report.resources[i];
            std::cout << i << '\t' << request.logical_path << '\t';
            if (request.location) std::cout << request.location->archive_index << ':' << request.location->entry_index;
            else std::cout << request.diagnostic;
            std::cout << '\n';
        }
        if (!report.diagnostic.empty()) std::cerr << report.diagnostic << '\n';
        if (!report.next_barrier.empty()) std::cout << "Next native barrier: " << report.next_barrier << '\n';
        return report.boot_scripts_completed && report.resources_resolved ? 0 : 3;
    } catch (const std::exception& error) { std::cerr << "dw4_boot: " << error.what() << '\n'; return 1; }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(dw4::path_utf8(std::filesystem::path(argv[i])));
    return run(args);
}
