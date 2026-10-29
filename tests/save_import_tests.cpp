// GC save import (dw4::save::parse_import / load_import / merge_import):
// synthetic files of every container, built here from the format
// descriptions (save_import.hpp), never from real saves.
#include "dw4/card_store.hpp"
#include "dw4/save_import.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

save::Record character(const char* name, const char* digimon, int level, int bits) {
    save::Record r;
    r.blank = false;
    r.used = true;
    r.unique = 0x1000u + std::uint32_t(level);
    r.digimon = digimon;
    r.name = save::name_to_card(name);
    r.level = level;
    r.hp = r.max_hp = 100 * level;
    r.mp = r.max_mp = 50 * level;
    r.bits = bits;
    r.devices.fill(-1);
    r.disks.fill(-1);
    r.bank.fill(-1);
    r.power_socket.fill(-1);
    r.power_pocket.fill(-1);
    r.main_slot = r.sub_slot = -1;
    r.main_pocket.fill(-1);
    r.flags[960] = 1;
    r.flags[2] = r.flags[3] = 1; // story phase 3
    r.base_isuse[0] = r.base_isuse[4] = 1;
    return r;
}

// A DW4 save as GCI bytes (Dolphin's entry + the file), boxes as given.
std::vector<std::uint8_t> make_gci(const std::vector<save::Record>& boxes, char header_mark = 'H') {
    std::vector<std::uint8_t> header(save::kHeaderSize, 0);
    header[0] = std::uint8_t(header_mark);
    save::GciEntry entry;
    entry.modified = 0x324C22A2;
    const auto head = save::encode_gci_entry(entry);
    const auto file = save::write_file(header, boxes);
    std::vector<std::uint8_t> gci(head.begin(), head.end());
    gci.insert(gci.end(), file.begin(), file.end());
    return gci;
}

std::vector<save::Record> sample_boxes() {
    std::vector<save::Record> boxes(4);
    boxes[0] = character("Tamer", "p_dorumon", 21, 4500);
    boxes[2] = character("Second", "p_agumon", 7, 120);
    return boxes;
}

// GCS: "GCSAVE" header of 0x110 bytes, the entry (block count 1, as the
// GameShark software writes it), the blocks.
std::vector<std::uint8_t> make_gcs(const std::vector<std::uint8_t>& gci) {
    std::vector<std::uint8_t> out(0x110, 0);
    std::memcpy(out.data(), "GCSAVE", 6);
    std::memcpy(out.data() + 0x10, "DOL", 3);
    out.insert(out.end(), gci.begin(), gci.end());
    out[0x110 + 0x38] = 0;
    out[0x110 + 0x39] = 1;
    return out;
}

// SAV: "DATELGC_SAVE" header of 0x80 bytes, the entry with the bytes of each
// pair at +0x06 and +0x2C..+0x3F swapped, the blocks.
std::vector<std::uint8_t> make_sav(const std::vector<std::uint8_t>& gci) {
    std::vector<std::uint8_t> out(0x80, 0);
    std::memcpy(out.data(), "DATELGC_SAVE", 12);
    out.insert(out.end(), gci.begin(), gci.end());
    std::swap(out[0x80 + 0x06], out[0x80 + 0x07]);
    for (std::size_t at = 0x2C; at < 0x40; at += 2) std::swap(out[0x80 + at], out[0x80 + at + 1]);
    return out;
}

void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) {
    b[at] = std::uint8_t(v >> 8);
    b[at + 1] = std::uint8_t(v);
}

struct CardFileSpec {
    std::vector<std::uint8_t> gci; // entry + blocks
    std::vector<std::uint16_t> blocks;
};

// A formatted memory card image of `user_blocks` + 5 blocks with the files
// placed on the given blocks (the block map chains them in that order).
// directory_counters / map_counters: the update counters of copy 1 / 2;
// broken_*: that copy's checksum is spoiled.
struct CardImageSpec {
    std::uint32_t user_blocks = 251;
    std::vector<CardFileSpec> files;
    std::array<std::uint16_t, 2> directory_counters{1, 0}, map_counters{1, 0};
    std::array<bool, 2> broken_directory{}, broken_map{};
    // A stale second directory without the files (an older card state).
    bool stale_second_directory = true;
};

std::vector<std::uint8_t> make_card_image(const CardImageSpec& spec) {
    const std::size_t total = spec.user_blocks + 5;
    std::vector<std::uint8_t> image(total * 8192, 0xFF);
    // Block 0: header (serial, format time, ..., size in Mbit at 0x22).
    std::fill(image.begin(), image.begin() + 0x200, 0);
    put16(image, 0x22, std::uint16_t(total / 16));
    const auto header_sum = save::card_checksum(std::span(image).subspan(0, 0x1FC));
    put16(image, 0x1FC, header_sum[0]);
    put16(image, 0x1FE, header_sum[1]);
    for (int copy = 0; copy < 2; ++copy) {
        const std::size_t dir = std::size_t(1 + copy) * 8192;
        std::fill(image.begin() + std::ptrdiff_t(dir), image.begin() + std::ptrdiff_t(dir + 8192), 0xFF);
        if (!(copy == 1 && spec.stale_second_directory))
            for (std::size_t f = 0; f < spec.files.size(); ++f) {
                auto entry = std::vector<std::uint8_t>(spec.files[f].gci.begin(), spec.files[f].gci.begin() + 64);
                put16(entry, 0x36, spec.files[f].blocks.front());
                put16(entry, 0x38, std::uint16_t(spec.files[f].blocks.size()));
                std::copy(entry.begin(), entry.end(), image.begin() + std::ptrdiff_t(dir + f * 64));
            }
        put16(image, dir + 0x1FFA, spec.directory_counters[std::size_t(copy)]);
        auto sum = save::card_checksum(std::span(image).subspan(dir, 0x1FFC));
        if (spec.broken_directory[std::size_t(copy)]) sum[0] ^= 0x1234;
        put16(image, dir + 0x1FFC, sum[0]);
        put16(image, dir + 0x1FFE, sum[1]);

        const std::size_t bat = std::size_t(3 + copy) * 8192;
        std::fill(image.begin() + std::ptrdiff_t(bat), image.begin() + std::ptrdiff_t(bat + 8192), 0);
        put16(image, bat + 4, spec.map_counters[std::size_t(copy)]);
        std::uint16_t used = 0;
        for (const auto& file : spec.files)
            for (std::size_t i = 0; i < file.blocks.size(); ++i) {
                const std::uint16_t next = i + 1 < file.blocks.size() ? file.blocks[i + 1] : 0xFFFF;
                put16(image, bat + 0x0A + (file.blocks[i] - 5u) * 2, next);
                ++used;
            }
        put16(image, bat + 6, std::uint16_t(spec.user_blocks - used));
        put16(image, bat + 8, 4);
        auto bat_sum = save::card_checksum(std::span(image).subspan(bat + 4, 8192 - 4));
        if (spec.broken_map[std::size_t(copy)]) bat_sum[1] ^= 0x0F0F;
        put16(image, bat, bat_sum[0]);
        put16(image, bat + 2, bat_sum[1]);
    }
    for (const auto& file : spec.files)
        for (std::size_t i = 0; i < file.blocks.size(); ++i)
            std::copy_n(file.gci.begin() + 64 + std::ptrdiff_t(i * 8192), 8192, image.begin() + std::ptrdiff_t(file.blocks[i] * 8192u));
    return image;
}

// Another game's 2-block file (F-Zero GX's maker / game codes).
std::vector<std::uint8_t> other_game_file() {
    save::GciEntry entry;
    entry.game = {'G', 'F', 'Z', 'E'};
    entry.maker = {'8', 'P'};
    entry.file_name = "f_zero.dat";
    entry.blocks = 2;
    const auto head = save::encode_gci_entry(entry);
    std::vector<std::uint8_t> gci(head.begin(), head.end());
    gci.resize(64 + 2 * 8192, 0x5A);
    return gci;
}

std::vector<std::uint8_t> file_part(const std::vector<std::uint8_t>& gci) {
    return std::vector<std::uint8_t>(gci.begin() + 64, gci.begin() + 64 + std::ptrdiff_t(save::kFileSize));
}

void containers() {
    const auto gci = make_gci(sample_boxes());
    std::string error;

    auto plain = save::parse_import(gci, error);
    check(plain && plain->container == "GCI" && file_part(plain->gci) == file_part(gci), "GCI: " + error);

    auto gcs = save::parse_import(make_gcs(gci), error);
    check(gcs && gcs->container == "GCS" && file_part(gcs->gci) == file_part(gci), "GCS: " + error);

    auto sav = save::parse_import(make_sav(gci), error);
    check(sav && sav->container == "SAV" && file_part(sav->gci) == file_part(gci), "SAV: " + error);
    check(save::decode_gci_entry(sav->gci).blocks == 11 && save::decode_gci_entry(sav->gci).image_offset == 0x40,
          "SAV entry fields swapped back");

    // A raw image: another game's file first, the DW4 file scattered over
    // the card along the block map; the second directory is stale (older,
    // without the DW4 file), the first block map broken (the second one,
    // older, still chains the file).
    CardImageSpec spec;
    spec.files.push_back({other_game_file(), {5, 6}});
    std::vector<std::uint16_t> scattered{7, 30, 9, 10, 100, 12, 13, 14, 250, 16, 17};
    spec.files.push_back({gci, scattered});
    spec.directory_counters = {9, 8};
    spec.map_counters = {9, 8};
    spec.broken_map = {true, false};
    const auto image = make_card_image(spec);
    check(image.size() == 256u * 8192, "card image size");
    auto raw = save::parse_import(image, error);
    check(raw && raw->container == "memory card image" && file_part(raw->gci) == file_part(gci), "raw image: " + error);
    check(raw->detail.find("251 blocks") != std::string::npos && raw->detail.find("directory 1") != std::string::npos,
          "raw image detail: " + raw->detail);

    // The newer directory's checksum fails: the older copy (which holds the
    // file) is used.
    spec.stale_second_directory = false;
    spec.directory_counters = {12, 11};
    spec.broken_directory = {true, false};
    raw = save::parse_import(make_card_image(spec), error);
    check(raw && raw->detail.find("directory 2") != std::string::npos && file_part(raw->gci) == file_part(gci),
          "raw image falls back to the checked directory: " + error);

    // Update counters wrap: 0x0001 is newer than 0xFFFF.
    spec.broken_directory = {false, false};
    spec.directory_counters = {0xFFFF, 0x0001};
    raw = save::parse_import(make_card_image(spec), error);
    check(raw && raw->detail.find("directory 2") != std::string::npos, "update counters wrap");

    // Both block maps broken: the file's blocks in a row (a 59-block card).
    CardImageSpec small;
    small.user_blocks = 59;
    small.files.push_back({gci, {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}});
    small.broken_map = {true, true};
    raw = save::parse_import(make_card_image(small), error);
    check(raw && file_part(raw->gci) == file_part(gci) && raw->detail.find("59 blocks") != std::string::npos,
          "raw image without a block map: " + error);

    // A card without the save, a damaged save, other files.
    CardImageSpec other;
    other.files.push_back({other_game_file(), {5, 6}});
    check(!save::parse_import(make_card_image(other), error) && error.find("no Digimon World 4 save") != std::string::npos,
          "card without the save: " + error);
    auto damaged = gci;
    damaged[64 + 0x2000 + 100] ^= 1;
    damaged[64 + 0xC000 + 100] ^= 1;
    check(!save::parse_import(damaged, error) && error.find("damaged") != std::string::npos, "damaged GCI: " + error);
    check(!save::parse_import(other_game_file(), error) && error.find("GFZE/8P") != std::string::npos, "other game's GCI: " + error);
    const std::vector<std::uint8_t> junk(1000, 7);
    check(!save::parse_import(junk, error) && error.find("not a GameCube save") != std::string::npos, "junk: " + error);

    // One damaged copy is still a save (the other copy passes).
    auto half = gci;
    half[64 + 0x2000 + 100] ^= 1;
    check(save::parse_import(half, error).has_value(), "one good copy is enough");
}

void folders() {
    const auto root = std::filesystem::temp_directory_path() / ("dw4_save_import_test_" + std::to_string(std::rand()));
    std::filesystem::remove_all(root);
    const auto gci = make_gci(sample_boxes());
    std::filesystem::create_directories(root / "GC" / "USA" / "Card A");
    std::filesystem::create_directories(root / "GC" / "USA" / "Card B");
    // A GCI folder card holds other games' files too.
    const auto other = other_game_file();
    std::ofstream(root / "GC" / "USA" / "Card A" / "8P-GFZE-f_zero.dat.gci", std::ios::binary)
        .write(reinterpret_cast<const char*>(other.data()), std::streamsize(other.size()));
    {
        std::ofstream out(root / "GC" / "USA" / "Card B" / std::string(save::kSaveFileName), std::ios::binary);
        out.write(reinterpret_cast<const char*>(gci.data()), std::streamsize(gci.size()));
    }
    std::string error;
    auto found = save::load_import(root / "GC" / "USA", error);
    check(found && found->container == "GCI folder" && found->file.parent_path().filename() == "Card B", "GCI folder root: " + error);
    found = save::load_import(root / "GC" / "USA" / "Card B", error);
    check(found && file_part(found->gci) == file_part(gci), "Card B folder: " + error);
    check(!save::load_import(root / "GC" / "USA" / "Card A", error), "a folder without the save");
    // A renamed GCI in a folder is found by its entry.
    std::filesystem::rename(root / "GC" / "USA" / "Card B" / std::string(save::kSaveFileName), root / "GC" / "USA" / "Card B" / "dw4.gci");
    found = save::load_import(root / "GC" / "USA" / "Card B", error);
    check(found && found->file.filename() == "dw4.gci", "renamed GCI in a folder: " + error);
    // Files of each container by path.
    const auto gcs_path = root / "save.gcs";
    const auto gcs = make_gcs(gci);
    std::ofstream(gcs_path, std::ios::binary).write(reinterpret_cast<const char*>(gcs.data()), std::streamsize(gcs.size()));
    found = save::load_import(gcs_path, error);
    check(found && found->container == "GCS" && found->file == gcs_path, "GCS file: " + error);
    check(!save::load_import(root / "missing.gci", error), "missing file");
    std::filesystem::remove_all(root);
}

void summaries() {
    const auto gci = make_gci(sample_boxes());
    const auto boxes = save::summarize_save(gci);
    check(boxes.has_value(), "summary of a valid save");
    check((*boxes)[0].used && (*boxes)[0].name == "Tamer" && (*boxes)[0].level == 21 && (*boxes)[0].bits == 4500 &&
              (*boxes)[0].phase == 3 && (*boxes)[0].forms == 2,
          "box 0 summary");
    check(!(*boxes)[1].used && (*boxes)[2].used && (*boxes)[2].digimon == "p_agumon", "boxes 1 / 2");
    check(save::describe_box((*boxes)[0]) == "Tamer - Dorumon Lv 21, 4500 bits, story phase 3, 2 forms",
          "describe: " + save::describe_box((*boxes)[0]));
    check(save::describe_box((*boxes)[1]) == "(empty)", "describe empty");
    check(save::form_label("p_impdrapm") == "Imperialdramon PM" && save::form_label("p_unknown") == "p_unknown", "form labels");
    auto broken = gci;
    broken[64 + 0x2000 + 9] ^= 1;
    broken[64 + 0xC000 + 9] ^= 1;
    check(!save::summarize_save(broken), "no summary without a valid copy");
}

void moves() {
    auto all = save::parse_box_moves("all");
    check(all && all->empty(), "'all' = whole file");
    check(save::parse_box_moves("") && save::parse_box_moves("")->empty(), "empty = whole file");
    // Players count the boxes 1..4.
    auto same = save::parse_box_moves("1, 3");
    check(same && same->size() == 2 && (*same)[0] == save::BoxMove{0, 0} && (*same)[1] == save::BoxMove{2, 2}, "same boxes");
    auto moved = save::parse_box_moves("1>2,4>1");
    check(moved && (*moved)[0] == save::BoxMove{0, 1} && (*moved)[1] == save::BoxMove{3, 0}, "moved boxes");
    check(!save::parse_box_moves("5") && !save::parse_box_moves("0"), "boxes 0 and 5 refused");
    check(!save::parse_box_moves("1>2,3>2"), "a target twice refused");
    check(!save::parse_box_moves("a") && !save::parse_box_moves("1>") && !save::parse_box_moves("1,,2"), "junk refused");
    auto copies = save::parse_box_moves("1>2,1>3");
    check(copies && copies->size() == 2, "one box into two targets");
}

std::optional<std::vector<save::Record>> records_of(const std::vector<std::uint8_t>& gci) {
    return save::read_file(std::span(gci).subspan(64, save::kFileSize));
}

void merging() {
    std::string error;
    const auto source = make_gci(sample_boxes(), 'S');
    std::vector<save::Record> mine(4);
    mine[0] = character("Mine", "p_vmon", 40, 9000);
    mine[1] = character("Other", "p_girumon", 3, 10);
    const auto target = make_gci(mine, 'T');

    // Whole file: the source's file bytes, a fresh entry.
    auto whole = save::merge_import(&target, source, {}, 0x1234, error);
    check(whole && file_part(*whole) == file_part(source) && save::decode_gci_entry(*whole).modified == 0x1234 &&
              save::decode_gci_entry(*whole).blocks == 11,
          "whole file import: " + error);

    // Source box 2 into target box 1; boxes 0, 2, 3 of the target stay.
    const std::vector<save::BoxMove> one{{2, 1}};
    auto merged = save::merge_import(&target, source, one, 7, error);
    check(merged.has_value(), "box import: " + error);
    check(merged->size() == 64 + save::kFileSize && (*merged)[64] == 'T', "the target's header stays");
    const auto records = records_of(*merged);
    check(records && save::name_from_card((*records)[0].name) == "Mine" && save::name_from_card((*records)[1].name) == "Second" &&
              (*records)[1].level == 7 && !(*records)[2].used,
          "box 2 went into box 1");
    // Byte copies: the moved buffer is identical, both copies identical.
    check(std::equal(merged->begin() + 64 + 0x2000, merged->begin() + 64 + 0x2000 + 40960, merged->begin() + 64 + 0xC000), "both copies equal");
    check(std::equal(merged->begin() + 64 + 0x2000 + 8 + 0x2000, merged->begin() + 64 + 0x2000 + 8 + 0x4000,
                     source.begin() + 64 + 0x2000 + 8 + 2 * 0x2000),
          "the box buffer is a byte copy");
    check(std::equal(merged->begin() + 64 + 0x2000 + 8, merged->begin() + 64 + 0x2000 + 8 + 0x2000, target.begin() + 64 + 0x2000 + 8),
          "the target's box 0 is untouched");
    check(save::valid_save_gci(*merged), "the merged save passes its check");

    // No target: the source's header, only the chosen boxes.
    const std::vector<save::BoxMove> first{{0, 3}};
    auto fresh = save::merge_import(nullptr, source, first, 7, error);
    const auto fresh_records = fresh ? records_of(*fresh) : std::nullopt;
    check(fresh && (*fresh)[64] == 'S' && fresh_records && !(*fresh_records)[0].used && (*fresh_records)[3].used &&
              save::name_from_card((*fresh_records)[3].name) == "Tamer",
          "box import without a target: " + error);

    // A target whose copies both fail: its boxes are gone (it is backed up first).
    auto broken = target;
    broken[64 + 0x2000 + 50] ^= 1;
    broken[64 + 0xC000 + 50] ^= 1;
    auto repaired = save::merge_import(&broken, source, first, 7, error);
    const auto repaired_records = repaired ? records_of(*repaired) : std::nullopt;
    check(repaired_records && !(*repaired_records)[0].used && (*repaired_records)[3].used, "box import over a broken target");

    // The first copy of the source is broken: its second copy is used.
    auto half = source;
    half[64 + 0x2000 + 50] ^= 1;
    merged = save::merge_import(&target, half, one, 7, error);
    check(merged && records_of(*merged) && (*records_of(*merged))[1].level == 7, "the source's second copy");

    const std::vector<save::BoxMove> bad{{5, 0}};
    check(!save::merge_import(&target, source, bad, 7, error), "box 5 refused");
    auto spoiled = source;
    spoiled[64 + 0x2000 + 50] ^= 1;
    spoiled[64 + 0xC000 + 50] ^= 1;
    check(!save::merge_import(&target, spoiled, one, 7, error) && error.find("damaged") != std::string::npos, "a damaged source");
}

} // namespace

int main() {
    try {
        containers();
        folders();
        summaries();
        moves();
        merging();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "save_import: " << checks << " checks passed\n";
    return 0;
}
