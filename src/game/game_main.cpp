// Native Digimon World 4 runtime: SDL3 window, OpenGL renderer and the
// reconstructed engine in dw4::engine. Original data is read from --data.
#include "gl_renderer.hpp"
#include "interpolation.hpp"
#include "mods.hpp"
#include "netplay.hpp"
#include "movie_decoder.hpp"
#include "rvz.hpp"
#include "settings.hpp"
#include "campaign.hpp"
#include "test_path.hpp"
#include "texture_pack.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/disc.hpp"
#include "dw4/engine/state_archive.hpp"
#include <algorithm>
#include <cmath>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <array>
#include <atomic>
#include <thread>
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
    bool stage_given{}; // --stage on the command line
    int smoke_frames = 0;
    std::string screenshot;
    std::vector<int> screenshot_frames;
    std::string input_script;
    std::filesystem::path load_state; // --load-state FILE: start from a save state file
    bool overlay = true;
    std::vector<float> debug_camera; // eye xyz, target xyz
    std::vector<std::pair<int, int>> flags;
    std::filesystem::path card_dir; // memory card GCI folders (Card A / Card B)
    std::filesystem::path convert_from, convert_to; // --convert-rvz IN OUT
    // Online co-op: --host (with --players, --port, --net-delay) or --join ADDRESS[:PORT].
    bool net_host{};
    std::string net_join;
    int net_port = 52040, net_players = 2, net_delay = 3;
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
// (button: A,B,X,Y,START,UP,DOWN,LEFT,RIGHT,Z,L,R or STICK:x,y with raw stick
// values, several joined by '+'; player 1-4, default 1) and "frame GOTO x z [duration]",
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
            // The button may join several with '+'; STICK:x,y holds the main
            // stick at the raw device values x,y (-128..127, y up, as
            // work/dref's dtm.py records them), which the SDK clamp turns into
            // the stick the game reads (gamecube_stick). It replaces the
            // direction buttons' full deflection.
            Press press{frame, 0, duration, std::clamp(player - 1, 0, 3)};
            for (std::size_t begin = 0; begin <= name.size();) {
                const std::size_t end = std::min(name.find('+', begin), name.size());
                const std::string part = name.substr(begin, end - begin);
                begin = end + 1;
                namespace b = dw4::pad_button;
                int x = 0, y = 0;
                if (part.rfind("STICK:", 0) == 0 && std::sscanf(part.c_str() + 6, "%d,%d", &x, &y) == 2)
                    press.stick = dw4::gamecube_stick(std::int8_t(std::clamp(x, -128, 127)),
                                                      std::int8_t(std::clamp(y, -128, 127)));
                else if (part == "A") press.bit |= b::a;
                else if (part == "B") press.bit |= b::b;
                else if (part == "X") press.bit |= b::x;
                else if (part == "Y") press.bit |= b::y;
                else if (part == "START") press.bit |= b::start;
                else if (part == "UP") press.bit |= b::up;
                else if (part == "DOWN") press.bit |= b::down;
                else if (part == "LEFT") press.bit |= b::left;
                else if (part == "RIGHT") press.bit |= b::right;
                else if (part == "Z") press.bit |= b::z;
                else if (part == "L") press.bit |= b::l;
                else if (part == "R") press.bit |= b::r;
            }
            presses.push_back(press);
        }
    }
    struct Send { int frame; std::string category, command, argument; int row = -1; };
    std::vector<Send> sends;
    struct Press { int frame; std::uint32_t bit; int duration; int slot; std::optional<dw4::PadStick> stick; };
    std::vector<Press> presses;
    // The STICK of the last press holding one at this frame on the slot.
    std::optional<dw4::PadStick> stick(int frame, int slot = 0) const {
        std::optional<dw4::PadStick> result;
        for (const auto& p : presses)
            if (p.stick && p.slot == slot && frame >= p.frame && frame < p.frame + p.duration) result = p.stick;
        return result;
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

// A key or controller button of the controls tables waiting for input.
struct KeyCapture {
    std::size_t button{};
    std::size_t slot{};
    bool gamepad{};
};

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

// Online co-op card handling: the GCI files of a card folder, a session copy
// of the shared cards, and the owner's copy written back at the end.
std::vector<dw4::game::CardFile> read_card(const std::filesystem::path& folder) {
    std::vector<dw4::game::CardFile> files;
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error)) return files;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (!entry.is_regular_file(error)) continue;
        std::ifstream in(entry.path(), std::ios::binary);
        files.push_back({entry.path().filename().string(), std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), {})});
    }
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return files;
}

void write_card(const std::filesystem::path& folder, const std::vector<dw4::game::CardFile>& files) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    for (const auto& file : files) {
        if (file.name.find_first_of("/\\:") != std::string::npos || file.name == ".." || file.name == ".") continue;
        std::ofstream out(folder / file.name, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(file.bytes.data()), std::streamsize(file.bytes.size()));
    }
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

int run(const Options& options, dw4::game::Settings& settings) {
    // The engine is replaced in place when a save state loads (same storage,
    // so `engine` keeps naming the running one).
    std::optional<dw4::engine::Engine> engine_storage;
    engine_storage.emplace(options.data);
    auto& engine = *engine_storage;
    // Memory cards: --card-dir, DW4_CARD_DIR, else saves/GC/USA next to the
    // program. Dolphin's GC/USA folder works too (same GCI files).
    // settings.ini [data] cards may point at another GCI root (e.g. Dolphin's
    // GC/USA folder to continue its saves); test runs ignore it.
    if (!options.card_dir.empty()) engine.set_card_root(options.card_dir);
    else if (const char* dir = std::getenv("DW4_CARD_DIR")) engine.set_card_root(dir);
    else if (!options.smoke_frames && !settings.cards.empty()) engine.set_card_root(settings.cards);
    else if (const char* base = SDL_GetBasePath()) engine.set_card_root(std::filesystem::path(base) / "saves" / "GC" / "USA");
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
    // Rebalancing / randomizer (settings.ini [gameplay]; test runs:
    // DW4_GAMEPLAY=exp,dealt,taken,seed).
    auto apply_gameplay = [&](dw4::engine::Cheats& cheats) {
        if (options.smoke_frames) {
            if (const char* text = std::getenv("DW4_GAMEPLAY")) {
                unsigned seed = 0;
                std::sscanf(text, "%d,%d,%d,%u", &cheats.exp_rate, &cheats.damage_dealt, &cheats.damage_taken, &seed);
                cheats.drop_seed = seed;
            }
            return;
        }
        cheats.exp_rate = settings.exp_rate;
        cheats.damage_dealt = settings.damage_dealt;
        cheats.damage_taken = settings.damage_taken;
        cheats.drop_seed = settings.drop_seed;
    };
    apply_gameplay(engine.cheats());
    // DW4_PLAYERS=n: test runs start with n joined players.
    if (const char* count = std::getenv("DW4_PLAYERS"))
        if (auto* sys = engine.get<dw4::engine::GameSystem>())
            for (int i = 0; i < std::clamp(std::atoi(count), 0, 4); ++i) sys->players[std::size_t(i)].joined = true;
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
    std::string first_stage = !options.smoke_frames && settings.skip_intro && !options.stage_given ? std::string("stagis10") : options.stage;
    // Online co-op: wait in the lobby, then start every machine the same way
    // with the shared cards.
    dw4::game::NetSession net;
    std::filesystem::path session_cards, own_card_folder;
    std::string own_card_name; // "Card A" (host) or "Card B" (first guest) in the session copy
    if (options.net_host || !options.net_join.empty()) {
        std::string error;
        const auto real_cards = engine.card_root();
        const auto my_card = read_card(real_cards / "Card A");
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
        engine.cheats().god = started.cheats & 1;
        engine.cheats().infinite_mp = started.cheats & 2;
        engine.cheats().one_hit = started.cheats & 4;
        engine.cheats().exp_rate = started.exp_rate;
        engine.cheats().damage_dealt = started.damage_dealt;
        engine.cheats().damage_taken = started.damage_taken;
        engine.cheats().drop_seed = started.drop_seed;
        // Every machine plays from the same card copies.
        session_cards = std::filesystem::temp_directory_path() /
                        ("dw4_session_" + std::to_string(net.local_slot()) + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        write_card(session_cards / "Card A", started.card_a);
        write_card(session_cards / "Card B", started.card_b);
        if (net.local_slot() == 0) own_card_name = "Card A";
        else if (net.local_slot() == 1) own_card_name = "Card B";
        own_card_folder = real_cards / "Card A";
        engine.set_card_root(session_cards);
        // The shared copies in the usual slots on every machine.
        for (int slot = 0; slot < 2; ++slot) engine.set_card_setting(slot, {});
        std::cout << "online: started as player " << net.local_slot() + 1 << " of " << started.players << ", delay " << started.delay << '\n';
    }
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
    // Output samples of the step that reached `frame`: one NTSC field
    // (800.8 at 48 kHz), so music and movie sound keep pace with world time.
    auto step_samples = [](std::uint64_t frame) {
        constexpr std::uint64_t rate = dw4::engine::SoundManager::kOutputRate;
        const auto upto = [](std::uint64_t f) { return f * rate * 1001 / 60000; };
        return int(upto(frame) - upto(frame ? frame - 1 : 0));
    };
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
            renderer.clear_cache();
        };
        if (!options.smoke_frames) {
            configure_pack();
            renderer.set_texture_pack(&texture_pack);
        } else if (std::getenv("DW4_TEXTURE_PACK") || std::getenv("DW4_DUMP_TEXTURES")) {
            // Test runs: DW4_TEXTURE_PACK=dir and / or DW4_DUMP_TEXTURES=dir.
            const char* pack = std::getenv("DW4_TEXTURE_PACK");
            const char* dump = std::getenv("DW4_DUMP_TEXTURES");
            texture_pack.configure(pack ? pack : "", dump ? dump : "");
            renderer.set_texture_pack(&texture_pack);
        }
        std::vector<SDL_Gamepad*> gamepads;
        ScriptedInput scripted;
        if (!options.input_script.empty()) scripted.load(options.input_script);
        std::vector<dw4::engine::DrawItem> items;
        dw4::engine::ShadowFrame shadow;
        dw4::engine::FrameCamera camera;
        dw4::engine::Vec4f clear{};
        std::array<dw4::engine::PadSample, 4> pads{};
        std::string rendered_stage;
        std::uint64_t rendered_serial{};
        std::unique_ptr<dw4::game::MovieDecoder> movie_decoder;
        std::uint32_t movie_serial = 0;
        std::string movie_stage; // stage the decoder's movie played in
        // DW4_OPEN_MENU=1 opens the F1 menu at start (menu screenshots).
        bool running = true, show_menu = std::getenv("DW4_OPEN_MENU") != nullptr;
        // Save states (this session): four slots, F5 saves and F9 loads slot 1.
        std::array<std::optional<SaveState>, 4> state_slots;
        std::vector<LoggedStep> input_log;
        bool load_step_drawn = false, last_step_loaded = false;
        std::uint64_t log_serial = 0;
        std::string state_message;
        auto message_until = std::chrono::steady_clock::now();
        // DW4_STATE_TEST=N (test runs): save at frame N, run to the end, load
        // and run again, comparing every frame's fingerprint.
        const int state_test = options.smoke_frames && std::getenv("DW4_STATE_TEST") ? std::atoi(std::getenv("DW4_STATE_TEST")) : 0;
        int state_pass = 1, state_mismatch = -1;
        bool state_test_save = false; // saves after the picture, like F5 or the menu
        std::vector<std::uint64_t> state_fingerprints;
        bool settings_changed = false;
        std::optional<KeyCapture> capture;
        auto next_present = std::chrono::steady_clock::now();
        std::uint64_t net_submitted = std::uint64_t(net.active() ? net.start().delay : 0);
        std::vector<dw4::engine::DrawItem> catch_up_items;
        dw4::engine::FrameCamera catch_up_camera;
        dw4::engine::Vec4f catch_up_clear{};
        dw4::engine::ShadowFrame catch_up_shadow;
        // Smooth motion above 60 presents per second (settings.ini smooth_motion).
        dw4::game::MotionInterpolator interpolator;
        double present_interval = 1.0 / 60.0;
        std::array<float, 240> frame_times{}; // ms per presented frame (F1 graph)
        std::size_t frame_time_at = 0;
        auto last_present = std::chrono::steady_clock::now();
        int frame = 0;
        const int trace_player = std::getenv("DW4_TRACE_PLAYER") ? std::atoi(std::getenv("DW4_TRACE_PLAYER")) : 0;
        // DW4_TRACE_FINGERPRINT=N prints the state fingerprint every N frames:
        // two runs of the same script must print the same lines (determinism).
        const int trace_fingerprint = std::getenv("DW4_TRACE_FINGERPRINT") ? std::atoi(std::getenv("DW4_TRACE_FINGERPRINT")) : 0;
        auto last = std::chrono::steady_clock::now();
        double accumulator = 0;
        const auto card_root = engine.card_root();
        // State files live in saves/states next to the program (not in test runs).
        std::filesystem::path state_dir;
        if (const char* program = SDL_GetBasePath()) state_dir = std::filesystem::path(program) / "saves" / "states";
        auto state_path = [&](std::size_t slot) { return state_dir / ("state" + std::to_string(slot + 1) + ".dw4state"); };
        if (!options.smoke_frames && !state_dir.empty())
            for (std::size_t slot = 0; slot < state_slots.size(); ++slot) state_slots[slot] = read_state_file(state_path(slot));
        auto can_save = [&]() -> std::string {
            if (net.active()) return "Save states are off in online sessions.";
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
            if (net.active()) {
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
            engine.set_card_root(card_root);
            engine.restore(state.snapshot);
            std::vector<dw4::engine::DrawItem> replay_items;
            dw4::engine::FrameCamera replay_camera;
            dw4::engine::Vec4f replay_clear{};
            dw4::engine::ShadowFrame replay_shadow;
            if (state.load_step_drawn) engine.render(replay_items, replay_camera, replay_clear, &replay_shadow);
            std::vector<std::int16_t> scratch(std::size_t(kAudioFrameSamples + 1) * 2);
            auto* sound = engine.get<dw4::engine::SoundManager>();
            for (const auto& step : state.inputs) {
                engine.step(step.pads);
                if (sound) sound->render(scratch.data(), std::size_t(step_samples(engine.frame()))); // music position
                if (step.drawn) engine.render(replay_items, replay_camera, replay_clear, &replay_shadow);
            }
            const bool exact = engine.fingerprint() == state.fingerprint;
            input_log = state.inputs;
            load_step_drawn = state.load_step_drawn;
            last_step_loaded = false;
            log_serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
            frame = state.frame;
            accumulator = 0;
            renderer.clear_cache();
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
            const auto real_cards = card_root;
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
            session_cards = std::filesystem::temp_directory_path() /
                            ("dw4_session_" + std::to_string(net.local_slot()) + "_" +
                             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            write_card(session_cards / "Card A", started.card_a);
            write_card(session_cards / "Card B", started.card_b);
            own_card_name = net.local_slot() == 0 ? "Card A" : net.local_slot() == 1 ? "Card B" : "";
            own_card_folder = real_cards / "Card A";
            engine.set_card_root(session_cards);
            engine.request_stage(started.stage);
            net_submitted = std::uint64_t(started.delay);
            input_log.clear();
            log_serial = 0;
            accumulator = 0;
            renderer.clear_cache();
            interpolator.reset();
            rendered_stage.clear();
            state_message = "Online: player " + std::to_string(net.local_slot() + 1) + " of " + std::to_string(started.players);
            message_until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        };
        auto end_session = [&] {
            if (!own_card_name.empty()) write_card(own_card_folder, read_card(session_cards / own_card_name));
            std::error_code error;
            if (!session_cards.empty()) std::filesystem::remove_all(session_cards, error);
            session_cards.clear();
            own_card_name.clear();
            net.close();
            engine.set_card_root(card_root);
            apply_gameplay(engine.cheats());
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
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                // A key binding being edited takes the next key (F1 still
                // closes the menu).
                // A controller binding being edited takes the next button or
                // trigger pull.
                if (capture && capture->gamepad &&
                    (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                     (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && event.gaxis.value > 16000 &&
                      (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)))) {
                    const dw4::game::PadInput input =
                        event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN
                            ? dw4::game::PadInput{dw4::game::PadInput::Kind::button, int(event.gbutton.button)}
                            : dw4::game::PadInput{dw4::game::PadInput::Kind::axis, int(event.gaxis.axis)};
                    settings.pad[capture->button][capture->slot] = input;
                    for (std::size_t b = 0; b < settings.pad.size(); ++b)
                        for (std::size_t k = 0; k < settings.pad[b].size(); ++k)
                            if ((b != capture->button || k != capture->slot) && settings.pad[b][k] == input)
                                settings.pad[b][k] = {};
                    capture.reset();
                    settings_changed = true;
                    continue;
                }
                if (capture && !capture->gamepad && event.type == SDL_EVENT_KEY_DOWN && event.key.scancode != SDL_SCANCODE_F1) {
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
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F5) save_state(0);
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_F9) load_state(0);
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
            // Controller buttons from settings.ini [gamepad] (the GameCube
            // layout by default), sticks fixed: left stick moves, the right
            // stick is the C stick.
            auto read_gamepad = [&settings](SDL_Gamepad* pad, std::uint32_t& buttons, dw4::PadStick& left, dw4::PadStick& right) {
                for (std::size_t b = 0; b < settings.pad.size(); ++b)
                    for (const auto& input : settings.pad[b]) {
                        const bool down =
                            input.kind == dw4::game::PadInput::Kind::button
                                ? SDL_GetGamepadButton(pad, SDL_GamepadButton(input.code))
                                : input.kind == dw4::game::PadInput::Kind::axis && SDL_GetGamepadAxis(pad, SDL_GamepadAxis(input.code)) > 16000;
                        if (down) buttons |= dw4::game::pad_key_bit(dw4::game::PadKey(b));
                    }
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
            // Fixed-step simulation at the NTSC field rate (59.94 Hz, one
            // original frame per field; Engine::step adds a field of time).
            const auto now = std::chrono::steady_clock::now();
            accumulator += std::chrono::duration<double>(now - last).count();
            last = now;
            int steps = options.smoke_frames ? (net.in_lobby() ? 0 : 1) : 0;
            if (options.smoke_frames && net.in_lobby()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
            // DW4_MENU_HOST=port / DW4_MENU_JOIN=port (test runs): the Online
            // tab's Host / Join at frame 100, on loopback.
            if (options.smoke_frames && frame == 100 && !net.active() && !net.in_lobby()) {
                std::string error;
                if (const char* port = std::getenv("DW4_MENU_HOST"))
                    net.host(std::atoi(port), 2, 3, data_hash(engine.resources()), error, true);
                else if (const char* join_port = std::getenv("DW4_MENU_JOIN"))
                    net.join("127.0.0.1", std::atoi(join_port), data_hash(engine.resources()), read_card(card_root / "Card A"), error);
                if (!error.empty()) std::cout << "online: " << error << '\n';
            }
            if (net.in_lobby()) {
                dw4::game::SessionStart start;
                start.stage = "stagis10"; // everyone meets at the title
                start.cheats = std::uint8_t((engine.cheats().god ? 1 : 0) | (engine.cheats().infinite_mp ? 2 : 0) |
                                            (engine.cheats().one_hit ? 4 : 0));
                start.card_a = read_card(card_root / "Card A");
                start.exp_rate = engine.cheats().exp_rate;
                start.damage_dealt = engine.cheats().damage_dealt;
                start.damage_taken = engine.cheats().damage_taken;
                start.drop_seed = engine.cheats().drop_seed;
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
            const bool paused = (show_menu && settings.pause_in_menu && !options.smoke_frames && !net.active()) || net.in_lobby();
            if (net.active())
                if (auto message = net.take_message(); !message.empty()) {
                    std::cout << "online: " << message << '\n';
                    state_message = message;
                    message_until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
                }
            if (paused) accumulator = 0;
            if (!options.smoke_frames && !paused) {
                while (accumulator >= dw4::engine::kFieldSeconds && steps < 4) { accumulator -= dw4::engine::kFieldSeconds; ++steps; }
                if (steps == 4) accumulator = 0;
            }
            // Online: a step runs once every player's input for it is here.
            std::uint64_t net_frame = 0;
            for (int i = 0; i < steps; ++i) {
                if (net.active()) {
                    net_frame = engine.frame() + 1;
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
                if (i > 0) {
                    engine.render(catch_up_items, catch_up_camera, catch_up_clear, &catch_up_shadow);
                    if (last_step_loaded) load_step_drawn = true;
                    else if (!input_log.empty()) input_log.back().drawn = true;
                }
                for (const auto& send : scripted.sends) {
                    if (send.frame != frame) continue;
                    dw4::engine::Packet packet;
                    if (!send.argument.empty()) dw4::engine::parse_packet_arguments(send.argument, packet);
                    engine.messenger().send(engine, send.category, std::nullopt, send.row, send.command, packet);
                }
                std::uint32_t input = held | scripted.held(frame);
                float stick_x = sx, stick_y = sy;
                if (const auto* g = scripted.active_goto(frame); g && g->seek && g->target == "campaign" && stick_x == 0 && stick_y == 0) {
                    // SEEK campaign: the story from a new game (src/game/campaign.cpp).
                    static dw4::game::CampaignBot campaign;
                    auto* models = engine.get<dw4::engine::ModelCtrlManager>();
                    auto* cameras = engine.get<dw4::engine::CameraManager>();
                    const auto* p = models ? models->player(0) : nullptr;
                    const auto out = campaign.update(engine, p, frame);
                    input |= out.buttons;
                    if (out.walk && p && p->node && cameras) {
                        const float dx = out.x - p->node->matrix(3, 0), dz = out.z - p->node->matrix(3, 2);
                        if (dx * dx + dz * dz > out.reach) {
                            dw4::engine::FrameCamera camera;
                            cameras->frame_camera(engine, camera);
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
                if (const auto stick = scripted.stick(frame)) pads[0].left = *stick;
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
                    if (const auto stick = scripted.stick(frame, int(i))) pads[i].left = *stick;
                    pads[i].right = scale(slot_right[i]);
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
                engine.step(pads);
                if (net.active() && engine.frame() % 60 == 0) net.report_fingerprint(engine.frame(), engine.fingerprint());
                // The input since the running stage's load, for save states.
                if (const auto* snapshot = engine.stage_snapshot(); snapshot && snapshot->serial != log_serial) {
                    log_serial = snapshot->serial;
                    input_log.clear();
                    load_step_drawn = false;
                    last_step_loaded = true;
                } else {
                    input_log.push_back({pads, false});
                    last_step_loaded = false;
                }
                mix_audio(step_samples(engine.frame()));
                ++frame;
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
            }
            if (steps > 0) interpolator.advance();
            // (Every stage load, also of the same stage: the texture cache is
            // keyed by document addresses, which a new load may reuse.)
            const std::uint64_t serial = engine.stage_snapshot() ? engine.stage_snapshot()->serial : 0;
            if (rendered_stage != engine.stage_name() || rendered_serial != serial) {
                renderer.clear_cache();
                rendered_stage = engine.stage_name();
                rendered_serial = serial;
                interpolator.reset();
            }
            engine.render(items, camera, clear, &shadow);
            if (steps > 0) {
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
            if (!interpolator.captured()) interpolator.capture(items, camera, shadow);
            if (!options.smoke_frames && settings.smooth_motion && present_interval < 1.0 / 75.0)
                interpolator.blend(items, camera, shadow, float(accumulator / dw4::engine::kFieldSeconds));
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
            // From the step that took a stage change on, the original presents
            // nothing new: the last picture stays until the loader draws.
            if (engine.presenting() || !renderer.draw_kept_picture(width, height)) {
                renderer.render(items, camera, clear, screen, wide, width, height);
                // A playing movie covers the screen with its current picture.
                // After its end Sofdec's last picture stays up until the
                // stage changes (the opening's end script switches a frame
                // later).
                if (const auto* sound = engine.get<dw4::engine::SoundManager>(); sound && sound->movie()) {
                    const auto* movie = sound->movie();
                    if (!movie_decoder || movie_serial != movie->serial) {
                        movie_decoder = std::make_unique<dw4::game::MovieDecoder>(movie->video);
                        movie_serial = movie->serial;
                        movie_stage = engine.stage_name();
                    }
                    if (movie_decoder->seek(movie->picture_at(engine.time())))
                        renderer.draw_movie(movie_decoder->rgba().data(), movie_decoder->width(), movie_decoder->height(), screen,
                                            width, height);
                } else if (movie_decoder && movie_stage == engine.stage_name() && !movie_decoder->rgba().empty()) {
                    renderer.draw_movie(movie_decoder->rgba().data(), movie_decoder->width(), movie_decoder->height(), screen,
                                        width, height);
                } else {
                    movie_decoder.reset();
                }
                renderer.keep_picture(width, height);
            }

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            if (show_menu) {
                ImGui::SetNextWindowSize(ImVec2(560, 560), ImGuiCond_FirstUseEver);
                ImGui::Begin("DW4 PC (F1)");
                if (ImGui::BeginTabBar("menu")) {
                if (ImGui::BeginTabItem("Settings")) {
                    {
                        // Presets: Original (the GameCube presentation and
                        // balance) or Modernized (widescreen, smooth motion,
                        // anti-aliasing, straight to the title).
                        const bool original = !settings.widescreen && !settings.smooth_motion && settings.msaa == 0 &&
                                              !settings.skip_intro && settings.exp_rate == 100 && settings.damage_dealt == 100 &&
                                              settings.damage_taken == 100 && settings.drop_seed == 0;
                        const bool modernized = settings.widescreen && settings.smooth_motion && settings.msaa >= 4 && settings.skip_intro;
                        int preset = original ? 0 : modernized ? 1 : 2;
                        static const char* presets[] = {"Original", "Modernized", "Custom"};
                        if (ImGui::Combo("Preset", &preset, presets, 3) && preset < 2) {
                            const bool modern = preset == 1;
                            settings.widescreen = modern;
                            settings.smooth_motion = modern;
                            settings.msaa = modern ? 4 : 0;
                            settings.skip_intro = modern;
                            if (!modern) {
                                settings.exp_rate = settings.damage_dealt = settings.damage_taken = 100;
                                settings.drop_seed = 0;
                                apply_gameplay(engine.cheats());
                            }
                            settings_changed = true;
                        }
                        ImGui::TextDisabled("Original: 4:3, 60 fps motion, no anti-aliasing, the intro and the original balance.");
                    }
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
                        {
                            float worst = 0, total = 0;
                            for (const float t : frame_times) {
                                worst = std::max(worst, t);
                                total += t;
                            }
                            char overlay[96];
                            std::snprintf(overlay, sizeof overlay, "avg %.2f ms  worst %.2f ms", total / float(frame_times.size()), worst);
                            ImGui::PlotLines("Frame time", frame_times.data(), int(frame_times.size()), int(frame_time_at % frame_times.size()),
                                             overlay, 0.0f, std::max(33.4f, worst), ImVec2(0, 48));
                        }
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
                                    const bool waiting = capture && !capture->gamepad && capture->button == b && capture->slot == k;
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
                        ImGui::TextDisabled("Click an input and press a controller button or trigger; right-click clears.");
                        ImGui::TextDisabled("The left stick moves, the right stick is the C stick.");
                        if (ImGui::BeginTable("pad", 4, ImGuiTableFlags_SizingStretchSame)) {
                            for (std::size_t b = 0; b < settings.pad.size(); ++b) {
                                ImGui::TableNextRow();
                                ImGui::TableNextColumn();
                                ImGui::TextUnformatted(dw4::game::pad_key_name(dw4::game::PadKey(b)));
                                for (std::size_t k = 0; k < settings.pad[b].size(); ++k) {
                                    ImGui::TableNextColumn();
                                    const bool waiting = capture && capture->gamepad && capture->button == b && capture->slot == k;
                                    std::string label = waiting ? "press..." : dw4::game::pad_input_name(settings.pad[b][k]);
                                    if (label.empty()) label = "-";
                                    label += "##pad" + std::to_string(b * 8 + k);
                                    if (ImGui::Button(label.c_str(), ImVec2(-1, 0))) capture = KeyCapture{b, k, true};
                                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                                        settings.pad[b][k] = {};
                                        capture.reset();
                                        settings_changed = true;
                                    }
                                }
                            }
                            ImGui::EndTable();
                        }
                        if (ImGui::Button("Default controller layout")) {
                            settings.reset_pad();
                            capture.reset();
                            settings_changed = true;
                        }
                    }
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
                    ImGui::TextDisabled("Memory cards: %s", engine.card_root().string().c_str());
                    ImGui::TextDisabled("(settings.ini [data] cards=<folder with Card A / Card B> changes it)");
                    ImGui::TextDisabled("Settings file: %s", settings.file.string().c_str());
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Online")) {
                    static int players = 2, port = 52040, delay = 3;
                    static char address[128] = "127.0.0.1";
                    if (net.active()) {
                        ImGui::Text("Playing online as player %d of %d (input delay %d steps).", net.local_slot() + 1, net.start().players,
                                    net.start().delay);
                        if (net.desync()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Desync at step %llu.", (unsigned long long)*net.desync());
                        if (ImGui::Button("Leave the session")) end_session();
                    } else if (net.in_lobby()) {
                        ImGui::TextUnformatted(lobby_status.c_str());
                        if (ImGui::Button("Cancel")) net.close();
                    } else {
                        ImGui::TextWrapped("Everyone starts at the title together; each machine's keyboard and first controller play "
                                           "its own player. The host's Card A and the first guest's Card A (as Card B) are shared "
                                           "and go back to their owners when the session ends.");
                        ImGui::SliderInt("Players", &players, 2, 4);
                        ImGui::InputInt("Port", &port);
                        ImGui::SliderInt("Input delay (steps)", &delay, 1, 10);
                        if (ImGui::Button("Host")) {
                            std::string error;
                            if (!net.host(port, players, delay, data_hash(engine.resources()), error)) {
                                state_message = "Online: " + error;
                                message_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                            }
                        }
                        ImGui::Separator();
                        ImGui::InputText("Host address", address, sizeof address);
                        if (ImGui::Button("Join")) {
                            std::string error;
                            if (!net.join(address, port, data_hash(engine.resources()), read_card(card_root / "Card A"), error)) {
                                state_message = "Online: " + error;
                                message_until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                            }
                        }
                        ImGui::TextDisabled("Guests outside your network need the host's port forwarded.");
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Cheats")) {
                    ImGui::TextWrapped("Optional cheats for this session. They change the original game; what they "
                                       "give (levels, bits) stays in saves.");
                    auto& cheats = engine.cheats();
                    ImGui::BeginDisabled(net.active());
                    ImGui::Checkbox("God mode (players lose no HP)", &cheats.god);
                    ImGui::Checkbox("Infinite MP", &cheats.infinite_mp);
                    ImGui::Checkbox("One-hit defeats (player hits)", &cheats.one_hit);
                    ImGui::SeparatorText("Rebalancing (saved; 100% is the original)");
                    bool gameplay_changed = false;
                    gameplay_changed |= ImGui::SliderInt("EXP from enemies", &settings.exp_rate, 10, 1000, "%d%%");
                    gameplay_changed |= ImGui::SliderInt("Damage of your hits", &settings.damage_dealt, 10, 1000, "%d%%");
                    gameplay_changed |= ImGui::SliderInt("Damage enemies do", &settings.damage_taken, 10, 1000, "%d%%");
                    ImGui::SeparatorText("Randomizer (saved; 0 is off)");
                    int seed = int(settings.drop_seed & 0x7FFFFFFF);
                    if (ImGui::InputInt("Item drop seed", &seed)) {
                        settings.drop_seed = std::uint32_t(std::max(0, seed));
                        gameplay_changed = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("New seed")) {
                        settings.drop_seed = std::uint32_t(std::chrono::steady_clock::now().time_since_epoch().count() & 0x7FFFFFFF) | 1u;
                        gameplay_changed = true;
                    }
                    ImGui::TextDisabled("Drops change into other items of the same kind; key items stay. The same seed gives the same drops.");
                    if (gameplay_changed) {
                        apply_gameplay(cheats);
                        settings_changed = true;
                    }
                    ImGui::EndDisabled();
                    if (net.active()) ImGui::TextDisabled("Online, the host's settings apply.");
                    if (auto* sys = engine.get<dw4::engine::GameSystem>(); sys && !net.active()) {
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
            if (!state_message.empty() && std::chrono::steady_clock::now() < message_until) {
                ImGui::SetNextWindowPos(ImVec2(12, 12));
                ImGui::SetNextWindowBgAlpha(0.6f);
                ImGui::Begin("##state", nullptr,
                             ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
                ImGui::TextUnformatted(state_message.c_str());
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
                frame_times[frame_time_at++ % frame_times.size()] = float(seconds * 1000.0);
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
        if (state_test)
            std::cout << "state test: " << (state_mismatch < 0 ? "identical after loading" : "differs from frame " + std::to_string(state_mismatch))
                      << " (" << state_fingerprints.size() << " frames compared)\n";
        if (settings_changed && !options.smoke_frames) settings.save();
        std::cout << "stage " << engine.stage_name() << ", frame " << engine.frame() << ", draws " << items.size() << '\n';
        if (net.active()) {
            std::cout << "online: final fingerprint " << std::hex << engine.fingerprint() << std::dec
                      << (net.desync() ? " (desync at step " + std::to_string(*net.desync()) + ")" : "") << '\n';
            // The owner keeps what the session saved on its card.
            if (options.smoke_frames) own_card_name.clear();
            end_session();
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
    // Test runs and the converter never open dialogs (they run unattended).
    bool interactive = !std::getenv("DW4_NO_ERROR_BOX");
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--smoke-test" || std::string_view(argv[i]) == "--convert-rvz") interactive = false;
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
            else if (arg == "--host") options.net_host = true;
            else if (arg == "--join") {
                options.net_join = next();
                if (const auto colon = options.net_join.rfind(':'); colon != std::string::npos) {
                    options.net_port = std::stoi(options.net_join.substr(colon + 1));
                    options.net_join.resize(colon);
                }
            } else if (arg == "--port") options.net_port = std::stoi(next());
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
            else if (arg == "--load-state") options.load_state = next();
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
                             "         [--load-state FILE]\n";
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
        return run(options, settings);
    } catch (const std::exception& error) {
        std::cerr << "dw4_game: " << error.what() << '\n';
        // Without a console the reason shows in a message box.
        if (interactive) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 4 PC", error.what(), nullptr);
        return 1;
    }
}
