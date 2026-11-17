#pragma once

// What the F1 menu does beyond the settings (docs/CONSOLE_MENU.md): the
// story loop's own actions and facts (game_main.cpp run()), built once
// before the loop from the loop's lambdas with the same reference captures.
// The PC's classic F1 menu calls them where it ran the code inline (the
// preset, the loading choice, the language, the save states, the cheats'
// rebalancing, the randomizer), Android's console menu calls the same ones:
// one apply path for both presentations.
#include "settings.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace dw4::engine {
struct Cheats;
}
namespace dw4::save {
class CardStore;
}

namespace dw4::game {

class AchievementTracker;
class CloudBackupPanel;
class Languages;
struct ModInfo;
struct TexturePackPanel;

// A line of text a page shows as it is, in a tone.
enum class InfoTone : std::uint8_t { normal, dim, warn, error, ok };
struct InfoLine {
    InfoTone tone = InfoTone::dim;
    std::string text;
};

// A joined player's line (F1 > Cheats > Players).
struct PlayerLine {
    int slot = 0;
    std::string stats; // "Level 5  HP 120/150  MP 40/40  Bits 1200"
};

struct F1Actions {
    // ---- General
    const Languages* languages = nullptr;
    std::function<void(const std::string& code)> select_language; // languages.select + [preferences] language, or the state message
    std::function<int()> preset_index;                            // 0 Original, 1 Modernized, 2 Custom
    std::function<void(bool modern)> apply_preset;                // the classic menu's preset block
    std::function<std::vector<InfoLine>()> preset_notes;          // the randomizer's coloured lines under the preset
    std::function<bool()> loading_fast;                           // as shown (online: the host's)
    std::function<void(bool fast)> set_loading;                   // [gameplay] loading, from the next stage change
    std::function<bool()> net_active;                             // a direct session runs
    std::function<bool()> online_room;                            // a world (room) session runs
    // ---- Save states
    std::function<std::string()> can_save;                        // empty: a state can be saved now, else why not
    std::function<std::string(std::size_t slot)> can_load;       // empty: the slot can be loaded now (also on the title), else why not
    std::function<void(std::size_t slot)> save_state;
    std::function<bool(std::size_t slot)> load_state;             // true: loaded
    std::function<std::string(std::size_t slot)> slot_text;       // "stagis10, 120 steps since its stage loaded"; empty: empty slot
    std::function<std::string(std::size_t slot)> slot_stage;      // the slot's stage
    std::size_t state_count = 4;
    const std::string* state_message = nullptr;                   // the last save / load message
    std::function<std::filesystem::path()> state_dir;
    // ---- Cheats
    std::function<engine::Cheats&()> cheats;
    std::function<bool()> cheats_online;                          // a session or a room: the cheats are off
    std::function<void()> gameplay_changed;                       // after the rebalancing or randomizer settings changed (offline)
    std::function<std::vector<InfoLine>()> randomizer_status;     // what the running game plays
    std::function<bool()> randomizer_can_unbind, randomizer_can_bind;
    std::function<void()> randomizer_unbind, randomizer_bind, randomizer_new_seed;
    std::function<std::vector<PlayerLine>()> players;             // joined players with stats (offline)
    std::function<void(int slot)> cheat_refill;
    std::function<void(int slot, int levels)> cheat_levels;
    std::function<void(int slot, int bits)> cheat_bits;
    // ---- Read only
    const AchievementTracker* achievements = nullptr;
    std::function<std::vector<InfoLine>()> achievement_notes;     // the gate's warning, the profile's place
    std::function<std::string()> play_time;                       // "H:MM"
    // F1 > Saves' "Where the save lives" (read every 2 s); `player_words`
    // (Android): the slots without their paths. The details: the files, the
    // store's choice, the Dolphin note.
    std::function<std::vector<InfoLine>(bool player_words)> saves_summary;
    std::function<std::vector<InfoLine>()> saves_details;
    std::function<std::string()> save_slot_a;                     // player_store.describe(0)
    std::function<std::filesystem::path()> settings_file;
    std::function<std::string()> controller_log;                  // its path, empty without one
    // ---- Panels
    CloudBackupPanel* cloud_panel = nullptr;
    const save::CardStore* card_store = nullptr;
    std::function<bool()> cloud_available;                        // no room, no session, no lobby
    std::function<TexturePackPanel()> texture_panel;
    std::vector<ModInfo>* mods = nullptr;
    std::filesystem::path mods_root;
    // ---- Online
    std::function<bool()> net_in_lobby;
    std::function<bool()> has_lobby;
    std::function<std::vector<InfoLine>()> session_lines;          // the Online tab's lines of the running branch
    std::function<std::vector<InfoLine>()> connection_lines;       // a room's connection (round 62)
    std::function<void()> end_session, net_close, leave_room, copy_log_path;
    // ---- Developer
    std::function<std::string()> stage_info;
    std::function<bool()> game_paused;                             // F1 pauses the game now (the classic menu's pause uses it too)
    // A notice top right (the toasts).
    std::function<void(const std::string& title, const std::string& text)> toast;
};

} // namespace dw4::game
