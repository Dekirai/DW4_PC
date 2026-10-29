// The run's save store (src/game/save_store.cpp): which store a run uses,
// the one-time migration of the old saves/GC/USA save, imports with a backup
// and the online session card that carries only the DW4 save.
#include "../src/game/save_store.hpp"

#include "dw4/save_data.hpp"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4;
namespace fs = std::filesystem;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void set_env(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

std::vector<std::uint8_t> make_gci(const char* name, int level) {
    std::vector<save::Record> boxes(4);
    boxes[0].blank = false;
    boxes[0].used = true;
    boxes[0].digimon = "p_girumon";
    boxes[0].name = save::name_to_card(name);
    boxes[0].level = level;
    const std::vector<std::uint8_t> header(save::kHeaderSize, 0);
    const auto head = save::encode_gci_entry(save::GciEntry{});
    const auto file = save::write_file(header, boxes);
    std::vector<std::uint8_t> gci(head.begin(), head.end());
    gci.insert(gci.end(), file.begin(), file.end());
    return gci;
}

void write_raw(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

int level_of(const std::optional<std::vector<std::uint8_t>>& gci) {
    if (!gci) return -1;
    const auto records = save::read_file(std::span(*gci).subspan(save::kGciEntrySize, save::kFileSize));
    return records ? (*records)[0].level : -1;
}

using Why = game::SaveStoreChoice::Why;

void choice(const fs::path& root) {
    set_env("DW4_CARD_DIR", nullptr);
    set_env("DW4_MIGRATE_FROM", nullptr);
    const fs::path program = root / "program";
    game::Settings settings;
    // Command line and environment first, then test runs, Dolphin's folder, the player's files.
    auto c = game::choose_save_store(root / "cards", root / "x.dat", true, settings, program);
    check(c.why == Why::card_dir && c.store.kind() == save::CardStore::Kind::folder && c.store.root() == root / "cards", "--card-dir wins");
    set_env("DW4_CARD_DIR", (root / "env").string().c_str());
    c = game::choose_save_store({}, root / "x.dat", true, settings, program);
    check(c.why == Why::env_card_dir && c.store.root() == root / "env", "DW4_CARD_DIR next");
    set_env("DW4_CARD_DIR", nullptr);
    c = game::choose_save_store({}, root / "x.dat", true, settings, program);
    check(c.why == Why::save_file && c.store.kind() == save::CardStore::Kind::single_file && c.store.save_path(0) == root / "x.dat" &&
              c.store.save_path(1) == root / "x_b.dat",
          "--save-file next");
    c = game::choose_save_store({}, {}, true, settings, program);
    check(c.why == Why::test_run && c.store.root() == program / "saves" / "GC" / "USA", "test runs keep the GCI folders");
    check(!settings.save_migrated && !c.settings_changed, "test runs never migrate");
    settings.cards = root / "Dolphin" / "GC" / "USA";
    c = game::choose_save_store({}, {}, false, settings, program);
    check(c.why == Why::dolphin_folder && c.store.root() == settings.cards, "[data] cards: the GCI folder (Dolphin live)");
    settings.cards.clear();
    c = game::choose_save_store({}, {}, false, settings, program);
    check(c.why == Why::player && c.store.save_path(0) == program / "saves" / "savedata.dat" &&
              c.store.save_path(1) == program / "saves" / "savedata_b.dat",
          "the player's default files");
    check(settings.save_migrated && c.settings_changed && c.notes.empty(), "nothing to migrate, flag set");
    settings.save_file = root / "elsewhere" / "my.dat";
    settings.save_file_b = "slotb.dat";
    c = game::choose_save_store({}, {}, false, settings, program);
    check(c.store.save_path(0) == root / "elsewhere" / "my.dat" && c.store.save_path(1) == program / "slotb.dat" && !c.settings_changed,
          "[data] save / save_b, relative to the program");
    check(game::slot_b_file("a/savedata.dat") == fs::path("a/savedata_b.dat"), "slot B file name");
}

void migration(const fs::path& root) {
    set_env("DW4_CARD_DIR", nullptr);
    const fs::path program = root / "program";
    write_raw(program / "saves" / "GC" / "USA" / "Card A" / std::string(save::kSaveFileName), make_gci("Old", 30));
    write_raw(program / "saves" / "GC" / "USA" / "Card B" / std::string(save::kSaveFileName), make_gci("OldB", 31));
    write_raw(program / "saves" / "GC" / "USA" / "Card A" / "01-G4SE-other.gci", std::vector<std::uint8_t>(500, 3));
    game::Settings settings;
    auto c = game::choose_save_store({}, {}, false, settings, program);
    check(c.notes.size() == 2 && c.settings_changed && settings.save_migrated, "both slots migrated");
    check(level_of(c.store.read(0)) == 30 && level_of(c.store.read(1)) == 31, "the saves were copied");
    check(fs::exists(program / "saves" / "GC" / "USA" / "Card A" / std::string(save::kSaveFileName)), "the old file stays");
    check(c.store.slot_exists(1), "slot B holds a card after the migration");
    // Deleted later: the old save does not come back.
    fs::remove(c.store.save_path(0));
    c = game::choose_save_store({}, {}, false, settings, program);
    check(c.notes.empty() && !c.store.read(0), "the migration runs once");
    // --save-file migrates only from DW4_MIGRATE_FROM (test runs).
    game::Settings fresh;
    c = game::choose_save_store({}, root / "test" / "savedata.dat", true, fresh, program);
    check(c.notes.empty() && !c.store.read(0), "--save-file alone does not migrate");
    set_env("DW4_MIGRATE_FROM", (program / "saves" / "GC" / "USA").string().c_str());
    c = game::choose_save_store({}, root / "test" / "savedata.dat", true, fresh, program);
    check(c.notes.size() == 2 && level_of(c.store.read(0)) == 30 && !fresh.save_migrated, "DW4_MIGRATE_FROM");
    set_env("DW4_MIGRATE_FROM", nullptr);
    check(c.problems.empty() && c.warnings.empty(), "nothing went wrong");
}

// A copy that fails (an I/O error) is tried again at the next start; a
// damaged old save is not, but the player is told at once and the F1 Saves
// tab keeps saying so while the slot has no save.
void migration_trouble(const fs::path& root) {
    set_env("DW4_CARD_DIR", nullptr);
    set_env("DW4_MIGRATE_FROM", nullptr);
    const fs::path program = root / "program";
    const fs::path legacy_a = program / "saves" / "GC" / "USA" / "Card A" / std::string(save::kSaveFileName);
    const fs::path legacy_b = program / "saves" / "GC" / "USA" / "Card B" / std::string(save::kSaveFileName);
    write_raw(legacy_a, make_gci("Old", 30));
    auto damaged = make_gci("OldB", 31);
    for (std::size_t i = save::kGciEntrySize; i < damaged.size(); i += 7) damaged[i] ^= 0x5A; // both data copies fail their check
    write_raw(legacy_b, damaged);
    // The save folder cannot be made: a file stands where it should be.
    write_raw(program / "blocked", std::vector<std::uint8_t>(4, 1));
    game::Settings settings;
    settings.save_file = "blocked/savedata.dat";
    settings.save_file_b = "blocked/savedata_b.dat";
    auto c = game::choose_save_store({}, {}, false, settings, program);
    check(!settings.save_migrated && !c.settings_changed, "a failed copy leaves the flag unset");
    check(c.problems.size() == 2 && c.warnings.size() == 2 && c.notes.empty(), "both slots are problems: " + std::to_string(c.problems.size()));
    check(c.problems[0].find("the copy failed") != std::string::npos && c.problems[0].find("tries again at the next start") != std::string::npos,
          "slot A: the copy failed, tried again: " + c.problems[0]);
    check(c.warnings[0].find("could not be copied") != std::string::npos && c.warnings[0].find("F1 > Saves") != std::string::npos,
          "the player hears of it: " + c.warnings[0]);
    check(c.problems[1].find("damaged") != std::string::npos && c.warnings[1].find("is damaged") != std::string::npos,
          "slot B: damaged: " + c.problems[1]);
    check(!c.store.read(0), "this start has no save in slot A");
    // Next start, the folder can be made now: slot A is copied, the damaged
    // slot B is not tried again but told once more (it was never copied).
    fs::remove(program / "blocked");
    c = game::choose_save_store({}, {}, false, settings, program);
    check(settings.save_migrated && c.settings_changed, "the retry succeeded: flag set");
    check(level_of(c.store.read(0)) == 30, "slot A copied on the retry");
    check(c.notes.size() == 1 && c.problems.size() == 1 && c.warnings.size() == 1, "slot B still damaged");
    check(!c.store.read(1), "the damaged save was not copied");
    // Later starts: no migration, no notice, but the F1 Saves tab keeps the
    // damaged old save in view while slot B has no save.
    c = game::choose_save_store({}, {}, false, settings, program);
    check(!c.settings_changed && c.notes.empty() && c.warnings.empty(), "no new notice");
    check(c.problems.size() == 1 && c.problems[0].find("Card B") != std::string::npos && c.problems[0].find("damaged") != std::string::npos,
          "the F1 tab still names it");
    // Once slot B has a save of its own, the old file is no news.
    std::string error;
    check(c.store.write(1, make_gci("New", 1), &error), "a new slot B save: " + error);
    c = game::choose_save_store({}, {}, false, settings, program);
    check(c.problems.empty(), "nothing left to say");
    // A damaged old save alone sets the flag at once (nothing to retry).
    game::Settings other;
    other.save_file = "other/savedata.dat";
    other.save_file_b = "other/savedata_b.dat";
    fs::remove(legacy_a);
    c = game::choose_save_store({}, {}, false, other, program);
    check(other.save_migrated && c.settings_changed && c.warnings.size() == 1 && c.problems.size() == 1,
          "damaged only: flag set, told");
}

void imports(const fs::path& root) {
    const auto store = save::CardStore::single_file(root / "imp" / "savedata.dat", root / "imp" / "savedata_b.dat");
    std::string error;
    save::ImportSource source;
    source.container = "GCI";
    source.gci = make_gci("Import", 44);
    // No save yet: no backup, the whole file.
    auto outcome = game::import_save(store, 0, source, {});
    check(outcome.ok && !outcome.backup && level_of(store.read(0)) == 44, "import into an empty slot: " + outcome.message);
    // A save: backed up first.
    store.write(0, make_gci("Mine", 10), &error);
    const std::vector<save::BoxMove> moves{{0, 2}};
    outcome = game::import_save(store, 0, source, moves);
    check(outcome.ok && outcome.backup && fs::exists(*outcome.backup) && level_of(save::read_whole_file(*outcome.backup)) == 10,
          "the backup holds the old save: " + outcome.message);
    check(outcome.backup->parent_path() == root / "imp" / "backups", "backups in saves/backups");
    const auto now = *store.read(0);
    const auto records = save::read_file(std::span(now).subspan(save::kGciEntrySize, save::kFileSize));
    check(records && (*records)[0].level == 10 && (*records)[2].level == 44, "box 1 went into box 3, box 1 stayed");
    check(outcome.message.find("box 1 -> 3") != std::string::npos, "message: " + outcome.message);
    // Slot B: its file appears.
    outcome = game::import_save(store, 1, source, {});
    check(outcome.ok && store.slot_exists(1) && level_of(store.read(1)) == 44, "import into slot B");
    // A broken source changes nothing.
    auto broken = source;
    broken.gci[64 + 0x2000 + 30] ^= 1;
    broken.gci[64 + 0xC000 + 30] ^= 1;
    const auto before = store.read(0);
    outcome = game::import_save(store, 0, broken, moves);
    check(!outcome.ok && store.read(0) == before, "a damaged source changes nothing");
}

void session(const fs::path& root) {
    // A Dolphin GCI folder with other games' saves: only the DW4 save is shared.
    const auto folder_store = save::CardStore::folder(root / "dolphin");
    folder_store.write(0, make_gci("Host", 20));
    write_raw(root / "dolphin" / "Card A" / "01-G4SE-gc4sword.gci", std::vector<std::uint8_t>(64 + 8192, 1));
    write_raw(root / "dolphin" / "Card A" / "8P-GFZE-f_zero.dat.gci", std::vector<std::uint8_t>(64 + 8192, 2));
    const auto shared = game::session_card(folder_store);
    check(shared.size() == 1 && shared[0].name == save::kSaveFileName && level_of(shared[0].bytes) == 20, "only the DW4 save is shared");
    check(game::session_card(save::CardStore::folder(root / "empty")).empty(), "no save, nothing shared");
    const auto single_store = save::CardStore::single_file(root / "single" / "savedata.dat", root / "single" / "savedata_b.dat");
    single_store.write(0, make_gci("Single", 21));
    const auto from_file = game::session_card(single_store);
    check(from_file.size() == 1 && from_file[0].name == save::kSaveFileName && level_of(from_file[0].bytes) == 21, "a single file is shared as the GCI");

    // A peer's list with other names: only the DW4 save lands in the session copy.
    std::vector<game::CardFile> peer = shared;
    peer.push_back({"01-G4SE-gc4sword.gci", std::vector<std::uint8_t>(10, 1)});
    peer.push_back({"../escape.gci", std::vector<std::uint8_t>(10, 1)});
    const auto session = root / "session" / "Card A";
    game::write_session_card(session, peer);
    std::size_t count = 0;
    for (const auto& entry : fs::directory_iterator(session)) count += entry.is_regular_file() ? 1 : 0;
    check(count == 1 && fs::exists(session / std::string(save::kSaveFileName)) && !fs::exists(root / "session" / "escape.gci"),
          "the session copy holds the DW4 save only");

    // Back to the owner: nothing when unchanged, the save (with .bak) when changed.
    write_raw(root / "back" / "Card A" / std::string(save::kSaveFileName), *single_store.read(0));
    check(game::write_back_session_card(single_store, root / "back" / "Card A").empty() && !fs::exists(root / "single" / "savedata.dat.bak"),
          "an unchanged save is not written back");
    write_raw(root / "back" / "Card A" / std::string(save::kSaveFileName), make_gci("Single", 25));
    const auto note = game::write_back_session_card(single_store, root / "back" / "Card A");
    check(!note.empty() && level_of(single_store.read(0)) == 25 && level_of(save::read_whole_file(root / "single" / "savedata.dat.bak")) == 21,
          "the changed save goes back: " + note);
    check(game::write_back_session_card(single_store, root / "none").empty(), "no session save, nothing written");
}

} // namespace

int main() {
    const auto root = fs::temp_directory_path() / ("dw4_save_store_test_" + std::to_string(std::rand()) + "_" + std::to_string(std::time(nullptr)));
    try {
        fs::remove_all(root);
        choice(root / "choice");
        migration(root / "migration");
        migration_trouble(root / "migration_trouble");
        imports(root / "imports");
        session(root / "session");
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cout << "save_store: " << checks << " checks passed\n";
    return 0;
}
