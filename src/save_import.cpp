#include "dw4/save_import.hpp"

#include "dw4/card_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>

namespace dw4::save {
namespace {

constexpr std::size_t kBlock = 8192;
constexpr std::size_t kSaveBytes = kGciEntrySize + kFileSize;
constexpr std::size_t kGcsHeader = 0x110; // GCS: the entry follows at 0x110, the blocks at 0x150
constexpr std::size_t kSavHeader = 0x80;  // SAV: the entry follows at 0x80, the blocks at 0xC0
constexpr std::size_t kDirectoryEntries = 127;
constexpr std::size_t kSystemBlocks = 5;

std::uint16_t be16(std::span<const std::uint8_t> b, std::size_t at) { return std::uint16_t(b[at] << 8 | b[at + 1]); }

bool starts_with(std::span<const std::uint8_t> bytes, std::string_view magic) {
    return bytes.size() >= magic.size() && std::memcmp(bytes.data(), magic.data(), magic.size()) == 0;
}

std::string printable(std::span<const std::uint8_t> bytes) {
    std::string text;
    for (const auto b : bytes) {
        if (b == 0) break;
        text.push_back(b >= 0x20 && b < 0x7F ? char(b) : '?');
    }
    return text;
}

// The names of a directory entry: "GDJE/B2 NDMWSaveData".
std::string entry_names(std::span<const std::uint8_t> entry) {
    return printable(entry.subspan(0, 4)) + "/" + printable(entry.subspan(4, 2)) + " " + printable(entry.subspan(8, 32));
}

bool dw4_entry(std::span<const std::uint8_t> entry) {
    if (entry.size() < kGciEntrySize) return false;
    const auto e = decode_gci_entry(entry);
    return std::string_view(e.game.data(), 4) == "GDJE" && std::string_view(e.maker.data(), 2) == "B2" && e.file_name == "NDMWSaveData";
}

// The offset of the first data copy that passes its check in a 90112-byte file.
std::optional<std::size_t> checked_copy(std::span<const std::uint8_t> file) {
    if (file.size() < kFileSize) return std::nullopt;
    for (const auto offset : kCopyOffsets) {
        const auto copy = file.subspan(offset, kCopySize);
        const std::uint32_t stored = std::uint32_t(copy[0]) << 24 | std::uint32_t(copy[1]) << 16 | std::uint32_t(copy[2]) << 8 | copy[3];
        const std::uint32_t version = std::uint32_t(copy[4]) << 24 | std::uint32_t(copy[5]) << 16 | std::uint32_t(copy[6]) << 8 | copy[7];
        if (version == kCopyVersion && stored == checksum(copy)) return offset;
    }
    return std::nullopt;
}

// A DW4 save from a container's directory entry and the blocks after it.
std::optional<ImportSource> from_entry(std::span<const std::uint8_t> entry, std::span<const std::uint8_t> blocks, std::string container,
                                       std::string detail, std::string& error) {
    if (!dw4_entry(entry)) {
        error = "the " + container + " file holds " + entry_names(entry) + ", not the Digimon World 4 save (GDJE/B2 NDMWSaveData)";
        return std::nullopt;
    }
    if (blocks.size() < kFileSize) {
        error = "the " + container + " file ends after " + std::to_string(blocks.size()) + " of the save's 90112 bytes";
        return std::nullopt;
    }
    ImportSource source;
    source.container = std::move(container);
    source.detail = std::move(detail);
    source.gci.assign(entry.begin(), entry.begin() + std::ptrdiff_t(kGciEntrySize));
    source.gci.insert(source.gci.end(), blocks.begin(), blocks.begin() + std::ptrdiff_t(kFileSize));
    if (!checked_copy(std::span<const std::uint8_t>(source.gci).subspan(kGciEntrySize))) {
        error = "the save in the " + source.container + " file is damaged (neither data copy passes its check)";
        return std::nullopt;
    }
    return source;
}

struct SystemCopy {
    std::span<const std::uint8_t> block;
    int number{};
    bool valid{};
    std::uint16_t counter{};
};

// Checked copies first, then the higher update counter (16-bit, wrapping).
void order_copies(std::array<SystemCopy, 2>& copies) {
    auto newer = [](const SystemCopy& a, const SystemCopy& b) {
        if (a.valid != b.valid) return a.valid;
        return std::int16_t(std::uint16_t(a.counter - b.counter)) > 0;
    };
    if (newer(copies[1], copies[0])) std::swap(copies[0], copies[1]);
}

// The file's blocks along a block map (BAT), or none when the chain breaks.
std::optional<std::vector<std::size_t>> follow_chain(std::span<const std::uint8_t> bat, std::size_t first, std::size_t count,
                                                     std::size_t total_blocks) {
    std::vector<std::size_t> chain;
    std::size_t block = first;
    while (chain.size() < count) {
        if (block < kSystemBlocks || block >= total_blocks || std::find(chain.begin(), chain.end(), block) != chain.end()) return std::nullopt;
        chain.push_back(block);
        if (chain.size() == count) break;
        const std::size_t at = 0x0A + (block - kSystemBlocks) * 2;
        if (at + 2 > bat.size()) return std::nullopt;
        block = be16(bat, at);
        if (block == 0xFFFF || block == 0) return std::nullopt;
    }
    return chain;
}

std::optional<ImportSource> parse_card_image(std::span<const std::uint8_t> bytes, std::string& error) {
    const std::size_t total = bytes.size() / kBlock;
    if (total < kSystemBlocks + 11) {
        error = "the memory card image is too small (" + std::to_string(total) + " blocks)";
        return std::nullopt;
    }
    auto block = [&](std::size_t n) { return bytes.subspan(n * kBlock, kBlock); };
    std::array<SystemCopy, 2> directories{}, maps{};
    for (int i = 0; i < 2; ++i) {
        const auto dir = block(std::size_t(1 + i));
        const auto sum = card_checksum(dir.subspan(0, 0x1FFC));
        directories[std::size_t(i)] = {dir, 1 + i, sum[0] == be16(dir, 0x1FFC) && sum[1] == be16(dir, 0x1FFE), be16(dir, 0x1FFA)};
        const auto bat = block(std::size_t(3 + i));
        const auto bat_sum = card_checksum(bat.subspan(4));
        maps[std::size_t(i)] = {bat, 3 + i, bat_sum[0] == be16(bat, 0) && bat_sum[1] == be16(bat, 2), be16(bat, 4)};
    }
    order_copies(directories);
    order_copies(maps);
    const std::string size = std::to_string(total - kSystemBlocks) + " blocks";
    bool found = false;
    for (const auto& directory : directories) {
        for (std::size_t e = 0; e < kDirectoryEntries; ++e) {
            const auto entry = directory.block.subspan(e * kGciEntrySize, kGciEntrySize);
            if (!dw4_entry(entry)) continue;
            found = true;
            const std::size_t first = be16(entry, 0x36), count = be16(entry, 0x38);
            if (count * kBlock < kFileSize) continue;
            std::vector<std::vector<std::size_t>> chains;
            for (const auto& map : maps)
                if (const auto chain = follow_chain(map.block, first, count, total)) chains.push_back(*chain);
            // A broken block map: the file's blocks in a row (how a freshly
            // written card lays them out).
            std::vector<std::size_t> row;
            for (std::size_t b = first; b < first + count && b < total; ++b) row.push_back(b);
            if (row.size() == count && first >= kSystemBlocks) chains.push_back(row);
            for (const auto& chain : chains) {
                std::vector<std::uint8_t> data;
                for (const auto b : chain) {
                    const auto part = block(b);
                    data.insert(data.end(), part.begin(), part.end());
                }
                std::string ignored;
                const std::string detail = size + ", directory " + std::to_string(directory.number) +
                                           (directory.valid ? "" : " (failed its check)") + ", file at block " + std::to_string(first);
                if (auto source = from_entry(entry, data, "memory card image", detail, ignored)) return source;
            }
        }
    }
    error = found ? "the Digimon World 4 save on this memory card image is damaged" : "no Digimon World 4 save on this memory card image";
    return std::nullopt;
}

std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

} // namespace

std::array<std::uint16_t, 2> card_checksum(std::span<const std::uint8_t> bytes) {
    std::uint16_t sum = 0, inverse = 0;
    for (std::size_t at = 0; at + 1 < bytes.size(); at += 2) {
        const std::uint16_t word = be16(bytes, at);
        sum = std::uint16_t(sum + word);
        inverse = std::uint16_t(inverse + (word ^ 0xFFFF));
    }
    if (sum == 0xFFFF) sum = 0;
    if (inverse == 0xFFFF) inverse = 0;
    return {sum, inverse};
}

std::optional<ImportSource> parse_import(std::span<const std::uint8_t> bytes, std::string& error) {
    if (starts_with(bytes, "GCSAVE")) {
        if (bytes.size() < kGcsHeader + kGciEntrySize) {
            error = "the GCS file is too short";
            return std::nullopt;
        }
        // The entry's block count is not reliable in GCS files: the blocks
        // are what follows it.
        return from_entry(bytes.subspan(kGcsHeader, kGciEntrySize), bytes.subspan(kGcsHeader + kGciEntrySize), "GCS", "GameShark / PSO save",
                          error);
    }
    if (starts_with(bytes, "DATELGC_SAVE")) {
        if (bytes.size() < kSavHeader + kGciEntrySize) {
            error = "the SAV file is too short";
            return std::nullopt;
        }
        std::array<std::uint8_t, kGciEntrySize> entry{};
        std::copy_n(bytes.begin() + std::ptrdiff_t(kSavHeader), kGciEntrySize, entry.begin());
        std::swap(entry[0x06], entry[0x07]);
        for (std::size_t at = 0x2C; at < 0x40; at += 2) std::swap(entry[at], entry[at + 1]);
        return from_entry(entry, bytes.subspan(kSavHeader + kGciEntrySize), "SAV", "MaxDrive / Action Replay save", error);
    }
    if (bytes.size() % kBlock == 0 && bytes.size() >= kBlock) return parse_card_image(bytes, error);
    if (bytes.size() >= kSaveBytes || (bytes.size() > kGciEntrySize && (bytes.size() - kGciEntrySize) % kBlock == 0)) {
        const std::size_t blocks = (bytes.size() - kGciEntrySize) / kBlock;
        return from_entry(bytes.subspan(0, kGciEntrySize), bytes.subspan(kGciEntrySize), "GCI",
                          std::to_string(blocks) + " blocks" + ((bytes.size() - kGciEntrySize) % kBlock ? " (odd size)" : ""), error);
    }
    error = "not a GameCube save or memory card image (" + std::to_string(bytes.size()) + " bytes)";
    return std::nullopt;
}

std::optional<ImportSource> load_import(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        // Dolphin's names first, then any GCI with the DW4 entry.
        std::vector<std::filesystem::path> candidates;
        const std::filesystem::path folders[] = {path, path / "Card A", path / "Card B"};
        for (const auto& folder : folders) candidates.push_back(folder / std::string(kSaveFileName));
        for (const auto& folder : folders) {
            std::vector<std::filesystem::path> files;
            for (std::filesystem::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".gci") files.push_back(it->path());
            ec.clear();
            std::sort(files.begin(), files.end());
            candidates.insert(candidates.end(), files.begin(), files.end());
        }
        std::string first_problem;
        for (const auto& candidate : candidates) {
            if (!std::filesystem::is_regular_file(candidate, ec)) continue;
            const auto bytes = read_whole_file(candidate);
            if (!bytes) continue;
            std::string problem;
            if (auto source = parse_import(*bytes, problem); source && source->container == "GCI") {
                source->container = "GCI folder";
                source->detail = candidate.filename().string();
                source->file = candidate;
                return source;
            }
            if (first_problem.empty() && candidate.filename() == std::string(kSaveFileName)) first_problem = problem;
        }
        error = !first_problem.empty() ? first_problem
                                       : "no Digimon World 4 save (" + std::string(kSaveFileName) + ") in this folder or its Card A / Card B";
        return std::nullopt;
    }
    const auto bytes = read_whole_file(path);
    if (!bytes) {
        error = "cannot read the file";
        return std::nullopt;
    }
    auto source = parse_import(*bytes, error);
    if (source) source->file = path;
    return source;
}

std::optional<std::array<BoxSummary, kRecordsPerCopy>> summarize_save(std::span<const std::uint8_t> gci) {
    if (gci.size() < kSaveBytes) return std::nullopt;
    const auto file = gci.subspan(kGciEntrySize, kFileSize);
    const auto offset = checked_copy(file);
    if (!offset) return std::nullopt;
    std::array<BoxSummary, kRecordsPerCopy> boxes{};
    for (std::size_t i = 0; i < kRecordsPerCopy; ++i) {
        const auto record = decode(file.subspan(*offset + 8 + i * kRecordStride, kRecordSize));
        auto& box = boxes[i];
        box.used = record.used;
        if (!box.used) continue;
        box.unique = record.unique;
        box.name = name_from_card(record.name);
        box.digimon = record.digimon;
        box.level = record.level;
        box.hp = record.hp;
        box.max_hp = record.max_hp;
        box.bits = record.bits;
        box.phase = 1;
        if (record.flags[86]) box.phase = 7;
        else
            for (int p = 6; p >= 2; --p)
                if (record.flags[std::size_t(p)]) {
                    box.phase = p;
                    break;
                }
        box.forms = int(std::count_if(record.base_isuse.begin(), record.base_isuse.end(), [](std::uint8_t b) { return b != 0; }));
    }
    return boxes;
}

std::string form_label(std::string_view model) {
    struct Name {
        std::string_view model, label;
    };
    static constexpr Name kNames[] = {
        {"p_agumon", "Agumon"},           {"p_vmon", "Veemon"},
        {"p_girumon", "Guilmon"},         {"p_dorumon", "Dorumon"},
        {"p_weregaru", "WereGarurumon"},  {"p_hekabut", "HerculesKabuterimon"},
        {"p_wargrey", "WarGreymon"},      {"p_bwargrey", "BlackWarGreymon"},
        {"p_beelzeb", "Beelzemon"},       {"p_alpha", "Alphamon"},
        {"p_impdrafm", "Imperialdramon FM"}, {"p_impdrapm", "Imperialdramon PM"},
        {"p_metalgaru", "MetalGarurumon"}, {"p_dukecrim", "Gallantmon CM"},
        {"p_susanoo", "Susanoomon"},
    };
    for (const auto& name : kNames)
        if (name.model == model) return std::string(name.label);
    return std::string(model);
}

std::string describe_box(const BoxSummary& box) {
    if (!box.used) return "(empty)";
    return (box.name.empty() ? std::string("?") : box.name) + " - " + form_label(box.digimon) + " Lv " + std::to_string(box.level) + ", " +
           std::to_string(box.bits) + " bits, story phase " + std::to_string(box.phase) + ", " + std::to_string(box.forms) +
           (box.forms == 1 ? " form" : " forms");
}

std::optional<std::vector<BoxMove>> parse_box_moves(std::string_view text) {
    std::string spec;
    for (const char c : text)
        if (!std::isspace(static_cast<unsigned char>(c))) spec.push_back(char(std::tolower(static_cast<unsigned char>(c))));
    std::vector<BoxMove> moves;
    if (spec.empty() || spec == "all") return moves;
    std::size_t at = 0;
    while (at <= spec.size()) {
        const auto comma = std::min(spec.find(',', at), spec.size());
        const std::string token = spec.substr(at, comma - at);
        auto box = [](std::string_view digit) { return digit.size() == 1 && digit[0] >= '1' && digit[0] <= '4' ? digit[0] - '1' : -1; };
        BoxMove move;
        if (const auto arrow = token.find('>'); arrow != std::string::npos) {
            move.from = box(std::string_view(token).substr(0, arrow));
            move.to = box(std::string_view(token).substr(arrow + 1));
        } else {
            move.from = move.to = box(token);
        }
        if (move.from < 0 || move.to < 0) return std::nullopt;
        for (const auto& other : moves)
            if (other.to == move.to) return std::nullopt;
        moves.push_back(move);
        at = comma + 1;
    }
    return moves;
}

std::optional<std::vector<std::uint8_t>> merge_import(const std::vector<std::uint8_t>* target, std::span<const std::uint8_t> source,
                                                      std::span<const BoxMove> moves, std::uint32_t modified, std::string& error) {
    if (source.size() < kSaveBytes) {
        error = "the imported save is too short";
        return std::nullopt;
    }
    const auto source_file = source.subspan(kGciEntrySize, kFileSize);
    const auto source_copy = checked_copy(source_file);
    if (!source_copy) {
        error = "the imported save is damaged";
        return std::nullopt;
    }
    GciEntry fresh;
    fresh.modified = modified;
    const auto head = encode_gci_entry(fresh);
    std::vector<std::uint8_t> out(head.begin(), head.end());
    if (moves.empty()) {
        out.insert(out.end(), source_file.begin(), source_file.end());
        return out;
    }
    for (std::size_t i = 0; i < moves.size(); ++i) {
        if (moves[i].from < 0 || moves[i].from > 3 || moves[i].to < 0 || moves[i].to > 3) {
            error = "box numbers go from 1 to 4";
            return std::nullopt;
        }
        for (std::size_t j = 0; j < i; ++j)
            if (moves[j].to == moves[i].to) {
                error = "box " + std::to_string(moves[i].to + 1) + " is the target twice";
                return std::nullopt;
            }
    }
    const bool have_target = target && target->size() >= kSaveBytes;
    std::vector<std::uint8_t> file(kFileSize, 0);
    const auto header_from = have_target ? std::span<const std::uint8_t>(*target).subspan(kGciEntrySize, kHeaderSize)
                                         : source_file.subspan(0, kHeaderSize);
    std::copy(header_from.begin(), header_from.end(), file.begin());
    std::vector<std::uint8_t> copy(kCopySize, 0);
    copy[7] = std::uint8_t(kCopyVersion);
    if (have_target) {
        const auto target_file = std::span<const std::uint8_t>(*target).subspan(kGciEntrySize, kFileSize);
        if (const auto offset = checked_copy(target_file)) {
            const auto from = target_file.subspan(*offset, kCopySize);
            std::copy(from.begin(), from.end(), copy.begin());
        }
    }
    for (const auto& move : moves) {
        const auto box = source_file.subspan(*source_copy + 8 + std::size_t(move.from) * kRecordStride, kRecordStride);
        std::copy(box.begin(), box.end(), copy.begin() + std::ptrdiff_t(8 + std::size_t(move.to) * kRecordStride));
    }
    const auto sum = checksum(copy);
    for (int i = 0; i < 4; ++i) copy[std::size_t(i)] = std::uint8_t(sum >> (24 - 8 * i));
    for (const auto offset : kCopyOffsets) std::copy(copy.begin(), copy.end(), file.begin() + std::ptrdiff_t(offset));
    out.insert(out.end(), file.begin(), file.end());
    return out;
}

} // namespace dw4::save
