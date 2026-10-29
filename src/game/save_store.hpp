#pragma once

// Where this run keeps the memory card saves (docs/SAVES.md "Where the save
// lives") and the PC-side save work around it: the choice of the card store,
// the one-time move of an old saves/GC/USA save into saves/savedata.dat,
// importing GC saves into a slot (with a backup first) and the online
// session's card copy, which carries only the DW4 save.
#include "dw4/card_store.hpp"
#include "dw4/save_import.hpp"
#include "netplay.hpp"
#include "settings.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dw4::game {

struct SaveStoreChoice {
    save::CardStore store;
    enum class Why {
        card_dir,       // --card-dir: GCI folders
        env_card_dir,   // DW4_CARD_DIR: GCI folders
        save_file,      // --save-file: single files
        test_run,       // --smoke-test without either: <program>/saves/GC/USA as GCI folders
        dolphin_folder, // settings.ini [data] cards: GCI folders (e.g. Dolphin's GC/USA, live)
        player,         // the default: settings.ini [data] save / save_b (saves/savedata.dat)
    } why = Why::player;
    std::vector<std::string> notes;    // what the migration copied, for the log and the F1 Saves tab
    std::vector<std::string> problems; // an old save it could not copy, for the log and the F1 Saves tab (highlighted)
    std::vector<std::string> warnings; // the same for the player when the migration ran: a notice at start
    bool settings_changed{};           // [data] save_migrated was set (the caller saves settings.ini)
};
const char* why_text(SaveStoreChoice::Why why);

// The store of this run, by priority: --card-dir, DW4_CARD_DIR, --save-file,
// a test run's default GCI folders, [data] cards, else the single save files
// of [data] save / save_b (relative paths: next to the program). In the
// player's default the old <program>/saves/GC/USA/Card A|B save is copied
// once (settings.ini [data] save_migrated); a copy that failed (an I/O
// error) leaves the flag unset, so the next start tries again, and a damaged
// old save is left where it is with a warning for the player (and stays in
// later starts' `problems` while its slot has no save). With --save-file only
// from the GCI root DW4_MIGRATE_FROM names (test runs).
SaveStoreChoice choose_save_store(const std::filesystem::path& card_dir, const std::filesystem::path& save_file, bool test_run,
                                  Settings& settings, const std::filesystem::path& program_dir);

// savedata.dat -> savedata_b.dat (slot B's file beside slot A's).
std::filesystem::path slot_b_file(const std::filesystem::path& slot_a);
// A path from settings.ini: relative ones are next to the program.
std::filesystem::path program_relative(const std::filesystem::path& path, const std::filesystem::path& program_dir);

// Imports `source` into the store's slot: the slot's save is backed up first
// (save::backup_folder), then replaced by save::merge_import. `message`
// tells what happened (also on failure).
struct ImportOutcome {
    bool ok{};
    std::string message;
    std::optional<std::filesystem::path> backup;
};
ImportOutcome import_save(const save::CardStore& store, int slot, const save::ImportSource& source, std::span<const save::BoxMove> moves);
// The current card time (seconds since 2000 on the PC's local clock).
std::uint32_t card_time_now();

// Online sessions: the files a machine shares - the DW4 save of its slot A
// only, never other files of the card.
std::vector<CardFile> session_card(const save::CardStore& store);
// The session copy of a card (a GCI folder): only the DW4 save is written.
void write_session_card(const std::filesystem::path& folder, const std::vector<CardFile>& files);
// After the session: the session copy's DW4 save goes back to the owner's
// slot A when it changed. Returns a note (empty: nothing to do).
std::string write_back_session_card(const save::CardStore& own, const std::filesystem::path& session_folder);

} // namespace dw4::game
