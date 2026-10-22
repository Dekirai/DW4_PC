// SAVEUTILITY (beSaveUtil): the memory card front end of docs/SAVES.md on
// GCI folders. The requests run as the beSv*Api state machines on the
// simulated card (card_timing.hpp) and end with the beSv*Api codes.
#include "dw4/engine/managers.hpp"
#include "dw4/save_data.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iterator>
#include <system_error>

namespace dw4::engine {
namespace {
// Request order of the command table (804CF848 from REQUEST_FILEMAKE).
constexpr std::string_view kRequests[] = {"REQUEST_FILEMAKE", "REQUEST_FILEFIND", "REQUEST_FORMAT", "REQUEST_USE_CHECK",
                                          "REQUEST_READ_MEDIA", "REQUEST_WRITE_MEDIA", "REQUEST_GETFREESIZE"};
constexpr const char* kGciName = "B2-GDJE-NDMWSaveData.gci";

std::filesystem::path card_folder(const Engine& engine, int slot) {
    return engine.card_root() / (slot == 0 ? "Card A" : "Card B");
}
// The engine clock in CPU ticks (486 MHz).
std::int64_t card_ticks(std::int64_t ns) { return ns / 1000 * 486 + ns % 1000 * 486 / 1000; }

bool inserted(const Engine& engine, int slot) {
    if (slot < 0 || slot > 1) return false;
    switch (engine.card_setting(slot).mode) {
    case CardSetting::Mode::none: return false;
    case CardSetting::Mode::inserted:
    case CardSetting::Mode::unformatted: return true;
    case CardSetting::Mode::automatic: break;
    }
    if (slot == 0) return true;
    std::error_code error;
    return std::filesystem::is_directory(card_folder(engine, slot), error);
}
std::optional<std::vector<std::uint8_t>> read_gci(const Engine& engine, int slot) {
    std::ifstream in(card_folder(engine, slot) / kGciName, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    if (bytes.size() < save::kGciEntrySize + save::kFileSize) return std::nullopt;
    return bytes;
}
// The first data copy whose checksum and version word hold (80321670 case 4).
std::optional<std::size_t> valid_copy(std::span<const std::uint8_t> gci, int version) {
    const auto file = gci.subspan(save::kGciEntrySize);
    for (const auto offset : save::kCopyOffsets) {
        const auto copy = file.subspan(offset, save::kCopySize);
        const std::uint32_t stored = std::uint32_t(copy[0]) << 24 | copy[1] << 16 | copy[2] << 8 | copy[3];
        if (copy[7] == version && stored == save::checksum(copy)) return offset;
    }
    return std::nullopt;
}
bool write_gci(const Engine& engine, int slot, std::span<const std::uint8_t> file) {
    std::error_code error;
    std::filesystem::create_directories(card_folder(engine, slot), error);
    save::GciEntry entry;
    // The entry's time is the console clock's (CARDSetStatus 0x8024C43C), the
    // local time of a console set to the PC's clock: the PC's local calendar
    // time counted from 2000-01-01, not UTC. The game never reads it back.
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    const bool known = localtime_s(&local, &now) == 0;
#else
    const bool known = localtime_r(&now, &local) != nullptr;
#endif
    if (known) entry.modified = save::card_time(local);
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

std::optional<CardSetting> parse_card_setting(std::string_view text) {
    CardSetting setting;
    while (!text.empty()) {
        const auto comma = text.find(',');
        auto word = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
        while (!word.empty() && std::isspace(static_cast<unsigned char>(word.front()))) word.remove_prefix(1);
        while (!word.empty() && std::isspace(static_cast<unsigned char>(word.back()))) word.remove_suffix(1);
        std::string lower(word);
        for (auto& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (lower.empty()) continue;
        if (lower == "auto") setting.mode = CardSetting::Mode::automatic;
        else if (lower == "none") setting.mode = CardSetting::Mode::none;
        else if (lower == "inserted") setting.mode = CardSetting::Mode::inserted;
        else if (lower == "unformatted") setting.mode = CardSetting::Mode::unformatted;
        else {
            std::uint32_t blocks = 0;
            const auto [end, error] = std::from_chars(lower.data(), lower.data() + lower.size(), blocks);
            if (error != std::errc() || end != lower.data() + lower.size() || blocks == 0 || blocks > 2043) return std::nullopt;
            setting.blocks = blocks;
        }
    }
    return setting;
}

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

void SaveUtilManager::clear_stage(Engine&) {
    rams_.clear();
    request_.reset();
}

// The slot's card: what Engine::card_setting puts there and the directory
// of its GCI folder (Dolphin's GCI folder card: every *.gci is a file, the
// GCI header +0x38 its block count, +0x2C the banner offset).
CardSlot SaveUtilManager::card_slot(Engine& engine, int slot, int version) const {
    CardSlot card;
    card.present = inserted(engine, slot);
    if (!card.present) return card;
    const auto& setting = engine.card_setting(slot);
    card.formatted = setting.mode != CardSetting::Mode::unformatted || formatted_[std::size_t(slot)];
    std::uint32_t used = 0, files = 0;
    std::error_code error;
    for (std::filesystem::directory_iterator it(card_folder(engine, slot), error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        auto extension = it->path().extension().string();
        for (auto& c : extension) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (extension != ".gci") continue;
        std::ifstream in(it->path(), std::ios::binary);
        std::array<std::uint8_t, save::kGciEntrySize> head{};
        if (!in.read(reinterpret_cast<char*>(head.data()), std::streamsize(head.size()))) continue;
        used += std::uint32_t(head[0x38] << 8 | head[0x39]);
        ++files;
    }
    card.free_blocks = setting.blocks > used ? setting.blocks - used : 0;
    card.free_files = files < card::kMaxFiles ? card::kMaxFiles - files : 0;
    if (const auto gci = read_gci(engine, slot)) {
        card.file = true;
        card.image_offset = std::uint32_t(gci->at(0x2C)) << 24 | gci->at(0x2D) << 16 | gci->at(0x2E) << 8 | gci->at(0x2F);
        card.data_valid = valid_copy(*gci, version).has_value();
    }
    return card;
}

bool SaveUtilManager::command(Engine& engine, Ram& base, std::string_view command, Packet& packet) {
    auto& ram = static_cast<UtilRam&>(base);
    auto& d = ram.deliver;
    if (command == "GENERATE") {
        ram.state = 1;
        ram.sub = ram.step = 0;
        ram.version = packet.integer(0, 4);
        return true;
    }
    if (command == "REMOVE") {
        ram.state = 1;
        ram.sub = 2;
        ram.step = 0;
        return true;
    }
    for (int request = 0; request < int(std::size(kRequests)); ++request) {
        if (command != kRequests[request]) continue;
        // 80323364: one request at a time; it starts at once (+0x5C).
        if (request_) return true;
        d.slot = packet.integer(0, -1);
        if (!packet.objects.empty() && (request == CardRequest::kFileMake || request == CardRequest::kReadMedia ||
                                        request == CardRequest::kWriteMedia))
            d.data = static_cast<std::vector<std::uint8_t>*>(packet.objects[0]);
        request_ = std::make_unique<CardRequest>(CardRequest::Kind(request), d.slot);
        for (int slot = 0; slot < 2; ++slot) cards_[std::size_t(slot)] = card_slot(engine, slot, ram.version);
        d.operation = request;
        d.done = false;
        ram.state = ram.sub = ram.step = 1;
        request_frame_ = engine.frame();
        return true;
    }
    if (command == "GETDELIVER") { packet.objects.push_back(&d); return true; }
    if (command == "ISBUSY") { packet.bools.push_back(d.done ? 0 : 1); return true; }
    if (command == "GETRESULT" || command == "GETOPERATE") { packet.ints.push_back(d.result); return true; }
    if (command == "ISRDY") { packet.bools.push_back(ram.ready ? 1 : 0); return true; }
    if (command == "CHECKMEM") {
        packet.ints.push_back(std::int32_t(d.free_bytes));
        packet.ints.push_back(std::int32_t(d.free_files));
        packet.bools.push_back(d.enough_space ? 1 : 0);
        packet.bools.push_back(d.free_entry ? 1 : 0);
        return true;
    }
    if (command == "XBOX_HD_CAPACHK" || command == "REPORT") return true;
    return false; // ISUSESAVEPOINT answers nothing
}

// +0x64 of the finished request into the deliver record, with the file work
// on the GCI folder.
void SaveUtilManager::finish(Engine& engine, UtilRam& ram) {
    auto& d = ram.deliver;
    const auto& request = *request_;
    const int slot = request.slot();
    int result = request.result();
    auto store = [&](const std::vector<std::uint8_t>* data) {
        const auto gci = read_gci(engine, slot);
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
        return write_gci(engine, slot, file);
    };
    switch (request.kind()) {
    case CardRequest::kFileMake:
    case CardRequest::kWriteMedia:
        if (result == CardRequest::kDone && !store(d.data)) result = CardRequest::kError;
        break;
    case CardRequest::kReadMedia:
        if (result == CardRequest::kDone) {
            const auto gci = read_gci(engine, slot);
            const auto offset = gci ? valid_copy(*gci, ram.version) : std::nullopt;
            if (!offset) result = CardRequest::kBadData;
            else if (d.data) {
                const auto copy = std::span<const std::uint8_t>(*gci).subspan(save::kGciEntrySize + *offset, save::kCopySize);
                d.data->assign(copy.begin() + 8, copy.end());
            }
        }
        break;
    case CardRequest::kGetFreeSize: {
        // 80322424: room for the file and a free directory entry.
        const std::uint32_t need = CardRequest::kFileBytes;
        if (result == CardRequest::kDone)
            result = request.free_files() == 0 ? CardRequest::kNoEntry : need <= request.free_bytes() ? CardRequest::kDone : CardRequest::kNoSpace;
        d.enough_space = need <= request.free_bytes();
        d.free_entry = request.free_files() > 0;
        d.free_bytes = request.free_bytes();
        d.free_files = request.free_files();
        break;
    }
    case CardRequest::kFormat:
        // The emulated card is formatted from now on; the folder's files stay.
        if (result == CardRequest::kDone && slot >= 0 && slot < 2) formatted_[std::size_t(slot)] = true;
        break;
    default:
        break;
    }
    d.result = result;
    d.done = true;
    if (std::getenv("DW4_TRACE_SAVE"))
        engine.log("saveutil " + std::string(kRequests[request.kind()]) + " slot " + std::to_string(slot) + " -> " +
                   std::to_string(result) + " after " + std::to_string(engine.frame() - request_frame_) + " frames (frame " +
                   std::to_string(engine.frame()) + ")");
}

void SaveUtilManager::update(Engine& engine) {
    const std::int64_t now = card_ticks(engine.time());
    for (auto& owned : rams_) {
        auto& ram = *owned;
        auto& d = ram.deliver;
        if (ram.state == 2) {
            // The utility is gone: no slots, not ready.
            for (auto& s : d.slots) s = {};
            ram.ready = false;
            ram.state = 3;
            continue;
        }
        if (ram.state != 1) continue;
        if (ram.sub == 0 || ram.sub == 2) {
            // beSvStartApi (CARDInit, the copies' version word) / beSvEndApi:
            // created in one update, done in the next.
            if (ram.step == 0) {
                if (ram.sub == 0)
                    for (auto& s : d.slots) s = {};
                ram.step = 1;
            } else if (ram.sub == 0) {
                ram.ready = true;
                ram.sub = 1;
                ram.step = 0;
            } else {
                ram.state = 2;
                ram.sub = ram.step = 0;
            }
            continue;
        }
        if (ram.sub != 1) continue;
        // beSvConnectCheck (803225FC / 80322688): CARDProbeEx on both slots.
        for (int slot = 0; slot < int(d.slots.size()); ++slot) d.slots[std::size_t(slot)].state = inserted(engine, slot) ? 1 : 0;
        if (ram.step != 1) continue;
        if (!request_) {
            ram.step = 0;
            continue;
        }
        const int slot = std::clamp(request_->slot(), 0, 1);
        auto card = cards_[std::size_t(slot)];
        if (request_->slot() != slot) card.present = false; // no such channel: the CARD calls fail
        const bool done = request_->update(channels_[std::size_t(slot)], card, now);
        cards_[std::size_t(slot)] = card;
        if (done) {
            finish(engine, ram);
            request_.reset();
            ram.step = 0;
        }
    }
}

} // namespace dw4::engine
