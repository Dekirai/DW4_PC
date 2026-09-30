// SAVEUTILITY (beSaveUtil): the memory card front end of docs/SAVES.md on
// GCI folders. Requests finish on the next update with the beSv*Api codes.
#include "dw4/engine/managers.hpp"
#include "dw4/save_data.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <system_error>

namespace dw4::engine {
namespace {
enum Result : int { kBusy = 1, kDone = 2, kError = 3, kNoFile = 4, kBroken = 8, kNoSpace = 9, kNoEntry = 10, kNoCard = 12 };
// Request order of the command table (804CF848 from REQUEST_FILEMAKE).
enum Request : int { kFileMake, kFileFind, kFormat, kUseCheck, kReadMedia, kWriteMedia, kGetFreeSize };
constexpr std::string_view kRequests[] = {"REQUEST_FILEMAKE", "REQUEST_FILEFIND", "REQUEST_FORMAT", "REQUEST_USE_CHECK",
                                          "REQUEST_READ_MEDIA", "REQUEST_WRITE_MEDIA", "REQUEST_GETFREESIZE"};
constexpr std::uint32_t kFreeBytes = 59 * 8192; // an empty 251-block card
constexpr const char* kGciName = "B2-GDJE-NDMWSaveData.gci";

std::filesystem::path card_folder(const Engine& engine, int slot) {
    return engine.card_root() / (slot == 0 ? "Card A" : "Card B");
}
bool inserted(const Engine& engine, int slot) {
    if (slot == 0) return true;
    std::error_code error;
    return slot == 1 && std::filesystem::is_directory(card_folder(engine, slot), error);
}
std::optional<std::vector<std::uint8_t>> read_gci(const Engine& engine, int slot) {
    std::ifstream in(card_folder(engine, slot) / kGciName, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.size() < save::kGciEntrySize + save::kFileSize) return std::nullopt;
    return bytes;
}
bool write_gci(const Engine& engine, int slot, std::span<const std::uint8_t> file) {
    std::error_code error;
    std::filesystem::create_directories(card_folder(engine, slot), error);
    save::GciEntry entry;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    entry.modified = std::uint32_t(std::max<std::int64_t>(0, now - 946684800)); // GameCube epoch 2000-01-01
    const auto head = save::encode_gci_entry(entry);
    const auto path = card_folder(engine, slot) / kGciName;
    const auto temporary = path.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(head.data()), std::streamsize(head.size()));
        out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
        if (!out) return false;
    }
    std::filesystem::rename(temporary, path, error);
    return !error;
}

// TPL image payload (the pixels, then the palette) of the banner / icon.
void append_tpl(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> tpl) {
    auto u16 = [&](std::size_t at) { return at + 2 <= tpl.size() ? std::uint32_t((tpl[at] << 8) | tpl[at + 1]) : 0u; };
    auto u32 = [&](std::size_t at) { return (u16(at) << 16) | u16(at + 2); };
    if (tpl.size() < 0x14 || u32(0) != 0x0020AF30u) return;
    const auto table = u32(8);
    const auto image = u32(table), palette = u32(table + 4);
    const auto height = u16(image), width = u16(image + 2), format = u32(image + 4), data = u32(image + 8);
    const std::size_t pixels = format == 9 ? std::size_t(width) * height : format == 5 ? std::size_t(width) * height * 2 : 0;
    if (data + pixels > tpl.size()) return;
    out.insert(out.end(), tpl.begin() + std::ptrdiff_t(data), tpl.begin() + std::ptrdiff_t(data + pixels));
    if (palette && format == 9) {
        const std::size_t count = u16(palette), colors = u32(palette + 8);
        if (colors + count * 2 <= tpl.size())
            out.insert(out.end(), tpl.begin() + std::ptrdiff_t(colors), tpl.begin() + std::ptrdiff_t(colors + count * 2));
    }
}
} // namespace

void SaveUtilManager::add_info(Engine& engine, const ObjectView& info) {
    for (const auto& record : info.list(6)) {
        auto ram = std::make_unique<UtilRam>();
        ram->manager = this;
        ram->info = info;
        ram->ram = record;
        ram->info_name = std::string(info.str(2));
        ram->index = int(rams_.size());
        engine.messenger().add_row("SAVEUTILITY", ram->info_name, ram->index, ram.get());
        // beSvDeliver [19] beSvPlatDataGC: comment [18], banner TPL [15], icon TPL [17].
        if (const auto platform = record.ref(12).ref(19); platform && header_.empty()) {
            const auto comment = platform.raw(18);
            header_.assign(comment.begin(), comment.end());
            header_.resize(64, 0);
            append_tpl(header_, platform.memory(15));
            append_tpl(header_, platform.memory(17));
            header_.resize(save::kHeaderSize, 0);
        }
        rams_.push_back(std::move(ram));
    }
}

void SaveUtilManager::clear_stage(Engine&) { rams_.clear(); }

bool SaveUtilManager::command(Engine&, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<UtilRam&>(base);
    auto& d = ram.deliver;
    if (command == "GENERATE") {
        ram.generated = true;
        ram.ending = false;
        ram.version = packet.integer(0, 4);
        return true;
    }
    if (command == "REMOVE") {
        ram.ending = true;
        return true;
    }
    for (int request = 0; request < int(std::size(kRequests)); ++request) {
        if (command != kRequests[request]) continue;
        if (!d.done) return true; // 80323364: one request at a time
        d.slot = packet.integer(0, 0);
        if (!packet.objects.empty()) d.data = static_cast<std::vector<std::uint8_t>*>(packet.objects[0]);
        d.operation = request;
        d.result = kBusy;
        d.done = false;
        ram.pending = request;
        return true;
    }
    if (command == "GETDELIVER") { packet.objects.push_back(&d); return true; }
    if (command == "ISBUSY") { packet.bools.push_back(d.done ? 0 : 1); return true; }
    if (command == "GETRESULT" || command == "GETOPERATE") { packet.ints.push_back(d.result); return true; }
    if (command == "ISRDY") { packet.bools.push_back(ram.ready ? 1 : 0); return true; }
    if (command == "CHECKMEM") {
        packet.ints.push_back(std::int32_t(d.free_bytes));
        packet.ints.push_back(std::int32_t(d.need_bytes));
        packet.bools.push_back(d.enough_space ? 1 : 0);
        packet.bools.push_back(d.free_entry ? 1 : 0);
        return true;
    }
    if (command == "XBOX_HD_CAPACHK" || command == "REPORT") return true;
    return false; // ISUSESAVEPOINT answers nothing
}

// The request of this frame (the beSv*Api state machines end in one step:
// the card mounts at once and the files are local).
void SaveUtilManager::run(Engine& engine, UtilRam& ram) {
    auto& d = ram.deliver;
    const int request = std::exchange(ram.pending, -1);
    d.done = true;
    if (!inserted(engine, d.slot)) {
        d.result = kNoCard;
        return;
    }
    const auto gci = read_gci(engine, d.slot);
    auto store = [&](const std::vector<std::uint8_t>* data) {
        std::vector<std::uint8_t> file(save::kFileSize, 0);
        const auto header = gci ? std::span<const std::uint8_t>(*gci).subspan(save::kGciEntrySize, save::kHeaderSize)
                                : std::span<const std::uint8_t>(header_);
        std::copy(header.begin(), header.end(), file.begin());
        std::vector<std::uint8_t> copy(save::kCopySize, 0);
        copy[7] = std::uint8_t(ram.version);
        if (data) std::copy_n(data->begin(), std::min(data->size(), save::kCopySize - 8), copy.begin() + 8);
        const auto sum = save::checksum(copy);
        for (int i = 0; i < 4; ++i) copy[std::size_t(i)] = std::uint8_t(sum >> (24 - 8 * i));
        for (const auto offset : save::kCopyOffsets) std::copy(copy.begin(), copy.end(), file.begin() + std::ptrdiff_t(offset));
        return write_gci(engine, d.slot, file) ? kDone : kError;
    };
    switch (request) {
    case kFileFind: d.result = gci ? kDone : kNoFile; break;
    case kUseCheck:
    case kFormat: d.result = kDone; break;
    case kFileMake:
    case kWriteMedia: d.result = store(d.data); break;
    case kReadMedia: {
        if (!gci) {
            d.result = kNoFile;
            break;
        }
        // The first copy whose checksum and version word hold (80321670).
        d.result = kBroken;
        const auto file = std::span<const std::uint8_t>(*gci).subspan(save::kGciEntrySize);
        for (const auto offset : save::kCopyOffsets) {
            const auto copy = file.subspan(offset, save::kCopySize);
            const std::uint32_t stored = std::uint32_t(copy[0]) << 24 | copy[1] << 16 | copy[2] << 8 | copy[3];
            if (copy[7] != ram.version || stored != save::checksum(copy)) continue;
            if (d.data) d.data->assign(copy.begin() + 8, copy.end());
            d.result = kDone;
            break;
        }
        break;
    }
    case kGetFreeSize:
        // 80322424: room for the file and a free directory entry.
        d.free_bytes = kFreeBytes;
        d.need_bytes = std::uint32_t(save::kFileSize);
        d.enough_space = d.need_bytes <= d.free_bytes;
        d.free_entry = true;
        d.result = d.enough_space ? kDone : kNoSpace;
        break;
    default: d.result = kError; break;
    }
}

void SaveUtilManager::update(Engine& engine) {
    for (auto& owned : rams_) {
        auto& ram = *owned;
        // beSvStartApi / beSvEndApi: the utility is ready a frame after
        // GENERATE and gone a frame after REMOVE.
        if (ram.ending) {
            ram.ready = ram.generated = ram.ending = false;
            continue;
        }
        if (ram.generated && !ram.ready) {
            ram.ready = true;
            continue;
        }
        if (ram.pending >= 0) run(engine, ram);
    }
}

} // namespace dw4::engine
