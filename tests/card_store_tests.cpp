// Memory card stores (dw4::save::CardStore): the single-file backend
// (saves/savedata.dat, .bak on every write, atomic replace), the GCI folder
// backend (Dolphin's Card A / Card B), the migration of an old GCI folder
// save and the backups taken before an import. Synthetic saves in a
// temporary folder.
#include "dw4/card_store.hpp"
#include "dw4/save_data.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
using namespace dw4;
namespace fs = std::filesystem;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::uint8_t> make_gci(int level) {
    std::vector<save::Record> boxes(4);
    boxes[0].blank = false;
    boxes[0].used = true;
    boxes[0].digimon = "p_agumon";
    boxes[0].name = save::name_to_card("Card");
    boxes[0].level = level;
    std::vector<std::uint8_t> header(save::kHeaderSize, 0);
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

int level_of(const std::vector<std::uint8_t>& gci) {
    const auto records = save::read_file(std::span(gci).subspan(save::kGciEntrySize, save::kFileSize));
    return records ? (*records)[0].level : -1;
}

void single_file(const fs::path& root) {
    const auto store = save::CardStore::single_file(root / "saves" / "savedata.dat", root / "saves" / "savedata_b.dat");
    check(store.kind() == save::CardStore::Kind::single_file, "kind");
    check(!store.slot_exists(0) && !store.slot_exists(1) && !store.read(0), "an empty store");
    check(store.usage(0).blocks == 0 && store.usage(0).files == 0, "no file, no usage");
    check(store.describe(0).ends_with("saves/savedata.dat"), "describe: " + store.describe(0));

    std::string error;
    check(store.write(0, make_gci(5), &error), "first write: " + error);
    check(store.slot_exists(0) && !store.slot_exists(1), "slot A exists after the write, slot B not");
    check(!fs::exists(root / "saves" / "savedata.dat.bak"), "no backup of nothing");
    check(!fs::exists(root / "saves" / "savedata.dat.tmp"), "no temporary file left");
    auto back = store.read(0);
    check(back && back->size() == save::kGciEntrySize + save::kFileSize && level_of(*back) == 5, "read back");
    check(store.usage(0).blocks == 11 && store.usage(0).files == 1, "the save takes 11 blocks");

    // The second write keeps the first as .bak.
    check(store.write(0, make_gci(6), &error), "second write: " + error);
    const auto backup = save::read_whole_file(root / "saves" / "savedata.dat.bak");
    check(backup && level_of(*backup) == 5 && level_of(*store.read(0)) == 6, "the .bak holds the previous save");
    check(store.write(0, make_gci(7), &error) && level_of(*save::read_whole_file(root / "saves" / "savedata.dat.bak")) == 6,
          "the .bak follows every write");

    // A damaged save (cut short) never replaces the last good backup.
    auto cut = make_gci(7);
    cut.resize(cut.size() / 2);
    write_raw(root / "saves" / "savedata.dat", cut);
    check(store.write(0, make_gci(8), &error), "write over a damaged save: " + error);
    check(level_of(*save::read_whole_file(root / "saves" / "savedata.dat.bak")) == 6 && level_of(*store.read(0)) == 8,
          "the good .bak stays, the new save is written");
    // A write that cannot complete leaves the save and its backup alone.
    fs::create_directories(root / "saves" / "savedata.dat.tmp"); // the temporary file cannot be made
    check(!store.write(0, make_gci(9), &error) && !error.empty(), "a failed write reports it");
    check(level_of(*store.read(0)) == 8 && level_of(*save::read_whole_file(root / "saves" / "savedata.dat.bak")) == 6,
          "a failed write changes nothing");
    fs::remove(root / "saves" / "savedata.dat.tmp");
    check(store.write(0, make_gci(7), &error) && level_of(*save::read_whole_file(root / "saves" / "savedata.dat.bak")) == 8,
          "writes work again");

    // Slot B only once its file exists.
    check(store.write(1, make_gci(9), &error) && store.slot_exists(1) && level_of(*store.read(1)) == 9, "slot B file");

    // A short file is no save, but the slot holds a card.
    write_raw(root / "short" / "savedata.dat", std::vector<std::uint8_t>(100, 0));
    const auto short_store = save::CardStore::single_file(root / "short" / "savedata.dat", root / "short" / "b.dat");
    check(short_store.slot_exists(0) && !short_store.read(0), "a short file");

    // Backups before an import: saves/backups/savedata-<time>.dat, unique names.
    const auto folder = save::backup_folder(store);
    check(folder == root / "saves" / "backups", "backup folder");
    const auto first = save::backup_save(store, 0, folder, &error);
    const auto second = save::backup_save(store, 0, folder, &error);
    check(first && second && *first != *second && fs::exists(*first) && fs::exists(*second), "two backups: " + error);
    check(first->filename().string().starts_with("savedata-") && first->extension() == ".dat", "backup name " + first->string());
    check(level_of(*save::read_whole_file(*first)) == 7, "the backup holds the save");
    const auto none = save::CardStore::single_file(root / "none" / "a.dat", root / "none" / "b.dat");
    check(!save::backup_save(none, 0, root / "none" / "backups", &error), "no backup without a save");
}

void folder(const fs::path& root) {
    const auto store = save::CardStore::folder(root / "GC");
    check(store.save_path(0) == root / "GC" / "Card A" / std::string(save::kSaveFileName), "folder save path");
    check(!store.slot_exists(0) && !store.slot_exists(1), "no folders yet");
    std::string error;
    check(store.write(0, make_gci(3), &error) && store.slot_exists(0) && level_of(*store.read(0)) == 3, "folder write: " + error);
    // The original GCI folder writes keep no backup.
    check(store.write(0, make_gci(4), &error) && !fs::exists(store.save_path(0).string() + ".bak"), "no .bak in GCI folders");
    // Usage counts every *.gci of the folder (another game's 2043 blocks: full).
    auto other = make_gci(1);
    other[0x38] = 0x07;
    other[0x39] = 0xFB; // 2043 blocks
    write_raw(root / "GC" / "Card A" / "01-G4SE-other.gci", other);
    write_raw(root / "GC" / "Card A" / "notes.txt", {1, 2, 3});
    const auto usage = store.usage(0);
    check(usage.blocks == 11 + 2043 && usage.files == 2, "folder usage " + std::to_string(usage.blocks));
    check(store.describe(0).ends_with("GC/Card A (GCI folder)"), "describe " + store.describe(0));
    check(save::backup_folder(store) == root / "GC" / "backups", "folder backups beside Card A / Card B");
    const auto copy = save::backup_save(store, 0, save::backup_folder(store), &error);
    check(copy && copy->filename().string().starts_with("B2-GDJE-NDMWSaveData-") && copy->extension() == ".gci", "folder backup name");
}

void migration(const fs::path& root) {
    const auto legacy = root / "old" / "Card A" / std::string(save::kSaveFileName);
    const auto target = root / "new" / "savedata.dat";
    std::string error;
    check(save::migrate_gci(legacy, target, &error) == save::Migration::no_source && !fs::exists(target), "no old save");
    write_raw(legacy, std::vector<std::uint8_t>(5000, 1));
    check(save::migrate_gci(legacy, target, &error) == save::Migration::invalid_source && !fs::exists(target), "a damaged old save");
    write_raw(legacy, make_gci(12));
#if defined(_WIN32)
    {
        // Another program holds the old save open without sharing: it cannot
        // be read now, which is a failed copy (tried again at the next start),
        // not a damaged save.
        HANDLE held = CreateFileW(legacy.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(held != INVALID_HANDLE_VALUE, "hold the old save open");
        error.clear();
        const auto result = save::migrate_gci(legacy, target, &error);
        CloseHandle(held);
        check(result == save::Migration::failed && !fs::exists(target) && error.starts_with("cannot read "), "a locked old save: " + error);
        error.clear();
    }
#endif
    check(save::migrate_gci(legacy, target, &error) == save::Migration::copied, "migration: " + error);
    check(fs::exists(legacy) && level_of(*save::read_whole_file(target)) == 12, "copied, the old file stays");
    check(*save::read_whole_file(target) == *save::read_whole_file(legacy), "a byte copy");
    write_raw(legacy, make_gci(13));
    check(save::migrate_gci(legacy, target, &error) == save::Migration::target_exists && level_of(*save::read_whole_file(target)) == 12,
          "an existing save file is never replaced");
    check(std::string(save::migration_text(save::Migration::copied)) == "copied", "migration text");
}

void validity() {
    auto gci = make_gci(2);
    check(save::valid_save_gci(gci), "valid");
    gci[64 + 0x2000 + 20] ^= 1;
    check(save::valid_save_gci(gci), "one copy suffices");
    gci[64 + 0xC000 + 20] ^= 1;
    check(!save::valid_save_gci(gci), "both copies broken");
    check(!save::valid_save_gci(std::vector<std::uint8_t>(64, 0)), "too short");
}

} // namespace

// Round G-core (docs/SEAMLESS.md 8.8): a store with a story override writes it
// into every used box of both copies with valid checksums and leaves empty
// boxes and damaged copies alone; without one the bytes are written unchanged.
void story_override(const fs::path& root) {
    dw4::save::Record used;
    used.blank = false;
    used.used = true;
    used.unique = 5;
    used.level = 3;
    used.flags[2] = 1;  // the engine's (borrowed) story: phase 2 ...
    used.flag_folder[5] = 1;
    const auto record = dw4::save::encode(used);
    std::vector<dw4::save::Record> records(2);
    records[0] = used;     // box 0 used, box 1 empty
    const auto file = dw4::save::write_file(std::vector<std::uint8_t>(dw4::save::kHeaderSize, 0), records);
    std::vector<std::uint8_t> gci(dw4::save::kGciEntrySize, 0);
    gci.insert(gci.end(), file.begin(), file.end());
    check(dw4::save::valid_save_gci(gci), "the test save is valid");
    dw4::save::StoryBytes own{};
    own[1] = 1;            // ... its own: phase 1
    own[1024 + 0] = 1;     // K0
    own[1028 + 3] = 7;     // a counter byte
    auto store = dw4::save::CardStore::single_file(root / "story_a.dat", root / "story_b.dat");
    check(store.write(0, gci), "a write without an override");
    check(*store.read(0) == gci, "without an override the bytes are written unchanged");
    store.set_story_override(own);
    check(store.write(0, gci), "a write with an override");
    const auto out = *store.read(0);
    check(dw4::save::valid_save_gci(out), "the overridden save is valid (checksums fixed)");
    for (const auto offset : dw4::save::kCopyOffsets) {
        const auto copy = dw4::save::kGciEntrySize + offset;
        const auto box0 = copy + 8 + dw4::save::kStoryOffset;
        check(std::equal(own.begin(), own.end(), out.begin() + std::ptrdiff_t(box0)), "box 0 of each copy holds the override");
        const auto box1 = copy + 8 + dw4::save::kRecordStride;
        check(std::all_of(out.begin() + std::ptrdiff_t(box1), out.begin() + std::ptrdiff_t(box1 + dw4::save::kRecordSize),
                          [](std::uint8_t b) { return b == 0; }), "an empty box stays empty");
        check(std::equal(out.begin() + std::ptrdiff_t(copy + 8), out.begin() + std::ptrdiff_t(copy + 8 + dw4::save::kStoryOffset), record.begin()),
              "the personal bytes stay");
    }
    // A damaged copy is left as it is.
    auto damaged = gci;
    damaged[dw4::save::kGciEntrySize + dw4::save::kCopyOffsets[1] + 100] ^= 0xFF;
    const auto fixed = dw4::save::with_story(damaged, own);
    check(std::equal(fixed.begin() + std::ptrdiff_t(dw4::save::kGciEntrySize + dw4::save::kCopyOffsets[1]),
                     fixed.begin() + std::ptrdiff_t(dw4::save::kGciEntrySize + dw4::save::kCopyOffsets[1] + dw4::save::kCopySize),
                     damaged.begin() + std::ptrdiff_t(dw4::save::kGciEntrySize + dw4::save::kCopyOffsets[1])),
          "a damaged copy stays as it is");
    check(dw4::save::with_story(std::vector<std::uint8_t>(10, 1), own) == std::vector<std::uint8_t>(10, 1), "short bytes come back unchanged");
}

int main() {
    const auto root = fs::temp_directory_path() / ("dw4_card_store_test_" + std::to_string(std::rand()) + "_" + std::to_string(std::time(nullptr)));
    try {
        fs::remove_all(root);
        single_file(root / "single");
        folder(root / "folder");
        migration(root / "migration");
        validity();
        story_override(root / "story");
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
    std::error_code ignored;
    fs::remove_all(root, ignored);
    std::cout << "card_store: " << checks << " checks passed\n";
    return 0;
}
