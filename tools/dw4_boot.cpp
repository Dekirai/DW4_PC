#include "dw4/startup_loading.hpp"
#include "dw4/controllers.hpp"
#include "dw4/settings.hpp"

#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {
int run_preliminary(const dw4::MountedResources& resources, const std::string& package) {
    const auto stage = dw4::load_startup_preliminary(resources, package);
    std::cout << "Preliminary package: " << package << '\n'
        << "Native callback context: fresh prepared system/messenger dependencies.\n"
        << "Original manager construction and audio services are not implemented by this context.\n"
        << "Preliminary files: " << stage.files.size() << " / " << stage.total_bytes << " bytes\n";
    std::vector<dw4::StartupSource> sources;
    for (std::size_t i = 0; i < stage.files.size(); ++i) {
        const auto& file = stage.files[i];
        std::cout << "File " << i << " [preliminary " << file.ordinal << "]: " << file.requested_path
            << " / " << file.location.archive_index << ':' << file.location.entry_index
            << " / " << file.exports.size() << " exports\n";
        sources.push_back({&file.document, file.requested_path});
    }
    if (!stage.files_loaded || !stage.info_routing_complete) {
        std::cerr << stage.diagnostic << '\n';
        std::cout << "No preliminary callbacks ran after incomplete loading or routing.\n";
        return 3;
    }
    // These are explicit dependencies of the recovered callbacks, not evidence
    // that the original full registry construction and preparation already ran.
    const auto callbacks = dw4::initialize_controller_prefix(sources, stage.infos.deferred_infos, {true, true});
    for (const auto& step : callbacks.steps) {
        std::cout << "Callback " << step.deferred_index << ": file " << step.source.file_index
            << " / export " << step.source.export_index << " / ref " << step.source.reference_index
            << " / " << step.type << " / " << (step.completed ? "initialized" : "stopped") << '\n';
    }
    std::cout << "Initialized infos: " << callbacks.registered_infos.size()
        << " / owned RAMs: " << callbacks.rams.size() << " / recipients: " << callbacks.recipients.size() << '\n';
    for (const auto& recipient : callbacks.recipients) {
        const auto& ram = callbacks.rams.at(recipient.ram_index);
        std::cout << "Recipient: " << recipient.category << " / " << recipient.name << " / " << recipient.instance_index
            << " / RAM " << ram.object.file_index << ':' << ram.object.reference_index << '\n';
    }
    for (const auto& ram : callbacks.rams) {
        std::cout << "RAM " << ram.object.file_index << ':' << ram.object.reference_index
            << ": parent info " << ram.parent_info.file_index << ':' << ram.parent_info.reference_index
            << " / ordinal " << ram.ram_no << " / isRun " << ram.is_run << " / state";
        for (const auto value : ram.state) std::cout << ' ' << value;
        std::cout << '\n';
    }
    if (!callbacks.diagnostic.empty()) std::cout << "Next native barrier: " << callbacks.diagnostic << '\n';
    else std::cout << "Next native barrier: original SOUND SEARCH / MAKEBINLIST / ISDONETRANS services.\n";
    std::cout << "No ordinary package members or common resources were requested.\n"
        << "func.lua and boot.lua were not executed.\n";
    // Even an empty or fully supported preliminary queue cannot establish the
    // original sound-transfer gate. This command never reports complete startup.
    return 3;
}
int run(const std::vector<std::string>& args) {
    try {
        bool help = false, preliminary = false, inspection_input = false;
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
            if (option == "--preliminary") { preliminary = true; continue; }
            if (++i == args.size()) throw std::runtime_error("Missing value for "+option);
            if (option == "--package") {
                package = args[i];
                if (package.empty()) throw std::runtime_error("Package name is empty.");
            } else {
                if (!dw4::set_boot_input_option(input,option,args[i])) throw std::runtime_error("Unknown option: "+option);
                inspection_input = true;
            }
        }
        if (preliminary && inspection_input) throw std::runtime_error("--preliminary stops before boot; boot inspection inputs do not apply.");
        dw4::validate_boot_inputs(input);
        if (help || args.empty()) {
            std::cout << "DW4 native boot-script execution\n"
                "Usage: dw4_boot EXTRACTED_ROOT --package LOGICAL_PATH [inspection state]\n"
                "       dw4_boot EXTRACTED_ROOT --package LOGICAL_PATH --preliminary\n"
                "--preliminary           Load only /s members and run supported callbacks in order.\n"
                "                        Uses explicit prepared callback dependencies; stops before sound services/boot.\n"
                "--boot-difficulty 0..2   Explicit DIFFICULTY input (default 0).\n"
                "--boot-camera 0..1       Explicit CAMERAMODE input (default 0).\n"
                "--boot-player-N NAME     Join slot N=0..3 with this model name; default no joined slots.\n"
                "--boot-weapon-N-S NAME   Weapon name for player N, slot S=0..2; default nil.\n"
                "USA country, area and pack-only getters retain their original constant 1.\n"
                "Runs func.lua and boot.lua, then loads the requested common resources and routes their exports.\n"
                "Default mode is aggregate inspection: original preliminary callbacks and sound gates are bypassed.\n"
                "Retains the VM and loaded documents; aggregate routes are not the original later registration queue.\n"
                "Loading-screen state, remaining managers, layer/text initialization and start/first remain pending.\n"
                "No original files are modified and no host filesystem Lua API is installed.\n";
            return help ? 0 : 2;
        }
        if (root.empty() || package.empty()) throw std::runtime_error("An extracted game directory and --package are required.");
        const auto resources = dw4::MountedResources::open(dw4::utf8_path(root));
        if (preliminary) return run_preliminary(resources,package);
        const auto session = dw4::load_startup_session(resources,package,input);
        const auto& report = session.boot.report();
        std::cout << "Aggregate inspection; original preliminary initialization and sound gates have not run.\n"
            << "Package: " << session.package_path << " / folder " << report.folder << '\n'
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
        std::cout << "Loaded files: " << session.files.size() << " / " << session.total_bytes << " bytes / "
            << session.object_count << " objects / " << session.field_count << " decoded fields\n";
        for (std::size_t i = 0; i < session.files.size(); ++i) {
            const auto& file = session.files[i];
            const auto stage = file.stage == dw4::StartupFileStage::preliminary ? "preliminary" :
                file.stage == dw4::StartupFileStage::package ? "package" : "common";
            std::cout << "File " << i << " [" << stage << ' ' << file.ordinal << "]: " << file.requested_path
                << " / " << file.location.archive_index << ':' << file.location.entry_index << " / " << file.document.bytes().size()
                << " bytes / " << file.exports.size() << " exports / " << file.document.external_directories.size() << " external-directory metadata names\n";
        }
        std::cout << "Info routing: " << session.infos.lua_infos.size() << " immediate Lua / " << session.infos.held_scenes.size()
            << " held scenes / " << session.infos.deferred_infos.size() << " deferred (including " << session.infos.late_scenes.size() << " late scenes)\n";
        const auto routes = [&](const char* label, const auto& infos) {
            for (std::size_t i = 0; i < infos.size(); ++i) {
                const auto& address = infos[i]; const auto& file = session.files.at(address.file_index);
                const auto& info = file.exports.at(address.export_index);
                std::cout << label << ' ' << i << ": file " << address.file_index << " / export " << address.export_index
                    << " / ref " << address.reference_index << " / " << info.type << " / " << info.name << '\n';
            }
        };
        routes("Lua info",session.infos.lua_infos); routes("Held scene",session.infos.held_scenes); routes("Deferred info",session.infos.deferred_infos);
        std::cout << "Retained Lua state: " << (session.boot.has_live_state() ? "yes" : "no") << '\n';
        if (!session.diagnostic.empty()) std::cerr << session.diagnostic << '\n';
        std::cout << "Next native barrier: " << session.next_barrier << '\n';
        return session.files_loaded && session.info_routing_complete ? 0 : 3;
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
