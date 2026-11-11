// Native Digimon World 4 runtime: SDL3 window, OpenGL renderer and the
// reconstructed engine in dw4::engine. Original data is read from --data.
#include "dev_present.hpp"
#include "gl_renderer.hpp"
#include "host_view.hpp"
#include "overflow_host.hpp"
#include "interpolation.hpp"
#include "mods.hpp"
#include "netplay.hpp"
#include "movie_decoder.hpp"
#include "rvz.hpp"
#include "save_store.hpp"
#include "settings.hpp"
#include "controls.hpp"
#include "campaign.hpp"
#include "online_script.hpp"
#include "randomizer_save.hpp"
#include "test_path.hpp"
#include "texture_pack.hpp"
#include "achievements.hpp"
#include "discord_presence.hpp"
#include "overhead_labels.hpp"
#include "ghost_figures.hpp"
#include "presence_art.hpp"
#include "presence_data.hpp"
#include "online_run.hpp"
#include "cloud_backup_panel.hpp"
#include "online_ui.hpp"
#include "online_ui_script.hpp"
#include "title_entry.hpp"
#include "online/lobby.hpp"
#include "online/title_driver.hpp"
#include "online_achievements.hpp"
#include "online/cpu_gate.hpp"
#include "online/env_check.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/engine/randomizer.hpp"
#include "dw4/disc.hpp"
#include "dw4/engine/state_archive.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <array>
#include <atomic>
#include <deque>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::filesystem::path data;
    std::string stage = "stagis08";
    bool stage_given{}; // --stage on the command line
    int smoke_frames = 0;
    std::string screenshot;
    std::vector<int> screenshot_frames;
    std::string input_script;
    std::filesystem::path online_script; // --online-script FILE: the scripted pad of an online room test run (online_script.hpp)
    std::filesystem::path online_card;   // --online-card FOLDER (DW4_ONLINE_DEV builds): the online card starts with these saves
    std::filesystem::path online_ui;     // --online-ui FILE (test runs): the account and room screens driven by a script (online_ui_script.hpp)
    std::filesystem::path online_folder; // --online-folder DIR (test runs): the remembered login there instead of saves/online beside the program
    std::filesystem::path load_state; // --load-state FILE: start from a save state file
    bool overlay = true;
    std::vector<float> debug_camera; // eye xyz, target xyz
    std::vector<std::pair<int, int>> flags;
    std::filesystem::path card_dir; // memory card GCI folders (Card A / Card B)
    std::filesystem::path save_file; // --save-file FILE: slot A's single save file (slot B: <stem>_b<ext>)
    // --import-save FILE[:BOXES] (--import-slot A|B) / --save-info FILE: GC
    // save import into the run's store, or only its preview; then exit.
    std::filesystem::path import_save, save_info;
    std::string import_boxes;
    int import_slot = 0;
    std::filesystem::path convert_from, convert_to; // --convert-rvz IN OUT
    // Online co-op: --host (with --players, --port, --net-delay) or --join ADDRESS[:PORT].
    bool net_host{};
    std::string net_join;
    int net_port = 52040, net_players = 2, net_delay = 3;
    // The online world (online_run.hpp, docs/SEAMLESS.md 19.21): --online-login
    // NAME --online-play [BOX] (Play from the test profile); --online-calibrate
    // STEPS prints the replay rate of this machine and exits.
    dw4::game::OnlineOptions online;
    int online_calibrate = 0;
    std::string online_calibrate_stage = "stag0102";
    // --loading original|fast: this run's load timing instead of settings.ini
    // [gameplay] loading (test runs: else DW4_FAST_LOADING=1).
    std::optional<bool> fast_loading;
    std::filesystem::path export_art; // --export-presence-art FOLDER
    // --overflow-run KEY=VALUE ...: a Data Overflow run (overflow_run.hpp's
    // RunRequest), at once or after story frame at=F.
    bool overflow_given{};
    std::vector<std::string> overflow_run;
};


using dw4::game::write_bmp;

// Scripted input for automated runs: lines "frame button [duration [player]]"
// (button: A,B,X,Y,START,Z,L,R, UP,DOWN,LEFT,RIGHT (the D-pad bit and the main
// stick pushed fully: the scripts' combined direction), DPAD_UP,DPAD_DOWN,
// DPAD_LEFT,DPAD_RIGHT (the D-pad only), STICK:x,y / CSTICK:x,y (main stick /
// C-stick at raw device values), several joined by '+'; player 1-4, default 1)
// and "frame GOTO x z [duration]",
// which steers player 1's stick toward world (x, z) relative to the camera, and
// "frame SEEK duration [target]", which walks to the nearest pickup or live
// enemy and presses A next to it (target: a model prefix first, mat:<name> a
// route onto a mat, explore:<mat> play through the stage to that mat,
// campaign: the story from a new game, src/game/campaign.cpp), and
// "frame SEND CATEGORY COMMAND [argument]".
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
            // The button may join several with '+'. UP / DOWN / LEFT / RIGHT
            // keep their meaning from before the separate D-pad (scripts,
            // determinism references and the campaign checkpoints rely on
            // it): the D-pad bit, and the main stick pushed fully unless a
            // GOTO / SEEK steers or a STICK holds it. DPAD_* is the D-pad
            // alone. STICK:x,y / CSTICK:x,y hold the main stick / C-stick at
            // the raw device values x,y (-128..127, y up, as work/dref's
            // dtm.py / dtm2.py record them), which the SDK clamp turns into
            // the stick the game reads (gamecube_stick).
            Press press{frame, 0, duration, std::clamp(player - 1, 0, 3)};
            for (std::size_t begin = 0; begin <= name.size();) {
                const std::size_t end = std::min(name.find('+', begin), name.size());
                const std::string part = name.substr(begin, end - begin);
                begin = end + 1;
                namespace b = dw4::pad_button;
                namespace d = dw4::pad_direction;
                int x = 0, y = 0;
                auto raw = [&](bool c_stick) {
                    return dw4::gamecube_stick(std::int8_t(std::clamp(x, -128, 127)), std::int8_t(std::clamp(y, -128, 127)), c_stick);
                };
                if (part.rfind("STICK:", 0) == 0 && std::sscanf(part.c_str() + 6, "%d,%d", &x, &y) == 2) press.stick = raw(false);
                else if (part.rfind("CSTICK:", 0) == 0 && std::sscanf(part.c_str() + 7, "%d,%d", &x, &y) == 2) press.c_stick = raw(true);
                else if (part == "A") press.bit |= b::a;
                else if (part == "B") press.bit |= b::b;
                else if (part == "X") press.bit |= b::x;
                else if (part == "Y") press.bit |= b::y;
                else if (part == "START") press.bit |= b::start;
                else if (part == "UP") press.bit |= b::up, press.push |= d::positive_y;
                else if (part == "DOWN") press.bit |= b::down, press.push |= d::negative_y;
                else if (part == "LEFT") press.bit |= b::left, press.push |= d::negative_x;
                else if (part == "RIGHT") press.bit |= b::right, press.push |= d::positive_x;
                else if (part == "DPAD_UP") press.bit |= b::up;
                else if (part == "DPAD_DOWN") press.bit |= b::down;
                else if (part == "DPAD_LEFT") press.bit |= b::left;
                else if (part == "DPAD_RIGHT") press.bit |= b::right;
                else if (part == "Z") press.bit |= b::z;
                else if (part == "L") press.bit |= b::l;
                else if (part == "R") press.bit |= b::r;
            }
            presses.push_back(press);
        }
    }
    struct Send { int frame; std::string category, command, argument; int row = -1; };
    std::vector<Send> sends;
    struct Press {
        int frame;
        std::uint32_t bit; // pad_button bits
        int duration;
        int slot;
        std::optional<dw4::PadStick> stick{}, c_stick{};
        std::uint32_t push{}; // pad_direction bits pushing the main stick (UP / DOWN / LEFT / RIGHT)
    };
    std::vector<Press> presses;
    // The STICK (CSTICK) of the last press holding one at this frame on the slot.
    std::optional<dw4::PadStick> stick(int frame, int slot = 0, bool c_stick = false) const {
        std::optional<dw4::PadStick> result;
        for (const auto& p : presses)
            if (p.slot == slot && frame >= p.frame && frame < p.frame + p.duration)
                if (const auto& held = c_stick ? p.c_stick : p.stick) result = held;
        return result;
    }
    // The main stick directions the slot's combined direction presses push.
    std::uint32_t pushed(int frame, int slot = 0) const {
        std::uint32_t directions = 0;
        for (const auto& p : presses)
            if (p.slot == slot && frame >= p.frame && frame < p.frame + p.duration) directions |= p.push;
        return directions;
    }
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
        return "This is not a GameCube disc image the runtime can read:\n" + path.string() +
               "\n\nWIA, CISO and NKit images can be converted to ISO or RVZ in Dolphin "
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

// An RVZ image becomes a plain disc image once, in data/ next to the program
// (a window shows the progress; closing it cancels). Returns the problem.
std::string prepare_rvz(const std::filesystem::path& rvz, const std::filesystem::path& iso) {
    std::atomic<double> progress{0.0};
    std::atomic<bool> cancel{false}, done{false};
    std::string result;
    std::thread worker([&] {
        result = dw4::game::convert_rvz(rvz, iso, &progress, &cancel);
        done.store(true);
    });
    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Digimon World 4 PC", 520, 96, 0);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    while (!done.load()) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) cancel.store(true);
        if (renderer) {
            SDL_SetRenderDrawColor(renderer, 24, 28, 40, 255);
            SDL_RenderClear(renderer);
            SDL_SetRenderDrawColor(renderer, 230, 230, 230, 255);
            SDL_RenderDebugText(renderer, 16, 16, "Preparing the game data from the RVZ image (once)...");
            const SDL_FRect frame{16, 48, 488, 24};
            SDL_RenderRect(renderer, &frame);
            const SDL_FRect bar{18, 50, float(484.0 * std::clamp(progress.load(), 0.0, 1.0)), 20};
            SDL_SetRenderDrawColor(renderer, 80, 160, 255, 255);
            SDL_RenderFillRect(renderer, &bar);
            SDL_RenderPresent(renderer);
        }
        SDL_Delay(16);
    }
    worker.join();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    return result;
}

// RVZ data: the converted image next to the program, converting it first
// when needed. Other paths come back unchanged.
std::filesystem::path resolve_rvz(const std::filesystem::path& path, std::string& problem) {
    if (!dw4::game::is_rvz(path)) return path;
    std::filesystem::path base;
    if (const char* program = SDL_GetBasePath()) base = program;
    const auto iso = base / "data" / (path.stem().string() + ".iso");
    std::error_code error;
    if (std::filesystem::is_regular_file(iso, error)) return iso;
    problem = prepare_rvz(path, iso);
    return problem.empty() ? iso : std::filesystem::path();
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
                                 "Choose your Digimon World 4 (USA) game data: a disc image (.iso / .gcm, or "
                                 "Dolphin's .rvz, converted once) or a Dolphin extraction folder. The choice is "
                                 "saved in settings.ini.",
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
    static const SDL_DialogFileFilter filters[] = {{"GameCube disc image", "iso;gcm;rvz"}, {"All files", "*"}};
    if (choice == 1) SDL_ShowOpenFileDialog(callback, &result, nullptr, filters, 2, nullptr, false);
    else SDL_ShowOpenFolderDialog(callback, &result, nullptr, nullptr, false);
    while (!result.done.load()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {}
        SDL_Delay(10);
    }
    return result.path;
}

using dw4::game::KeyCapture;

// A save state: the snapshot of the running stage's load and the controller
// input of every step since; loading replays them on a new engine.
// One logged step: the controllers and whether a picture was drawn after it
// (the loading screens run on the drawing clock, as on the original).
struct LoggedStep {
    std::array<dw4::engine::PadSample, 4> pads;
    bool drawn{};
};

struct SaveState {
    dw4::engine::StageSnapshot snapshot;
    bool load_step_drawn{}; // a picture followed the step that started the stage load
    std::vector<LoggedStep> inputs;
    std::uint64_t fingerprint{};
    int frame{};
};

// Save state files (saves/states/state<N>.dw4state next to the program):
// "DW4STATE", version, the snapshot bytes, then the logged steps.
constexpr std::uint32_t kStateFileVersion = 1;

bool write_state_file(const std::filesystem::path& path, const SaveState& state) {
    std::vector<std::uint8_t> out;
    dw4::engine::StateArchive a(out);
    std::array<char, 8> magic{'D', 'W', '4', 'S', 'T', 'A', 'T', 'E'};
    a.io(magic);
    std::uint32_t version = kStateFileVersion;
    a.io(version);
    auto snapshot = dw4::engine::encode_snapshot(state.snapshot);
    a.io(snapshot);
    bool drawn = state.load_step_drawn;
    a.io(drawn);
    std::uint32_t count = std::uint32_t(state.inputs.size());
    a.io(count);
    for (auto step : state.inputs) {
        for (auto& pad : step.pads) {
            a.io(pad.connected);
            a.io(pad.left.x);
            a.io(pad.left.y);
            a.io(pad.right.x);
            a.io(pad.right.y);
            a.io(pad.buttons);
        }
        a.io(step.drawn);
    }
    std::uint64_t fingerprint = state.fingerprint;
    a.io(fingerprint);
    std::int32_t frame = state.frame;
    a.io(frame);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
    return bool(file);
}

std::optional<SaveState> read_state_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    try {
        dw4::engine::StateArchive a{std::span<const std::uint8_t>(bytes)};
        std::array<char, 8> magic{};
        a.io(magic);
        std::uint32_t version = 0;
        a.io(version);
        if (std::string(magic.data(), 8) != "DW4STATE" || version != kStateFileVersion) return std::nullopt;
        SaveState state;
        std::vector<std::uint8_t> snapshot;
        a.io(snapshot);
        state.snapshot = dw4::engine::decode_snapshot(snapshot);
        a.io(state.load_step_drawn);
        std::uint32_t count = 0;
        a.io(count);
        if (count > bytes.size()) return std::nullopt;
        state.inputs.resize(count);
        for (auto& step : state.inputs) {
            for (auto& pad : step.pads) {
                a.io(pad.connected);
                a.io(pad.left.x);
                a.io(pad.left.y);
                a.io(pad.right.x);
                a.io(pad.right.y);
                a.io(pad.buttons);
            }
            a.io(step.drawn);
        }
        a.io(state.fingerprint);
        std::int32_t frame = 0;
        a.io(frame);
        state.frame = frame;
        return state;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// Online co-op card handling (save_store.hpp): a machine shares only the DW4
// save of its slot A (never the card's other files); every machine plays
// from a session copy of the shared cards, and the owner's save goes back
// at the end.
using dw4::game::session_card;
using dw4::game::write_session_card;

// The names of a session card's files for the log ("-" when none).
std::string card_names(const std::vector<dw4::game::CardFile>& files) {
    std::string names;
    for (const auto& file : files) names += (names.empty() ? "" : ",") + file.name;
    return names.empty() ? "-" : names;
}

// The program's folder ("" when unknown).
std::filesystem::path program_dir() {
    const char* base = SDL_GetBasePath();
    return base ? std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(base))) : std::filesystem::path();
}

// The online account's achievements of a room (online_achievements.hpp):
// their own HTTPS worker with the room's login, the account's profile in
// saves/online/<account>/achievements.dat (test runs: DW4_ONLINE_PROFILE,
// else none).
std::unique_ptr<dw4::game::OnlineAchievements> make_online_achievements(dw4::game::OnlineRun& online, bool created, bool test_run,
                                                                        const std::filesystem::path& online_folder) {
    const auto& room = online.room();
    auto worker = std::make_unique<dw4::game::online::HttpWorker>(room.endpoint, std::string("DW4_PC/") + DW4_PC_VERSION);
    worker->set_token(room.token);
    dw4::game::OnlineAchievementConfig config;
    // (Round I1a: the server takes the world context from the relay, 19.8.9.)
    config.room_code = "world";
    config.owner_hint = created;
    config.room_interval = 0; // (no room to ask: the session's facts, set_world_facts)
    config.randomizer = room.settings.randomizer();
    config.protocol = int(dw4::game::kOnlineProtocol);
    config.client_version = DW4_PC_VERSION;
    std::string account;
    for (const char c : room.account) account += std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' ? char(std::tolower(c)) : '_';
    if (!test_run && !account.empty()) config.profile_file = online_folder / account / "achievements.dat";
    else if (const char* file = std::getenv("DW4_ONLINE_PROFILE"); test_run && file && *file) config.profile_file = file;
    return std::make_unique<dw4::game::OnlineAchievements>(std::make_unique<dw4::game::online::achievements::HttpQueue>(std::move(worker)),
                                                           std::move(config));
}

// The import preview: one line per box of the save in `source`.
std::vector<std::string> import_preview(const dw4::save::ImportSource& source) {
    std::vector<std::string> lines;
    const auto boxes = dw4::save::summarize_save(source.gci);
    if (!boxes) return lines;
    for (std::size_t i = 0; i < boxes->size(); ++i) lines.push_back("box " + std::to_string(i + 1) + ": " + dw4::save::describe_box((*boxes)[i]));
    return lines;
}

// The game data identity peers compare (catalog paths and locations).
std::uint64_t data_hash(const dw4::MountedResources& resources) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto mix = [&](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            h ^= bytes[i];
            h *= 0x100000001b3ull;
        }
    };
    for (const auto& archive : resources.catalog().archives) mix(&archive.declared_entries, sizeof archive.declared_entries);
    // Mods change the data: their file names and sizes count.
    for (const auto& folder : resources.overrides()) {
        std::vector<std::string> files;
        std::error_code error;
        for (auto it = std::filesystem::recursive_directory_iterator(folder, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
            if (it->is_regular_file(error))
                files.push_back(std::filesystem::relative(it->path(), folder, error).generic_string() + ":" +
                                std::to_string(it->file_size(error)));
        std::sort(files.begin(), files.end());
        for (const auto& file : files) mix(file.data(), file.size());
    }
    for (const auto& resource : resources.catalog().resources) {
        mix(resource.logical_path.data(), resource.logical_path.size());
        if (resource.location) {
            mix(&resource.location->archive_index, sizeof resource.location->archive_index);
            mix(&resource.location->entry_index, sizeof resource.location->entry_index);
        }
    }
    return h;
}

// Round I1b: the online script's go-along view of the session (online_script.hpp).
void go_facts(dw4::game::OnlineScriptFacts& facts, const dw4::game::online::Session& session, double now) {
    for (const auto& offer : session.go_offers())
        if (offer.open) {
            ++facts.offers;
            facts.open_offers.emplace_back(offer.id, offer.stage);
            if (!facts.newest_offer) {
                facts.newest_offer = offer.id;
                facts.offer_stage = offer.stage;
            }
        }
    for (const auto& offer : session.go_offers())
        if (!offer.open && !facts.closing_offer) facts.closing_offer = offer.id;
    facts.notices = int(session.go_notices(now).size());
    static const char* kGo[] = {"none", "asked", "waiting", "boss", "stamped", "going"};
    facts.go = kGo[std::min<std::size_t>(std::size_t(session.go_state()), 5)];
    facts.go_back = session.overlay(now).go_back;
    for (const auto& offer : session.go_offers())
        if (offer.open) {
            facts.go_ready = session.go_grey(offer).empty();
            break;
        }
}

int run(const Options& options, dw4::game::Settings& settings) {
    // The engine is replaced in place when a save state loads (same storage,
    // so `engine` keeps naming the running one).
    std::optional<dw4::engine::Engine> engine_storage;
    engine_storage.emplace(options.data);
    auto& engine = *engine_storage;
    // Dev presentation tools of test runs (dev_present.hpp; off unless their
    // DW4_DEV_* variables are set): a fixed virtual refresh with smooth
    // motion, the window settings of a settings.ini copy, picture dumps.
    const auto dev = options.smoke_frames ? dw4::game::DevPresent::from_environment() : dw4::game::DevPresent{};
    const bool dev_window = options.smoke_frames && dev.window_settings();
    if (dev_window) dev.apply(settings);
    // Steps of the engines a console reboot replaced (Engine::reboot_due):
    // the session's step count for the online frames and the sound output.
    std::uint64_t frame_base = 0;
    // Memory cards (save_store.hpp): --card-dir, DW4_CARD_DIR (GCI folders),
    // --save-file (single files), a test run's GCI folders
    // <program>/saves/GC/USA, settings.ini [data] cards (a GCI root such as
    // Dolphin's GC/USA, live), else the single save files [data] save /
    // save_b (saves/savedata.dat next to the program; an old saves/GC/USA
    // save is copied there once).
    const auto save_choice = dw4::game::choose_save_store(options.card_dir, options.save_file, options.smoke_frames != 0, settings, program_dir());
    if (save_choice.settings_changed && !options.smoke_frames) settings.save();
    engine.set_card_store(save_choice.store);
    for (const auto& note : save_choice.notes) std::cout << "saves: " << note << '\n';
    for (const auto& problem : save_choice.problems) std::cout << "saves: " << problem << '\n';
    if (save_choice.why == dw4::game::SaveStoreChoice::Why::save_file || save_choice.why == dw4::game::SaveStoreChoice::Why::player)
        std::cout << "saves: slot A " << save_choice.store.describe(0) << ", slot B " << save_choice.store.describe(1) << '\n';
    // The randomizer's bond to the save (randomizer_save.hpp) lives next to
    // the save: beside saves/savedata.dat (or the --save-file), in the GCI
    // root of --card-dir / DW4_CARD_DIR, else saves/ beside the program (a
    // test run's default folders; never inside a [data] cards folder such
    // as Dolphin's GC/USA).
    std::filesystem::path randomizer_folder;
    switch (save_choice.why) {
    case dw4::game::SaveStoreChoice::Why::card_dir:
    case dw4::game::SaveStoreChoice::Why::env_card_dir: randomizer_folder = save_choice.store.root(); break;
    case dw4::game::SaveStoreChoice::Why::save_file:
    case dw4::game::SaveStoreChoice::Why::player: randomizer_folder = save_choice.store.save_path(0).parent_path(); break;
    case dw4::game::SaveStoreChoice::Why::test_run:
    case dw4::game::SaveStoreChoice::Why::dolphin_folder:
        if (const auto program = program_dir(); !program.empty()) randomizer_folder = program / "saves";
        break;
    }
    // What the slots hold: DW4_CARD_A / DW4_CARD_B, else settings.ini [data]
    // card_a / card_b (not in test runs): "none" (an empty slot),
    // "unformatted", "inserted" and / or the card's blocks ("251"; default
    // 2043 like Dolphin's GCI folder cards).
    for (int slot = 0; slot < 2; ++slot) {
        const char* variable = slot == 0 ? "DW4_CARD_A" : "DW4_CARD_B";
        std::string text;
        if (const char* value = std::getenv(variable)) text = value;
        else if (!options.smoke_frames) text = slot == 0 ? settings.card_a : settings.card_b;
        if (text.empty()) continue;
        if (const auto setting = dw4::engine::parse_card_setting(text)) engine.set_card_setting(slot, *setting);
        else std::cerr << variable << ": unknown memory card setting '" << text << "'\n";
    }
    // The player's own cards (what a state load and the end of an online
    // session go back to).
    const auto player_store = engine.card_store();
    const std::array<dw4::engine::CardSetting, 2> player_card_settings{engine.card_setting(0), engine.card_setting(1)};
    for (const auto& [flag, value] : options.flags) engine.flags()[flag] = static_cast<std::uint8_t>(value);
    // Content mods: mods/<id> next to the program (else in the working
    // folder); test runs only with DW4_MODS=<mods folder>.
    std::filesystem::path mods_root;
    if (options.smoke_frames) {
        if (const char* folder = std::getenv("DW4_MODS")) mods_root = folder;
    } else {
        std::error_code error;
        if (const char* program = SDL_GetBasePath(); program && std::filesystem::is_directory(std::filesystem::path(program) / "mods", error))
            mods_root = std::filesystem::path(program) / "mods";
        else
            mods_root = "mods";
    }
    std::vector<dw4::game::ModInfo> mods = mods_root.empty() ? std::vector<dw4::game::ModInfo>{} : dw4::game::scan_mods(mods_root, settings.disabled_mods);
    const auto mod_folders = dw4::game::mod_overrides(mods);
    engine.set_resource_overrides(mod_folders);
    for (const auto& mod : mods)
        if (mod.enabled && mod.problem.empty()) std::cout << "mod: " << mod.name << " " << mod.version << " (" << mod.file_count << " files)\n";
    // Fast loading (settings.ini [gameplay] loading; --loading; test runs:
    // DW4_FAST_LOADING=1). A stage load latches it (Engine::fast_loading).
    // A choice in the F1 menu replaces the command line's.
    std::optional<bool> loading_override = options.fast_loading;
    auto fast_loading_choice = [&] {
        if (loading_override) return *loading_override;
        if (options.smoke_frames) {
            const char* text = std::getenv("DW4_FAST_LOADING");
            if (!text && !dev.settings_file.empty()) return settings.fast_loading;
            return text && std::atoi(text) != 0;
        }
        return settings.fast_loading;
    };
    // The randomizer of new games (settings.ini [randomizer]; test runs:
    // DW4_RANDOMIZER=seed[,enemies[,zones[,drops]]], or the old
    // DW4_GAMEPLAY drop seed) and the one bound to player 1's save once a
    // game started (randomizer_save.hpp).
    std::optional<dw4::game::RandomizerConfig> randomizer_bound;
    std::uint32_t randomizer_unique = 0;
    std::string randomizer_name; // player 1's character the bond belongs to
    std::set<std::uint32_t> new_characters; // UNIQUEs of player 1's new games in this session
    auto randomizer_settings = [&] {
        dw4::game::RandomizerConfig config;
        if (options.smoke_frames) {
            if (const char* text = std::getenv("DW4_RANDOMIZER")) config = dw4::game::parse_randomizer(text).value_or(config);
            else if (const char* gameplay = std::getenv("DW4_GAMEPLAY")) {
                int exp = 0, dealt = 0, taken = 0;
                unsigned seed = 0;
                std::sscanf(gameplay, "%d,%d,%d,%u", &exp, &dealt, &taken, &seed);
                config.seed = seed;
                config.drops = seed != 0;
            }
            return config;
        }
        config.seed = settings.rand_seed;
        config.drops = settings.rand_drops;
        config.enemies = settings.rand_enemies;
        // settings.ini [randomizer] zones = dungeons: the current rule of the
        // table (a bond keeps the rule its game started with).
        config.zones = settings.rand_zones ? dw4::engine::randomizer::kZoneRuleCurrent : 0;
        return config;
    };
    // Rebalancing (settings.ini [gameplay]; test runs:
    // DW4_GAMEPLAY=exp,dealt,taken,seed), the randomizer and the load timing.
    auto apply_gameplay = [&](dw4::engine::Cheats& cheats) {
        cheats.fast_loading = fast_loading_choice();
        dw4::game::apply_randomizer(randomizer_bound.value_or(randomizer_settings()), cheats);
        if (options.smoke_frames) {
            if (const char* text = std::getenv("DW4_GAMEPLAY")) {
                unsigned seed = 0;
                std::sscanf(text, "%d,%d,%d,%u", &cheats.exp_rate, &cheats.damage_dealt, &cheats.damage_taken, &seed);
            }
            return;
        }
        cheats.exp_rate = settings.exp_rate;
        cheats.damage_dealt = settings.damage_dealt;
        cheats.damage_taken = settings.damage_taken;
    };
    apply_gameplay(engine.cheats());
    // DW4_PLAYERS=n: test runs start with n joined players.
    if (const char* count = std::getenv("DW4_PLAYERS"))
        if (auto* sys = engine.get<dw4::engine::GameSystem>())
            for (int i = 0; i < std::clamp(std::atoi(count), 0, 4); ++i) sys->players[std::size_t(i)].joined = true;
    // Seamless co-op test switches (development and test runs only; they
    // change the simulation, so an online session refuses them,
    // docs/SEAMLESS.md 6.12), applied to every new engine before its first
    // step:
    //   DW4_CAMERAS=<mask>   those slots get their own box camera
    //                        (OnlineRules::player_cameras); two or more bits
    //                        also switch the leash off
    //   DW4_CAM_LOCAL=<slot> this machine's view slot and split identity
    //   DW4_LEASH_OFF=1      the leash off alone (the solo check of 4.6)
    //   DW4_ONLINE=<mask>    the online overrides on with these slots present
    //                        (the exit port, the gate disc fork, give up)
    const auto apply_test_online = [](dw4::engine::Engine& e) {
        static const char* cameras = std::getenv("DW4_CAMERAS");
        static const char* local = std::getenv("DW4_CAM_LOCAL");
        static const char* leash = std::getenv("DW4_LEASH_OFF");
        static const char* online = std::getenv("DW4_ONLINE");
        if (!cameras && !local && !leash && !online) return;
        dw4::engine::OnlineRules rules;
        if (cameras) rules.player_cameras = std::uint8_t(std::strtol(cameras, nullptr, 0) & 15);
        rules.leash_off = leash != nullptr || std::popcount(unsigned(rules.player_cameras)) >= 2;
        if (online) {
            rules.enabled = true;
            rules.present = std::uint8_t(std::strtol(online, nullptr, 0) & 15);
        }
        const int slot = local ? std::clamp(std::atoi(local), 0, 3) : 0;
        e.set_view_slot(slot);
        e.set_split_identity(slot);
        e.set_online(rules);
    };
    apply_test_online(engine);
    // DW4_DIFFICULTY=n: the difficulty a test run plays (0 normal, 1 hard, 2
    // very hard). The character select writes GAMERAM DIFFICULTY before the
    // first stage (80368050 at 0x803687D4, then TITLE2 GAMESTART), so the first
    // stage's boot.lua (generatorN/H/V.igb), start.lua (GBOX-E/H/V), NPC
    // scripts and drop lists (802FCC44) all see it; a test character from
    // DW4_CARD_RECORD too. A title run's own choice replaces it at GAMESTART.
    if (const char* level = std::getenv("DW4_DIFFICULTY"))
        if (auto* sys = engine.get<dw4::engine::GameSystem>()) sys->difficulty = std::clamp(std::atoi(level), 0, 2);
    if (options.stage_given)
        if (auto* sys = engine.get<dw4::engine::GameSystem>()) sys->direct_start = true;
    // Skip intro: boot straight to the title (stagIS10) instead of the
    // license, logos and opening movie.
    std::string first_stage = (!options.smoke_frames || !dev.settings_file.empty()) && settings.skip_intro && !options.stage_given
                                  ? std::string("stagis10")
                                  : options.stage;
    // Online co-op: wait in the lobby, then start every machine the same way
    // with the shared cards.
    dw4::game::NetSession net;
    std::filesystem::path session_cards;
    std::string own_card_name; // "Card A" (host) or "Card B" (first guest) in the session copy
    if (options.net_host || !options.net_join.empty()) {
        std::string error;
        const auto my_card = session_card(player_store);
        const bool ok = options.net_host
                            ? net.host(options.net_port, options.net_players, options.net_delay, data_hash(engine.resources()), error,
                                       options.smoke_frames != 0)
                            : net.join(options.net_join, options.net_port, data_hash(engine.resources()), my_card, error);
        if (!ok) throw std::runtime_error(error);
        dw4::game::SessionStart start;
        start.stage = first_stage;
        start.cheats = std::uint8_t((engine.cheats().god ? 1 : 0) | (engine.cheats().infinite_mp ? 2 : 0) | (engine.cheats().one_hit ? 4 : 0));
        start.card_a = my_card;
        start.exp_rate = engine.cheats().exp_rate;
        start.damage_dealt = engine.cheats().damage_dealt;
        start.damage_taken = engine.cheats().damage_taken;
        start.drop_seed = engine.cheats().drop_seed;
        start.fast_loading = engine.cheats().fast_loading;
        start.rand_seed = engine.cheats().rand_seed;
        start.rand_enemies = engine.cheats().rand_enemies;
        start.rand_zones = engine.cheats().rand_zones;
        std::string status, shown;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(options.smoke_frames ? 1 : 30);
        SDL_Init(SDL_INIT_VIDEO);
        SDL_Window* lobby_window = options.smoke_frames ? nullptr : SDL_CreateWindow("Digimon World 4 PC - Online", 560, 96, 0);
        SDL_Renderer* lobby_renderer = lobby_window ? SDL_CreateRenderer(lobby_window, nullptr) : nullptr;
        bool cancelled = false;
        while (!net.lobby(start, status, error)) {
            if (!error.empty()) break;
            if (status != shown) {
                std::cout << "online: " << status << '\n';
                shown = status;
            }
            SDL_Event event;
            while (SDL_PollEvent(&event))
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) cancelled = true;
            if (lobby_renderer) {
                SDL_SetRenderDrawColor(lobby_renderer, 24, 28, 40, 255);
                SDL_RenderClear(lobby_renderer);
                SDL_SetRenderDrawColor(lobby_renderer, 230, 230, 230, 255);
                const std::string where = options.net_host ? "Hosting on port " + std::to_string(options.net_port)
                                                           : "Joining " + options.net_join + ":" + std::to_string(options.net_port);
                SDL_RenderDebugText(lobby_renderer, 16, 20, where.c_str());
                SDL_RenderDebugText(lobby_renderer, 16, 52, status.c_str());
                SDL_RenderPresent(lobby_renderer);
            }
            if (cancelled || std::chrono::steady_clock::now() > deadline) {
                error = cancelled ? "Cancelled." : "Nobody joined in time.";
                break;
            }
            SDL_Delay(10);
        }
        if (lobby_renderer) SDL_DestroyRenderer(lobby_renderer);
        if (lobby_window) SDL_DestroyWindow(lobby_window);
        if (!error.empty()) throw std::runtime_error("online: " + error);
        const auto& started = net.start();
        first_stage = started.stage;
        // A guest of a host that started a stage directly (--stage) starts it
        // directly too: GameSystem::direct_start loads the players' weapon
        // packages at their GENERATE (without it the guest desynced at the
        // first attack, SEAMLESS.md 7.1). The boot and title stages are
        // stagis*.
        if (!options.net_host && first_stage.rfind("stagis", 0) != 0)
            if (auto* sys = engine.get<dw4::engine::GameSystem>()) sys->direct_start = true;
        engine.cheats().god = started.cheats & 1;
        engine.cheats().infinite_mp = started.cheats & 2;
        engine.cheats().one_hit = started.cheats & 4;
        engine.cheats().exp_rate = started.exp_rate;
        engine.cheats().damage_dealt = started.damage_dealt;
        engine.cheats().damage_taken = started.damage_taken;
        engine.cheats().drop_seed = started.drop_seed;
        engine.cheats().fast_loading = started.fast_loading; // the host's choice for everyone
        engine.cheats().rand_seed = started.rand_seed;      // the host's randomizer too
        engine.cheats().rand_enemies = started.rand_enemies;
        engine.cheats().rand_zones = started.rand_zones;
        // Every machine plays from the same card copies.
        session_cards = std::filesystem::temp_directory_path() /
                        ("dw4_session_" + std::to_string(net.local_slot()) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        write_session_card(session_cards / "Card A", started.card_a);
        write_session_card(session_cards / "Card B", started.card_b);
        std::cout << "online: shared cards: Card A " << card_names(started.card_a) << ", Card B " << card_names(started.card_b) << '\n';
        if (net.local_slot() == 0) own_card_name = "Card A";
        else if (net.local_slot() == 1) own_card_name = "Card B";
        engine.set_card_root(session_cards);
        // The shared copies in the usual slots on every machine.
        for (int slot = 0; slot < 2; ++slot) engine.set_card_setting(slot, {});
        std::cout << "online: started as player " << net.local_slot() + 1 << " of " << started.players << ", delay " << started.delay << '\n';
    }
    // Seamless online rooms (docs/SEAMLESS.md 10, online_run.hpp): the CPU
    // gate and the environment check, the login and the room, the replay
    // rate calibration (a hidden replay, one engine at a time), then the
    // engine starts again at the title in online mode with the room's
    // settings and an empty online card (round S1 keeps nothing online).
    std::unique_ptr<dw4::game::OnlineRun> online;
    // The online account's achievements in the room (online_achievements.hpp).
    std::unique_ptr<dw4::game::OnlineAchievements> online_achievements;
    const auto steady_seconds = [] { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
    // Leaves the room: the online account's last counters and open unlocks
    // first (they need the room membership), then the online character's
    // automatic save, BYE and the REST leave (OnlineRun::leave).
    auto leave_room = [&] {
        if (online_achievements) online_achievements->flush(steady_seconds(), 2.0);
        online->leave();
    };
    if (options.online_calibrate || options.online.wanted()) {
        if (const auto refusal = dw4::game::online::online_cpu_refusal()) throw std::runtime_error("online: " + *refusal);
        if (const auto variable = dw4::game::online::first_forbidden_env([](const char* name) -> const char* { return std::getenv(name); }))
            throw std::runtime_error("online: " + dw4::game::online::env_refusal(*variable));
        if (net.active() || options.net_host || !options.net_join.empty()) throw std::runtime_error("online: a room and --host / --join are exclusive");
    }
    auto fresh_engine = [&] {
        engine_storage.reset();
        engine_storage.emplace(options.data);
        engine.set_resource_overrides(mod_folders);
    };
    auto calibrate = [&](int steps, double seconds) {
        fresh_engine();
        // (DW4_ONLINE_CALIBRATION=nomusic: without the music position, to see what it costs.)
        const char* mode = std::getenv("DW4_ONLINE_CALIBRATION");
        const bool music = !(mode && std::string_view(mode) == "nomusic");
        const auto result = dw4::game::online::calibrate_replay_rate(engine, options.online_calibrate_stage, steps, seconds, music);
        fresh_engine();
        std::printf("online: replay rate %s%s: %d steps in %.3f s, %.0f steps per second (four players, union of their cameras not yet)\n",
                    result.stage.c_str(), music ? "" : " without the music position", result.steps, result.seconds, result.steps_per_second);
        return result;
    };
    if (options.online_calibrate) return calibrate(options.online_calibrate, 1e9).ok ? 0 : 1;
    // The remembered login and the online character's account folders
    // (saves/online beside the program; --online-folder in test runs).
    const auto online_folder = options.smoke_frames && !options.online_folder.empty() ? options.online_folder
                               : (program_dir().empty() ? std::filesystem::path(".") : program_dir()) / "saves" / "online";
    // Opens a room's session (the command line's room before the game loop,
    // the one the title's Online screens or F1 Online prepared during it):
    // the replay rate calibration (a hidden replay, one engine at a time),
    // the session over `http` (logged in with the room's token), the online
    // character's virtual card, the relay connection. The engine is a new one
    // afterwards with the online card; the caller starts the title. False
    // (and `error`, `online` reset) when the character cannot be loaded or
    // the relay cannot be reached.
    //
    // The online character's virtual card (docs/SEAMLESS.md 9): card A is
    // saves/online/<account>/online_card.dat, built from the server's
    // character (OnlineRun::load_character), card B is absent; every engine
    // of the session plays from it, never from the player's own saves.
    auto open_room = [&](dw4::game::OnlineRoom room, std::unique_ptr<dw4::game::online::HttpWorker> http, bool created,
                         std::string& error) -> bool {
        std::cout << "online: the world as " << room.account << " (box " << room.box << ", camera " << room.settings.camera_mode << ", light "
                  << room.settings.light_mode << ", fast loading " << (room.settings.fast_loading ? 1 : 0) << ")\n";
        room.endpoint = http->endpoint();
        const auto rate = calibrate(100000, 0.5);
        auto online_card = std::make_shared<dw4::save::CardStore>();
        const auto use_online_card = [&engine, online_card] {
            engine.set_card_store(*online_card);
            engine.set_card_setting(0, {});
            dw4::engine::CardSetting absent;
            absent.mode = dw4::engine::CardSetting::Mode::none;
            engine.set_card_setting(1, absent);
        };
        const auto data = data_hash(engine.resources());
        dw4::game::online::EngineHooks hooks;
        hooks.engine = [&]() -> dw4::engine::Engine& { return engine; };
        hooks.fresh_engine = [&, use_online_card] {
            fresh_engine();
            use_online_card();
        };
        hooks.hidden = [&] { return dw4::game::OverheadLabels::hidden(engine); };
        hooks.figure_hidden = [&] { return dw4::game::OverheadLabels::hidden(engine, false); };
        hooks.busy = [&] { return dw4::game::OverheadLabels::window_open(engine); };
        online = std::make_unique<dw4::game::OnlineRun>(std::move(http), std::move(room), std::move(hooks), data,
                                                        rate.ok ? std::uint32_t(rate.steps_per_second) : 3000u);
        {
            // saves/online.log (round 62): what to send in when online play lags (DW4_ONLINE_LOG: another file, test runs).
            const char* log_override = std::getenv("DW4_ONLINE_LOG");
            online->open_log(log_override && *log_override ? std::filesystem::path(log_override) : online_folder.parent_path() / "online.log",
                             DW4_PC_VERSION);
        }
        online->link().start_online(0, 1);
        // The relay first, the character after it (round 59 review): the
        // relay's answer replaces a connection of this account that another
        // PC still holds in the room, whose session then ends without
        // uploading; loaded the other way round, that PC's leaving save came
        // after this load and this PC's first upload met a 409.
        if (!online->connect(steady_seconds(), error)) {
            online.reset();
            return false;
        }
        if (!online->load_character(online_folder, DW4_PC_VERSION, error)) {
            online->leave(0.0);
            online.reset();
            return false;
        }
        *online_card = online->character_card();
        // (A 409's reload gives a guest's own story anew: the store with it, for this
        // engine and the fresh ones of joins and resyncs; round G-core's review.)
        online->card_story_changed = [&engine, online_card, run = online.get()] {
            *online_card = run->character_card();
            engine.set_card_store(*online_card);
        };
        // --online-card FOLDER (development builds, test runs): the card
        // starts with the DW4 save of FOLDER (or of its "Card A") instead of
        // the server's character, a character to call at the title; its
        // saves go up as the online character (a new account's first one).
        if (!options.online_card.empty()) {
#ifdef DW4_ONLINE_DEV
            auto from = options.online_card;
            if (std::filesystem::is_directory(from / "Card A")) from /= "Card A";
            std::filesystem::path save = from / dw4::save::kSaveFileName;
            if (!std::filesystem::exists(save))
                for (const auto& entry : std::filesystem::directory_iterator(from))
                    if (entry.path().extension() == ".gci") save = entry.path();
            const auto bytes = dw4::save::read_whole_file(save);
            if (!bytes || !dw4::save::valid_save_gci(*bytes) || !online->character()->card().write(*bytes)) {
                online->leave(0.0);
                online.reset();
                error = "--online-card: no valid save in " + from.string();
                return false;
            }
            std::cout << "online: the online card starts with " << save.string() << '\n';
#else
            online.reset();
            error = "--online-card is a development build option";
            return false;
#endif
        }
        use_online_card();
        std::cout << "online: " << online->character()->status_text() << " (card " << online->character()->card().file().string() << ")\n";
        // The online account's achievements in the room (SEAMLESS.md 11).
        online_achievements = make_online_achievements(*online, created, options.smoke_frames != 0, online_folder);
        return true;
    };
    if (options.online.wanted()) {
        auto http = std::make_unique<dw4::game::online::HttpWorker>(dw4::game::online::server_endpoint(), std::string("DW4_PC/") + DW4_PC_VERSION);
        if (http->endpoint().development) std::cout << "online: development server " << http->endpoint().url() << '\n';
        std::string error;
        auto room = dw4::game::online_enter_world(*http, options.online, data_hash(engine.resources()), DW4_PC_VERSION, online_folder, error);
        if (!room) throw std::runtime_error("online: " + error);
        if (!open_room(std::move(*room), std::move(http), false, error)) throw std::runtime_error("online: " + error);
        first_stage = "stagis10"; // the title, as every online session starts
    }
    // The account and room screens (online_ui.hpp): the title's Online panel
    // and the F1 Online tab, over their own HTTP worker. Test runs have them
    // only with --online-ui (no network otherwise); nothing is sent before
    // the player opens them (or a remembered login is checked at the title).
    std::unique_ptr<dw4::game::online::OnlineLobby> lobby;
    if (!options.smoke_frames || !options.online_ui.empty()) {
        dw4::game::online::LobbyConfig config;
        config.protocol = int(dw4::game::kOnlineProtocol);
        config.data_hash = data_hash(engine.resources());
        config.client_version = DW4_PC_VERSION;
        config.folder = online_folder;
        const auto endpoint = dw4::game::online::server_endpoint();
        config.server_key = endpoint.host + ":" + std::to_string(endpoint.port);
        config.not_set_up = endpoint.pinned && endpoint.pins.empty();
        if (const auto refusal = dw4::game::online::online_cpu_refusal()) config.refusal = *refusal;
        else if (const auto variable = dw4::game::online::first_forbidden_env([](const char* name) -> const char* { return std::getenv(name); }))
            config.refusal = dw4::game::online::env_refusal(*variable);
        // (A test run's screens talk to a loopback development server only.)
        if (options.smoke_frames && !endpoint.development)
            throw std::runtime_error("online-ui: test runs need a DW4_ONLINE_DEV build with DW4_ONLINE_SERVER set to a loopback server");
        lobby = std::make_unique<dw4::game::online::OnlineLobby>(dw4::game::online::worker_queue(endpoint, std::string("DW4_PC/") + DW4_PC_VERSION),
                                                                 std::move(config));
        if (online) {
            lobby->open();
            lobby->adopt_world(online->room().account, online->room().token);
        }
    }
    // A UI test run starts at the title (the panel's screen).
    if (!options.online_ui.empty() && !options.stage_given) first_stage = "stagis10";
    // DW4_ENTRY=n (test runs): the first stage starts at its player point
    // group n, as when arriving from another stage's exit.
    if (const char* entry = std::getenv("DW4_ENTRY"); entry && options.smoke_frames)
        if (auto* points = engine.get<dw4::engine::PlayerPointManager>()) points->set_point_no(std::atoi(entry));
    // DW4_COUNTERS=n:v,... (test runs): story counters the first stage starts
    // with (COUNTERget/COUNTERset). COUNTER 2 names the dungeon whose b1/f1
    // start.lua last cleared the dungeon flags 100-299 (a flag preset for a
    // dungeon survives only with it, dngq3b1e: 903); COUNTER 0 is a quest's
    // time limit in seconds (dngq3b2e putitemyes.lua: 900).
    if (const char* list = std::getenv("DW4_COUNTERS"); list && options.smoke_frames)
        for (const char* at = list; *at;) {
            const int counter = std::atoi(at);
            while (*at && *at != ':' && *at != ',') ++at;
            if (*at == ':') engine.counters()[counter] = std::atoi(++at);
            while (*at && *at != ',') ++at;
            if (*at == ',') ++at;
        }
    engine.request_stage(first_stage);
    auto make_camera = [](dw4::engine::Vec3f eye, dw4::engine::Vec3f target) {
        dw4::engine::FrameCamera c;
        c.view = dw4::engine::look_at(eye, target, {0, 1, 0});
        // The stage cameras' horizontal field of view on the 640x448 viewer
        // window, as CameraManager::frame_camera.
        c.projection = dw4::engine::perspective_horizontal(54.43f * 3.14159265f / 180.0f, 640.0f / 448.0f, 1.0f, 4000.0f);
        return c;
    };
    if (options.debug_camera.size() >= 6)
        engine.debug_camera = make_camera({options.debug_camera[0], options.debug_camera[1], options.debug_camera[2]},
                                          {options.debug_camera[3], options.debug_camera[4], options.debug_camera[5]});
    // Interactive free camera (F2): position + yaw/pitch.
    bool free_camera = false;
    dw4::engine::Vec3f free_eye{0, 60, 200};
    float free_yaw = 3.14159265f, free_pitch = -0.2f;

    // Test runs draw no pictures unless they need them (screenshots, texture
    // pack or dump folders, DW4_RENDER=1): no window, no OpenGL context, no
    // menu. The scene is still traversed after every step exactly as for a
    // picture (Engine::traverse_scene), since animation clocks, motion
    // combiners and recorded matrices advance there, so such a run simulates
    // the same frames as one that draws them.
    const bool draw = !options.smoke_frames || !options.screenshot.empty() || std::getenv("DW4_TEXTURE_PACK") ||
                      std::getenv("DW4_DUMP_TEXTURES") || (std::getenv("DW4_RENDER") && std::atoi(std::getenv("DW4_RENDER")) != 0) ||
                      dev.presenting() || !dev.dump_dir.empty();
    if (!SDL_Init(draw ? SDL_INIT_VIDEO | SDL_INIT_GAMEPAD : SDL_INIT_EVENTS)) throw std::runtime_error(SDL_GetError());
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    ImFont* tag_font = nullptr;  // the overhead name tags' font (also the online screens' headings)
    ImFont* body_font = nullptr; // the online screens' text over the title
    int samples = 0;
    if (draw) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        Uint64 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
        if (options.smoke_frames) flags |= SDL_WINDOW_HIDDEN;
        // Multisampling is chosen with the window (settings.ini msaa, read at
        // start-up); without support the window opens without it.
        // Test runs: none unless DW4_MSAA=n (screenshots at a player's settings);
        // with DW4_DEV_SETTINGS / DW4_DEV_WINDOW the settings' msaa.
        const int msaa = options.smoke_frames && !dev_window
                             ? (std::getenv("DW4_MSAA") ? std::atoi(std::getenv("DW4_MSAA")) : 0)
                             : settings.msaa;
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa > 1 ? 1 : 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa > 1 ? msaa : 0);
        // Test runs ignore settings.ini: 1280x960 unless DW4_WINDOW=WxH, 4:3
        // unless DW4_WIDESCREEN=1.
        // (DW4_DEV_SETTINGS / DW4_DEV_WINDOW: the settings' window.)
        int window_width = options.smoke_frames && !dev_window ? 1280 : settings.width;
        int window_height = options.smoke_frames && !dev_window ? 960 : settings.height;
        if (options.smoke_frames && !dev_window) {
            if (const char* size = std::getenv("DW4_WINDOW")) std::sscanf(size, "%dx%d", &window_width, &window_height);
            settings.widescreen = std::getenv("DW4_WIDESCREEN") != nullptr;
        }
        window = SDL_CreateWindow("Digimon World 4 PC", window_width, window_height, flags);
        context = window ? SDL_GL_CreateContext(window) : nullptr;
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
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &samples);
        if (samples > 1) glEnable(GL_MULTISAMPLE);

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
        ImGui_ImplSDL3_InitForOpenGL(window, context);
        ImGui_ImplOpenGL3_Init("#version 330");
        // The menus keep ImGui's own font (the first one added is the
        // default); the overhead name tags take a bold system face when
        // there is one (Segoe UI Bold, Arial Bold: readable over any
        // background at their small size), else ImGui's scalable one.
        auto& fonts = *ImGui::GetIO().Fonts;
        fonts.AddFontDefaultBitmap(); // what ImGui picks on its own at this size (13 px)
        if (const char* windows = std::getenv("WINDIR"))
            for (const char* face : {"segoeuib.ttf", "arialbd.ttf"}) {
                const auto file = std::filesystem::path(windows) / "Fonts" / face;
                std::error_code error;
                if (!tag_font && std::filesystem::is_regular_file(file, error)) tag_font = fonts.AddFontFromFileTTF(file.string().c_str(), 18.0f);
            }
        if (!tag_font) tag_font = fonts.AddFontDefaultVector();
        // The online screens over the title: a regular system face beside
        // the bold one (Segoe UI, Arial), else ImGui's scalable one.
        if (const char* windows = std::getenv("WINDIR"))
            for (const char* face : {"segoeui.ttf", "arial.ttf"}) {
                const auto file = std::filesystem::path(windows) / "Fonts" / face;
                std::error_code error;
                if (!body_font && std::filesystem::is_regular_file(file, error)) body_font = fonts.AddFontFromFileTTF(file.string().c_str(), 18.0f);
            }
        if (!body_font) body_font = fonts.AddFontDefaultVector();
    }

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
    // DW4_AUDIO_MIX=1 (test runs): the mixer runs as with an audio device,
    // its output is dropped (the streams' decoding positions advance: the
    // canonical snapshot check, SEAMLESS.md 6.8).
    const bool audio_mix = options.smoke_frames && std::getenv("DW4_AUDIO_MIX") && std::atoi(std::getenv("DW4_AUDIO_MIX")) != 0;
    // (The output samples of a step and the mix: host_view.hpp.)
    using dw4::game::kAudioFrameSamples;
    using dw4::game::step_samples;

    {
        std::optional<dw4::game::GlRenderer> gl_renderer;
        if (draw) gl_renderer.emplace();
#ifdef DW4_GHOST_PROBE
        if (draw && std::getenv("DW4_PROBE_GHOSTS_REPORT"))
            std::printf("ghost probe: OpenGL %s on %s\n", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
                        reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
#endif
        dw4::game::GlRenderer* const renderer = gl_renderer ? &*gl_renderer : nullptr;
        // The window, the renderer, the sound output, the controllers, the
        // F1 menu and the pictures as the host's shared parts see them
        // (host_view.hpp: the story and a Data Overflow run use the same).
        dw4::game::HostView view(settings, dev);
        view.test_run = options.smoke_frames != 0;
        view.draw = draw;
        view.window = window;
        view.renderer = renderer;
        view.samples = samples;
        view.tag_font = tag_font;
        view.audio = audio;
        view.audio_dump_path = audio_dump_path;
        view.audio_mix = audio_mix;
        auto& audio_dump = view.audio_dump;
        auto mix_audio = [&](int frames) { dw4::game::mix_audio(view, engine, frames); };
        // Texture packs: settings.ini [mods] textures (default mods/textures
        // next to the program, else in the working folder); dumps go to
        // mods/dump/textures. Test runs use neither.
        dw4::game::TexturePack texture_pack;
        auto mods_folder = [&](const char* sub) {
            std::filesystem::path base;
            if (const char* program = SDL_GetBasePath()) base = program;
            std::error_code error;
            if (!base.empty() && std::filesystem::is_directory(base / "mods", error)) return base / "mods" / sub;
            return std::filesystem::path("mods") / sub;
        };
        auto configure_pack = [&] {
            const auto pack = settings.texture_pack.empty() ? mods_folder("textures") : settings.texture_pack;
            texture_pack.configure(pack, settings.dump_textures ? mods_folder("dump/textures") : std::filesystem::path());
            renderer->clear_cache();
        };
        if (!options.smoke_frames) {
            configure_pack();
            renderer->set_texture_pack(&texture_pack);
        } else if (std::getenv("DW4_TEXTURE_PACK") || std::getenv("DW4_DUMP_TEXTURES")) {
            // Test runs: DW4_TEXTURE_PACK=dir and / or DW4_DUMP_TEXTURES=dir.
            const char* pack = std::getenv("DW4_TEXTURE_PACK");
            const char* dump = std::getenv("DW4_DUMP_TEXTURES");
            texture_pack.configure(pack ? pack : "", dump ? dump : "");
            renderer->set_texture_pack(&texture_pack);
        }
        auto& gamepads = view.gamepads;
        ScriptedInput scripted;
        if (!options.input_script.empty()) scripted.load(options.input_script);
        // --online-script FILE (test runs of online rooms, tools/online_test):
        // this machine's pad from a list of steps (online_script.hpp).
        dw4::game::OnlineScript online_script;
        if (!options.online_script.empty()) {
            std::string error;
            if (!online_script.load(options.online_script, error)) throw std::runtime_error("online script: " + error);
        }
        auto& items = view.picture.items;
        auto& shadow = view.picture.shadow;
        auto& camera = view.picture.camera;
        auto& clear = view.picture.clear;
        std::array<dw4::engine::PadSample, 4> pads{};
        auto& rendered_stage = view.picture.rendered_stage;
        auto& movie_decoder = view.picture.movie_decoder;
        auto& movie_serial = view.picture.movie_serial;
        auto& movie_stage = view.picture.movie_stage;
        // DW4_OPEN_MENU=1 opens the F1 menu at start (menu screenshots);
        // DW4_MENU_TAB=<tab> selects that tab when it first shows.
        bool running = true;
        auto& show_menu = view.show_menu;
        show_menu = std::getenv("DW4_OPEN_MENU") != nullptr;
        auto& menu_tab = view.menu_tab;
        menu_tab = std::getenv("DW4_MENU_TAB") ? std::getenv("DW4_MENU_TAB") : "";
        auto tab_flags = [&](const char* name) -> ImGuiTabItemFlags {
            if (menu_tab != name) return ImGuiTabItemFlags_None;
            menu_tab.clear();
            return ImGuiTabItemFlags_SetSelected;
        };
        // Save states (this session): four slots, F5 saves and F9 loads slot 1.
        std::array<std::optional<SaveState>, 4> state_slots;
        std::vector<LoggedStep> input_log;
        bool load_step_drawn = false, last_step_loaded = false;
        auto& items_current = view.picture.items_current; // `items` is the draw list of the last update
        std::uint64_t log_serial = 0;
        std::string state_message;
        auto message_until = std::chrono::steady_clock::now();
        // DW4_STATE_TEST=N (test runs): save at frame N, run to the end, load
        // and run again, comparing every frame's fingerprint.
        const int state_test = options.smoke_frames && std::getenv("DW4_STATE_TEST") ? std::atoi(std::getenv("DW4_STATE_TEST")) : 0;
        int state_pass = 1, state_mismatch = -1;
        bool state_test_save = false; // saves after the picture, like F5 or the menu
        std::vector<std::uint64_t> state_fingerprints;
        auto& settings_changed = view.settings_changed;
        // F1 Saves tab: the boxes of the slots (read again every 2 s while
        // shown), the import preview and the file dialog's answer (it may
        // come from another thread).
        struct SavesTab {
            std::array<std::optional<std::array<dw4::save::BoxSummary, 4>>, 2> boxes;
            std::array<bool, 2> has_file{};
            std::chrono::steady_clock::time_point read_at{};
            char path[1024]{};
            std::optional<dw4::save::ImportSource> source;
            std::array<bool, 4> chosen{};
            std::array<int, 4> into{0, 1, 2, 3};
            int slot = 0;
            std::string message;
            std::mutex dialog_mutex;
            std::string dialog_path;
            std::atomic<bool> dialog_done{false};
        };
        static SavesTab saves_tab;
        // F1 Saves > Cloud backup (docs/SEAMLESS.md 9; outside rooms only).
        dw4::game::CloudBackupPanel cloud_panel(online_folder, DW4_PC_VERSION, int(dw4::game::kOnlineProtocol));
        // DW4_MENU_CLOUD=account:password[:upload|download] (test runs): the
        // cloud backup panel logs in at its first draw and runs the action.
        if (const char* cloud_test = std::getenv("DW4_MENU_CLOUD"); cloud_test && options.smoke_frames) {
            const std::string text = cloud_test;
            const auto a = text.find(':'), b = a == std::string::npos ? a : text.find(':', a + 1);
            if (a != std::string::npos)
                cloud_panel.set_test(text.substr(0, a), text.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1),
                                     b == std::string::npos ? std::string() : text.substr(b + 1));
        }
        auto& capture = view.capture;
        auto& next_present = view.next_present;
        next_present = std::chrono::steady_clock::now();
        float online_audio_ratio = 1.0f; // the audio stream's frequency ratio in an online room
        std::uint64_t net_submitted = std::uint64_t(net.active() ? net.start().delay : 0);
        // Smooth motion above 60 presents per second (settings.ini smooth_motion).
        auto& interpolator = view.interpolator;
        // Seamless co-op: the steps START is held while the local member is
        // defeated (give up after kGiveUpFrames).
        int give_up_held = 0;
        constexpr int kGiveUpFrames = 120;
        auto& present_interval = view.present_interval;
        view.last_present = std::chrono::steady_clock::now();
        int frame = 0;
        // ---- Presence features: overhead name tags, Discord Rich Presence,
        // achievements. Host only: they read the engine after a step and
        // never write it (the engine's port events are observe-only).
        //
        // A new engine (state load, console reboot, online start) starts the
        // achievement tracker's stage over.
        std::uint64_t engine_generation = 0;
        // The stage (serial and generation) a save state was loaded in: no
        // achievement counts until the next stage load.
        std::optional<std::uint64_t> state_loaded_stage;
        auto stage_key = [&]() -> std::uint64_t {
            const std::uint64_t serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
            return (engine_generation << 40) ^ serial;
        };
        auto own_slot = [&] { return net.active() ? net.local_slot() : online ? online->session().own_slot() : engine.view_slot(); }; // (offline: slot 0 unless a seamless co-op test run sets the view slot)
        // This machine's player's place (presentation: the ghost figures' cap and camera fade).
        auto own_position = [&]() -> std::optional<dw4::engine::Vec3f> {
            if (const auto* models = engine.get<dw4::engine::ModelCtrlManager>())
                if (const auto* player = models->player(own_slot()); player && player->node && player->generated)
                    return dw4::engine::Vec3f{player->node->matrix(3, 0), player->node->matrix(3, 1), player->node->matrix(3, 2)};
            return std::nullopt;
        };
        // Overhead name tags (settings.ini [hud]; test runs: DW4_LABELS=
        // auto|all|others|off, DW4_TRACE_LABELS=N prints their screen
        // positions every N frames, without a window too).
        auto& labels = view.labels;
        // Ghost figures (ghost_figures.hpp, SEAMLESS.md 12): the other room
        // members in this hub stage as their Digimon, from their presence;
        // presentation only (DW4_GHOSTS=0 turns them off; DW4_TRACE_GHOSTS=N
        // prints them every N frames in test runs). `present_remote`: the
        // room members' presence samples of this loop pass (the figures and
        // the remote tags use the same ones).
        dw4::game::GhostFigures ghosts;
        const bool ghosts_enabled = !std::getenv("DW4_GHOSTS") || std::string_view(std::getenv("DW4_GHOSTS")) != "0";
        const int trace_ghosts = options.smoke_frames && std::getenv("DW4_TRACE_GHOSTS") ? std::atoi(std::getenv("DW4_TRACE_GHOSTS")) : 0;
        std::vector<dw4::game::online::RemoteSample> present_remote;
        double ghost_cost_max = 0, present_max = 0; // (DW4_TRACE_GHOSTS: the figures' own milliseconds a pass, the longest present)
        int ghost_trace_bucket = -1;
#ifdef DW4_GHOST_PROBE
        // Round I1a's ghost measurement (docs/SEAMLESS.md 19.13; the build
        // option DW4_GHOST_PROBE, never in a player build). Test runs only:
        // DW4_PROBE_GHOSTS=N stands N figures (forms and weapons in turn, half
        // of them walking a small circle) around the own player in a hub stage
        // without an online session; DW4_PROBE_GHOSTS_REPORT=P prints every P
        // drawn passes the pass time (pass start to after the swap, with a
        // glFinish first, so the GPU's work counts), the figures' update and
        // draw time and the pose / traversal split (mean, p50, p99, max).
        const int probe_ghosts = options.smoke_frames && std::getenv("DW4_PROBE_GHOSTS") ? std::atoi(std::getenv("DW4_PROBE_GHOSTS")) : 0;
        const int probe_report = std::getenv("DW4_PROBE_GHOSTS_REPORT") ? std::max(1, std::atoi(std::getenv("DW4_PROBE_GHOSTS_REPORT"))) : 0;
        std::vector<double> probe_pass_ms, probe_figures_ms, probe_pose_ms, probe_traverse_ms, probe_items;
        // DW4_PROBE_GHOSTS_SPREAD: units between two figures of the spiral (default 6).
        const float probe_spread = std::getenv("DW4_PROBE_GHOSTS_SPREAD") ? float(std::atof(std::getenv("DW4_PROBE_GHOSTS_SPREAD"))) : 6.0f;
        double probe_update_ms = 0;
        int probe_drawn = 0, probe_reused = 0;
        auto probe_print = [&](const char* what, std::vector<double>& v) {
            if (v.empty()) return;
            std::sort(v.begin(), v.end());
            double sum = 0;
            for (const double x : v) sum += x;
            std::printf("  %-10s mean %.3f p50 %.3f p99 %.3f max %.3f%s\n", what, sum / double(v.size()), v[v.size() / 2],
                        v[std::min(v.size() - 1, v.size() * 99 / 100)], v.back(), std::string_view(what) == "items" ? "" : " ms");
            v.clear();
        };
#endif
        const int trace_labels = options.smoke_frames && std::getenv("DW4_TRACE_LABELS") ? std::atoi(std::getenv("DW4_TRACE_LABELS")) : 0;
        view.trace_labels = trace_labels;
        auto& traced_occlusion = view.traced_occlusion; // the labels the last picture hid (traces)
        auto label_mode = [&] { return view.label_mode(); };
        // Discord Rich Presence (settings.ini [discord]). Test runs talk only
        // to a test pipe (DW4_DISCORD_PIPE=<pipe name prefix>), never to a
        // running Discord; DW4_TRACE_PRESENCE=1 prints each new activity.
        const char* discord_pipe = options.smoke_frames ? std::getenv("DW4_DISCORD_PIPE") : nullptr;
        std::optional<dw4::game::DiscordPresence> discord;
        if (!options.smoke_frames || discord_pipe) {
            dw4::game::DiscordPresence::Options discord_options;
            if (discord_pipe) discord_options.pipe_prefix = discord_pipe;
            discord.emplace(discord_options);
        }
        const bool trace_presence = options.smoke_frames && std::getenv("DW4_TRACE_PRESENCE");
        auto unix_now = [] {
            return std::int64_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        };
        const std::int64_t session_started = unix_now();
        std::optional<dw4::game::discord::Activity> traced_activity;
        // An online instance's party (docs/SEAMLESS.md 13, 19.4: the party's
        // size from round I4, until then the instance's x/4): an id that names
        // no instance; Discord shows "(n of 4)" after the state. At home: none.
        auto room_party = [&](dw4::game::discord::Activity& activity) {
            if (!online || !online->session().in_instance()) return;
            activity.party_id = dw4::game::discord::party_id("wid-" + std::to_string(online->session().wid()) + "-" + std::to_string(online->session().host()));
            activity.party_size = std::clamp(online->session().instance_players(), 1, 4);
            activity.party_max = 4;
        };
        auto presence_activity = [&] {
            dw4::game::discord::Activity activity;
            activity.start = session_started;
            room_party(activity);
            const std::string stage = engine.stage_name();
            const auto* sys = engine.get<dw4::engine::GameSystem>();
            const int slot = own_slot();
            const bool menus = stage.empty() || dw4::game::is_menu_stage(stage) || engine.rebooting() || !sys ||
                               !sys->players[std::size_t(slot)].joined || !sys->players[std::size_t(slot)].stats.ready;
            if (menus) {
                activity.details = "In Menus";
                if (online) activity.state = "Online";
                activity.large_image = dw4::game::kLogoImageKey;
                activity.large_text = "Digimon World 4";
                return activity;
            }
            const auto& player = sys->players[std::size_t(slot)];
            const auto* form = dw4::game::form_by_species(player.stats.species);
            if (!form) form = dw4::game::form_by_model(player.digimon);
            activity.details = std::string(form ? form->name : "Digimon") + " Lv " + std::to_string(player.stats.level);
            const auto* area = dw4::game::find_area(stage);
            activity.state = area ? dw4::game::area_display_name(*area) : stage;
            activity.large_image = area ? area->key : dw4::game::kLogoImageKey;
            activity.large_text = area ? area->region : "Digimon World 4";
            if (net.active()) activity.large_text += " - online co-op, " + std::to_string(net.start().players) + " players";
            if (online) activity.large_text += " - online";
            if (form) activity.small_image = form->key;
            activity.small_text = dw4::game::difficulty_name(sys->difficulty);
            return activity;
        };
        // Achievements (settings.ini [achievements]; saves/profile.dat next
        // to the program). Test runs only with DW4_ACHIEVEMENTS=1, their
        // profile in DW4_PROFILE (none written without it);
        // DW4_TRACE_EVENTS=1 prints the engine's port events.
        const bool achievements_test = options.smoke_frames && std::getenv("DW4_ACHIEVEMENTS");
        const bool trace_events = options.smoke_frames && std::getenv("DW4_TRACE_EVENTS");
        std::filesystem::path profile_path;
        if (!options.smoke_frames) {
            if (const char* program = SDL_GetBasePath()) profile_path = std::filesystem::path(program) / "saves" / "profile.dat";
        } else if (const char* file = std::getenv("DW4_PROFILE")) {
            profile_path = file;
        }
        dw4::game::AchievementTracker achievements(profile_path.empty() ? dw4::game::AchievementProfile{}
                                                                        : dw4::game::AchievementProfile::load(profile_path).value_or(dw4::game::AchievementProfile{}));
        auto achievements_on = [&] { return options.smoke_frames ? achievements_test : settings.achievements; };
        auto last_profile_save = std::chrono::steady_clock::now();
        auto save_profile = [&] {
            if (!profile_path.empty() && achievements.dirty() && achievements.profile().save(profile_path)) achievements.clear_dirty();
            last_profile_save = std::chrono::steady_clock::now();
        };
        struct Toast {
            std::string title, text;
            std::chrono::steady_clock::time_point until;
        };
        std::deque<Toast> toasts;
        bool go_back_b_held = false; // (round I1b: B on a GO's held loading screen, pressed once)
        // The account and room screens (online_ui.hpp): over the title (its
        // menu's third entry "Online" opens them), the F1 Online tab, the test
        // run's script (--online-ui).
        dw4::game::OnlineUiState online_ui;
        dw4::game::TitleEntry title_entry; // the title menu's third entry in the picture
        // Data Overflow (docs/SURVIVAL.md 13.3, overflow_host.hpp): the switch
        // to a run and back, the F1 dev start's choices, --overflow-run's
        // request (offline only; without at=F it runs before the story's
        // first step, and a test run ends with it).
        dw4::game::overflow::StorySwitch overflow_switch;
        dw4::game::overflow::DevStartState overflow_dev;
        std::optional<dw4::game::overflow::RunRequest> overflow_cli;
        bool overflow_quit_after = false;
        if (options.overflow_given) {
            std::string error;
            overflow_cli = dw4::game::overflow::parse_run_request(options.overflow_run, error);
            if (!overflow_cli) throw std::runtime_error(error);
            if (net.active() || online || options.online.wanted() || options.net_host || !options.net_join.empty())
                throw std::runtime_error("--overflow-run: Data Overflow is offline only");
            if (overflow_cli->at < 0) {
                overflow_switch.request(std::move(*overflow_cli), true);
                overflow_cli.reset();
                overflow_quit_after = options.smoke_frames != 0;
            }
        }
        dw4::game::TitleOnline title_online;
        bool title_online_shown = false; // the screens over the title (the last frame)
        bool online_tab_shown = false;   // F1 shows the Online tab (the last frame)
        bool menu_shown_before = false;  // the F1 menu was open in the last frame (it opens on Online in the world)
        bool instance_line_shown = false; // the line under the own status window showed in the last frame (19.5.4)
        dw4::game::RoomLive last_room_live; // the last frame's session facts (a UI test run's wait-instance and wait-members)
        // After the screens close, the game's pad waits until every button
        // is up (the B that closed them must not also cancel the menu).
        auto& pad_latch = view.pad_latch;
        // A UI test run's presses on the game's pad (dw4::pad_button bits),
        // held until that engine frame.
        std::uint32_t ui_pad = 0;
        std::uint64_t ui_pad_until = 0;
        std::string ui_shot;            // a screenshot the script asked for this frame
        dw4::game::OnlineUiScript ui_script;
        if (!options.online_ui.empty()) {
            std::string error;
            if (!ui_script.load(options.online_ui, error)) throw std::runtime_error("online-ui: " + error);
        }
        // The controller's View / Back button opens and closes the F1 menu
        // (dw4::game::host_event), a UI test run's "view" too.
        auto view_button = [&] { dw4::game::toggle_menu(view); };
        // Opens the online screens over the title (the menu's Online entry;
        // a test run's "panel open").
        auto open_title_online = [&] {
            if (!lobby) return;
            title_online.active = title_online.focus = true;
            show_menu = false;
            if (!lobby->opened()) lobby->open();
        };
        // Keys a UI test run presses into the menus (pressed one frame, released the next).
        std::vector<int> ui_keys, ui_keys_down;
        // Round I1a's Play (SEAMLESS.md 19.5.1, online/title_driver.hpp): the
        // title's Game Start and the box's Call Digimon behind one loading
        // screen, straight into the own lobby; never with an online test
        // script or an input script (their campaign bot drives the title as
        // before).
        dw4::game::online::TitleDriver title_driver;
        double welcome_at = -1; // the drive reached the lobby: "Welcome back, ..." then (GET /world asked first)
        std::map<std::string, bool> exit_mats_home; // "stage/mat": the exit mat's script names only hub stages (the host's exit line)
        auto start_title_drive = [&] {
            if (!online || online_script.active() || !options.input_script.empty()) return;
            dw4::game::online::TitleDriver::Config config;
            // The card box holding the online character (none: a new account's
            // first character, the original new-character flow).
            if (auto* character = online->character())
                if (const auto gci = character->card().read(); gci && dw4::save::valid_save_gci(*gci)) {
                    for (int k = 0; k < 4 && !config.box; ++k) {
                        const int box = (character->box() + k) % 4;
                        if (const auto record = dw4::game::online::record_of_box(*gci, box); record && dw4::game::online::record_used(*record))
                            config.box = box;
                    }
                }
            config.difficulty = online->room().difficulty; // (--online-difficulty, development builds)
            title_driver.start(config);
            welcome_at = -1; // (an earlier session's welcome that never came: gone)
            std::cout << "online: Play drives the title (" << (config.box ? "box " + std::to_string(*config.box) : std::string("no online character yet"))
                      << ")\n";
        };
        if (online && options.online.wanted()) start_title_drive();
#ifdef DW4_ONLINE_DEV
        // DW4_TEST_TITLE_DRIVE=BOX (development test runs, offline): Play's
        // title drive on this run's own title with the card's box BOX, the
        // app's input path without a server (ctest online_title drives the
        // engine alone).
        const char* title_drive_test = options.smoke_frames ? std::getenv("DW4_TEST_TITLE_DRIVE") : nullptr;
        if (title_drive_test && !online) title_driver.start({std::clamp(std::atoi(title_drive_test), 0, 3), std::nullopt, 5400});
#else
        constexpr const char* title_drive_test = nullptr;
#endif
        // An old save the one-time move into saves/savedata.dat could not
        // copy (save_store.cpp): the player hears of it at once.
        for (const auto& warning : save_choice.warnings)
            toasts.push_back({"Saves", warning, std::chrono::steady_clock::now() + std::chrono::seconds(15)});
        dw4::game::AchievementGate last_gate = dw4::game::AchievementGate::open;
        std::uint64_t taint_stage = ~0ull;
        bool stage_cheats = false, stage_rebalanced = false;
        auto achievement_gate = [&]() {
            using Gate = dw4::game::AchievementGate;
            // The campaign / explore bots and scripted walks drive the player.
            if (scripted.active_goto(frame)) return Gate::bot;
            const auto key = stage_key();
            if (key != taint_stage) {
                taint_stage = key;
                stage_cheats = stage_rebalanced = false;
            }
            const auto& cheats = engine.cheats();
            stage_cheats |= cheats.god || cheats.infinite_mp || cheats.one_hit;
            stage_rebalanced |= cheats.exp_rate != 100 || cheats.damage_dealt != 100 || cheats.damage_taken != 100;
            if (stage_cheats) return Gate::cheats;
            if (stage_rebalanced) return Gate::rebalancing;
            if (state_loaded_stage && *state_loaded_stage == key) return Gate::save_state;
            return Gate::open;
        };
        auto achievement_step = [&] {
            dw4::game::AchievementStep step;
            step.stage = engine.stage_name();
            step.live = !engine.loading() && engine.presenting() && engine.stage_active() && !dw4::game::is_menu_stage(step.stage);
            step.online = net.active();
            step.hosting = net.active() && net.role() == dw4::game::NetSession::Role::host;
            step.online_players = net.active() ? net.start().players : 0;
            step.stage_serial = stage_key();
            // An online room (SEAMLESS.md 11): this machine's slot only, the
            // story is the room owner's, the room's members count.
            const int room_slot = online ? std::clamp(own_slot(), 0, 3) : 0;
            if (online) {
                step.online = true;
                step.own_slot = room_slot;
                step.shared_story = true;
                // Round I1a (19.8.9): the session's facts (the world has no
                // room to ask): the story is the own at home and the host's in
                // an instance; the instance's players; "Connected" only as a
                // guest in someone else's instance.
                const auto& world = online->session();
                step.story_owner = step.hosting = !world.in_instance() || world.is_host();
                step.online_players = std::max(1, world.instance_players());
                step.joined_other = world.in_instance() && !world.is_host();
                if (online_achievements) online_achievements->set_world_facts(step.story_owner, step.online_players);
            }
            if (const auto* sys = engine.get<dw4::engine::GameSystem>()) {
                step.difficulty = sys->difficulty;
                for (int slot = 0; slot < 4; ++slot) {
                    const auto& player = sys->players[std::size_t(slot)];
                    auto& seen = step.players[std::size_t(slot)];
                    seen.joined = player.joined && player.stats.ready;
                    seen.local = online ? slot == room_slot : !net.active() || slot == net.local_slot();
                    seen.unique = player.unique;
                    seen.level = player.stats.level;
                    seen.exp = player.stats.exp;
                    seen.species = player.stats.species;
                    seen.hp = player.hp;
                    seen.max_hp = player.max_hp;
                    seen.bits = player.bits;
                    seen.bank_bits = player.bank_bits;
                    seen.devices = int(player.devices.size());
                    for (const auto& device : player.devices) {
                        seen.best_rank = std::max(seen.best_rank, device.rank);
                        seen.best_value = std::max(seen.best_value, device.value);
                    }
                    for (const auto& [species, reached] : player.base_isuse)
                        if (species >= 0 && species < 16 && reached) seen.forms[std::size_t(species)] = true;
                    seen.form_levels = player.base_level;
                    seen.techniques = int(player.wazalist.size());
                }
                for (const auto& [card, owned] : sys->players[std::size_t(room_slot)].cards) step.cards += owned == 1 ? 1 : 0;
            }
            // (find, not operator[]: a read must not add entries.)
            for (std::int32_t flag = 89; flag <= 98; ++flag)
                if (const auto found = engine.flags().find(flag); found != engine.flags().end() && found->second) ++step.numemon_freed;
            return step;
        };
        // Randomizer runs (item drops, enemy or loading zone shuffle) unlock
        // into their own list.
        auto randomized = [&] {
            const auto& cheats = engine.cheats();
            return cheats.drop_seed != 0 || cheats.enemy_shuffle() || cheats.zone_shuffle();
        };
        // After every step: the step's events into the tracker.
        auto observe_step = [&] {
            auto events = engine.take_port_events();
            if (trace_events)
                for (const auto& e : events)
                    std::printf("[%d] event %s slot=%d value=%d extra=%d %s\n", frame, dw4::engine::port_event_name(e.kind), e.slot, e.value,
                                e.extra, e.text.c_str());
            // Player 1's new games (the randomizer binds only those).
            for (const auto& e : events)
                if (e.kind == dw4::engine::PortEvent::Kind::new_character && e.slot == 0) new_characters.insert(std::uint32_t(e.value));
            // The online character: the save room's card writes go up (SEAMLESS.md 9).
            if (online) online->port_events(events);
            // The online account's achievements count whatever the local
            // setting says (a player who turned the local ones off got no
            // online ones either); test runs: DW4_ACHIEVEMENTS for both.
            const bool local_on = achievements_on();
            const bool online_on = online_achievements && online && !engine.replay() && (local_on || !options.smoke_frames);
            if (!local_on && !online_on) return;
            last_gate = achievement_gate();
            const auto step = achievement_step();
            const auto unlocked = local_on ? achievements.observe(step, events, last_gate, randomized(), unix_now())
                                           : decltype(achievements.observe(step, events, last_gate, randomized(), unix_now())){};
            // The online account (SEAMLESS.md 11): only the member's own live
            // ticks ("was there"); a catch-up replays without port events.
            if (online_on) {
                using Mode = dw4::game::online::Mode;
                const auto mode = online->session().mode();
                const auto& cheats = engine.cheats();
                online_achievements->set_cheats(cheats.god || cheats.infinite_mp || cheats.one_hit,
                                                cheats.exp_rate != 100 || cheats.damage_dealt != 100 || cheats.damage_taken != 100);
                if (mode == Mode::solo || mode == Mode::sealed || mode == Mode::transition)
                    for (const auto& unlock : online_achievements->observe(step, events, last_gate, randomized(), unix_now(), steady_seconds()))
                        if (unlock.def && options.smoke_frames)
                            std::printf("[%d] online achievement %s (%s)\n", frame, unlock.def->id,
                                        dw4::game::achievement_online_kind_name(dw4::game::find_achievement_online(unlock.def->id)->kind));
            }
            for (const auto& unlock : unlocked) {
                if (!unlock.def) continue;
                if (options.smoke_frames)
                    std::printf("[%d] achievement %s%s (%s)\n", frame, unlock.def->id, unlock.randomizer ? " [randomizer]" : "", unlock.def->title);
                if (settings.achievement_popups || options.smoke_frames)
                    toasts.push_back({std::string(unlock.randomizer ? "Achievement unlocked (randomizer)" : "Achievement unlocked"),
                                      std::string(unlock.def->title) + " - " + unlock.def->description,
                                      std::chrono::steady_clock::now() + std::chrono::seconds(6)});
            }
            if (!unlocked.empty() || std::chrono::steady_clock::now() - last_profile_save > std::chrono::minutes(2)) save_profile();
        };
        // DW4_DEV_DUMP: the presents of a test run, counted from its start.
        std::optional<dw4::game::DevDumper> dev_dumper;
        if (!dev.dump_dir.empty()) dev_dumper.emplace(dev);
        int dev_present = 0;
        if (dev.presenting()) present_interval = 1.0 / dev.hz;
        const int trace_player = std::getenv("DW4_TRACE_PLAYER") ? std::atoi(std::getenv("DW4_TRACE_PLAYER")) : 0;
        // DW4_TRACE_FINGERPRINT=N prints the state fingerprint every N frames:
        // two runs of the same script must print the same lines (determinism).
        const int trace_fingerprint = std::getenv("DW4_TRACE_FINGERPRINT") ? std::atoi(std::getenv("DW4_TRACE_FINGERPRINT")) : 0;
        auto last = std::chrono::steady_clock::now();
        double accumulator = 0;
        // State files live in saves/states next to the program (not in test runs).
        std::filesystem::path state_dir;
        if (const char* program = SDL_GetBasePath()) state_dir = std::filesystem::path(program) / "saves" / "states";
        auto state_path = [&](std::size_t slot) { return state_dir / ("state" + std::to_string(slot + 1) + ".dw4state"); };
        if (!options.smoke_frames && !state_dir.empty())
            for (std::size_t slot = 0; slot < state_slots.size(); ++slot) state_slots[slot] = read_state_file(state_path(slot));
        // The randomizer's bond (randomizer_save.hpp): when player 1's
        // character changes at the character select (a new game or a called
        // save), the seed and modes bound to it apply. A new game without one
        // is bound to the current settings when they randomize; a called save
        // without one plays the original game (the F1 menu can bind it).
        // Test runs bind every character to DW4_RANDOMIZER unless
        // DW4_RANDOMIZER_BIND=new.
        const bool bind_called_saves = options.smoke_frames && !(std::getenv("DW4_RANDOMIZER_BIND") && std::string_view(std::getenv("DW4_RANDOMIZER_BIND")) == "new");
        auto bind_randomizer = [&] {
            auto* sys = engine.get<dw4::engine::GameSystem>();
            if (!sys || !sys->game_started) return;
            const auto& player = sys->players[0];
            if (!player.joined || player.unique == 0 || player.unique == randomizer_unique) return;
            randomizer_unique = player.unique;
            std::string name;
            for (const char16_t c : player.name)
                name.push_back(c >= 0xFF01 && c <= 0xFF5E ? char(c - 0xFEE0) : c == 0x3000 ? ' ' : c < 0x80 ? char(c) : '?');
            while (!name.empty() && (name.back() == ' ' || name.back() == '\0')) name.pop_back();
            randomizer_name = name;
            const auto bound = dw4::game::load_randomizer_binding(randomizer_folder, player.unique);
            const auto config = randomizer_settings();
            const bool new_game = new_characters.contains(player.unique);
            switch (dw4::game::choose_randomizer_bond(bound.has_value(), new_game, config.active(), bind_called_saves)) {
            case dw4::game::BondChoice::use_bond: randomizer_bound = bound; break;
            case dw4::game::BondChoice::bind_settings:
                randomizer_bound = config;
                dw4::game::store_randomizer_binding(randomizer_folder, player.unique, name, config);
                dw4::game::write_randomizer_spoiler(randomizer_folder, player.unique, config);
                break;
            case dw4::game::BondChoice::original:
                // An explicit "off": later changes of the settings are for new games.
                randomizer_bound = dw4::game::RandomizerConfig{};
                if (config.active()) {
                    std::cout << "randomizer: save " << dw4::game::randomizer_unique_key(player.unique)
                              << " has no bond and plays the original game\n";
                    toasts.push_back({"Randomizer", "This save has no randomizer bond, so it plays the original game. F1 > Cheats can bind "
                                                    "the randomizer settings to it.",
                                      std::chrono::steady_clock::now() + std::chrono::seconds(8)});
                }
                break;
            }
            apply_gameplay(engine.cheats());
            if (randomizer_bound->active())
                std::cout << "randomizer: save " << dw4::game::randomizer_unique_key(player.unique) << " plays seed " << randomizer_bound->seed
                          << " (enemies " << randomizer_bound->enemies << ", zones " << randomizer_bound->zones << ", drops "
                          << randomizer_bound->drops << (new_game ? ", new game" : "") << ")\n";
        };
        auto can_save = [&]() -> std::string {
            if (net.active() || online) return "Save states are off in online sessions.";
            if (engine.rebooting()) return "The console is restarting.";
            const auto* snapshot = engine.stage_snapshot();
            if (!snapshot) return "No stage has loaded yet.";
            if (snapshot->stage.rfind("stagis", 0) == 0) return "Save states work after the title screens.";
            return {};
        };
        auto save_state = [&](std::size_t slot) {
            if (const auto problem = can_save(); !problem.empty()) {
                state_message = problem;
            } else {
                state_slots[slot] = SaveState{*engine.stage_snapshot(), load_step_drawn, input_log, engine.fingerprint(), frame};
                state_message = "Saved state " + std::to_string(slot + 1) + " (" + engine.stage_name() + ")";
                if (!options.smoke_frames && !state_dir.empty() && !write_state_file(state_path(slot), *state_slots[slot]))
                    state_message += " - the file could not be written";
            }
            message_until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        };
        auto load_state = [&](std::size_t slot) {
            if (net.active() || online) {
                state_message = "Save states are off in online sessions.";
                message_until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                return;
            }
            if (!state_slots[slot]) {
                state_message = "State " + std::to_string(slot + 1) + " is empty.";
                message_until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                return;
            }
            const SaveState state = *state_slots[slot];
            const auto started = std::chrono::steady_clock::now();
            engine_storage.reset();
            engine_storage.emplace(options.data);
            engine.set_resource_overrides(mod_folders);
            engine.set_card_store(player_store);
            for (int slot = 0; slot < 2; ++slot) engine.set_card_setting(slot, player_card_settings[std::size_t(slot)]);
            apply_test_online(engine);
            engine.restore(state.snapshot);
            if (state.load_step_drawn) engine.traverse_scene();
            std::vector<std::int16_t> scratch(std::size_t(kAudioFrameSamples + 1) * 2);
            auto* sound = engine.get<dw4::engine::SoundManager>();
            const auto replay_started = std::chrono::steady_clock::now();
            std::chrono::steady_clock::duration audio_time{};
            for (const auto& step : state.inputs) {
                engine.step(step.pads);
                if (sound) {
                    const auto mixed = options.smoke_frames ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
                    sound->render(scratch.data(), std::size_t(step_samples(engine.frame()))); // music position
                    if (options.smoke_frames) audio_time += std::chrono::steady_clock::now() - mixed;
                }
                if (step.drawn) engine.traverse_scene();
            }
            // (Test runs: the replay's own rate without the engine's start,
            // and without the music mix, which a catch-up replay leaves out.)
            const double replay_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - replay_started).count();
            const double audio_seconds = std::chrono::duration<double>(audio_time).count();
            if (options.smoke_frames && !state.inputs.empty())
                std::printf("state replay: %zu steps in %.3f s (%.3f s music mix), %.0f steps per second, %.0f without the mix\n",
                            state.inputs.size(), replay_seconds, audio_seconds, double(state.inputs.size()) / std::max(replay_seconds, 1e-9),
                            double(state.inputs.size()) / std::max(replay_seconds - audio_seconds, 1e-9));
            const bool exact = engine.fingerprint() == state.fingerprint;
            // (No achievement counts in the rest of this stage.)
            ++engine_generation;
            state_loaded_stage = stage_key();
            labels.reset();
            // The replay loaded with the state's load timing; from the next
            // stage load on the current choice applies again.
            engine.cheats().fast_loading = fast_loading_choice();
            input_log = state.inputs;
            load_step_drawn = state.load_step_drawn;
            last_step_loaded = false;
            log_serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
            frame = state.frame;
            accumulator = 0;
            if (renderer) renderer->clear_cache();
            interpolator.reset();
            rendered_stage.clear();
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            char line[160];
            std::snprintf(line, sizeof line, "Loaded state %zu (%zu steps replayed in %.1f s)%s", slot + 1, state.inputs.size(),
                          seconds, exact ? "" : " - the replay differs from the saved session");
            state_message = line;
            if (options.smoke_frames) std::cout << "state: " << line << '\n';
            message_until = std::chrono::steady_clock::now() + std::chrono::seconds(exact ? 2 : 6);
        };
        // Hosting or joining from the F1 menu: the lobby runs while the game
        // waits; once everyone is in, every machine starts at the title.
        std::string lobby_status;
        auto begin_session = [&] {
            const auto& started = net.start();
            engine_storage.reset();
            engine_storage.emplace(options.data);
            engine.set_resource_overrides(mod_folders);
            engine.cheats().god = started.cheats & 1;
            engine.cheats().infinite_mp = started.cheats & 2;
            engine.cheats().one_hit = started.cheats & 4;
            engine.cheats().exp_rate = started.exp_rate;
            engine.cheats().damage_dealt = started.damage_dealt;
            engine.cheats().damage_taken = started.damage_taken;
            engine.cheats().drop_seed = started.drop_seed;
            engine.cheats().fast_loading = started.fast_loading; // the host's choice for everyone
            engine.cheats().rand_seed = started.rand_seed;      // the host's randomizer too
            engine.cheats().rand_enemies = started.rand_enemies;
            engine.cheats().rand_zones = started.rand_zones;
            session_cards = std::filesystem::temp_directory_path() /
                            ("dw4_session_" + std::to_string(net.local_slot()) + "_" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            write_session_card(session_cards / "Card A", started.card_a);
            write_session_card(session_cards / "Card B", started.card_b);
            std::cout << "online: shared cards: Card A " << card_names(started.card_a) << ", Card B " << card_names(started.card_b) << '\n';
            own_card_name = net.local_slot() == 0 ? "Card A" : net.local_slot() == 1 ? "Card B" : "";
            engine.set_card_root(session_cards);
            engine.request_stage(started.stage);
            frame_base = 0;
            net_submitted = std::uint64_t(started.delay);
            ++engine_generation;
            labels.reset();
            input_log.clear();
            log_serial = 0;
            accumulator = 0;
            if (renderer) renderer->clear_cache();
            interpolator.reset();
            rendered_stage.clear();
            state_message = "Online: player " + std::to_string(net.local_slot() + 1) + " of " + std::to_string(started.players);
            message_until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        };
        auto end_session = [&] {
            // Only the DW4 save goes back, and only when the session changed it.
            if (!own_card_name.empty())
                if (const auto note = dw4::game::write_back_session_card(player_store, session_cards / own_card_name); !note.empty()) {
                    std::cout << note << '\n';
                    engine.log(note);
                }
            std::error_code error;
            if (!session_cards.empty()) std::filesystem::remove_all(session_cards, error);
            session_cards.clear();
            own_card_name.clear();
            net.close();
            engine.set_card_store(player_store);
            for (int slot = 0; slot < 2; ++slot) engine.set_card_setting(slot, player_card_settings[std::size_t(slot)]);
            apply_gameplay(engine.cheats());
        };
        // The console reboots (Engine::reboot_due, the reset button): a new
        // engine boots from the disc as at power-on, with the license screen,
        // the memory card check, the logos and the opening (straight to the
        // title with Skip intro, not online). What is outside the console
        // stays: the memory cards and their slots, the mods, the PC options
        // and cheats.
        auto reboot_console = [&] {
            const auto cheats = engine.cheats();
            const auto cards = engine.card_store();
            const std::array<dw4::engine::CardSetting, 2> slots{engine.card_setting(0), engine.card_setting(1)};
            frame_base += engine.frame();
            engine_storage.reset();
            engine_storage.emplace(options.data);
            engine.set_resource_overrides(mod_folders);
            engine.set_card_store(cards);
            for (int slot = 0; slot < 2; ++slot) engine.set_card_setting(slot, slots[std::size_t(slot)]);
            engine.cheats() = cheats;
            if (!net.active()) apply_test_online(engine);
            engine.request_stage(!net.active() && !options.smoke_frames && settings.skip_intro ? "stagis10" : "stagis08");
            ++engine_generation;
            labels.reset();
            input_log.clear();
            log_serial = 0;
            load_step_drawn = last_step_loaded = false;
            movie_decoder.reset();
            movie_serial = 0;
            movie_stage.clear();
            if (renderer) renderer->clear_cache();
            interpolator.reset();
            rendered_stage.clear();
            std::cout << "console reboot at frame " << frame << std::endl;
        };
        // A new engine for the pictures, labels, movies and achievements (an
        // online session's start and end).
        auto restart_presentation = [&] {
            ++engine_generation;
            labels.reset();
            input_log.clear();
            log_serial = 0;
            load_step_drawn = last_step_loaded = false;
            movie_decoder.reset();
            movie_serial = 0;
            movie_stage.clear();
            if (renderer) renderer->clear_cache();
            interpolator.reset();
            rendered_stage.clear();
            accumulator = 0;
        };
        // Back to the offline title with the player's own cards (after an
        // online session, or when one could not start).
        auto offline_title = [&] {
            fresh_engine();
            engine.set_card_store(player_store);
            for (int slot = 0; slot < 2; ++slot) engine.set_card_setting(slot, player_card_settings[std::size_t(slot)]);
            apply_gameplay(engine.cheats());
            engine.request_stage("stagis10");
            restart_presentation();
        };
        // --load-state FILE: the run starts where that state was saved.
        if (!options.load_state.empty()) {
            state_slots[0] = read_state_file(options.load_state);
            if (!state_slots[0]) throw std::runtime_error("unreadable state file " + options.load_state.string());
            load_state(0);
            // (A campaign run resumed: the driver's memory saved with the state.)
            if (const auto memory = std::filesystem::path(options.load_state).replace_extension(".mem"); std::filesystem::exists(memory))
                dw4::game::campaign_memory_load(memory);
            // DW4_CHEATS_FROM_ENV=1 (test runs): the test cheats from the
            // environment (DW4_GOD, DW4_ONE_HIT, DW4_INFINITE_MP) instead of
            // the state's, from the loaded frame on (a cheat run resumed
            // without them).
            if (std::getenv("DW4_CHEATS_FROM_ENV")) {
                engine.cheats().god = std::getenv("DW4_GOD") != nullptr;
                engine.cheats().one_hit = std::getenv("DW4_ONE_HIT") != nullptr;
                engine.cheats().infinite_mp = std::getenv("DW4_INFINITE_MP") != nullptr;
            }
            // DW4_LEVEL_ON_LOAD=n, DW4_DISKS_ON_LOAD=record:count,... (test
            // aids for research runs, as DW4_LEVEL / DW4_DISKS for a new
            // player): player 1's level (the EXP that reaches it; RECALCPLAYER
            // refills HP and MP) and disk counts on the loaded state, to try a
            // later stage at the level and with the supplies a run would
            // bring there (a cheat run's state reaches it at a lower level).
            if (auto* sys = engine.get<dw4::engine::GameSystem>()) {
                auto& player = sys->players[0];
                if (const char* level = std::getenv("DW4_LEVEL_ON_LOAD")) {
                    auto& stats = player.stats;
                    stats.level = std::clamp(std::atoi(level), 1, 999);
                    const std::size_t form = std::size_t(std::clamp(stats.species, 0, 15));
                    player.base_level[form] = stats.level;
                    const int below = stats.level - 1;
                    stats.exp = (below + 140) * below * below + below * 200;
                    player.base_exp[form] = stats.exp;
                    player.hp = 0;
                    sys->recalc_player(engine, 0);
                }
                if (const char* disks = std::getenv("DW4_DISKS_ON_LOAD"))
                    for (const char* at = disks; *at;) {
                        int record = 0, count = 0;
                        if (std::sscanf(at, "%d:%d", &record, &count) == 2) player.disk_count(record) = std::clamp(count, 0, 9);
                        while (*at && *at != ',') ++at;
                        if (*at == ',') ++at;
                    }
            }
        }
        // DW4_STAGE_STATES=DIR (test runs): a state file at every stage start
        // (DIR/<frame>_<stage>.dw4state), for resuming long scripted runs.
        const char* stage_states = std::getenv("DW4_STAGE_STATES");
        // Round S1 test tools (SEAMLESS.md 6.8, 7.1), test runs only:
        // DW4_SNAPSHOT_DUMP=DIR writes every stage load's snapshot as
        // DIR/<serial>_<stage>.raw and .canon (SnapshotForm); DW4_PAD_LOG=FILE
        // records the four pads of every step (the frame before it, then per
        // pad connected u8, four f32 sticks, u32 buttons); DW4_PAD_REPLAY=FILE
        // steps with those pads instead of the composed ones wherever the log
        // has the frame (a run resumed with --load-state replays another
        // process's session: the cross-process join check).
        const char* snapshot_dump = options.smoke_frames ? std::getenv("DW4_SNAPSHOT_DUMP") : nullptr;
        std::unique_ptr<std::FILE, int (*)(std::FILE*)> pad_log(nullptr, &std::fclose);
        if (const char* path = options.smoke_frames ? std::getenv("DW4_PAD_LOG") : nullptr) pad_log.reset(std::fopen(path, "wb"));
        std::map<int, std::array<dw4::engine::PadSample, 4>> pad_replay;
        if (const char* path = options.smoke_frames ? std::getenv("DW4_PAD_REPLAY") : nullptr)
            if (std::FILE* f = std::fopen(path, "rb")) {
                std::uint32_t at = 0;
                while (std::fread(&at, sizeof at, 1, f) == 1) {
                    std::array<dw4::engine::PadSample, 4> logged{};
                    bool complete = true;
                    for (auto& pad : logged) {
                        std::uint8_t connected = 0;
                        float sticks[4]{};
                        complete = complete && std::fread(&connected, 1, 1, f) == 1 && std::fread(sticks, sizeof(float), 4, f) == 4 &&
                                   std::fread(&pad.buttons, sizeof pad.buttons, 1, f) == 1;
                        pad.connected = connected != 0;
                        pad.left = {sticks[0], sticks[1]};
                        pad.right = {sticks[2], sticks[3]};
                    }
                    if (!complete) break;
                    pad_replay[int(at)] = logged;
                }
                std::fclose(f);
                std::cout << "pad replay: " << pad_replay.size() << " steps\n";
            }
        // DW4_TRACE_LOADTIME=1 (test runs): for every stage load the fields
        // from the step that took the change to the stage start (first.lua)
        // and to the first step after it in which player 1 has control with
        // pad input on, and the real time the loop passes of the load took
        // (all of them and the longest); DW4_TRACE_LOADTIME=2 also every pass
        // of a load with the loader's state.
        const int trace_loadtime = std::getenv("DW4_TRACE_LOADTIME") ? std::atoi(std::getenv("DW4_TRACE_LOADTIME")) : 0;
        struct LoadTiming {
            std::string stage;
            std::uint64_t serial{}, begin{}, started{}, control{};
            double total_ms{}, longest_ms{};
            bool fast{};
        } load_timing;
        auto print_load_timing = [&] {
            const std::string control = load_timing.control ? std::to_string(load_timing.control - load_timing.begin) : std::string("-");
            std::printf("loadtime %s%s: started after %lld fields, control after %s fields, loop %.1f ms (longest pass %.1f ms)\n",
                        load_timing.stage.c_str(), load_timing.fast ? " (fast)" : "",
                        load_timing.started ? (long long)(load_timing.started - load_timing.begin) : -1LL, control.c_str(),
                        load_timing.total_ms, load_timing.longest_ms);
        };
        while (running) {
            // Data Overflow (SURVIVAL.md 13.3): between two passes, once the
            // story has faded out, a run takes over until it is over; the
            // story engine waits untouched and goes on from where it was.
            if (overflow_switch.due()) {
                const auto request = overflow_switch.take();
                std::cout << "overflow: run " << dw4::game::overflow::describe(request) << " at story frame " << frame << " ("
                          << engine.stage_name() << ")\n";
                dw4::game::overflow::RunHost run_host;
                run_host.discord = discord ? &*discord : nullptr;
                run_host.screenshot = options.screenshot;
                run_host.test_frames = options.smoke_frames;
                const auto outcome = dw4::game::overflow::run_from_story(view, engine, request, run_host);
                std::printf("overflow: run %s after %llu steps (%s Lv %d), run hash %016llx%s%s\n", dw4::game::overflow::run_end_name(outcome.end),
                            (unsigned long long)outcome.steps, dw4::game::overflow::form_display_name(outcome.form).c_str(), outcome.level,
                            (unsigned long long)outcome.run_hash, outcome.error.empty() ? "" : ": ", outcome.error.c_str());
                // Back in the story (13.3 step 4): the pacing starts over (the
                // story does not catch up the run's time), the pad waits for
                // released buttons, the picture and the sound fade in.
                accumulator = 0;
                last = std::chrono::steady_clock::now();
                next_present = view.last_present = last;
                pad_latch = true;
                overflow_switch.returned();
                state_message = outcome.end == dw4::game::overflow::RunEnd::failed ? "Data Overflow could not run: " + outcome.error
                                                                                    : std::string("Back from Data Overflow (") +
                                                                                          dw4::game::overflow::run_end_name(outcome.end) + ")";
                message_until = last + std::chrono::seconds(3);
                if (outcome.end == dw4::game::overflow::RunEnd::window_closed || overflow_quit_after) running = false;
                continue;
            }
            const auto pass_start = std::chrono::steady_clock::now();
            bool pass_presented = false; // (online.log's pictures per second)
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                // A binding being edited, ImGui, quit, the window size,
                // controllers, the View button, F1 and F11 (host_view.hpp).
                if (dw4::game::host_event(view, event)) continue;
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F5) save_state(0);
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F9) load_state(0);
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F2) {
                    free_camera = !free_camera;
                    if (!free_camera) engine.debug_camera.reset();
                }
                if (free_camera && event.type == SDL_EVENT_MOUSE_MOTION && (event.motion.state & SDL_BUTTON_RMASK)) {
                    free_yaw -= event.motion.xrel * 0.005f;
                    free_pitch = std::clamp(free_pitch - event.motion.yrel * 0.005f, -1.5f, 1.5f);
                }
            }
            if (view.quit_requested) running = false;
            // Input: player 1 is the keyboard and the first gamepad, players 2-4
            // the further gamepads, each on its own controller slot. The main
            // stick, the D-pad and the C-stick stay separate inputs like on a
            // GameCube pad (controls.hpp): movement keys and a left stick walk,
            // D-pad keys and buttons only set D-pad bits, C-stick keys and a
            // right stick move the C-stick.
            std::array<dw4::HostPadInput, 4> live{};
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
            } else if (!show_menu && !title_online.active) {
                // The keyboard and the controllers (host_view.hpp).
                live = dw4::game::read_live_input(view);
            }
            // (Data Overflow: the story steps with neutral live pads while it
            // fades out, as with the F1 menu open; its sound fades along.)
            if (overflow_switch.fading_out()) live = {};
            // (Behind Play's loading screen the title is not heard either.)
            view.gain = overflow_switch.gain() * (title_driver.covering() ? 0.0f : 1.0f);
            // Fixed-step simulation at the NTSC field rate (59.94 Hz, one
            // original frame per field; Engine::step adds a field of time).
            const auto now = std::chrono::steady_clock::now();
            // (DW4_DEV_PRESENT: every pass is one present of the virtual refresh.)
            const double pass_seconds = dev.presenting() ? 1.0 / dev.hz : std::chrono::duration<double>(now - last).count();
            accumulator += pass_seconds;
            last = now;
            // (Online rooms keep real time in test runs too: the relay seals on its clock.)
            int steps = options.smoke_frames && !dev.presenting() && !online && !ui_script.loaded() ? (net.in_lobby() ? 0 : 1) : 0;
            if (options.smoke_frames && net.in_lobby()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            // DW4_MENU_HOST=port / DW4_MENU_JOIN=port (test runs): a direct LAN
            // session hosted or joined at frame 100, on loopback (what the old
            // Online tab's Host / Join did; the command line keeps --host / --join).
            if (options.smoke_frames && frame == 100 && !net.active() && !net.in_lobby()) {
                std::string error;
                if (const char* port = std::getenv("DW4_MENU_HOST"))
                    net.host(std::atoi(port), 2, 3, data_hash(engine.resources()), error, true);
                else if (const char* join_port = std::getenv("DW4_MENU_JOIN"))
                    net.join("127.0.0.1", std::atoi(join_port), data_hash(engine.resources()), session_card(player_store), error);
                if (!error.empty()) std::cout << "online: " << error << '\n';
            }
            if (net.in_lobby()) {
                dw4::game::SessionStart start;
                start.stage = "stagis10"; // everyone meets at the title
                start.cheats = std::uint8_t((engine.cheats().god ? 1 : 0) | (engine.cheats().infinite_mp ? 2 : 0) |
                                            (engine.cheats().one_hit ? 4 : 0));
                start.card_a = session_card(player_store);
                start.exp_rate = engine.cheats().exp_rate;
                start.damage_dealt = engine.cheats().damage_dealt;
                start.damage_taken = engine.cheats().damage_taken;
                start.drop_seed = engine.cheats().drop_seed;
                start.fast_loading = engine.cheats().fast_loading;
                start.rand_seed = engine.cheats().rand_seed;
                start.rand_enemies = engine.cheats().rand_enemies;
                start.rand_zones = engine.cheats().rand_zones;
                std::string error;
                if (net.lobby(start, lobby_status, error)) {
                    begin_session();
                } else if (!error.empty()) {
                    state_message = "Online: " + error;
                    message_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    net.close();
                } else {
                    state_message = "Online: " + lobby_status;
                    message_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
                }
            }
            // (The online screens over an offline title wait while the title is
            // no menu, so no attract movie starts while the player types; over
            // the menu the title runs on: it waits for the player anyway.)
            const auto* title_menu = engine.get<dw4::engine::Title2Manager>();
            const bool paused = (show_menu && settings.pause_in_menu && !options.smoke_frames && !net.active() && !online) || net.in_lobby() ||
                                (title_online.active && !online && !(title_menu && title_menu->menu_open()));
            if (net.active())
                if (auto message = net.take_message(); !message.empty()) {
                    std::cout << "online: " << message << '\n';
                    state_message = message;
                    message_until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                }
            if (paused) accumulator = 0;
            if ((!options.smoke_frames || dev.presenting() || online || ui_script.loaded()) && !paused) {
                while (accumulator >= dw4::engine::kFieldSeconds && steps < 4) { accumulator -= dw4::engine::kFieldSeconds; ++steps; }
                if (steps == 4) accumulator = 0;
            }
            // Online rooms: the session decides (solo-led: this clock; sealed:
            // the sealed ticks at the server's pace; parked or catching up:
            // none).
            if (online) {
                online->pass(steady_seconds());
                if (online_achievements) online_achievements->pass(steady_seconds());
                steps = online->session().steps_allowed(steps, pass_seconds);
                if (online->session().paces()) accumulator = 0;
                if (options.smoke_frames && steps == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else if (options.smoke_frames && ui_script.loaded() && steps == 0) {
                // (A UI test run keeps real time without a vsync of its own.)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            // The title menu's third entry "Online" (Engine::title_online_entry):
            // offline with the online screens only (never in an online session,
            // a direct session or a test run without --online-ui; DW4_TITLE_ONLINE=1
            // shows it in a test run without them, for pictures of the menu).
            static const bool title_online_test = options.smoke_frames && std::getenv("DW4_TITLE_ONLINE") != nullptr;
            engine.title_online_entry = (lobby || title_online_test) && !online && !net.active() && !net.in_lobby() && !title_entry.failed();
            // Online: a step runs once every player's input for it is here.
            std::uint64_t net_frame = 0;
            for (int i = 0; i < steps; ++i) {
                // (An online room's barrier, a missing sealed tick or a restore ends the pass.)
                if (online && !online->session().step_ready()) break;
                if (net.active()) {
                    net_frame = frame_base + engine.frame() + 1;
                    net.poll();
                    if (options.smoke_frames) {
                        const auto wait_until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
                        while (!net.ready(net_frame) && std::chrono::steady_clock::now() < wait_until) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                            net.poll();
                        }
                    }
                    if (!net.ready(net_frame)) {
                        if (options.smoke_frames) throw std::runtime_error("online: inputs stopped arriving");
                        accumulator = std::min(accumulator, 4.0 * dw4::engine::kFieldSeconds);
                        break;
                    }
                }
                // As on the original (one picture per 60 Hz step), the scene
                // is traversed once after every step: catching up several
                // steps traverses the skipped pictures without drawing them
                // (loading screens and route animations run on that clock).
                // A field the running update covers draws nothing new.
                if (i > 0 && !engine.held()) {
                    engine.traverse_scene();
                    items_current = false;
                    if (last_step_loaded) load_step_drawn = true;
                    else if (!input_log.empty()) input_log.back().drawn = true;
                }
                for (const auto& send : scripted.sends) {
                    if (send.frame != frame) continue;
                    dw4::engine::Packet packet;
                    if (!send.argument.empty()) dw4::engine::parse_packet_arguments(send.argument, packet);
                    // An input-script SEND has no counterpart in the original.
                    const dw4::engine::Messenger::PortOnly script_send(engine.messenger());
                    engine.messenger().send(engine, send.category, std::nullopt, send.row, send.command, packet);
                }
                // Player 1: the live devices, the script's presses and the
                // bots. stick_x / stick_y are the main stick in host units
                // (a live analog stick or movement keys, else a bot's
                // steering); the bots steer only while it is centred.
                std::uint32_t input = live[0].buttons | scripted.held(frame) | (engine.frame() < ui_pad_until ? ui_pad : 0u);
                float stick_x = live[0].stick.x, stick_y = live[0].stick.y;
                if (stick_x == 0 && stick_y == 0) {
                    const auto keys = live[0].stick_directions;
                    if (keys & dw4::pad_direction::negative_x) stick_x = -1;
                    if (keys & dw4::pad_direction::positive_x) stick_x = 1;
                    if (keys & dw4::pad_direction::positive_y) stick_y = 1;
                    if (keys & dw4::pad_direction::negative_y) stick_y = -1;
                }
                const std::uint32_t before_bots = input;
                if (online_script.active() && stick_x == 0 && stick_y == 0) {
                    // The online test driver plays this member's slot with its
                    // own camera (online_script.hpp).
                    dw4::game::OnlineScriptFacts facts;
                    facts.frame = frame;
                    if (online) {
                        facts.own_slot = online->session().own_slot();
                        facts.present = online->session().present();
                        facts.mode = dw4::game::online::mode_name(online->session().mode());
                        facts.tick = online->session().tick();
                        if (const auto& story = online->session().confirmed_story()) facts.room_story = &*story;
                        // (Round I1b: the go-along view.)
                        go_facts(facts, online->session(), steady_seconds());
                    }
                    facts.ghosts = ghosts.shown();
                    facts.figures = ghosts.staying();
                    const auto out = online_script.update(engine, facts);
                    if (!out.shot.empty() && !options.screenshot.empty()) ui_shot = out.shot;
                    if (online && (out.go_along || out.go_cancel || out.go_close)) {
                        auto& session = online->session();
                        if (out.go_along && session.go_along(out.go_along)) std::cout << "online: go along (offer " << out.go_along << ") at frame " << frame << '\n';
                        if (out.go_cancel && session.cancel_go_along()) std::cout << "online: go along cancelled at frame " << frame << '\n';
                        if (out.go_close) session.close_go_notices(out.go_close);
                    }
                    if (online && out.go_along_raw && online->session().go_along_raw(out.go_along_raw))
                        std::cout << "online: go along (closed offer " << out.go_along_raw << ") at frame " << frame << '\n';
                    if (online && out.test_hook &&
                        !online->session().test_hook_request(dw4::game::online::wire::TestHook(out.test_hook), out.test_value))
                        std::cout << "online: the test hook could not be asked at frame " << frame << " (not sealed)\n";
                    if (online && out.go_back && online->session().go_back_to_lobby())
                        std::cout << "online: back to the lobby from a held go along at frame " << frame << '\n';
                    // The run ends once the script is done.
                    if (online_script.finished()) running = false;
                    input |= out.buttons;
                    if (out.push & dw4::pad_direction::negative_x) stick_x = -1;
                    if (out.push & dw4::pad_direction::positive_x) stick_x = 1;
                    if (out.push & dw4::pad_direction::positive_y) stick_y = 1;
                    if (out.push & dw4::pad_direction::negative_y) stick_y = -1;
                    auto* models = engine.get<dw4::engine::ModelCtrlManager>();
                    auto* cameras = engine.get<dw4::engine::CameraManager>();
                    const auto* p = models ? models->player(facts.own_slot) : nullptr;
                    if (out.walk && p && p->node && cameras) {
                        const float dx = out.x - p->node->matrix(3, 0), dz = out.z - p->node->matrix(3, 2);
                        if (dx * dx + dz * dz > out.reach) {
                            dw4::engine::FrameCamera camera;
                            cameras->frame_camera(engine, camera, facts.own_slot);
                            dw4::engine::Mat4 inverse;
                            if (dw4::engine::invert(camera.view, inverse)) {
                                const float relative = std::atan2(dx, dz) - std::atan2(inverse(2, 0), inverse(2, 2));
                                stick_x = std::sin(relative);
                                stick_y = -std::cos(relative);
                            }
                        }
                    }
                } else if (const auto* g = scripted.active_goto(frame); g && g->seek && g->target == "campaign" && stick_x == 0 && stick_y == 0) {
                    // SEEK campaign: the story from a new game (src/game/campaign.cpp).
                    static dw4::game::CampaignBot campaign;
                    auto* models = engine.get<dw4::engine::ModelCtrlManager>();
                    auto* cameras = engine.get<dw4::engine::CameraManager>();
                    const auto* p = models ? models->player(0) : nullptr;
                    const auto out = campaign.update(engine, p, frame);
                    // A segment run that has saved its checkpoint ends here
                    // (DW4_CAMPAIGN_STOP).
                    if (campaign.finished()) running = false;
                    input |= out.buttons;
                    if (out.walk && p && p->node && cameras) {
                        const float dx = out.x - p->node->matrix(3, 0), dz = out.z - p->node->matrix(3, 2);
                        if (dx * dx + dz * dz > out.reach) {
                            dw4::engine::FrameCamera camera;
                            cameras->frame_camera(engine, camera, 0); // the bot's player's own camera (per-slot cameras)
                            dw4::engine::Mat4 inverse;
                            if (dw4::engine::invert(camera.view, inverse)) {
                                const float relative = std::atan2(dx, dz) - std::atan2(inverse(2, 0), inverse(2, 2));
                                stick_x = std::sin(relative);
                                stick_y = -std::cos(relative);
                            }
                        }
                    }
                } else if (const auto* g = scripted.active_goto(frame); g && stick_x == 0 && stick_y == 0) {
                    auto* models = engine.get<dw4::engine::ModelCtrlManager>();
                    auto* cameras = engine.get<dw4::engine::CameraManager>();
                    if (const auto* p = models ? models->player(0) : nullptr; p && p->node && cameras) {
                        float tx = g->x, tz = g->z, reach = 4.0f;
                        // SEEK mat:<name> walks onto a floor mat (stage exits) and
                        // confirms its prompt.
                        if (g->seek && g->target.rfind("explore:", 0) == 0) {
                            // SEEK explore:<mat> plays through the stage towards a mat.
                            static dw4::game::ExploreBot explorer;
                            // A ride's targets on the way to the goal come first
                            // (the stag0103 boat, the stag0203 train: campaign.cpp
                            // kRideTargets).
                            std::string stage = engine.stage_name();
                            std::transform(stage.begin(), stage.end(), stage.begin(), [](unsigned char c) { return char(std::tolower(c)); });
                            dw4::game::CampaignBot::Output ride;
                            if (p->control_enabled && p->seq_name.rfind("SQ_DEMO", 0) != 0 &&
                                dw4::game::ride_targets_step(engine, *p, stage, g->target.substr(8), frame, ride)) {
                                input |= ride.buttons;
                                if (ride.walk) {
                                    tx = ride.x;
                                    tz = ride.z;
                                    reach = ride.reach;
                                } else {
                                    tx = p->node->matrix(3, 0);
                                    tz = p->node->matrix(3, 2);
                                }
                            } else {
                                const auto decision = explorer.update(engine, *p, g->target.substr(8), frame);
                                tx = decision.x;
                                tz = decision.z;
                                reach = decision.reach;
                                if (decision.attack) {
                                    if (decision.jump_attack) {
                                        if (frame % 40 < 3) input |= dw4::pad_button::x;
                                        else if (frame % 40 >= 10 && frame % 40 < 13) input |= dw4::pad_button::a;
                                    } else if (frame % (decision.fast_attack ? 10 : 20) < 3) {
                                        input |= dw4::pad_button::a;
                                    }
                                }
                                if ((decision.confirm && frame % 30 < 3) || (decision.pick_up && frame % 20 < 3) || decision.press_a)
                                    input |= dw4::pad_button::a;
                                if (decision.press_b) input |= dw4::pad_button::b;
                                if (decision.jump) input |= dw4::pad_button::x;
                                if (decision.press_r) input |= dw4::pad_button::r;
                            }
                        } else if (g->seek && g->target.rfind("mat:", 0) == 0) {
                            if (auto* mats = engine.get<dw4::engine::MatCtrlManager>())
                                if (const auto centre = mats->mat_centre(g->target.substr(4))) {
                                    tx = centre->x;
                                    tz = centre->z;
                                }
                            // A only on the mat (the stage change prompt); elsewhere
                            // it would attack instead of walking.
                            const float mx = tx - p->node->matrix(3, 0), mz = tz - p->node->matrix(3, 2);
                            if (mx * mx + mz * mz < 400.0f && frame % 30 < 3) input |= dw4::pad_button::a;
                        } else if (g->seek) {
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
                        // SEEK mat:<name> walks a route around walls (DW4_SEEK_DIRECT=1:
                        // a straight line).
                        if (g->seek && g->target.rfind("mat:", 0) == 0 && !std::getenv("DW4_SEEK_DIRECT")) {
                            static std::vector<dw4::engine::Vec3f> route;
                            static std::size_t route_next = 0;
                            static dw4::engine::Vec3f route_goal{1e9f, 0, 1e9f};
                            static int route_frame = -1000;
                            const dw4::engine::Vec3f me{p->node->matrix(3, 0), p->node->matrix(3, 1), p->node->matrix(3, 2)};
                            const dw4::engine::Vec3f goal{tx, me.y, tz};
                            auto flat = [](const dw4::engine::Vec3f& a, const dw4::engine::Vec3f& b) {
                                return std::hypot(a.x - b.x, a.z - b.z);
                            };
                            // Routes lead to mats; enemies and pickups are walked at
                            // directly (a boss body is not a place to route to).
                            const float route_from = 20.0f;
                            if (auto* hitland = engine.get<dw4::engine::HitLandManager>();
                                hitland && flat(goal, me) > route_from &&
                                (flat(goal, route_goal) > 15.0f || frame - route_frame > 180 || route_next >= route.size())) {
                                const dw4::game::WithoutLimitCircle without_limit(*hitland);
                                route = dw4::game::find_walk_path(hitland->world(), me, goal, hitland->delivery_flags(p->info_name, p->index));
                                if (std::getenv("DW4_TRACE_ROUTE"))
                                    std::printf("[%d] route (%.0f,%.0f,%.0f) -> (%.0f,%.0f): %zu points%s\n", frame, me.x, me.y, me.z, goal.x,
                                                goal.z, route.size(),
                                                route.empty() ? "" : (" ending (" + std::to_string(int(route.back().x)) + "," +
                                                                      std::to_string(int(route.back().z)) + ")").c_str());
                                if (std::getenv("DW4_TRACE_ROUTE") && !route.empty())
                                    for (const auto& m : models->rams())
                                        if (m->generated && m->node && std::hypot(m->node->matrix(3, 0) - route.back().x,
                                                                                  m->node->matrix(3, 2) - route.back().z) < 60.0f)
                                            std::printf("      near the end: %s#%d %s %s (%.0f,%.0f,%.0f)\n", m->info_name.c_str(), m->index,
                                                        m->category.c_str(), m->seq_name.c_str(), m->node->matrix(3, 0),
                                                        m->node->matrix(3, 1), m->node->matrix(3, 2));
                                route_next = 0;
                                route_goal = goal;
                                route_frame = frame;
                            }
                            while (route_next < route.size() && flat(route[route_next], me) < 5.0f) ++route_next;
                            if (flat(goal, me) > route_from && route_next < route.size()) {
                                tx = route[route_next].x;
                                tz = route[route_next].z;
                            }
                        }
                        const float dx = tx - p->node->matrix(3, 0), dz = tz - p->node->matrix(3, 2);
                        // No steering while the player has no control (an open
                        // prompt would take the stick as a cursor move).
                        const bool steer = !(g->seek && (g->target.rfind("mat:", 0) == 0 || g->target.rfind("explore:", 0) == 0)) ||
                                           (p->control_enabled && p->seq_name.rfind("SQ_DEMO", 0) != 0);
                        if (steer && dx * dx + dz * dz > reach) {
                            dw4::engine::FrameCamera camera;
                            cameras->frame_camera(engine, camera, 0); // the bot's player's own camera (per-slot cameras)
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
                // The scripts' combined directions (UP / DOWN / LEFT / RIGHT)
                // and the bots' direction presses push a centred main stick
                // as well as setting the D-pad bit; live D-pad input and the
                // scripts' DPAD_* never move the stick.
                // Play's title drive (19.5.1): behind the loading screen the own
                // pad is the driver's alone (D-pad bits); the difficulty dialog
                // is the player's.
                bool driven = false;
                if (title_driver.active() && !online && !title_drive_test) title_driver.stop(); // (the session ended: the title is the player's)
                if (title_driver.active()) {
                    const std::uint32_t pressed = title_driver.step(dw4::game::online::read_title(engine, own_slot()));
                    if (!online && title_drive_test && title_driver.changed())
                        std::printf("[%d] title drive (test run): %s (%s), %d steps, %d presses\n", frame,
                                    dw4::game::online::title_phase_name(title_driver.phase()), title_driver.note().c_str(), title_driver.steps(),
                                    title_driver.presses());
                    if (title_driver.covering() || pressed) {
                        driven = true;
                        input = pressed;
                        stick_x = stick_y = 0;
                    }
                }
                const std::uint32_t bot_bits = driven ? 0u : input & ~before_bots;
                std::uint32_t push = driven ? 0u : scripted.pushed(frame);
                if (bot_bits & dw4::pad_button::up) push |= dw4::pad_direction::positive_y;
                if (bot_bits & dw4::pad_button::down) push |= dw4::pad_direction::negative_y;
                if (bot_bits & dw4::pad_button::left) push |= dw4::pad_direction::negative_x;
                if (bot_bits & dw4::pad_button::right) push |= dw4::pad_direction::positive_x;
                // The original device's sample (host_pad_sample: full
                // deflection 72/74 main stick, 59/74 C-stick after the SDK
                // clamp); STICK / CSTICK hold raw device values.
                pads[0] = driven ? dw4::host_pad_sample({input, 0u, 0u, {0.0f, 0.0f}, {0.0f, 0.0f}})
                                 : dw4::host_pad_sample({input, push, live[0].c_stick_directions, {stick_x, stick_y}, live[0].c_stick});
                if (const auto stick = scripted.stick(frame)) pads[0].left = *stick;
                if (const auto stick = scripted.stick(frame, 0, true)) pads[0].right = *stick;
                for (std::size_t i = 1; i < 4; ++i) {
                    auto in = live[i];
                    in.buttons |= scripted.held(frame, int(i));
                    in.stick_directions |= scripted.pushed(frame, int(i));
                    pads[i] = dw4::host_pad_sample(in);
                    pads[i].connected = i < gamepads.size() || scripted.uses_slot(int(i));
                    if (const auto stick = scripted.stick(frame, int(i))) pads[i].left = *stick;
                    if (const auto stick = scripted.stick(frame, int(i), true)) pads[i].right = *stick;
                }
                if (net.active()) {
                    // This machine's controller plays its slot; the others
                    // come from the session.
                    while (net_submitted < net_frame + std::uint64_t(net.start().delay)) {
                        ++net_submitted;
                        net.submit_local(net_submitted, pads[0]);
                    }
                    net.poll();
                    pads = net.take(net_frame);
                }
                // (The local START, before the session hands out the sealed pads:
                // giving up counts the held button, not the sealed ticks, of
                // which a lagging member's are filled.)
                const bool local_start = (pads[0].buttons & dw4::pad_button::start) != 0;
                if (online) {
                    // One player per machine: this machine's pad, quantized as
                    // every machine executes it; the others come sealed. The
                    // local B + X + START hold asks "Leave room?" (7.4).
                    if (online->leave_combo(pads[0].buttons)) {
                        // The same hold again closes it ("stay").
                        online->leave_prompt = !online->leave_prompt;
                        std::cout << "online: leave prompt " << (online->leave_prompt ? "at" : "closed at") << " frame " << frame << '\n';
                    }
                    pads = online->session().before_step(pads[0], steady_seconds());
                }
                if (!pad_replay.empty())
                    if (const auto logged = pad_replay.find(frame); logged != pad_replay.end()) pads = logged->second;
                if (pad_log) {
                    const std::uint32_t at = std::uint32_t(frame);
                    std::fwrite(&at, sizeof at, 1, pad_log.get());
                    for (const auto& pad : pads) {
                        const std::uint8_t connected = pad.connected ? 1 : 0;
                        const float sticks[4] = {pad.left.x, pad.left.y, pad.right.x, pad.right.y};
                        std::fwrite(&connected, 1, 1, pad_log.get());
                        std::fwrite(sticks, sizeof(float), 4, pad_log.get());
                        std::fwrite(&pad.buttons, sizeof pad.buttons, 1, pad_log.get());
                    }
                }
                // Port events only while achievements, the event trace, the
                // randomizer's bond (new games) or the online character
                // (card_written) want them.
                if (const bool collect = achievements_on() || trace_events || (!net.active() && randomizer_settings().active()) || online;
                    engine.port_events_enabled() != collect)
                    engine.set_port_events(collect);
                // Seamless co-op, a defeated local member (SEAMLESS.md 4.1,
                // 5): START held for two seconds gives up (a fork into the
                // original game over). The engine takes the intent before a
                // step; an online client sends it as INTENT instead and the
                // server stamps it at a tick (the client lane), so only the
                // offline test runs (DW4_ONLINE) apply it here.
                if (engine.online_shared()) {
                    const int local = engine.view_slot();
                    // (Not while no player model is generated: loads, the entry, a departure's fade.)
                    const bool defeated = dw4::game::online_member_defeated(engine, local);
                    // Online the local START counts (a filled sealed tick, up to six repeats then neutral under lag,
                    // reset the count, so a member with a poor uplink could hardly give up).
                    const bool start_held = online ? local_start : (pads[std::size_t(local)].buttons & dw4::pad_button::start) != 0;
                    give_up_held = defeated && start_held ? give_up_held + 1 : 0;
                    // (An online client sends the choice as INTENT: the server stamps it at a tick every member applies.)
                    if (give_up_held == kGiveUpFrames && online) online->session().send_intent(dw4::game::online::wire::IntentKind::give_up, 0);
                    else if (give_up_held == kGiveUpFrames && !net.active()) engine.online_intent(local, dw4::engine::IntentKind::give_up, 0);
                } else {
                    give_up_held = 0;
                }
                // Test runs of the seamless co-op exits: DW4_TEST_GATEDISC=
                // frame:slot uses that member's gate disc at the frame (what
                // the quick panel does, panel.cpp row 8), DW4_TEST_GIVEUP=
                // frame:slot gives that member up.
                {
                    static const auto test_hook = [](const char* name) {
                        std::pair<int, int> hook{-1, -1};
                        if (const char* text = std::getenv(name)) std::sscanf(text, "%d:%d", &hook.first, &hook.second);
                        return hook;
                    };
                    static const auto gatedisc = test_hook("DW4_TEST_GATEDISC");
                    static const auto give_up = test_hook("DW4_TEST_GIVEUP");
                    if (frame + 1 == gatedisc.first)
                        if (auto* sys = engine.get<dw4::engine::GameSystem>()) {
                            sys->gate_stage_name = "Player";
                            sys->gate_stage_no = std::clamp(gatedisc.second, 0, 3);
                            engine.run_script("gatedisc.lua");
                        }
                    if (frame + 1 == give_up.first) engine.online_intent(std::clamp(give_up.second, 0, 3), dw4::engine::IntentKind::give_up, 0);
                    // DW4_TEST_RULES=frame:cameras:present: a membership step at
                    // that frame (the camera mask and the present members; the
                    // leash goes off from two members).
                    static const auto rules_step = [] {
                        std::array<int, 3> step{-1, 0, 0};
                        if (const char* text = std::getenv("DW4_TEST_RULES")) std::sscanf(text, "%d:%i:%i", &step[0], &step[1], &step[2]);
                        return step;
                    }();
                    if (frame + 1 == rules_step[0]) {
                        auto rules = engine.online();
                        rules.player_cameras = std::uint8_t(rules_step[1] & 15);
                        rules.present = std::uint8_t(rules_step[2] & 15);
                        rules.enabled = rules.enabled || rules.present != 0;
                        rules.leash_off = rules.leash_off || std::popcount(unsigned(rules.present)) >= 2;
                        engine.set_online(rules);
                    }
                }
#ifdef DW4_ONLINE_DEV
                // DW4_ONLINE_TEST_DESYNC=TICK (development builds; the resync
                // scenarios of tools/online_test): before this member's first
                // live sealed tick at or after TICK, one draw of the system
                // generator that no other machine makes: a desync the relay's
                // fingerprint vote finds and a resync repairs (6.9).
                // DW4_ONLINE_TEST_DESYNC=join+N (scenario JS2): the same draw N
                // sealed ticks after this member's own JOIN in a visit (a
                // joiner whose engine came from a checkpoint drifts after its
                // READY passed: 6.13.4's rebuilt member).
                if (online) {
                    struct DesyncHook {
                        long long tick = -1; // TICK, or N after the own JOIN
                        bool after_join = false;
                    };
                    static const DesyncHook desync = [] {
                        DesyncHook hook;
                        const char* text = std::getenv("DW4_ONLINE_TEST_DESYNC");
                        if (!text || !*text) return hook;
                        hook.after_join = std::string_view(text).starts_with("join+");
                        hook.tick = std::max(-1LL, std::atoll(hook.after_join ? text + 5 : text));
                        return hook;
                    }();
                    const auto joined = online->session().joined_tick();
                    const long long desync_tick = !desync.after_join ? desync.tick : desync.tick >= 0 && joined ? *joined + desync.tick : -1LL;
                    // DW4_ONLINE_TEST_TICKSTATE_SKIP=1 (scenario JS): the tick
                    // state misses the model RAMs' HP and the system generator
                    // on this machine.
                    static const bool skip_hp = [] {
                        const char* text = std::getenv("DW4_ONLINE_TEST_TICKSTATE_SKIP");
                        const bool on = text && *text == '1';
                        dw4::engine::Engine::set_tick_state_skip_hp(on);
                        return on;
                    }();
                    (void)skip_hp;
                    static bool desynced = false;
                    if (!desynced && desync_tick >= 0 && online->session().mode() == dw4::game::online::Mode::sealed &&
                        online->session().tick() >= std::uint64_t(desync_tick)) {
                        desynced = true;
                        (void)dw4::engine::fight::random();
                        if (desync.after_join)
                            std::printf("online: test desync at tick %u (%lld ticks after the own join at %u%s)\n", online->session().tick(),
                                        desync.tick, *joined, online->session().from_checkpoint() ? ", engine from a checkpoint" : "");
                        else
                            std::printf("online: test desync at tick %u\n", online->session().tick());
                    }
                }
#endif
                engine.step(pads);
                if (online) {
                    online->session().after_step(steady_seconds());
                    online->after_live_step(steady_seconds());
                }
                // DW4_TRACE_ONLINE (test runs of the seamless co-op exits):
                // forks, headings and the exit prompt's state as they happen.
                if (static const bool trace_online = std::getenv("DW4_TRACE_ONLINE") != nullptr; trace_online && !online) {
                    if (const auto split = engine.take_split())
                        std::printf("[%d] online split leavers %x%s, this copy %s; present %x\n", frame + 1, unsigned(split->leavers),
                                    split->in_stage ? " in stage" : "", split->local_leaves ? "leaves" : "stays", unsigned(engine.online().present));
                    if (const auto heading = engine.take_heading())
                        std::printf("[%d] online heading %s point %d leavers %x\n", frame + 1, heading->stage.c_str(), heading->point,
                                    unsigned(heading->leavers));
                    static std::string traced_prompt;
                    const auto& prompt = engine.exit_prompt();
                    char line[160];
                    std::snprintf(line, sizeof line, "active %d owner %d asking %d yes %x stays %x fade %x to %s%s", prompt.active ? 1 : 0, prompt.owner,
                                  prompt.asking, unsigned(prompt.yes), unsigned(prompt.stays), unsigned(prompt.sees_fade), prompt.stage.c_str(),
                                  prompt.boss_destination ? " (boss)" : "");
                    if (line != traced_prompt) {
                        std::printf("[%d] online exit prompt %s\n", frame + 1, line);
                        traced_prompt = line;
                    }
                }
                if (engine.port_events_enabled()) observe_step();
                if (!net.active() && !online) bind_randomizer();
                if (net.active() && (frame_base + engine.frame()) % 60 == 0) net.report_fingerprint(frame_base + engine.frame(), engine.fingerprint());
                // The input since the running stage's load, for save states.
                if (const auto* snapshot = engine.stage_snapshot(); snapshot && snapshot->serial != log_serial) {
                    log_serial = snapshot->serial;
                    if (snapshot_dump) {
                        char name[64];
                        std::snprintf(name, sizeof name, "%03llu_%s", static_cast<unsigned long long>(snapshot->serial), snapshot->stage.c_str());
                        for (const auto form : {dw4::engine::SnapshotForm::raw, dw4::engine::SnapshotForm::canonical}) {
                            const auto bytes = engine.snapshot_bytes(form);
                            const auto file = std::filesystem::path(snapshot_dump) /
                                              (std::string(name) + (form == dw4::engine::SnapshotForm::raw ? ".raw" : ".canon"));
                            std::ofstream out(file, std::ios::binary | std::ios::trunc);
                            out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                        }
                    }
                    input_log.clear();
                    load_step_drawn = false;
                    last_step_loaded = true;
                } else {
                    input_log.push_back({pads, false});
                    last_step_loaded = false;
                }
                mix_audio(step_samples(frame_base + engine.frame()));
                // Test tool: DW4_AUDIO_TOPUP=FRAME:SAMPLES[,FRAME:SAMPLES...]
                // renders SAMPLES more after the step of FRAME, as the queue
                // top-up of interactive play does (smoke tests with a dump).
                static const auto topups = [] {
                    std::vector<std::pair<std::uint64_t, int>> list;
                    if (const char* spec = std::getenv("DW4_AUDIO_TOPUP"))
                        for (std::istringstream items(spec); items.good();) {
                            std::string item;
                            std::getline(items, item, ',');
                            unsigned long long at = 0;
                            int samples = 0;
                            if (std::sscanf(item.c_str(), "%llu:%d", &at, &samples) == 2 && samples > 0) list.emplace_back(at, samples);
                        }
                    return list;
                }();
                for (const auto& [at, samples] : topups)
                    if (options.smoke_frames && at == frame_base + engine.frame()) mix_audio(samples);
                ++frame;
                // Data Overflow: the fades count story steps; --overflow-run
                // at=F starts the fade out after story frame F, or once a run
                // may start (the stage has loaded).
                overflow_switch.story_stepped();
                if (overflow_cli && overflow_cli->at >= 0 && frame >= overflow_cli->at && dw4::game::overflow::start_refusal(engine, false).empty()) {
                    overflow_switch.request(std::move(*overflow_cli));
                    overflow_cli.reset();
                }
                if (state_test) {
                    if (state_pass == 1) {
                        if (frame == state_test) state_test_save = true;
                        if (frame > state_test) state_fingerprints.push_back(engine.fingerprint());
                    } else if (state_mismatch < 0) {
                        const auto at = std::size_t(frame - state_test - 1);
                        if (at < state_fingerprints.size() && state_fingerprints[at] != engine.fingerprint()) state_mismatch = frame;
                    }
                }
                if (trace_fingerprint > 0 && frame % trace_fingerprint == 0) {
                    // With the system generator's state (not in the netplay
                    // fingerprint): a draw more or less shows up here first.
                    std::ostringstream random;
                    random << dw4::engine::fight::generator();
                    std::printf("[%d] fingerprint %016llx random %016zx\n", frame, static_cast<unsigned long long>(engine.fingerprint()),
                                std::hash<std::string>{}(random.str()));
                    // DW4_TRACE_NET=1 (Data Overflow's isolation check, SURVIVAL.md
                    // T3): the net fingerprint (the generator, the models' turns, the
                    // sound's random value, ...) and the running stage's canonical
                    // snapshot as well.
                    if (static const bool trace_net = std::getenv("DW4_TRACE_NET") != nullptr; trace_net)
                        std::printf("[%d] net %016llx canon %016llx\n", frame, static_cast<unsigned long long>(engine.net_fingerprint()),
                                    static_cast<unsigned long long>(
                                        dw4::engine::online_hash(engine.snapshot_bytes(dw4::engine::SnapshotForm::canonical))));
                }
                // DW4_TRACE_CAMERAS=N (test runs, seamless co-op): every N
                // frames the per-slot camera state (the box hash the net
                // fingerprint takes), the online rules and the pictures' slot.
                if (static const int every = std::getenv("DW4_TRACE_CAMERAS") ? std::atoi(std::getenv("DW4_TRACE_CAMERAS")) : 0;
                    every > 0 && frame % every == 0)
                    if (auto* cameras = engine.get<dw4::engine::CameraManager>()) {
                        // (and the picture camera's view and projection, bit for bit)
                        dw4::engine::FrameCamera picture;
                        cameras->frame_camera(engine, picture, engine.picture_slot());
                        std::uint64_t view_hash = 0xcbf29ce484222325ull;
                        for (float v : picture.view.m) view_hash = (view_hash ^ std::bit_cast<std::uint32_t>(v)) * 0x100000001b3ull;
                        for (float v : picture.projection.m) view_hash = (view_hash ^ std::bit_cast<std::uint32_t>(v)) * 0x100000001b3ull;
                        std::printf("[%d] cameras %016llx mask %x present %x leash %s picture %d view %016llx\n", frame,
                                    static_cast<unsigned long long>(cameras->player_box_hash()), unsigned(engine.player_cameras()),
                                    unsigned(engine.online().present), engine.online().leash_off ? "off" : "on", engine.picture_slot(),
                                    static_cast<unsigned long long>(view_hash));
                    }
                // DW4_TRACE_PLAYERS=N (development) prints every player's position every N frames.
                if (static const int every = std::getenv("DW4_TRACE_PLAYERS") ? std::atoi(std::getenv("DW4_TRACE_PLAYERS")) : 0;
                    every > 0 && frame % every == 0)
                    if (auto* models = engine.get<dw4::engine::ModelCtrlManager>()) {
                        std::printf("[%d] players", frame);
                        for (int slot = 0; slot < 4; ++slot)
                            if (const auto* p = models->player(slot); p && p->node)
                                std::printf(" %d:(%.0f,%.0f,%.0f)%s", slot + 1, p->node->matrix(3, 0), p->node->matrix(3, 1),
                                            p->node->matrix(3, 2), p->control_enabled ? "" : "*");
                        // The players' limit circle: L on, l off, - none in this stage.
                        if (auto* hitland = engine.get<dw4::engine::HitLandManager>()) {
                            const auto circle = hitland->child_hit("limit_Circle");
                            std::printf(" %s", !circle ? "-" : *circle ? "L" : "l");
                        }
                        // Cumulative traversal ns / passes (render, traverse, union, picture).
                        std::printf(" | t");
                        for (int i = 0; i < 4; ++i)
                            std::printf(" %llu/%llu", (unsigned long long)engine.traversal_ns[std::size_t(i)],
                                        (unsigned long long)engine.traversal_count[std::size_t(i)]);
                        std::printf("\n");
                    }
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
                if (engine.reboot_due()) reboot_console();
            }
            if (online) {
                auto& session = online->session();
                const double online_now = steady_seconds();
                // A catch-up or co-arrival restored another engine.
                if (session.take_engine_replaced()) {
                    ++engine_generation;
                    labels.reset();
                    input_log.clear();
                    log_serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
                    load_step_drawn = last_step_loaded = false;
                    movie_decoder.reset();
                    movie_serial = 0;
                    movie_stage.clear();
                    if (renderer) renderer->clear_cache();
                    interpolator.reset();
                    rendered_stage.clear();
                }
                for (auto& notice : session.take_notices()) {
                    std::cout << "online: " << notice << '\n';
                    toasts.push_back({"Online", notice, std::chrono::steady_clock::now() + std::chrono::seconds(5)});
                }
                // Play's title drive (19.5.1): its end, and the lobby's welcome
                // ("Welcome back, Kim. 14 players online, 2 friends.") once GET
                // /world and GET /friends had a moment to answer.
                if (title_driver.changed()) {
                    using Phase = dw4::game::online::TitleDriver::Phase;
                    std::printf("[%d] online: Play's title drive: %s (%s), %d steps, %d presses\n", frame,
                                dw4::game::online::title_phase_name(title_driver.phase()), title_driver.note().c_str(), title_driver.steps(),
                                title_driver.presses());
                    std::fflush(stdout);
                    if (title_driver.phase() == Phase::done) {
                        if (lobby) {
                            lobby->refresh_world();
                            lobby->refresh_friends();
                        }
                        welcome_at = steady_seconds() + 1.5;
                    } else if (title_driver.phase() == Phase::gave_up) {
                        const auto* title = engine.get<dw4::engine::Title2Manager>();
                        if (title && title->state() == 1)
                            toasts.push_back({"Online", "Press START to play your online character.",
                                              std::chrono::steady_clock::now() + std::chrono::seconds(6)});
                    }
                }
                if (welcome_at >= 0 && steady_seconds() >= welcome_at) {
                    welcome_at = -1;
                    const std::string text = dw4::game::online::api::welcome_text(
                        online->room().account, lobby && lobby->world_counts() ? std::optional<int>(lobby->world_counts()->players) : std::nullopt,
                        lobby ? lobby->friends_online() : std::nullopt);
                    std::cout << "online: " << text << '\n';
                    toasts.push_back({"Online", text, std::chrono::steady_clock::now() + std::chrono::seconds(6)});
                }
                // (Round G-core: the guest's story at its Game Start, the closed call shop.)
                for (auto& notice : online->take_notices()) {
                    std::cout << "online: " << notice << '\n';
                    toasts.push_back({"Online", notice, std::chrono::steady_clock::now() + std::chrono::seconds(8)});
                }
                if (auto* character = online->character())
                    for (auto& notice : character->take_notices()) {
                        std::cout << "online: character: " << notice << '\n';
                        toasts.push_back({"Online character", notice, std::chrono::steady_clock::now() + std::chrono::seconds(10)});
                    }
                // The online account's achievements: new unlocks as toasts, refusals in the log.
                if (online_achievements)
                    for (auto& notice : online_achievements->take_notices()) {
                        std::cout << "online: " << notice << '\n';
                        if (notice.rfind("Online achievement: ", 0) == 0 && (settings.achievement_popups || options.smoke_frames))
                            toasts.push_back({"Online", notice, std::chrono::steady_clock::now() + std::chrono::seconds(6)});
                    }
                // The loading screen's line, the connection, the leave prompt.
                const auto overlay = session.overlay(online_now);
                std::string line = overlay.loading;
                if (line.empty() && overlay.waiting_for_server) line = "Waiting for the server...";
                if (line.empty()) line = overlay.status; // (19.5.5: "Kim lost the connection. Waiting for Kim... 52 s")
                if (line.empty() && overlay.slow_uplink) line = "Your connection is slow; the others see you lag.";
                // The online test driver while a barrier is held (no step runs, so the
                // step loop's update does not come): its conditions, and goback.
                if (online_script.active() && session.mode() == dw4::game::online::Mode::parked) {
                    dw4::game::OnlineScriptFacts facts;
                    facts.frame = frame;
                    facts.parked = true;
                    facts.own_slot = session.own_slot();
                    facts.present = session.present();
                    facts.mode = dw4::game::online::mode_name(session.mode());
                    facts.tick = session.tick();
                    go_facts(facts, session, online_now);
                    facts.ghosts = ghosts.shown();
                    facts.figures = ghosts.staying();
                    if (online_script.update(engine, facts).go_back && session.go_back_to_lobby())
                        std::cout << "online: back to the lobby from a held go along at frame " << frame << '\n';
                    if (online_script.finished()) running = false;
                }
                // Round I1b (19.30.7): a GO's barrier held over 10 s: B takes the member back to the lobby.
                if (overlay.go_back) {
                    line += session.is_host() ? "   B: End the instance for everyone" : "   B: Back to the lobby";
                    const bool b = (live[0].buttons & dw4::pad_button::b) != 0;
                    if (b && !go_back_b_held && session.go_back_to_lobby()) std::cout << "online: back to the lobby from a held go along at frame " << frame << '\n';
                    go_back_b_held = b;
                }
                if (online->leave_prompt) {
                    // After the loading or waiting line, never instead of it.
                    const std::string prompt = "Go offline?  F1 > Online > Go offline, or hold B + X + START again to stay.";
                    line = line.empty() ? prompt : line + "   " + prompt;
                }
                if (!line.empty()) {
                    state_message = line;
                    message_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
                }
                // Room members in this stage outside the own instance: name tags
                // (and in a hub stage ghost figures, below).
                present_remote = session.remote_tags(online_now);
                std::vector<dw4::game::OverheadLabels::RemotePlayer> remote;
                for (const auto& tag : present_remote)
                    remote.push_back({tag.member, tag.name, tag.stage, {tag.x, tag.y, tag.z}, tag.species, tag.level, tag.hp, tag.max_hp, tag.mp,
                                      tag.max_mp, tag.busy, session.member_flags(std::uint32_t(tag.member))});
                labels.set_remote_players(std::move(remote));
                // The audio device follows the simulation's pace (6.4).
                if (audio) {
                    const float ratio = float(session.audio_ratio());
                    if (ratio != online_audio_ratio && SDL_SetAudioStreamFrequencyRatio(audio, ratio)) online_audio_ratio = ratio;
                }
                if (session.closed()) {
                    // (Another PC of this account took the place: its game goes on from the server's character, so this
                    // PC's leaving save stays pending in the card cache instead of overwriting that.)
                    if (session.replaced()) online->replaced();
                    online->save_on_leave(); // (the automatic save when leaving the room, SEAMLESS.md 9)
                    const auto reason = dw4::game::online::tidy_reason(session.close_reason());
                    std::cout << "online: " << reason << '\n';
                    toasts.push_back({"Online", reason, std::chrono::steady_clock::now() + std::chrono::seconds(8)});
                    // (Out of the room: open reports wait in the account's profile for the next one.)
                    if (online_achievements) online_achievements->save();
                    online_achievements.reset();
                    online.reset();
                    if (audio) SDL_SetAudioStreamFrequencyRatio(audio, 1.0f);
                    online_audio_ratio = 1.0f;
                    labels.set_remote_players({});
                    present_remote.clear();
                    ghosts.clear();
                    if (lobby) lobby->session_ended(reason);
                    // (A UI test run goes on at the offline title, as a player does.)
                    if (options.smoke_frames && !ui_script.loaded()) running = false;
                    else offline_title();
                }
            }
            // Ghost figures (SEAMLESS.md 12): in a hub stage (a solo hub
            // instance) the room members whose presence names this stage, while
            // the picture shows the stage (no load, scene or menu; a window of
            // this member's own keeps them). Members elsewhere are preloaded.
            // Presentation only: nothing here reaches the engine.
            const auto ghost_update_start = std::chrono::steady_clock::now();
#ifdef DW4_GHOST_PROBE
            if (probe_ghosts > 0 && !online && ghosts_enabled) {
                ghosts.set_resources(engine.resources());
                static std::optional<dw4::engine::Vec3f> anchor;
                std::vector<dw4::game::GhostTarget> targets;
                const auto own = own_position();
                if (own && !anchor && engine.presenting() && !engine.loading()) anchor = own;
                // (Shown whatever hides the tags: a direct start into the lobby runs
                // its talk until a button ends it; the cost is the same.)
                if (anchor) {
                    static const char* const weapons[] = {"", "w_common05", "w_common07", "w_common03"};
                    const auto forms = dw4::game::forms();
                    const double t = steady_seconds();
                    for (int k = 0; k < probe_ghosts; ++k) {
                        // A spiral from 15 units out, 6 units a figure; every
                        // second figure walks a circle of 10 units.
                        const float angle = float(k) * 2.39996f, radius = 15.0f + probe_spread * float(k);
                        dw4::engine::Vec3f at{anchor->x + radius * std::cos(angle), anchor->y, anchor->z + radius * std::sin(angle)};
                        float yaw = angle, speed = 0;
                        if (k % 2 == 1) {
                            const float phase = float(t * 2.0) + float(k);
                            at.x += 10.0f * std::cos(phase);
                            at.z += 10.0f * std::sin(phase);
                            yaw = phase + 3.14159f;
                            speed = 20.0f;
                        }
                        targets.push_back({std::uint64_t(1000 + k), 0, at, yaw, forms[std::size_t(k) % forms.size()].species, "", true, speed,
                                           weapons[k % 4]});
                    }
                }
                ghosts.update(std::move(targets), own, steady_seconds());
                probe_update_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ghost_update_start).count();
            } else
#endif
            if (online && ghosts_enabled) {
                ghosts.set_resources(engine.resources());
                const bool show = online->session().hub() && !dw4::game::OverheadLabels::hidden(engine, false);
                std::vector<dw4::game::GhostTarget> targets;
                for (const auto& sample : present_remote) {
                    ghosts.preload(sample.species, sample.weapon);
                    if (!show || !dw4::game::same_stage(sample.stage, engine.stage_name())) continue;
                    targets.push_back({sample.member, sample.segment, {sample.x, sample.y, sample.z}, sample.yaw, sample.species, sample.clip,
                                       sample.loop, sample.speed, sample.weapon});
                }
                ghosts.update(std::move(targets), own_position(), steady_seconds());
            } else if (ghosts.drawn() || !present_remote.empty()) {
                present_remote.clear();
                ghosts.clear();
            }
            ghost_cost_max = std::max(ghost_cost_max, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ghost_update_start).count());
            // (Every N frames, also when a pass of several steps jumps over the multiple.)
            if (trace_ghosts > 0 && steps > 0 && frame / trace_ghosts != ghost_trace_bucket) {
                ghost_trace_bucket = frame / trace_ghosts;
                std::printf("[%d] ghosts shown %d, forms %zu, stage %s, own cost at most %.2f ms a pass, longest present %.1f ms\n", frame,
                            ghosts.shown(), ghosts.loaded_forms(), engine.stage_name().c_str(), ghost_cost_max, present_max);
                // (The own player's placement against the figure's rebuild from
                // the presence's yaw: the rows must agree.)
                if (const auto* models = engine.get<dw4::engine::ModelCtrlManager>())
                    if (const auto* player = models->player(own_slot()); player && player->node) {
                        const auto& m = player->node->matrix;
                        const float yaw = std::atan2(m(2, 0), m(2, 2));
                        const auto g = dw4::game::ghost_matrix({m(3, 0), m(3, 1), m(3, 2)}, yaw);
                        float worst = 0;
                        for (int r = 0; r < 3; ++r)
                            for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(g(r, c) - m(r, c)));
                        std::printf("[%d] ghost own at (%.1f, %.1f, %.1f) yaw %.2f, rebuilt rotation off by %.4f\n", frame, m(3, 0), m(3, 1), m(3, 2),
                                    yaw, worst);
                    }
                for (const auto& line : ghosts.describe()) std::printf("[%d] ghost %s\n", frame, line.c_str());
                ghost_cost_max = present_max = 0;
            }
            // The title menu's third entry was chosen: the online screens open
            // over the menu (which stays, its cursor on Online).
            if (engine.title_online_request) {
                engine.title_online_request = false;
                if (lobby) open_title_online();
                else std::cout << "title: Online chosen (this run has no online screens)\n";
            }
            // The account and room screens: the lobby's answers, the session it
            // prepared (over the title or in F1 Online), the leave it asks for,
            // and a UI test run's script.
            if (lobby) {
                const auto* sys = engine.get<dw4::engine::GameSystem>();
                lobby->set_offline_game(!online && ((sys && sys->game_started) || !dw4::game::is_menu_stage(engine.stage_name())));
                lobby->update(steady_seconds(), title_online_shown || (show_menu && online_tab_shown) || ui_script.active());
                if (online && lobby->take_leave_request()) {
                    online->leave_prompt = false;
                    leave_room();
                }
                if (auto ready = lobby->take_ready()) {
                    // Play (19.5.1): the world session, straight into the own lobby.
                    auto room = dw4::game::world_from_entry(ready->entry, ready->account, ready->token, ready->relay_port);
                    auto http = std::make_unique<dw4::game::online::HttpWorker>(dw4::game::online::server_endpoint(),
                                                                                 std::string("DW4_PC/") + DW4_PC_VERSION);
                    http->set_token(ready->token);
                    std::string error;
                    bool opened = false;
                    try {
                        opened = open_room(room, std::move(http), false, error);
                    } catch (const std::exception& e) {
                        error = e.what();
                    }
                    if (opened) {
                        // Every online session starts at the title, with the online character's card.
                        engine.request_stage("stagis10");
                        restart_presentation();
                        // (The screens over the title show the room: its Play
                        // closes them, B too.)
                        show_menu = false;
                        lobby->session_started();
                        std::cout << "online: the world session starts at the title\n";
                        // Play goes straight into the own lobby (19.5.1).
                        start_title_drive();
                        if (!title_driver.active() && !title_online.active)
                            toasts.push_back({"Online", "Press START to play your online character.",
                                              std::chrono::steady_clock::now() + std::chrono::seconds(6)});
                    } else {
                        std::cout << "online: the session did not start: " << error << '\n';
                        online.reset();
                        offline_title();
                        lobby->session_failed("The online game could not start: " + error);
                    }
                }
                if (ui_script.active()) {
                    dw4::game::UiScriptHost host{*lobby, online_ui, title_online, show_menu, menu_tab};
                    host.online_active = online != nullptr;
                    host.live = online ? &last_room_live : nullptr;
                    {
                        const std::string stage = engine.stage_name();
                        const auto* title = engine.get<dw4::engine::Title2Manager>();
                        host.title_shown = stage.size() == 8 && dw4::game::is_menu_stage(stage) && stage.substr(6) == "10" && engine.presenting() &&
                                           !engine.loading() && title && title->state() >= 1;
                        host.menu_open = host.title_shown && title->menu_open();
                        host.menu_waiting = host.title_shown && title->menu_waiting();
                        host.cursor = title ? title->cursor() : 0;
                        host.title_entry = engine.title_online_entry;
                    }
                    host.screens_shown = title_online_shown;
                    host.stage = engine.stage_name();
                    host.frame = frame;
                    host.now = steady_seconds();
                    ui_script.update(host);
                    if (!host.shot.empty()) ui_shot = host.shot;
                    if (host.view_button) view_button();
                    if (host.open_screens) open_title_online();
                    if (host.pad) {
                        ui_pad = host.pad;
                        ui_pad_until = engine.frame() + 3; // held for three steps, as an input script's "A 3"
                    }
                    ui_keys.insert(ui_keys.end(), host.keys.begin(), host.keys.end());
                    if (host.end) running = false;
                }
            }
            if (steps > 0 && draw) interpolator.advance();
            if (steps > 0 && (label_mode() != dw4::game::LabelMode::off || trace_labels)) labels.capture(engine, own_slot());
            if (discord) {
                discord->configure(options.smoke_frames ? true : settings.discord, settings.discord_client_id);
                const auto activity = presence_activity();
                if (trace_presence && activity != traced_activity) {
                    std::printf("[%d] presence details='%s' state='%s' large=%s small=%s (%s)\n", frame, activity.details.c_str(),
                                activity.state.c_str(), activity.large_image.c_str(), activity.small_image.c_str(), activity.small_text.c_str());
                    traced_activity = activity;
                }
                discord->set_activity(activity);
            }
            // DW4_TRACE_LABELS=N (test runs): the labels' screen positions for
            // the test window (1280x960 unless DW4_WINDOW, DW4_WIDESCREEN=1),
            // with the frame camera, every N frames.
            if (trace_labels > 0 && steps > 0 && frame % trace_labels == 0) {
                int width = 1280, height = 960;
                if (const char* size = std::getenv("DW4_WINDOW")) std::sscanf(size, "%dx%d", &width, &height);
                dw4::game::LabelView view;
                if (auto* cameras = engine.get<dw4::engine::CameraManager>()) cameras->frame_camera(engine, view.camera, engine.picture_slot());
                const bool wider = width * 3 > height * 4;
                const int screen_width = wider ? height * 4 / 3 : width;
                const int screen_height = wider ? height : width * 3 / 4;
                const bool wide = std::getenv("DW4_WIDESCREEN") && wider;
                view.wide_x = wide ? 0.0f : float((width - screen_width) / 2);
                view.wide_y = float((height - screen_height) / 2);
                view.wide_width = float(wide ? width : screen_width);
                view.wide_height = float(screen_height);
                view.widen = wide ? (float(screen_width) / float(screen_height)) / (float(width) / float(height)) : 1.0f;
                view.window_height = float(height);
                const bool hidden = dw4::game::OverheadLabels::hidden(engine);
                for (const auto& label : labels.current()) {
                    const auto point = dw4::game::project_label(label.head, view);
                    const bool shown = !hidden && (label.hp > 0 || label.capsule) && point && dw4::game::label_visible(label_mode(), label.own, labels.players());
                    std::printf("[%d] label slot=%d%s '%s' Lv %d HP %d/%d MP %d/%d head=(%.1f, %.1f, %.1f) %.1f above the feet ", frame,
                                label.slot, label.capsule ? " capsule" : "", label.name.c_str(), label.level, label.hp, label.max_hp, label.mp, label.max_mp, label.head.x,
                                label.head.y, label.head.z, label.head.y - label.feet.y);
                    const auto feet = dw4::game::project_label(label.feet, view, 1.0f);
                    if (point)
                        std::printf("screen=(%.1f, %.1f) feet=(%.1f, %.1f) distance %.0f alpha %.2f%s%s\n", point->x, point->y,
                                    feet ? feet->x : -1.0f, feet ? feet->y : -1.0f, point->distance, dw4::game::label_fade(point->distance),
                                    shown ? "" : " (hidden)", traced_occlusion.contains(label.key) ? " (behind the scene)" : "");
                    else
                        std::printf("off screen%s\n", hidden ? " (hidden)" : "");
                }
            }
            // (Every stage load, also of the same stage: the texture cache is
            // keyed by document addresses, which a new load may reuse.)
            // (The story's ghost figures start over too: no figure fades across the load.)
            if (dw4::game::note_stage(view, engine, &ghosts)) save_profile();
            // Smooth motion: the picture is drawn between the previous step
            // and this one (MotionInterpolator::blend below).
            const bool smooth_blending = view.smooth_blending();
            // The draw list of this pass, or its traversal without pictures
            // (a field the running update covers keeps the last one).
            if (dw4::game::make_picture(view, engine, steps, smooth_blending) == dw4::game::PictureMade::drawn) {
                // The title menu's third entry "Online" (title_entry.hpp).
                if (static const bool trace = std::getenv("DW4_TRACE_TITLE_ENTRY") != nullptr; trace && options.smoke_frames)
                    dw4::game::TitleEntry::trace(items, engine.frame());
                if (engine.title_online_entry) {
                    const auto* title = engine.get<dw4::engine::Title2Manager>();
                    title_entry.apply(items, title && title->menu_open(), title && title->menu_open() && title->cursor() == 2, engine.frame());
                }
            }
            // The idempotence check of the pictures (docs/SEAMLESS.md 4.4, a
            // permanent check of test runs): DW4_EXTRA_RENDERS=n draws the
            // same world time n more times, as a machine presenting faster
            // than 60 Hz does between two steps (into a scratch list, headless
            // runs too); DW4_EXTRA_WIDEN=1 alternates the widescreen widening
            // between them and DW4_EXTRA_PREVIOUS=1 gives them the previous
            // step's picture camera as smooth motion's previous view (the
            // round 49 rule). The fingerprints must equal a run without.
            if (static const int extra_renders = std::getenv("DW4_EXTRA_RENDERS") ? std::atoi(std::getenv("DW4_EXTRA_RENDERS")) : 0;
                extra_renders > 0 && steps > 0 && !engine.held()) {
                static const bool extra_widen = std::getenv("DW4_EXTRA_WIDEN") != nullptr;
                static const bool extra_previous = std::getenv("DW4_EXTRA_PREVIOUS") != nullptr;
                static std::optional<dw4::engine::Mat4> previous_step_view;
                const float widen = engine.picture_widen;
                const auto previous = engine.picture_previous_view;
                std::vector<dw4::engine::DrawItem> scratch_items;
                dw4::engine::FrameCamera scratch_camera;
                dw4::engine::Vec4f scratch_clear;
                dw4::engine::ShadowFrame scratch_shadow;
                for (int k = 0; k < extra_renders; ++k) {
                    if (extra_widen) engine.picture_widen = (k & 1) ? 0.5f : 0.75f;
                    if (extra_previous) engine.picture_previous_view = previous_step_view;
                    engine.render(scratch_items, scratch_camera, scratch_clear, &scratch_shadow);
                }
                if (!draw) engine.traverse_scene();
                engine.picture_widen = widen;
                engine.picture_previous_view = previous;
                if (extra_previous)
                    if (auto* cameras = engine.get<dw4::engine::CameraManager>()) {
                        dw4::engine::FrameCamera now;
                        cameras->frame_camera(engine, now, engine.picture_slot());
                        previous_step_view = now.view;
                    }
            }
            if (steps > 0 && !engine.held()) {
                if (last_step_loaded) load_step_drawn = true;
                else if (!input_log.empty()) input_log.back().drawn = true;
            }
            if (state_test_save) {
                save_state(0);
                state_test_save = false;
            }
            if (stage_states && steps > 0 && last_step_loaded && can_save().empty()) {
                const auto state_file = std::filesystem::path(stage_states) / (std::to_string(frame) + "_" + engine.stage_name() + ".dw4state");
                write_state_file(state_file, SaveState{*engine.stage_snapshot(), load_step_drawn, input_log, engine.fingerprint(), frame});
                // (The campaign driver's memory beside it, for resumed runs.)
                dw4::game::campaign_memory_save(std::filesystem::path(state_file).replace_extension(".mem"));
            }
            // The picture, the menu and the sound output.
            if (draw) {
                // (DW4_DEV_DUMP: what the blend did, for presents.csv.)
                const bool report_blend = dev_dumper && dev.dumping(frame);
                // The story's online presentation (round 60, host_view.hpp): the ghost
                // figures and the remote tags at this pass's presence samples.
                dw4::game::RemotePresentation remote_view;
                remote_view.ghosts = &ghosts;
                remote_view.samples = &present_remote;
                if (ghosts.active() && engine.presenting()) remote_view.own_position = own_position();
                remote_view.cost_max = &ghost_cost_max;
#ifdef DW4_GHOST_PROBE
                if (probe_report) ghost_cost_max = 0; // (this pass's figure draw alone, read after present_scene)
#endif
                const auto scene = dw4::game::present_scene(view, engine, accumulator, smooth_blending, report_blend, &remote_view);
#ifdef DW4_GHOST_PROBE
                const double probe_draw_ms = probe_report ? ghost_cost_max : 0.0;
#endif
                const int width = scene.width, height = scene.height;
                const float present_alpha = scene.present_alpha;
                const auto& blend_report = scene.blend_report;
                const auto& screen = scene.screen;
                const auto& wide = scene.wide;

                // Keyboard and controller navigation of the menus while one takes
                // the input (the F1 menu, the active title panel); never while
                // the game reads the pad.
                // (Over the title the cursor always shows, as in the game's
                // own menus: A presses the highlighted button at once.)
                dw4::game::set_menu_navigation(show_menu || title_online.active, title_online.active && !show_menu);
                // (A UI test run's keys: last frame's go up, this frame's go down.)
                for (const int key : ui_keys_down) ImGui::GetIO().AddKeyEvent(ImGuiKey(key), false);
                ui_keys_down.clear();
                for (const int key : ui_keys) ImGui::GetIO().AddKeyEvent(ImGuiKey(key), true);
                ui_keys_down.swap(ui_keys);
                // (A UI test run's controller keys count as a controller's.)
                dw4::game::new_ui_frame(ui_script.loaded());
                // The running room for the screens (the relay's roster).
                dw4::game::RoomLive room_live;
                if (online) {
                    auto& session = online->session();
                    room_live.active = true;
                    room_live.member = session.member_id();
                    room_live.wid = session.wid();
                    room_live.host = session.host();
                    room_live.is_host = session.is_host();
                    room_live.host_name = session.member_name(session.host());
                    if (const auto& info = session.instance(); info && info->wid == session.wid()) {
                        room_live.visibility = int(info->visibility);
                        room_live.area = info->area;
                    }
                    room_live.own_slot = session.own_slot();
                    room_live.inst = session.inst();
                    room_live.mode = dw4::game::online::mode_name(session.mode());
                    room_live.delay = session.delay();
                    room_live.leave_prompt = online->leave_prompt;
                    room_live.in_game = online->link().game_started();
                    if (auto* character = online->character()) room_live.character = character->status_text();
                    room_live.story = online->story_line();
                    // Round I1b (19.30.6): the *Go along* box.
                    for (const auto& offer : session.go_offers()) {
                        dw4::game::RoomLive::GoRow row;
                        row.id = offer.id;
                        row.text = session.go_row_text(offer);
                        row.grey = session.go_grey(offer);
                        row.closing = !offer.open;
                        row.own = session.go_offer_id() == offer.id && session.go_state() != dw4::game::online::GoState::none;
                        room_live.go_rows.push_back(std::move(row));
                    }
                    room_live.go_line = session.go_line();
                    if (const auto& info = session.instance(); info && info->wid != 0)
                        for (const auto& e : info->members)
                            room_live.members.push_back({e.member, session.member_name(e.member), e.stage, 0, e.level, 0, 0,
                                                         (e.flags & dw4::game::online::wire::InstanceMember::connected) != 0,
                                                         (e.flags & dw4::game::online::wire::InstanceMember::lagging) != 0,
                                                         (e.flags & dw4::game::online::wire::InstanceMember::host) != 0});
                }
                last_room_live = room_live;
                bool online_tab_now = false;
                // (Round I1a, 19.5.4: the F1 menu opens on the Online tab while
                // a world session runs: its Instance page, the status line's View.
                // menu_tab is a one-shot request the tab bar consumes, so it is
                // empty at every opening by the player, whatever tab he chose
                // last; a tab a UI script or DW4_MENU_TAB named is kept.)
                if (show_menu && !menu_shown_before && online && (menu_tab.empty() || menu_tab == "Online")) {
                    menu_tab = "Online";
                    // (From the status window's line, "View: change": the
                    // Instance page with the visibility focused.)
                    online_ui.focus_visibility = instance_line_shown;
                    // (Round I1b, 19.30.6: View while a go-along notice has been
                    // up for half a second focuses its row; earlier the cursor
                    // stays below the box, so a reflexive A starts nothing.)
                    // While the own request waits ("... View: cancel") View
                    // focuses its row, where A cancels.
                    const auto& go_session = online->session();
                    const auto notices = go_session.go_notices(steady_seconds());
                    const auto go_state = go_session.go_state();
                    const bool go_waiting = go_state == dw4::game::online::GoState::asked || go_state == dw4::game::online::GoState::waiting ||
                                            go_state == dw4::game::online::GoState::boss;
                    online_ui.focus_go_id = 0;
                    online_ui.focus_go = false;
                    if (go_waiting) {
                        online_ui.focus_go = true;
                        online_ui.focus_go_id = go_session.go_offer_id();
                    } else if (!notices.empty() && steady_seconds() - notices.front()->notice_at >= 0.5) {
                        online_ui.focus_go = true;
                        online_ui.focus_go_id = notices.front()->id;
                    }
                    online_ui.focus_below_go = !online_ui.focus_go && !online_ui.focus_visibility && !go_session.go_offers().empty();
                }
                menu_shown_before = show_menu;
                // Overhead name tags over the picture, under the menus.
                dw4::game::draw_tags(view, scene);
                if (show_menu) {
                    ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_FirstUseEver);
                    ImGui::Begin("DW4 PC (F1)");
                    if (ImGui::BeginTabBar("menu")) {
                    if (ImGui::BeginTabItem("Settings", nullptr, tab_flags("Settings"))) {
                        {
                            // Presets: Original (the GameCube presentation, load
                            // times and balance) or Modernized (widescreen, smooth
                            // motion, anti-aliasing, straight to the title, fast
                            // loading).
                            // "Original" only while the running game is not randomized
                            // either: a loaded save's bond keeps its randomizer whatever
                            // the settings say.
                            const auto playing = dw4::game::randomizer_from_cheats(engine.cheats());
                            const bool original = !settings.widescreen && !settings.smooth_motion && settings.msaa == 0 &&
                                                  !settings.skip_intro && !settings.fast_loading && settings.exp_rate == 100 &&
                                                  settings.damage_dealt == 100 && settings.damage_taken == 100 && settings.rand_seed == 0 &&
                                                  !playing.active();
                            const bool modernized = settings.widescreen && settings.smooth_motion && settings.msaa >= 4 && settings.skip_intro &&
                                                    settings.fast_loading;
                            int preset = original ? 0 : modernized ? 1 : 2;
                            static const char* presets[] = {"Original", "Modernized", "Custom"};
                            if (ImGui::Combo("Preset", &preset, presets, 3) && preset < 2) {
                                const bool modern = preset == 1;
                                settings.widescreen = modern;
                                settings.smooth_motion = modern;
                                settings.msaa = modern ? 4 : 0;
                                settings.skip_intro = modern;
                                settings.fast_loading = modern;
                                loading_override.reset();
                                if (!modern) {
                                    settings.exp_rate = settings.damage_dealt = settings.damage_taken = 100;
                                    settings.rand_seed = 0;
                                }
                                // (Online the host's gameplay settings stay.)
                                if (!net.active()) apply_gameplay(engine.cheats());
                                settings_changed = true;
                            }
                            ImGui::PushTextWrapPos(0.0f);
                            ImGui::TextDisabled("Original: 4:3, 60 fps motion, no anti-aliasing, the intro, GameCube load times and the "
                                                "original balance.");
                            if (playing.active()) {
                                if (net.active())
                                    ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "This session plays the host's randomizer (%s).",
                                                       dw4::game::describe_randomizer(playing).c_str());
                                else if (randomizer_unique != 0 && randomizer_bound && randomizer_bound->active())
                                    ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1),
                                                       "The loaded save plays the randomizer (%s): its bond keeps it, whatever the preset. "
                                                       "F1 > Cheats can let this save play the original game.",
                                                       dw4::game::describe_randomizer(*randomizer_bound).c_str());
                                else
                                    ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "The randomizer is on for new games (%s, F1 > Cheats).",
                                                       dw4::game::describe_randomizer(playing).c_str());
                            }
                            ImGui::PopTextWrapPos();
                            // Loading: the original's disc and loading-thread time or none
                            // of it (shown as it applies: online the host's choice).
                            const bool fast = net.active() ? engine.cheats().fast_loading : fast_loading_choice();
                            int loading = fast ? 1 : 0;
                            static const char* loadings[] = {"Original", "Fast"};
                            ImGui::BeginDisabled(net.active());
                            if (ImGui::Combo("Loading", &loading, loadings, 2)) {
                                settings.fast_loading = loading == 1;
                                loading_override.reset();
                                apply_gameplay(engine.cheats());
                                settings_changed = true;
                            }
                            ImGui::EndDisabled();
                            ImGui::PushTextWrapPos(0.0f);
                            if (net.active())
                                ImGui::TextDisabled("Online, the host's loading choice applies to everyone.");
                            else if (fast)
                                ImGui::TextDisabled("Fast: a stage change skips the GameCube's disc reads and loading waits and takes "
                                                    "well under a second. Applies from the next stage change.");
                            else
                                ImGui::TextDisabled("Original: a stage change takes as long as on the GameCube (about 7-15 seconds). "
                                                    "Applies from the next stage change.");
                            ImGui::PopTextWrapPos();
                        }
                        // Display, audio, name tags / Discord / achievements, keyboard and
                        // controllers (host_view.hpp: a Data Overflow run's F1 shows them too).
                        dw4::game::settings_display_section(view);
                        dw4::game::settings_audio_section(view);
                        dw4::game::settings_presence_section(view, discord ? discord->status().c_str() : "off");
                        dw4::game::settings_keyboard_section(view);
                        dw4::game::settings_controllers_section(view);
                        if (ImGui::CollapsingHeader("Save states", ImGuiTreeNodeFlags_DefaultOpen)) {
                            ImGui::TextDisabled("F5 saves and F9 loads state 1. Files: %s", state_dir.string().c_str());
                            for (std::size_t slot = 0; slot < state_slots.size(); ++slot) {
                                ImGui::PushID(int(slot));
                                if (ImGui::SmallButton("Save")) save_state(slot);
                                ImGui::SameLine();
                                if (ImGui::SmallButton("Load")) load_state(slot);
                                ImGui::SameLine();
                                if (state_slots[slot])
                                    ImGui::Text("%zu: %s, %zu steps since its stage loaded", slot + 1, state_slots[slot]->snapshot.stage.c_str(),
                                                state_slots[slot]->inputs.size());
                                else
                                    ImGui::TextDisabled("%zu: empty", slot + 1);
                                ImGui::PopID();
                            }
                        }
                        if (ImGui::CollapsingHeader("Mods")) {
                            ImGui::TextDisabled("Folders in %s with a mod.ini; changes apply at the next start.", mods_root.string().c_str());
                            if (mods.empty()) ImGui::TextDisabled("No mods found.");
                            for (auto& mod : mods) {
                                bool enabled = !settings.disabled_mods.count(mod.id);
                                if (ImGui::Checkbox(("##mod" + mod.id).c_str(), &enabled)) {
                                    if (enabled) settings.disabled_mods.erase(mod.id);
                                    else settings.disabled_mods.insert(mod.id);
                                    settings_changed = true;
                                }
                                ImGui::SameLine();
                                ImGui::Text("%s %s%s%s (%zu files)", mod.name.c_str(), mod.version.c_str(), mod.author.empty() ? "" : " by ",
                                            mod.author.c_str(), mod.file_count);
                                if (!mod.problem.empty()) {
                                    ImGui::SameLine();
                                    ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "- %s", mod.problem.c_str());
                                }
                                if (!mod.description.empty()) ImGui::TextDisabled("    %s", mod.description.c_str());
                            }
                        }
                        if (ImGui::CollapsingHeader("Texture packs")) {
                            ImGui::Text("%zu replacement textures loaded.", texture_pack.replacement_count());
                            ImGui::TextDisabled("PNG files named ..._<16 hex digit hash>.png below %s",
                                                (settings.texture_pack.empty() ? mods_folder("textures") : settings.texture_pack).string().c_str());
                            if (ImGui::Checkbox("Dump textures (to mods/dump/textures)", &settings.dump_textures)) {
                                settings_changed = true;
                                configure_pack();
                            }
                            if (ImGui::Button("Reload texture pack")) configure_pack();
                        }
                        if (ImGui::Checkbox("Pause the game while this menu is open", &settings.pause_in_menu)) settings_changed = true;
                        if (ImGui::Checkbox("Skip intro at the next start (straight to the title)", &settings.skip_intro))
                            settings_changed = true;
                        ImGui::TextDisabled("Save (memory card slot A): %s", player_store.describe(0).c_str());
                        ImGui::TextDisabled("(the Saves tab shows slot B, the backups and imports GameCube saves)");
                        ImGui::TextDisabled("Settings file: %s", settings.file.string().c_str());
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Gameplay", nullptr, tab_flags("Gameplay"))) {
                        // Data Overflow's dev start (SURVIVAL.md 15.1, round O1a).
                        const std::string refusal = overflow_switch.busy() ? std::string("A run is starting.")
                                                                           : dw4::game::overflow::start_refusal(engine, net.active() || net.in_lobby() || online || title_online.active);
                        if (auto request = dw4::game::overflow::draw_dev_start(overflow_dev, refusal)) {
                            overflow_switch.request(std::move(*request));
                            dw4::game::toggle_menu(view);
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Saves", nullptr, tab_flags("Saves"))) {
                        auto& tab = saves_tab;
                        const auto now = std::chrono::steady_clock::now();
                        if (now - tab.read_at > std::chrono::seconds(2)) {
                            for (int slot = 0; slot < 2; ++slot) {
                                const auto gci = player_store.read(slot);
                                tab.has_file[std::size_t(slot)] = gci.has_value();
                                tab.boxes[std::size_t(slot)] = gci ? dw4::save::summarize_save(*gci) : std::nullopt;
                            }
                            tab.read_at = now;
                        }
                        ImGui::SeparatorText("Where the save lives");
                        const bool single = player_store.kind() == dw4::save::CardStore::Kind::single_file;
                        for (int slot = 0; slot < 2; ++slot) {
                            const bool card = slot == 0 || player_store.slot_exists(1) ||
                                              player_card_settings[1].mode == dw4::engine::CardSetting::Mode::inserted;
                            ImGui::TextWrapped("Slot %c: %s%s", slot == 0 ? 'A' : 'B', player_store.describe(slot).c_str(),
                                               !card ? " (no memory card in this slot)" : tab.has_file[std::size_t(slot)] ? "" : " (no save yet)");
                            if (const auto& boxes = tab.boxes[std::size_t(slot)])
                                for (std::size_t i = 0; i < boxes->size(); ++i)
                                    ImGui::TextDisabled("    Box %zu: %s", i + 1, dw4::save::describe_box((*boxes)[i]).c_str());
                            else if (tab.has_file[std::size_t(slot)])
                                ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "    The save is damaged (neither data copy passes its check).");
                        }
                        ImGui::TextDisabled("Chosen by %s.", dw4::game::why_text(save_choice.why));
                        if (single)
                            ImGui::TextWrapped("Each save keeps the previous file as .bak beside it. The file holds the same bytes as "
                                               "Dolphin's B2-GDJE-NDMWSaveData.gci (rename it to use it there). settings.ini [data] "
                                               "save / save_b move the files; [data] cards = a GCI folder (e.g. Dolphin's GC/USA) "
                                               "plays from that folder instead. Slot B holds a card once its file exists.");
                        for (const auto& note : save_choice.notes) ImGui::TextWrapped("%s", note.c_str());
                        for (const auto& problem : save_choice.problems) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.6f, 0.4f, 1));
                            ImGui::TextWrapped("%s", problem.c_str());
                            ImGui::PopStyleColor();
                        }
                        if (net.active()) ImGui::TextWrapped("Online, the session plays from shared copies of the cards.");

                        ImGui::SeparatorText("Import a GameCube save");
                        ImGui::TextWrapped("Dolphin / GCMM .gci, GameShark .gcs, MaxDrive / Action Replay .sav, memory card images "
                                           ".raw / .gcp (Dolphin, Nintendont, card dumps) or a GCI folder.");
                        if (tab.dialog_done.exchange(false)) {
                            std::lock_guard lock(tab.dialog_mutex);
                            std::snprintf(tab.path, sizeof tab.path, "%s", tab.dialog_path.c_str());
                            tab.source.reset();
                        }
                        ImGui::InputText("File or folder", tab.path, sizeof tab.path);
                        ImGui::SameLine();
                        if (ImGui::Button("Browse...")) {
                            static const SDL_DialogFileFilter filters[] = {{"GameCube saves", "gci;gcs;sav;raw;gcp"}, {"All files", "*"}};
                            const SDL_DialogFileCallback callback = [](void* user, const char* const* files, int) {
                                auto* t = static_cast<SavesTab*>(user);
                                if (files && files[0]) {
                                    std::lock_guard lock(t->dialog_mutex);
                                    t->dialog_path = files[0];
                                    t->dialog_done.store(true);
                                }
                            };
                            SDL_ShowOpenFileDialog(callback, &tab, window, filters, 2, nullptr, false);
                        }
                        // DW4_MENU_IMPORT=<file> (test runs): that file's preview at the first menu frame.
                        static bool preview_from_env = options.smoke_frames && std::getenv("DW4_MENU_IMPORT");
                        if (preview_from_env) std::snprintf(tab.path, sizeof tab.path, "%s", std::getenv("DW4_MENU_IMPORT"));
                        if (ImGui::Button("Preview") || std::exchange(preview_from_env, false)) {
                            std::string error;
                            tab.source = dw4::save::load_import(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(tab.path))), error);
                            tab.message = tab.source ? std::string() : "Cannot import: " + error;
                            tab.chosen = {};
                            tab.into = {0, 1, 2, 3};
                            if (tab.source)
                                if (const auto boxes = dw4::save::summarize_save(tab.source->gci))
                                    for (std::size_t i = 0; i < boxes->size(); ++i) tab.chosen[i] = (*boxes)[i].used;
                        }
                        if (tab.source) {
                            const auto saved = dw4::save::save_time_text(tab.source->gci);
                            ImGui::TextWrapped("%s - %s%s%s", tab.source->container.c_str(), tab.source->detail.c_str(),
                                               saved.empty() ? "" : ", saved ", saved.c_str());
                            const auto boxes = dw4::save::summarize_save(tab.source->gci);
                            static const char* box_names[] = {"Box 1", "Box 2", "Box 3", "Box 4"};
                            for (std::size_t i = 0; boxes && i < boxes->size(); ++i) {
                                ImGui::PushID(int(i));
                                ImGui::Checkbox("##take", &tab.chosen[i]);
                                ImGui::SameLine();
                                ImGui::Text("Box %zu", i + 1);
                                ImGui::SameLine();
                                // The target box beside the box it takes.
                                ImGui::BeginDisabled(!tab.chosen[i]);
                                ImGui::SetNextItemWidth(80);
                                ImGui::Combo("##into", &tab.into[i], box_names, 4);
                                ImGui::EndDisabled();
                                ImGui::SameLine();
                                ImGui::PushTextWrapPos(0.0f);
                                ImGui::TextUnformatted(dw4::save::describe_box((*boxes)[i]).c_str());
                                ImGui::PopTextWrapPos();
                                ImGui::PopID();
                            }
                            static const char* slots[] = {"Slot A", "Slot B"};
                            ImGui::SetNextItemWidth(120);
                            ImGui::Combo("Into the save of", &tab.slot, slots, 2);
                            std::vector<dw4::save::BoxMove> moves;
                            bool clash = false;
                            for (int i = 0; i < 4; ++i)
                                if (tab.chosen[std::size_t(i)]) {
                                    for (const auto& m : moves) clash = clash || m.to == tab.into[std::size_t(i)];
                                    moves.push_back({i, tab.into[std::size_t(i)]});
                                }
                            if (clash) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Two boxes go into the same box.");
                            auto run_import = [&](const std::vector<dw4::save::BoxMove>& chosen) {
                                const auto outcome = dw4::game::import_save(player_store, tab.slot, *tab.source, chosen);
                                tab.message = outcome.message;
                                engine.log(outcome.message);
                                tab.read_at = {};
                            };
                            ImGui::BeginDisabled(net.active() || moves.empty() || clash);
                            if (ImGui::Button("Import the chosen boxes")) run_import(moves);
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            ImGui::BeginDisabled(net.active());
                            if (ImGui::Button("Replace the whole save")) run_import({});
                            ImGui::EndDisabled();
                            ImGui::PushTextWrapPos(0.0f);
                            ImGui::TextDisabled("The current save is copied to %s first.",
                                                dw4::save::backup_folder(player_store).generic_string().c_str());
                            ImGui::TextDisabled("The game reads the card again at the next Call Digimon or save room visit.");
                            ImGui::PopTextWrapPos();
                        }
                        if (!tab.message.empty()) ImGui::TextWrapped("%s", tab.message.c_str());
                        cloud_panel.draw(player_store, !online && !net.active() && !net.in_lobby());
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Online", nullptr, tab_flags("Online"))) {
                        // The account and room screens (online_ui.hpp); a direct
                        // --host / --join session of the command line (LAN and
                        // tests) shows its own state instead.
                        online_tab_now = true;
                        if (net.active()) {
                            ImGui::Text("Playing a direct session as player %d of %d (input delay %d steps).", net.local_slot() + 1,
                                        net.start().players, net.start().delay);
                            if (net.desync()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Desync at step %llu.", (unsigned long long)*net.desync());
                            if (ImGui::Button("Leave the session")) end_session();
                        } else if (net.in_lobby()) {
                            ImGui::TextUnformatted(lobby_status.c_str());
                            if (ImGui::Button("Cancel")) net.close();
                        } else if (lobby) {
                            if (!lobby->opened()) lobby->open();
                            dw4::game::draw_online_screens(*lobby, online_ui, online ? &room_live : nullptr);
                        } else if (online) {
                            const auto& room = online->room();
                            ImGui::Text("The world as %s (instance %u)", room.account.c_str(), online->session().wid());
                            ImGui::Text("Instance %u, slot %d, %s, input delay %d", online->session().inst(), online->session().own_slot() + 1,
                                        dw4::game::online::mode_name(online->session().mode()), online->session().delay());
                            if (auto* character = online->character()) ImGui::TextDisabled("%s", character->status_text().c_str());
                            ImGui::TextDisabled("%s", online->story_line().c_str());
                            if (online->leave_prompt) ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "Go offline?");
                            if (ImGui::Button("Go offline")) leave_room();
                            if (online->leave_prompt && ImGui::Button("Stay")) online->leave_prompt = false;
                        }
                        // The connection of a room session (round 62): what the
                        // indicator shows, with the details to report a lag.
                        if (online) {
                            const auto& session = online->session();
                            const auto& net = session.net();
                            const auto& recent = session.stats().recent();
                            const auto& delayed = session.stats().delayed_total();
                            const bool playing = session.mode() == dw4::game::online::Mode::sealed;
                            const auto quality = dw4::game::online::link_quality(playing ? session.delay() : 0, net.network_rtt_ms(), recent.drop_rate(),
                                                                                 session.overlay(steady_seconds()).waiting_for_server);
                            ImGui::SeparatorText("Connection");
                            const ImVec4 colour = quality == dw4::game::online::LinkQuality::good   ? ImVec4(0.45f, 0.9f, 0.45f, 1)
                                                  : quality == dw4::game::online::LinkQuality::fair ? ImVec4(1.0f, 0.85f, 0.3f, 1)
                                                                                                    : ImVec4(1.0f, 0.4f, 0.4f, 1);
                            // (Wrapped: the lines are longer than the menu is wide; round 62 review.)
                            const auto dim = [](const char* format, auto... args) {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                                ImGui::TextWrapped(format, args...);
                                ImGui::PopStyleColor();
                            };
                            ImGui::TextColored(colour, "Link: %s", dw4::game::online::link_quality_name(quality));
                            if (net.rtt_ms >= 0 && net.from_relay)
                                ImGui::TextWrapped("Ping (network round trip to the server): %.0f ms. The server measures %.0f ms (spread %d ms): that "
                                                   "includes about %.0f ms of waiting for this game's next picture.",
                                                   net.network_rtt_ms(), net.rtt_ms, int(net.rtt_spread_ms), std::max(0.0, net.pass_wait_ms));
                            else if (net.rtt_ms >= 0)
                                ImGui::TextWrapped("Ping (network round trip to the server): %.0f ms (TCP estimate)", net.rtt_ms);
                            else
                                dim("Ping: not measured yet");
                            if (playing)
                                ImGui::TextWrapped("Input delay: %d ticks (%.0f ms). With others in your stage every input waits this long.",
                                                   session.delay(), session.delay() * dw4::engine::kFieldSeconds * 1000.0);
                            else
                                ImGui::TextWrapped("Input delay: none (you play this stage on your own)");
                            if (recent.samples)
                                ImGui::TextWrapped("Your inputs, last seconds: %.0f ms until they run (max %.0f); %.1f %% came too late for the server.",
                                                   recent.exec_avg() * 1000.0, recent.exec_max * 1000.0, recent.drop_rate() * 100.0);
                            if (net.from_relay && net.sealed)
                                ImGui::TextWrapped("Server: %llu of your inputs filled in, %llu late, of %llu in this stage.",
                                                   static_cast<unsigned long long>(net.filled), static_cast<unsigned long long>(net.late),
                                                   static_cast<unsigned long long>(net.sealed));
                            dim("Session: %llu inputs with others, latency avg %.0f ms; %d reconnects, %d resyncs, %zu stalls caught up.",
                                static_cast<unsigned long long>(delayed.samples), delayed.exec_avg() * 1000.0, net.reconnects, net.resyncs,
                                session.backlog_runs());
                            if (online->log().is_open()) {
                                const auto log_path = online->log().path();
                                dim("When online play lags, send this file (it holds no password):");
                                ImGui::TextWrapped("%s", log_path.string().c_str());
                                if (ImGui::Button("Copy path")) ImGui::SetClipboardText(log_path.string().c_str());
                                ImGui::SameLine();
                                if (ImGui::Button("Open folder")) {
                                    // A file:/// URL of the folder, UTF-8 percent-encoded (spaces, non-ASCII names).
                                    std::string url = "file:///";
                                    const auto folder = log_path.parent_path().generic_u8string();
                                    for (const auto c : folder) {
                                        const auto b = static_cast<unsigned char>(c);
                                        if (std::isalnum(b) || b == '/' || b == ':' || b == '.' || b == '_' || b == '-' || b == '~') {
                                            url += char(b);
                                        } else {
                                            char hex[4];
                                            std::snprintf(hex, sizeof hex, "%%%02X", unsigned(b));
                                            url += hex;
                                        }
                                    }
                                    SDL_OpenURL(url.c_str());
                                }
                            }
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Cheats", nullptr, tab_flags("Cheats"))) {
                        ImGui::TextWrapped("Optional cheats for this session. They change the original game; what they "
                                           "give (levels, bits) stays in saves.");
                        auto& cheats = engine.cheats();
                        // Online, the old direct connection or a room (SEAMLESS.md
                        // 9.1.10): the controls disabled, the player buttons hidden
                        // (the engine keeps the cheats off in a room, Engine::step).
                        const bool cheats_online = net.active() || online != nullptr;
                        if (online) ImGui::TextColored(ImVec4(1, 0.75f, 0.4f, 1), "Cheats are off online.");
                        ImGui::BeginDisabled(cheats_online);
                        ImGui::Checkbox("God mode (players lose no HP)", &cheats.god);
                        ImGui::Checkbox("Infinite MP", &cheats.infinite_mp);
                        ImGui::Checkbox("One-hit defeats (player hits)", &cheats.one_hit);
                        ImGui::SeparatorText("Rebalancing (saved; 100% is the original)");
                        bool gameplay_changed = false;
                        gameplay_changed |= ImGui::SliderInt("EXP from enemies", &settings.exp_rate, 10, 1000, "%d%%");
                        gameplay_changed |= ImGui::SliderInt("Damage of your hits", &settings.damage_dealt, 10, 1000, "%d%%");
                        gameplay_changed |= ImGui::SliderInt("Damage enemies do", &settings.damage_taken, 10, 1000, "%d%%");
                        ImGui::SeparatorText("Randomizer");
                        // What the running game plays (the bond of player 1's save,
                        // the host's online), then the settings new games bind.
                        {
                            const ImVec4 lit(1, 0.75f, 0.4f, 1);
                            ImGui::PushTextWrapPos(0.0f);
                            if (net.active()) {
                                ImGui::Text("This session: %s (the host's).",
                                            dw4::game::describe_randomizer(dw4::game::randomizer_from_cheats(engine.cheats())).c_str());
                            } else if (randomizer_unique != 0 && randomizer_bound) {
                                const std::string who = randomizer_name.empty() ? "This save" : "This save (" + randomizer_name + ")";
                                if (randomizer_bound->active())
                                    ImGui::TextColored(lit, "%s plays %s.", who.c_str(), dw4::game::describe_randomizer(*randomizer_bound).c_str());
                                else
                                    ImGui::Text("%s plays the original game.", who.c_str());
                            } else {
                                ImGui::TextDisabled("No save in play: a new game binds the settings below to its save.");
                            }
                            ImGui::PopTextWrapPos();
                            if (!net.active() && randomizer_unique != 0 && randomizer_bound) {
                                if (randomizer_bound->active() && ImGui::Button("Play this save without the randomizer")) {
                                    // An explicit "off" bond: the save plays the original
                                    // game from now on (enemies spawning from here, the next
                                    // stage change, drops), also after a restart.
                                    randomizer_bound = dw4::game::RandomizerConfig{};
                                    dw4::game::store_randomizer_binding(randomizer_folder, randomizer_unique, randomizer_name, *randomizer_bound);
                                    std::cout << "randomizer: save " << dw4::game::randomizer_unique_key(randomizer_unique)
                                              << " plays the original game now\n";
                                    toasts.push_back({"Randomizer", "This save plays the original game now.",
                                                      std::chrono::steady_clock::now() + std::chrono::seconds(6)});
                                    gameplay_changed = true;
                                }
                                if (randomizer_settings().active() && !(*randomizer_bound == randomizer_settings())) {
                                    if (randomizer_bound->active()) ImGui::SameLine();
                                    if (ImGui::Button("Bind the settings below to this save")) {
                                        randomizer_bound = randomizer_settings();
                                        dw4::game::store_randomizer_binding(randomizer_folder, randomizer_unique, randomizer_name, *randomizer_bound);
                                        dw4::game::write_randomizer_spoiler(randomizer_folder, randomizer_unique, *randomizer_bound);
                                        std::cout << "randomizer: save " << dw4::game::randomizer_unique_key(randomizer_unique) << " bound to "
                                                  << dw4::game::describe_randomizer(*randomizer_bound) << "\n";
                                        gameplay_changed = true;
                                    }
                                }
                            }
                        }
                        ImGui::SeparatorText("Settings for new games (saved; seed 0 is off)");
                        int seed = int(settings.rand_seed & 0x7FFFFFFF);
                        if (ImGui::InputInt("Seed", &seed)) {
                            settings.rand_seed = std::uint32_t(std::max(0, seed));
                            gameplay_changed = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("New seed")) {
                            settings.rand_seed = std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7FFFFFFF) | 1u;
                            gameplay_changed = true;
                        }
                        gameplay_changed |= ImGui::Checkbox("Item drops", &settings.rand_drops);
                        static const char* enemy_modes[] = {"Off", "Every enemy on its own", "One family per wave"};
                        gameplay_changed |= ImGui::Combo("Enemies", &settings.rand_enemies, enemy_modes, 3);
                        static const char* zone_modes[] = {"Off", "Dungeon entrances within an area"};
                        gameplay_changed |= ImGui::Combo("Loading zones", &settings.rand_zones, zone_modes, 2);
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextDisabled("Drops change into other items of the same kind (key items stay). Enemies become other "
                                            "Digimon with the original enemy's strength, EXP and drops; bosses and story fights stay. "
                                            "The dungeon entrances of an area are shuffled among the area's dungeons (an entrance may "
                                            "keep its own). Only a new game binds these settings to its save (saves/randomizer.ini, "
                                            "spoiler log beside it); a save from before keeps playing the original game until you "
                                            "bind the settings to it above.");
                        ImGui::PopTextWrapPos();
                        if (gameplay_changed && !cheats_online) {
                            apply_gameplay(cheats);
                            settings_changed = true;
                        }
                        ImGui::EndDisabled();
                        if (net.active()) ImGui::TextDisabled("Online, the host's settings apply.");
                        if (auto* sys = engine.get<dw4::engine::GameSystem>(); sys && !cheats_online) {
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
                    if (ImGui::BeginTabItem("Achievements", nullptr, tab_flags("Achievements"))) {
                        const auto list = dw4::game::achievement_list();
                        const auto& profile = achievements.profile();
                        ImGui::Text("%zu of %zu unlocked", profile.unlocked.size(), list.size());
                        if (!profile.unlocked_randomizer.empty()) {
                            ImGui::SameLine();
                            ImGui::TextDisabled("(randomizer runs: %zu)", profile.unlocked_randomizer.size());
                        }
                        const std::int64_t minutes = profile.stat("play_minutes");
                        ImGui::TextDisabled("In-game time %lld h %02lld min (without loading).", (long long)(minutes / 60), (long long)(minutes % 60));
                        if (!achievements_on()) ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "Achievements are switched off (Settings).");
                        else if (last_gate != dw4::game::AchievementGate::open)
                            ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "%s", dw4::game::achievement_gate_text(last_gate));
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextDisabled("Offline achievements stay on this PC (%s). Cheats, rebalancing and a save state loaded in "
                                            "the running stage pause them; runs with the randomizer count separately.",
                                            profile_path.empty() ? "not saved" : profile_path.string().c_str());
                        ImGui::PopTextWrapPos();
                        auto day = [](std::int64_t unix_seconds) {
                            const std::chrono::sys_days days = std::chrono::floor<std::chrono::days>(std::chrono::sys_seconds(std::chrono::seconds(unix_seconds)));
                            const std::chrono::year_month_day date(days);
                            char text[16];
                            std::snprintf(text, sizeof text, "%04d-%02u-%02u", int(date.year()), unsigned(date.month()), unsigned(date.day()));
                            return std::string(text);
                        };
                        for (int category = 0; category <= int(dw4::game::AchievementCategory::multiplayer); ++category) {
                            const auto cat = dw4::game::AchievementCategory(category);
                            std::size_t total = 0, done = 0;
                            for (const auto& def : list)
                                if (def.category == cat) {
                                    ++total;
                                    done += profile.unlocked.count(def.id) ? 1 : 0;
                                }
                            const std::string header = std::string(dw4::game::achievement_category_name(cat)) + " (" + std::to_string(done) + "/" +
                                                       std::to_string(total) + ")###cat" + std::to_string(category);
                            if (!ImGui::CollapsingHeader(header.c_str())) continue;
                            if (!ImGui::BeginTable(("ach" + std::to_string(category)).c_str(), 3, ImGuiTableFlags_SizingStretchProp)) continue;
                            for (const auto& def : list) {
                                if (def.category != cat) continue;
                                const auto found = profile.unlocked.find(def.id);
                                const auto random = profile.unlocked_randomizer.find(def.id);
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                if (found != profile.unlocked.end()) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1), "%s", def.title);
                                else ImGui::TextDisabled("%s", def.title);
                                ImGui::TableNextColumn();
                                ImGui::TextWrapped("%s", def.description);
                                ImGui::TableNextColumn();
                                if (found != profile.unlocked.end()) ImGui::Text("%s", day(found->second).c_str());
                                else if (def.stat && def.goal > 0)
                                    ImGui::ProgressBar(float(std::min<std::int64_t>(profile.stat(def.stat), def.goal)) / float(def.goal), ImVec2(-1, 0),
                                                       (std::to_string(std::min<std::int64_t>(profile.stat(def.stat), def.goal)) + " / " + std::to_string(def.goal)).c_str());
                                else ImGui::TextDisabled("-");
                                if (random != profile.unlocked_randomizer.end()) {
                                    ImGui::SameLine();
                                    ImGui::TextDisabled("(randomizer %s)", day(random->second).c_str());
                                }
                            }
                            ImGui::EndTable();
                        }
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Debug", nullptr, tab_flags("Debug"))) {
                    ImGui::Text("Stage: %s  (%s)", engine.stage_name().c_str(), engine.loader_state().c_str());
                    ImGui::Text("Frame %llu  draws %zu  meshes %zu  textures %zu", (unsigned long long)engine.frame(), items.size(),
                                renderer->mesh_count(), renderer->texture_count());
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
                online_tab_shown = show_menu && online_tab_now;
                // The online screens over the title (its menu's third entry
                // opens them; a room started from them shows there until Play).
                if (online_ui.stay_requested) {
                    // ("Stay" answered B + X + START's "Leave room?".)
                    online_ui.stay_requested = false;
                    if (online && online->leave_prompt) {
                        online->leave_prompt = false;
                        std::cout << "online: leave prompt closed (Stay) at frame " << frame << '\n';
                    }
                }
                // The Instance page's choices (19.5.4): the host's visibility
                // (INSTANCE_OP) and *Leave instance* (INTENT leave).
                if (online_ui.visibility_requested >= 0) {
                    if (online) online->session().set_visibility(dw4::game::online::wire::Visibility(online_ui.visibility_requested));
                    online_ui.visibility_requested = -1;
                }
                if (online_ui.leave_instance_requested) {
                    online_ui.leave_instance_requested = false;
                    if (online && online->session().leave_instance())
                        std::cout << "online: leave instance at frame " << frame
                                  << (online->session().leave_pending() ? " (kept until the INTENT can go)" : "") << '\n';
                }
                // Round I1b (19.30.6): A on a *Go along* row (the menu closes:
                // nothing has left yet), A on the own waiting row, B in the box.
                if (online_ui.go_requested) {
                    if (online && online->session().go_along(std::uint16_t(online_ui.go_requested))) {
                        std::cout << "online: go along (offer " << online_ui.go_requested << ") at frame " << frame << '\n';
                        online_ui.close_requested = true;
                    }
                    online_ui.go_requested = 0;
                }
                if (online_ui.go_cancel_requested) {
                    online_ui.go_cancel_requested = false;
                    if (online && online->session().cancel_go_along()) std::cout << "online: go along cancelled at frame " << frame << '\n';
                }
                if (online_ui.go_closed) {
                    if (online) online->session().close_go_notices(std::uint16_t(online_ui.go_closed));
                    online_ui.go_closed = 0;
                }
                if (online_ui.close_requested) {
                    // ("Play" on the playing screen, here or in F1 Online.)
                    online_ui.close_requested = false;
                    if (title_online.active) pad_latch = true;
                    title_online.active = false;
                    show_menu = false;
                }
                title_online_shown = false;
                if (title_online.active && lobby && !show_menu) {
                    if (!lobby->opened()) lobby->open();
                    dw4::game::draw_title_online(*lobby, online_ui, title_online, online ? &room_live : nullptr, tag_font, body_font);
                    title_online_shown = true;
                    if (title_online.close || online_ui.close_requested) {
                        title_online.active = title_online.close = online_ui.close_requested = false;
                        pad_latch = true;
                    }
                } else if (title_online.active && !lobby) {
                    title_online.active = false;
                }
                if (!state_message.empty() && std::chrono::steady_clock::now() < message_until) {
                    ImGui::SetNextWindowPos(ImVec2(12, 12));
                    ImGui::SetNextWindowBgAlpha(0.6f);
                    ImGui::Begin("##state", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                    ImGui::TextUnformatted(state_message.c_str());
                    ImGui::End();
                }
                // The line under the own status window (19.5.4, presentation
                // only, it takes no game button): the instance's visibility;
                // View opens the F1 menu on the Online tab's Instance page.
                instance_line_shown = false;
                if (online && online->session().in_instance() && !show_menu) {
                    const auto* windows = engine.get<dw4::engine::SubWindowManager>();
                    if (windows && windows->own_window_open(online->session().own_slot())) {
                        const auto& session = online->session();
                        static const char* kVisibility[] = {"Public", "Friends", "Private"};
                        int vis = 0;
                        if (const auto& info = session.instance(); info && info->wid == session.wid()) vis = std::clamp(int(info->visibility), 0, 2);
                        const std::string line = session.is_host() ? std::string("Instance: ") + kVisibility[vis] + "   View: change"
                                                                   : "Instance: " + session.member_name(session.host()) + "'s (" + kVisibility[vis] + ")";
                        const ImVec2 display = ImGui::GetIO().DisplaySize;
                        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y - 64), ImGuiCond_Always, ImVec2(0.5f, 1));
                        ImGui::SetNextWindowBgAlpha(0.6f);
                        ImGui::Begin("##instance_line", nullptr,
                                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                        ImGui::TextUnformatted(line.c_str());
                        ImGui::End();
                        instance_line_shown = true;
                    }
                }
                // The host's exit prompt into a hub stage and his gate disk
                // (19.5.4): one more line while guests play in his instance. The
                // exit port's prompt (a stage group of two or more) names its
                // destination; a stage group of one runs the original prompt,
                // whose window (i_zzstchgwb) is up while the own player stands on
                // an exit mat whose script names only hub stages; the gate disk
                // (no prompt: it takes him home at once) while it is under the
                // own disk panel's cursor.
                if (online && online->session().is_host() && online->session().instance_players() > 1) {
                    const int own = engine.view_slot();
                    const auto& prompt = engine.exit_prompt();
                    bool home_line = prompt.active && prompt.owner == own && prompt.asking == prompt.owner &&
                                     dw4::game::online::is_hub_stage(prompt.stage);
                    if (!home_line && !prompt.active) {
                        auto* models = engine.get<dw4::engine::ModelCtrlManager>();
                        auto* mats = engine.get<dw4::engine::MatCtrlManager>();
                        bool window = false;
                        for (int i = 0; models && i < 4 && !window; ++i)
                            if (const auto* ram = models->find("i_zzstchgwb", i); ram && ram->node) window = true;
                        if (window && mats)
                            for (const auto& mat : mats->mat_boxes()) {
                                if (!(mats->players_on(mat.name) & (1u << own))) continue;
                                const std::string key = engine.stage_name() + "/" + mat.name;
                                auto known = exit_mats_home.find(key);
                                if (known == exit_mats_home.end()) {
                                    bool home = false;
                                    engine.for_each_script([&](std::string_view name, std::string_view chunk) {
                                        if (name == mat.name + ".lua" && chunk.find("funcStageChange") != std::string_view::npos &&
                                            dw4::game::online::chunk_leads_home(chunk))
                                            home = true;
                                    });
                                    known = exit_mats_home.emplace(key, home).first;
                                }
                                if (known->second) home_line = true;
                            }
                    }
                    if (!home_line)
                        if (const auto* panels = engine.get<dw4::engine::PanelManager>()) home_line = panels->disk_under_cursor(own) == 8;
                    if (home_line) {
                        const ImVec2 display = ImGui::GetIO().DisplaySize;
                        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y - 96), ImGuiCond_Always, ImVec2(0.5f, 1));
                        ImGui::SetNextWindowBgAlpha(0.6f);
                        ImGui::Begin("##host_exit", nullptr,
                                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                        ImGui::TextUnformatted("Your guests will return to their lobbies.");
                        ImGui::End();
                    }
                }
                // Seamless co-op notices, bottom centre (presentation only):
                // the defeated member's hint, the exit prompt's names.
                if (const auto lines = dw4::game::online_notices(engine, give_up_held, kGiveUpFrames); !lines.empty()) {
                    const ImVec2 display = ImGui::GetIO().DisplaySize;
                    ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y - 24), ImGuiCond_Always, ImVec2(0.5f, 1));
                    ImGui::SetNextWindowBgAlpha(0.6f);
                    ImGui::Begin("##online", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                    for (const auto& line : lines) ImGui::TextUnformatted(line.c_str());
                    ImGui::End();
                }
                // The network indicator of a room session (round 62), lower
                // left: ping and input delay, coloured by the link's quality.
                if (online && settings.net_indicator && !online->session().closed()) {
                    const auto& session = online->session();
                    const auto& net = session.net();
                    const bool playing = session.mode() == dw4::game::online::Mode::sealed;
                    const auto overlay_now = session.overlay(steady_seconds());
                    const auto quality = dw4::game::online::link_quality(playing ? session.delay() : 0, net.network_rtt_ms(),
                                                                         session.stats().recent().drop_rate(),
                                                                         overlay_now.waiting_for_server || overlay_now.connection_lost);
                    const ImVec4 colour = quality == dw4::game::online::LinkQuality::good   ? ImVec4(0.45f, 0.9f, 0.45f, 1)
                                          : quality == dw4::game::online::LinkQuality::fair ? ImVec4(1.0f, 0.85f, 0.3f, 1)
                                                                                            : ImVec4(1.0f, 0.4f, 0.4f, 1);
                    const ImVec2 display = ImGui::GetIO().DisplaySize;
                    ImGui::SetNextWindowPos(ImVec2(12, display.y - 12), ImGuiCond_Always, ImVec2(0, 1));
                    ImGui::SetNextWindowBgAlpha(0.45f);
                    ImGui::Begin("##net", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                    // (The network's round trip: the relay's less the wait for this game's next pass.)
                    std::string text = net.rtt_ms >= 0 ? "Ping " + std::to_string(int(net.network_rtt_ms() + 0.5)) + " ms" : std::string("Ping -");
                    if (playing)
                        text += "  Delay " + std::to_string(session.delay()) + " (" +
                                std::to_string(int(session.delay() * dw4::engine::kFieldSeconds * 1000.0 + 0.5)) + " ms)";
                    ImGui::TextColored(colour, "%s", text.c_str());
                    ImGui::End();
                }
                // Play's loading screen (19.5.1): the title's Game Start and the
                // box's Call Digimon run behind it (the F1 menu and the toasts
                // stay above it).
                if (title_driver.covering()) {
                    const ImVec2 display = ImGui::GetIO().DisplaySize;
                    auto* backdrop = ImGui::GetBackgroundDrawList();
                    backdrop->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 255));
                    const char* entering = "Entering the server...";
                    const ImVec2 size = ImGui::CalcTextSize(entering);
                    backdrop->AddText(ImVec2((display.x - size.x) * 0.5f, (display.y - size.y) * 0.5f), IM_COL32(230, 230, 230, 255), entering);
                }
                // Round I1b (19.30.6): the go-along notices, top centre (they
                // take no game button: View opens the F1 menu on the Instance
                // page), a bar shrinking over their 20 s; the own request's line
                // where the notice was. Also over the F1 menu.
                if (online && online->session().in_instance()) {
                    const auto& session = online->session();
                    const double now_s = steady_seconds();
                    const auto notices = session.go_notices(now_s);
                    const std::string line = session.go_line();
                    if (!notices.empty() || !line.empty()) {
                        const bool keyboard = gamepads.empty();
                        const ImVec2 display = ImGui::GetIO().DisplaySize;
                        ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, 12), ImGuiCond_Always, ImVec2(0.5f, 0));
                        ImGui::SetNextWindowBgAlpha(0.75f);
                        ImGui::Begin("##go_along", nullptr,
                                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                        if (!line.empty()) {
                            const auto state = session.go_state();
                            const bool cancellable = state == dw4::game::online::GoState::asked || state == dw4::game::online::GoState::waiting;
                            ImGui::TextUnformatted((line + (cancellable ? (keyboard ? "  F1: cancel" : "  View: cancel") : "")).c_str());
                        }
                        for (const auto* notice : notices) {
                            ImGui::TextUnformatted(session.go_notice_text(*notice).c_str());
                            ImGui::SameLine(0, 24);
                            ImGui::TextDisabled(keyboard ? "F1: answer" : "View: answer");
                            const float left = float(std::clamp(1.0 - (now_s - notice->notice_at) / std::max(1.0, notice->notice_seconds), 0.0, 1.0));
                            ImGui::ProgressBar(left, ImVec2(-1, 3), "");
                        }
                        ImGui::End();
                    }
                }
                // Achievement notices, top right.
                while (!toasts.empty() && std::chrono::steady_clock::now() > toasts.front().until) toasts.pop_front();
                if (!toasts.empty()) {
                    const ImVec2 display = ImGui::GetIO().DisplaySize;
                    ImGui::SetNextWindowPos(ImVec2(display.x - 12, 12), ImGuiCond_Always, ImVec2(1, 0));
                    ImGui::SetNextWindowBgAlpha(0.75f);
                    ImGui::Begin("##achievements", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                    // Long notices (a save path) wrap instead of running off
                    // the window's left edge.
                    ImGui::PushTextWrapPos(std::min(display.x - 40.0f, std::max(360.0f, display.x * 0.4f)));
                    for (const auto& toast : toasts) {
                        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1), "%s", toast.title.c_str());
                        ImGui::TextUnformatted(toast.text.c_str());
                    }
                    ImGui::PopTextWrapPos();
                    ImGui::End();
                }
                // Data Overflow's fade (nothing while no run is near).
                dw4::game::overflow::draw_fade(overflow_switch.black());
                dw4::game::render_ui();

                const bool shoot = (!options.screenshot.empty() &&
                                    ((options.screenshot_frames.empty() && frame == options.smoke_frames) ||
                                     std::find(options.screenshot_frames.begin(), options.screenshot_frames.end(), frame) != options.screenshot_frames.end())) ||
                                   !ui_shot.empty();
                if (shoot) {
                    std::string path = options.screenshot;
                    // (A UI test run's "shot NAME": NAME.bmp beside --screenshot's file.)
                    if (!ui_shot.empty()) {
                        path = (std::filesystem::path(options.screenshot).parent_path() / (ui_shot + ".bmp")).string();
                        ui_shot.clear();
                    } else if (!options.screenshot_frames.empty()) {
                        const auto dot = path.rfind('.');
                        path = path.substr(0, dot) + "_" + std::to_string(frame) + (dot == std::string::npos ? ".bmp" : path.substr(dot));
                    }
                    dw4::game::write_screenshot(path, width, height);
                    std::cout << "screenshot " << path << " at frame " << frame << " stage " << engine.stage_name() << '\n';
                }
                // DW4_DEV_DUMP / DW4_DEV_IDS: every present of the window,
                // then the same draw list as item numbers (not presented).
                if (dev_dumper && dev.dumping(frame)) {
                    std::vector<std::uint8_t> pixels(dev.pictures ? std::size_t(width * height * 4) : 0);
                    if (dev.pictures) glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                    dev_dumper->picture(dev_present, frame, steps, present_alpha, smooth_blending ? &blend_report : nullptr,
                                        engine.stage_name(), width, height, pixels);
                    if (dev.ids && dev.pictures) {
                        // (The picture's own list: its own shadow, not the ghost figures'.)
                        renderer->set_shadow(&shadow);
                        renderer->set_id_pass(true);
                        renderer->render(items, camera, clear, screen, wide, width, height);
                        renderer->set_id_pass(false);
                        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                        std::unordered_map<const void*, std::string> files;
                        for (const auto& file : engine.loaded_files())
                            if (file.document) files[file.document.get()] = file.logical_path;
                        dev_dumper->ids(dev_present, width, height, pixels, items, files);
                    }
                }
                ++dev_present;
#ifdef DW4_GHOST_PROBE
                if (probe_report) glFinish(); // (the GPU's work of this pass counts in the pass time)
#endif
                // The sound queue, the swap (the online session notes the present
                // right after it), the frame time (host_view.hpp).
                dw4::game::finish_present(view, engine, [&] {
                    pass_presented = true;
                    if (online) online->session().presented(steady_seconds());
                });
                present_max = std::max(present_max, view.last_present_seconds * 1000.0);
#ifdef DW4_GHOST_PROBE
                if (probe_report && ghosts.drawn() > 0) {
                    probe_pass_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pass_start).count());
                    probe_figures_ms.push_back(probe_update_ms + probe_draw_ms);
                    const auto costs = ghosts.probe_costs();
                    probe_pose_ms.push_back(costs.pose_ms);
                    probe_traverse_ms.push_back(costs.traverse_ms);
                    probe_items.push_back(double(costs.items));
                    probe_drawn = int(ghosts.drawn());
                    probe_reused += costs.reused;
                    if (int(probe_pass_ms.size()) >= probe_report) {
                        std::printf("[%d] ghost probe: %d figures drawn, %d poses reused in %zu passes, stage %s\n", frame, probe_drawn, probe_reused,
                                    probe_pass_ms.size(), engine.stage_name().c_str());
                        probe_print("pass", probe_pass_ms);
                        probe_print("figures", probe_figures_ms);
                        probe_print("pose", probe_pose_ms);
                        probe_print("traverse", probe_traverse_ms);
                        probe_print("items", probe_items);
                        probe_reused = 0;
                        std::fflush(stdout);
                    }
                } else if (probe_report && probe_ghosts == 0 && engine.presenting() && !engine.loading()) {
                    // (The same pass time without figures: DW4_PROBE_GHOSTS=0.)
                    probe_pass_ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pass_start).count());
                    if (int(probe_pass_ms.size()) >= probe_report) {
                        std::printf("[%d] ghost probe: no figures, %zu passes, stage %s\n", frame, probe_pass_ms.size(), engine.stage_name().c_str());
                        probe_print("pass", probe_pass_ms);
                        std::fflush(stdout);
                    }
                }
#endif
            }
            // (A run without pictures: the pass's end stands for the present, for the input latency of online test runs.)
            if (!draw && online) online->session().presented(steady_seconds());
            if (trace_loadtime && steps > 0) {
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - pass_start).count();
                if (const auto* snapshot = engine.stage_snapshot(); snapshot && snapshot->serial != load_timing.serial) {
                    if (load_timing.serial && !load_timing.control) print_load_timing();
                    load_timing = {snapshot->stage, snapshot->serial, snapshot->frame};
                    load_timing.fast = engine.fast_loading();
                }
                if (load_timing.serial && !load_timing.started) {
                    if (trace_loadtime > 1)
                        std::printf("[%llu] load pass %.2f ms (%s)\n", (unsigned long long)engine.frame(), ms, engine.loader_state().c_str());
                    load_timing.total_ms += ms;
                    load_timing.longest_ms = std::max(load_timing.longest_ms, ms);
                    if (!engine.loading()) load_timing.started = engine.frame();
                }
                if (load_timing.started && !load_timing.control && engine.pad_input_enabled())
                    if (auto* models = engine.get<dw4::engine::ModelCtrlManager>())
                        if (const auto* p = models->player(0); p && p->control_enabled) {
                            load_timing.control = engine.frame();
                            print_load_timing();
                        }
            }
#ifdef DW4_ONLINE_DEV
            // DW4_ONLINE_TEST_STALL=PERIOD_MS:STALL_MS[:PASS_MS] (development
            // builds, the latency scenarios of tools/online_test): this client
            // behaves like a window Windows throttles in the background, its
            // host loop stalled for STALL_MS every PERIOD_MS and each pass
            // taking at least PASS_MS (SEAMLESS.md 6.4, round 62).
            if (online) {
                static const auto stall = [] {
                    std::array<int, 3> v{0, 0, 0};
                    if (const char* text = std::getenv("DW4_ONLINE_TEST_STALL")) std::sscanf(text, "%d:%d:%d", &v[0], &v[1], &v[2]);
                    return v;
                }();
                static auto next_stall = std::chrono::steady_clock::now() + std::chrono::milliseconds(stall[0]);
                if (stall[2] > 0) std::this_thread::sleep_for(std::chrono::milliseconds(stall[2]));
                if (stall[0] > 0 && stall[1] > 0 && std::chrono::steady_clock::now() >= next_stall) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(stall[1]));
                    next_stall = std::chrono::steady_clock::now() + std::chrono::milliseconds(stall[0]);
                }
            }
#endif
            // Frame limit (settings.ini fps_limit): presents are spaced out;
            // the simulation keeps its fixed 60 Hz steps.
            dw4::game::pace_presents(view);
#ifdef DW4_ONLINE_DEV
            // DW4_ONLINE_TEST_PASS_HZ=HZ (development builds, the latency
            // scenarios of tools/online_test): each pass ends on a grid of HZ
            // per second, as a pass that waits for the vsync of a display at
            // HZ (a hidden test client has none; round 62 review: 60.000 Hz
            // beats against the relay's 59.94 Hz).
            if (online) {
                static const double pass_hz = [] {
                    const char* text = std::getenv("DW4_ONLINE_TEST_PASS_HZ");
                    return text ? std::max(0.0, std::atof(text)) : 0.0;
                }();
                if (pass_hz > 0) {
                    static const auto grid = std::chrono::steady_clock::now();
                    const double at = std::chrono::duration<double>(std::chrono::steady_clock::now() - grid).count();
                    const double next = (std::floor(at * pass_hz) + 1.0) / pass_hz;
                    SDL_DelayPrecise(Uint64((next - at) * 1e9));
                }
            }
#endif
            if (online) {
                // online.log: the rates of passes and pictures, vsync, the display, the window's state (round 62 review).
                dw4::game::OnlineRun::HostFacts facts;
                facts.presented = pass_presented;
                if (window) {
                    const auto flags = SDL_GetWindowFlags(window);
                    facts.window = true;
                    facts.vsync = settings.vsync;
                    SDL_GL_GetSwapInterval(&facts.swap_interval);
                    facts.fps_limit = settings.fps_limit;
                    if (const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)))
                        facts.refresh_hz = mode->refresh_rate_numerator > 0 && mode->refresh_rate_denominator > 0
                                               ? double(mode->refresh_rate_numerator) / double(mode->refresh_rate_denominator)
                                               : double(mode->refresh_rate);
                    facts.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
                    facts.minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
                    facts.occluded = (flags & SDL_WINDOW_OCCLUDED) != 0;
                    facts.hidden = (flags & SDL_WINDOW_HIDDEN) != 0;
                }
                online->host_pass(steady_seconds(), facts);
            }
            // DW4_SMOKE_STAGES=n: a test run also ends once the n-th stage has loaded.
            static const int smoke_stages = options.smoke_frames && std::getenv("DW4_SMOKE_STAGES") ? std::atoi(std::getenv("DW4_SMOKE_STAGES")) : 0;
            if (const auto* snapshot = engine.stage_snapshot(); smoke_stages && snapshot && snapshot->serial >= std::uint64_t(smoke_stages))
                running = false;
            if (options.smoke_frames && frame >= options.smoke_frames) {
                if (state_test && state_pass == 1 && state_slots[0]) {
                    // DW4_STATE_FILE=path: the state goes through a file first.
                    if (const char* file = std::getenv("DW4_STATE_FILE")) {
                        write_state_file(file, *state_slots[0]);
                        state_slots[0] = read_state_file(file);
                        std::cout << "state file: " << (state_slots[0] ? "read back" : "unreadable") << '\n';
                    }
                    if (state_slots[0]) load_state(0);
                    state_pass = 2;
                } else {
                    running = false;
                }
            }
        }
        // (The last load of the run, without player control so far.)
        if (trace_loadtime && load_timing.serial && !load_timing.control) print_load_timing();
        if (state_test)
            std::cout << "state test: " << (state_mismatch < 0 ? "identical after loading" : "differs from frame " + std::to_string(state_mismatch))
                      << " (" << state_fingerprints.size() << " frames compared)\n";
        if (settings_changed && !options.smoke_frames) settings.save();
        save_profile();
        if (options.smoke_frames && achievements_on())
            std::printf("achievements: %zu unlocked (%zu randomizer), kills %lld, in-game minutes %lld\n", achievements.profile().unlocked.size(),
                        achievements.profile().unlocked_randomizer.size(), (long long)achievements.profile().stat("kills"),
                        (long long)achievements.profile().stat("play_minutes"));
        if (options.smoke_frames && discord)
            std::printf("presence: %s, %llu activity updates sent\n", discord->status().c_str(), (unsigned long long)discord->sent_count());
        // Without pictures the last frame's draw list is collected once here
        // (a second traversal at the same time changes nothing), for the
        // draw count and DW4_DRAW_DUMP.
        if (!draw) engine.render(items, camera, clear, &shadow);
        std::cout << "stage " << engine.stage_name() << ", frame " << engine.frame() << ", draws " << items.size() << '\n';
        // DW4_TRACE_TRAVERSAL_TIME (development): traversal times; DW4_CAM_CHECK
        // (test runs): the one-player camera check of the per-slot cameras.
        if (std::getenv("DW4_TRACE_TRAVERSAL_TIME")) {
            static constexpr const char* kNames[4] = {"render (sim+picture)", "traverse_scene", "union sim pass", "picture-only pass"};
            for (int i = 0; i < 4; ++i)
                if (engine.traversal_count[std::size_t(i)])
                    std::printf("traversal %s: %llu passes, %.1f us each, %.3f s total\n", kNames[i],
                                (unsigned long long)engine.traversal_count[std::size_t(i)],
                                double(engine.traversal_ns[std::size_t(i)]) / 1000.0 / double(engine.traversal_count[std::size_t(i)]),
                                double(engine.traversal_ns[std::size_t(i)]) * 1e-9);
        }
        if (std::getenv("DW4_TRACE_TRAVERSAL_TIME"))
            if (auto* models = engine.get<dw4::engine::ModelCtrlManager>())
                for (int slot = 0; slot < 4; ++slot)
                    if (const auto* p = models->player(slot); p && p->node) {
                        dw4::engine::FrameCamera own;
                        if (auto* cameras = engine.get<dw4::engine::CameraManager>()) cameras->frame_camera(engine, own, slot);
                        dw4::engine::Mat4 inverse;
                        dw4::engine::invert(own.view, inverse);
                        std::printf("player %d at (%.1f, %.1f, %.1f), its camera at (%.1f, %.1f, %.1f)\n", slot + 1, p->node->matrix(3, 0),
                                    p->node->matrix(3, 1), p->node->matrix(3, 2), inverse(3, 0), inverse(3, 1), inverse(3, 2));
                    }
        if (std::getenv("DW4_CAM_CHECK"))
            if (auto* cameras = engine.get<dw4::engine::CameraManager>())
                std::printf("camera check: slot 0's own camera differed from the shared one in %llu of %llu frames\n",
                            (unsigned long long)cameras->check_mismatches(), (unsigned long long)cameras->check_frames());
        if (net.active()) {
            std::cout << "online: final fingerprint " << std::hex << engine.fingerprint() << std::dec
                      << (net.desync() ? " (desync at step " + std::to_string(*net.desync()) + ")" : "") << '\n';
            // The owner keeps what the session saved on its card.
            if (options.smoke_frames) own_card_name.clear();
            end_session();
        }
        if (online) {
            std::printf("online: final mode %s, instance %u visit %u tick %u, slot %d of %x, net fingerprint %016llx\n",
                        dw4::game::online::mode_name(online->session().mode()), online->session().inst(), online->session().visit(),
                        online->session().tick(), online->session().own_slot(), online->session().present(),
                        static_cast<unsigned long long>(online->link().net_fingerprint()));
            {
                const auto& t = online->session().stats().total();
                std::printf("online: final latency samples %llu dropped %llu exec avg %.1f ms p50 %.1f p95 %.1f max %.1f, present avg %.1f ms, "
                            "steps %.2f, delay %d, backlogs %zu, starved %zu\n",
                            static_cast<unsigned long long>(t.samples), static_cast<unsigned long long>(t.dropped), t.exec_avg() * 1000.0,
                            t.exec_quantile(0.5) * 1000.0, t.exec_quantile(0.95) * 1000.0, t.exec_max * 1000.0, t.present_avg() * 1000.0,
                            t.steps_avg(), online->session().delay(), online->session().backlog_runs(), online->session().starved_steps());
            }
            leave_room();
            online_achievements.reset();
            online.reset();
            std::error_code ignored;
            if (!session_cards.empty()) std::filesystem::remove_all(session_cards, ignored);
        }
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
        // 16-bit stereo PCM WAV at the output rate.
        auto write_wav = [](const char* path, const std::vector<std::int16_t>& samples) {
            std::ofstream wav(path, std::ios::binary);
            const std::uint32_t rate = dw4::engine::SoundManager::kOutputRate;
            const std::uint32_t bytes = std::uint32_t(samples.size() * sizeof(std::int16_t));
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
            wav.write(reinterpret_cast<const char*>(samples.data()), std::streamsize(bytes));
        };
        if (audio_dump_path) {
            // Everything mixed.
            write_wav(audio_dump_path, audio_dump);
            std::cout << "audio dump " << audio_dump_path << " (" << audio_dump.size() / 2 << " frames)\n";
            // Test tool: DW4_AUDIO_DUMP_STREAMS=FILE.wav, the streams' part of it (SoundManager::stream_part).
            if (const char* part_path = std::getenv("DW4_AUDIO_DUMP_STREAMS"))
                if (const auto* sound = engine.get<dw4::engine::SoundManager>()) write_wav(part_path, sound->stream_part());
        }
        // The session's notes of original behaviour the port lacks (none: no list).
        if (!engine.notes().empty()) std::cout << "unimplemented:\n";
        for (const auto& note : engine.notes()) std::cout << "  " << note.text << " x" << note.count << '\n';
        for (auto* pad : gamepads) SDL_CloseGamepad(pad);
    }
    if (draw) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
    if (audio) SDL_DestroyAudioStream(audio);
    if (context) SDL_GL_DestroyContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

// --save-info FILE / --import-save FILE[:BOXES]: the preview of a GC save
// (any container save_import.hpp reads), and the import into this run's
// store (--card-dir / --save-file / the player's save files, slot A or B),
// backing up the current save first. No game data, no window.
int save_command(const Options& options, dw4::game::Settings& settings) {
    const auto& path = options.save_info.empty() ? options.import_save : options.save_info;
    std::string error;
    const auto source = dw4::save::load_import(path, error);
    if (!source) {
        std::cerr << "dw4_game: " << path.string() << ": " << error << '\n';
        return 1;
    }
    const auto saved = dw4::save::save_time_text(source->gci);
    std::cout << "save: " << source->container << " (" << source->detail << (saved.empty() ? "" : ", saved " + saved) << ") "
              << source->file.generic_string() << '\n';
    for (const auto& line : import_preview(*source)) std::cout << "  " << line << '\n';
    if (options.import_save.empty()) return 0;
    const auto moves = dw4::save::parse_box_moves(options.import_boxes);
    if (!moves) {
        std::cerr << "dw4_game: unknown boxes '" << options.import_boxes << "'\n";
        return 1;
    }
    // A run without --smoke-test: the player's store (with the one-time
    // migration first, so an old save is not left behind).
    const auto choice = dw4::game::choose_save_store(options.card_dir, options.save_file, false, settings, program_dir());
    if (choice.settings_changed) settings.save();
    for (const auto& note : choice.notes) std::cout << "saves: " << note << '\n';
    for (const auto& problem : choice.problems) std::cout << "saves: " << problem << '\n';
    const auto outcome = dw4::game::import_save(choice.store, options.import_slot, *source, *moves);
    std::cout << (outcome.ok ? "import: " : "import failed: ") << outcome.message << '\n';
    if (!outcome.ok) return 1;
    if (const auto now = choice.store.read(options.import_slot))
        if (const auto boxes = dw4::save::summarize_save(*now))
            for (std::size_t i = 0; i < boxes->size(); ++i)
                std::cout << "  now box " << i + 1 << ": " << dw4::save::describe_box((*boxes)[i]) << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // Test runs and the converter never open dialogs (they run unattended).
    bool interactive = !std::getenv("DW4_NO_ERROR_BOX");
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--smoke-test" || std::string_view(argv[i]) == "--convert-rvz" ||
            std::string_view(argv[i]) == "--import-save" || std::string_view(argv[i]) == "--save-info" ||
            std::string_view(argv[i]) == "--export-presence-art")
            interactive = false;
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
            else if (arg == "--save-file") options.save_file = next();
            else if (arg == "--save-info") options.save_info = next();
            else if (arg == "--import-save") {
                // FILE[:BOXES]: a trailing ":all" / ":1,3" / ":1>2" (not a drive letter's colon).
                std::string value = next();
                if (const auto colon = value.rfind(':'); colon != std::string::npos && colon > 1 &&
                                                         dw4::save::parse_box_moves(value.substr(colon + 1))) {
                    options.import_boxes = value.substr(colon + 1);
                    value.resize(colon);
                }
                options.import_save = value;
            } else if (arg == "--import-slot") {
                const std::string value = next();
                if (value != "A" && value != "B" && value != "a" && value != "b") throw std::runtime_error("--import-slot takes A or B");
                options.import_slot = (value == "B" || value == "b") ? 1 : 0;
            }
            else if (arg == "--host") options.net_host = true;
            else if (arg == "--join") {
                options.net_join = next();
                if (const auto colon = options.net_join.rfind(':'); colon != std::string::npos) {
                    options.net_port = std::stoi(options.net_join.substr(colon + 1));
                    options.net_join.resize(colon);
                }
            } else if (arg == "--online-login") options.online.user = next();
            else if (arg == "--online-play") {
                // (Round I1a: --online-play [BOX] replaces --online-create / --online-join.)
                options.online.play = true;
                if (i + 1 < argc && argv[i + 1][0] >= '1' && argv[i + 1][0] <= '4' && argv[i + 1][1] == 0) options.online.box = std::stoi(next());
            }
            else if (arg == "--online-camera") options.online.camera = std::clamp(std::stoi(next()), 0, 1);
            else if (arg == "--online-light") options.online.light = std::clamp(std::stoi(next()), 0, 1);
            else if (arg == "--online-fast") options.online.fast = true;
            else if (arg == "--online-difficulty") {
                // (Development builds only, W12's test runs: it answers the
                // Game Start's difficulty dialog. A release build ignores it.)
                const int difficulty = std::clamp(std::stoi(next()), 0, 2);
#ifdef DW4_ONLINE_DEV
                options.online.difficulty = difficulty;
#else
                (void)difficulty;
                std::cout << "--online-difficulty is for development builds; ignored\n";
#endif
            }
            else if (arg == "--online-calibrate") options.online_calibrate = std::max(60, std::stoi(next()));
            else if (arg == "--online-calibrate-stage") options.online_calibrate_stage = next();
            else if (arg == "--port") options.net_port = std::stoi(next());
            else if (arg == "--players") options.net_players = std::stoi(next());
            else if (arg == "--net-delay") options.net_delay = std::stoi(next());
            else if (arg == "--convert-rvz") {
                options.convert_from = next();
                options.convert_to = next();
            }
            else if (arg == "--stage") {
                options.stage = next();
                options.stage_given = true;
            }
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
            else if (arg == "--online-script") options.online_script = next();
            else if (arg == "--online-card") options.online_card = next();
            else if (arg == "--online-ui") options.online_ui = next();
            else if (arg == "--online-folder") options.online_folder = next();
            else if (arg == "--load-state") options.load_state = next();
            else if (arg == "--export-presence-art") options.export_art = next();
            else if (arg == "--overflow-run") {
                // KEY=VALUE tokens up to the next option (overflow_run.hpp).
                options.overflow_given = true;
                while (i + 1 < argc && std::string_view(argv[i + 1]).rfind("--", 0) != 0) options.overflow_run.emplace_back(argv[++i]);
            }
            else if (arg == "--loading") {
                const std::string value = next();
                if (value != "original" && value != "fast") throw std::runtime_error("--loading takes original or fast");
                options.fast_loading = value == "fast";
            }
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
                             "         [--screenshot-frames N,M,...] [--input SCRIPT] [--card-dir GCI_ROOT]\n"
                             "         [--save-file FILE] [--load-state FILE] [--loading original|fast]\n"
                             "         [--export-presence-art FOLDER] (Discord art asset PNGs from your game data)\n"
                             "         [--overflow-run form=p_agumon field=stagbs02 seed=N [at=F] [frames=N] ...] (Data Overflow)\n"
                             "dw4_game --save-info FILE|FOLDER\n"
                             "dw4_game --import-save FILE|FOLDER[:all|BOXES] [--import-slot A|B] [--card-dir GCI_ROOT | --save-file FILE]\n"
                             "         BOXES: 1,3 (the same boxes) or 1>2,4>1 (box 1 into box 2, ...); .gci .gcs .sav .raw .gcp\n";
                return 0;
            } else throw std::runtime_error("unknown option " + arg);
        }
        if (!options.convert_from.empty()) {
            // --convert-rvz IN OUT: write the plain disc image and exit.
            std::atomic<double> progress{0.0};
            std::atomic<bool> done{false};
            std::string problem;
            std::thread worker([&] {
                problem = dw4::game::convert_rvz(options.convert_from, options.convert_to, &progress, nullptr);
                done.store(true);
            });
            int shown = -1;
            while (!done.load()) {
                const int percent = int(progress.load() * 100.0);
                if (percent / 10 != shown / 10) std::cout << percent << "%\n" << std::flush;
                shown = percent;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            worker.join();
            if (!problem.empty()) throw std::runtime_error(problem);
            std::cout << "wrote " << options.convert_to.string() << '\n';
            return 0;
        }
        auto settings = dw4::game::Settings::load();
        if (!options.save_info.empty() || !options.import_save.empty()) return save_command(options, settings);
        if (options.data.empty()) options.data = settings.data;
        // First start: pick and check the disc image or extraction, then
        // remember it.
        if (options.data.empty() && !options.smoke_frames) {
            for (;;) {
                auto chosen = choose_game_data();
                if (chosen.empty()) return 1;
                std::string problem;
                chosen = resolve_rvz(chosen, problem);
                if (problem.empty()) problem = check_game_data(chosen);
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
        {
            std::string problem;
            options.data = resolve_rvz(options.data, problem);
            if (problem.empty()) problem = check_game_data(options.data);
            if (!problem.empty()) throw std::runtime_error(problem);
        }
        if (!options.export_art.empty()) return dw4::game::export_presence_art(options.data, options.export_art);
        return run(options, settings);
    } catch (const std::exception& error) {
        std::cerr << "dw4_game: " << error.what() << '\n';
        // Without a console the reason shows in a message box.
        if (interactive) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 4 PC", error.what(), nullptr);
        return 1;
    }
}
