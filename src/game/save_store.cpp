#include "save_store.hpp"

#include "dw4/save_data.hpp"

#include <cstdlib>
#include <ctime>
#include <system_error>

namespace dw4::game {
namespace {

std::string text_of(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// Copies the old GCI folder saves of `legacy_root` (Card A / Card B) into
// the store's files; notes what happened.
void migrate(const std::filesystem::path& legacy_root, const save::CardStore& store, std::vector<std::string>& notes) {
    for (int slot = 0; slot < 2; ++slot) {
        const auto legacy = legacy_root / (slot == 0 ? "Card A" : "Card B") / std::string(save::kSaveFileName);
        std::string error;
        const auto result = save::migrate_gci(legacy, store.save_path(slot), &error);
        if (result == save::Migration::no_source || result == save::Migration::target_exists) continue;
        std::string note = "Old save " + text_of(legacy) + ": " + save::migration_text(result);
        if (result == save::Migration::copied) note += " to " + text_of(store.save_path(slot)) + " (the old file stays)";
        if (!error.empty()) note += " (" + error + ")";
        notes.push_back(note);
    }
}

} // namespace

const char* why_text(SaveStoreChoice::Why why) {
    switch (why) {
    case SaveStoreChoice::Why::card_dir: return "--card-dir";
    case SaveStoreChoice::Why::env_card_dir: return "DW4_CARD_DIR";
    case SaveStoreChoice::Why::save_file: return "--save-file";
    case SaveStoreChoice::Why::test_run: return "test run";
    case SaveStoreChoice::Why::dolphin_folder: return "settings.ini [data] cards";
    case SaveStoreChoice::Why::player: return "settings.ini [data] save";
    }
    return "?";
}

std::filesystem::path slot_b_file(const std::filesystem::path& slot_a) {
    auto name = slot_a.stem().string() + "_b" + slot_a.extension().string();
    return slot_a.parent_path() / name;
}

std::filesystem::path program_relative(const std::filesystem::path& path, const std::filesystem::path& program_dir) {
    if (path.empty() || path.is_absolute() || program_dir.empty()) return path;
    return program_dir / path;
}

SaveStoreChoice choose_save_store(const std::filesystem::path& card_dir, const std::filesystem::path& save_file, bool test_run,
                                  Settings& settings, const std::filesystem::path& program_dir) {
    SaveStoreChoice choice;
    if (!card_dir.empty()) {
        choice.store = save::CardStore::folder(card_dir);
        choice.why = SaveStoreChoice::Why::card_dir;
    } else if (const char* dir = std::getenv("DW4_CARD_DIR"); dir && *dir) {
        choice.store = save::CardStore::folder(dir);
        choice.why = SaveStoreChoice::Why::env_card_dir;
    } else if (!save_file.empty()) {
        choice.store = save::CardStore::single_file(save_file, slot_b_file(save_file));
        choice.why = SaveStoreChoice::Why::save_file;
        if (const char* from = std::getenv("DW4_MIGRATE_FROM"); from && *from) migrate(from, choice.store, choice.notes);
    } else if (test_run) {
        choice.store = save::CardStore::folder(program_dir / "saves" / "GC" / "USA");
        choice.why = SaveStoreChoice::Why::test_run;
    } else if (!settings.cards.empty()) {
        choice.store = save::CardStore::folder(settings.cards);
        choice.why = SaveStoreChoice::Why::dolphin_folder;
    } else {
        const auto a = program_relative(settings.save_file.empty() ? std::filesystem::path("saves/savedata.dat") : settings.save_file, program_dir);
        const auto b = settings.save_file_b.empty() ? slot_b_file(a) : program_relative(settings.save_file_b, program_dir);
        choice.store = save::CardStore::single_file(a, b);
        choice.why = SaveStoreChoice::Why::player;
        if (!settings.save_migrated) {
            migrate(program_dir / "saves" / "GC" / "USA", choice.store, choice.notes);
            settings.save_migrated = true;
            choice.settings_changed = true;
        }
    }
    return choice;
}

std::uint32_t card_time_now() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &now) != 0) return 0;
#else
    if (!localtime_r(&now, &local)) return 0;
#endif
    return save::card_time(local);
}

ImportOutcome import_save(const save::CardStore& store, int slot, const save::ImportSource& source, std::span<const save::BoxMove> moves) {
    ImportOutcome outcome;
    const auto current = store.read(slot);
    if (store.slot_exists(slot) && std::filesystem::exists(store.save_path(slot))) {
        std::string error;
        outcome.backup = save::backup_save(store, slot, save::backup_folder(store), &error);
        if (!outcome.backup) {
            outcome.message = "Nothing imported: the current save could not be backed up (" + error + ").";
            return outcome;
        }
    }
    std::string error;
    const auto merged = save::merge_import(current ? &*current : nullptr, source.gci, moves, card_time_now(), error);
    if (!merged) {
        outcome.message = "Nothing imported: " + error + ".";
        return outcome;
    }
    if (!store.write(slot, *merged, &error)) {
        outcome.message = "Nothing imported: " + error + ".";
        return outcome;
    }
    std::string what;
    if (moves.empty()) what = "the whole save";
    else
        for (const auto& move : moves)
            what += (what.empty() ? "box " : ", box ") + std::to_string(move.from + 1) + " -> " + std::to_string(move.to + 1);
    outcome.ok = true;
    outcome.message = "Imported " + what + " (" + source.container + ") into " + store.describe(slot) + ".";
    if (outcome.backup) outcome.message += " Backup of the previous save: " + text_of(*outcome.backup) + ".";
    return outcome;
}

std::vector<CardFile> session_card(const save::CardStore& store) {
    std::vector<CardFile> files;
    if (auto gci = store.read(0)) files.push_back({std::string(save::kSaveFileName), std::move(*gci)});
    return files;
}

void write_session_card(const std::filesystem::path& folder, const std::vector<CardFile>& files) {
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    for (const auto& file : files) {
        if (file.name != save::kSaveFileName) continue; // nothing but the DW4 save from a peer
        save::write_file_atomic(folder / file.name, file.bytes, false);
    }
}

std::string write_back_session_card(const save::CardStore& own, const std::filesystem::path& session_folder) {
    const auto copy = save::read_whole_file(session_folder / std::string(save::kSaveFileName));
    if (!copy || copy->size() < save::kGciEntrySize + save::kFileSize) return {};
    if (const auto current = own.read(0); current && *current == *copy) return {};
    std::string error;
    if (!own.write(0, *copy, &error)) return "online: the session's save could not be written back: " + error;
    return "online: the session's save went back to " + own.describe(0);
}

} // namespace dw4::game
