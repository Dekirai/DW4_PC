#include "dw4/save_data.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dw4::save {
namespace {
class Writer {
public:
    explicit Writer(std::span<std::uint8_t> out) : out_(out) {}
    void u8(std::size_t at, std::uint8_t v) { out_[at] = v; }
    void u16(std::size_t at, std::uint16_t v) {
        out_[at] = std::uint8_t(v >> 8);
        out_[at + 1] = std::uint8_t(v);
    }
    void u32(std::size_t at, std::uint32_t v) {
        u16(at, std::uint16_t(v >> 16));
        u16(at + 2, std::uint16_t(v));
    }
    void i32(std::size_t at, std::int32_t v) { u32(at, std::uint32_t(v)); }
    template <std::size_t N> void ints(std::size_t at, const std::array<std::int32_t, N>& values) {
        for (std::size_t i = 0; i < N; ++i) i32(at + i * 4, values[i]);
    }
    template <std::size_t N> void bytes(std::size_t at, const std::array<std::uint8_t, N>& values) {
        std::copy(values.begin(), values.end(), out_.begin() + std::ptrdiff_t(at));
    }

private:
    std::span<std::uint8_t> out_;
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> in) : in_(in) {}
    std::uint8_t u8(std::size_t at) const { return in_[at]; }
    std::uint16_t u16(std::size_t at) const { return std::uint16_t((in_[at] << 8) | in_[at + 1]); }
    std::uint32_t u32(std::size_t at) const { return (std::uint32_t(u16(at)) << 16) | u16(at + 2); }
    std::int32_t i32(std::size_t at) const { return std::int32_t(u32(at)); }
    template <std::size_t N> void ints(std::size_t at, std::array<std::int32_t, N>& values) const {
        for (std::size_t i = 0; i < N; ++i) values[i] = i32(at + i * 4);
    }
    template <std::size_t N> void bytes(std::size_t at, std::array<std::uint8_t, N>& values) const {
        std::copy_n(in_.begin() + std::ptrdiff_t(at), N, values.begin());
    }

private:
    std::span<const std::uint8_t> in_;
};

std::size_t bounded_length(const char* text, std::size_t limit) {
    std::size_t n = 0;
    while (n < limit && text[n]) ++n;
    return n;
}

// Record layout (80364AEC).
constexpr std::size_t kUsed = 0, kUnique = 4, kDigimon = 8, kDigimonSize = 32, kName = 40, kNameChars = 8;
constexpr std::size_t kLevel = 72, kHp = 76, kMaxHp = 80, kMp = 84, kMaxMp = 88, kXdata = 92, kBits = 96;
constexpr std::size_t kDevices = 100, kDisks = 244, kCards = 292, kBank = 344, kBankBits = 728;
constexpr std::size_t kPowerSocket = 732, kPowerPocket = 744, kMainSlot = 764, kMainPocket = 768, kSubSlot = 788;
constexpr std::size_t kFlags = 792, kFlagFolder = 1816, kCounters = 1828, kBaseIsuse = 1892;
constexpr std::size_t kBaseLevel = 1908, kBaseExp = 1972, kBaseSkill = 2036, kBaseUpcnt = 2612;
static_assert(kBaseUpcnt + 16 * 11 * 4 == kRecordSize);
} // namespace

std::array<std::uint8_t, kRecordSize> encode(const Record& r) {
    std::array<std::uint8_t, kRecordSize> out{};
    if (r.blank) return out;
    Writer w(out);
    w.u8(kUsed, r.used ? 1 : 0);
    w.u32(kUnique, r.unique);
    std::memcpy(&out[kDigimon], r.digimon.data(), std::min(r.digimon.size(), kDigimonSize - 1));
    w.u16(kName, 0xFFFF);
    for (std::size_t i = 0; i < std::min(r.name.size(), kNameChars); ++i) w.u16(kName + 2 + i * 2, r.name[i]);
    w.i32(kLevel, r.level);
    w.i32(kHp, r.hp);
    w.i32(kMaxHp, r.max_hp);
    w.i32(kMp, r.mp);
    w.i32(kMaxMp, r.max_mp);
    w.i32(kXdata, r.xdata);
    w.i32(kBits, r.bits);
    w.ints(kDevices, r.devices);
    w.ints(kDisks, r.disks);
    w.bytes(kCards, r.cards);
    w.ints(kBank, r.bank);
    w.i32(kBankBits, r.bank_bits);
    w.ints(kPowerSocket, r.power_socket);
    w.ints(kPowerPocket, r.power_pocket);
    w.i32(kMainSlot, r.main_slot);
    w.ints(kMainPocket, r.main_pocket);
    w.i32(kSubSlot, r.sub_slot);
    w.bytes(kFlags, r.flags);
    w.bytes(kFlagFolder, r.flag_folder);
    w.ints(kCounters, r.counters);
    w.bytes(kBaseIsuse, r.base_isuse);
    w.ints(kBaseLevel, r.base_level);
    w.ints(kBaseExp, r.base_exp);
    for (std::size_t s = 0; s < 16; ++s) w.ints(kBaseSkill + s * 9 * 4, r.base_skill[s]);
    for (std::size_t s = 0; s < 16; ++s) w.ints(kBaseUpcnt + s * 11 * 4, r.base_upcnt[s]);
    return out;
}

Record decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kRecordSize) throw std::runtime_error("save record: short data");
    Reader rd(bytes);
    Record r;
    r.blank = std::all_of(bytes.begin(), bytes.begin() + std::ptrdiff_t(kRecordSize), [](std::uint8_t b) { return b == 0; });
    r.used = rd.u8(kUsed) != 0;
    r.unique = rd.u32(kUnique);
    const auto* name = reinterpret_cast<const char*>(&bytes[kDigimon]);
    r.digimon.assign(name, bounded_length(name, kDigimonSize));
    for (std::size_t i = 0; i < kNameChars; ++i) {
        const std::uint16_t c = rd.u16(kName + 2 + i * 2);
        if (c == 0) break;
        r.name.push_back(char16_t(c));
    }
    r.level = rd.i32(kLevel);
    r.hp = rd.i32(kHp);
    r.max_hp = rd.i32(kMaxHp);
    r.mp = rd.i32(kMp);
    r.max_mp = rd.i32(kMaxMp);
    r.xdata = rd.i32(kXdata);
    r.bits = rd.i32(kBits);
    rd.ints(kDevices, r.devices);
    rd.ints(kDisks, r.disks);
    rd.bytes(kCards, r.cards);
    rd.ints(kBank, r.bank);
    r.bank_bits = rd.i32(kBankBits);
    rd.ints(kPowerSocket, r.power_socket);
    rd.ints(kPowerPocket, r.power_pocket);
    r.main_slot = rd.i32(kMainSlot);
    rd.ints(kMainPocket, r.main_pocket);
    r.sub_slot = rd.i32(kSubSlot);
    rd.bytes(kFlags, r.flags);
    rd.bytes(kFlagFolder, r.flag_folder);
    rd.ints(kCounters, r.counters);
    rd.bytes(kBaseIsuse, r.base_isuse);
    rd.ints(kBaseLevel, r.base_level);
    rd.ints(kBaseExp, r.base_exp);
    for (std::size_t s = 0; s < 16; ++s) rd.ints(kBaseSkill + s * 9 * 4, r.base_skill[s]);
    for (std::size_t s = 0; s < 16; ++s) rd.ints(kBaseUpcnt + s * 11 * 4, r.base_upcnt[s]);
    return r;
}

std::u16string name_to_card(std::string_view ascii) {
    std::u16string out;
    for (const char c : ascii) {
        if (out.size() == kNameChars) break;
        const auto u = static_cast<unsigned char>(c);
        out.push_back(u == ' ' ? char16_t(0x3000) : (u > 0x20 && u < 0x7F) ? char16_t(u + 0xFEE0) : char16_t(u));
    }
    return out;
}

std::string name_from_card(std::u16string_view card) {
    std::string out;
    for (const char16_t c : card) {
        if (c >= 0xFF01 && c <= 0xFF5E) out.push_back(char(c - 0xFEE0));
        else if (c == 0x3000) out.push_back(' ');
        else if (c < 0x80) out.push_back(char(c));
        else out.push_back('?');
    }
    return out;
}

std::uint32_t checksum(std::span<const std::uint8_t> copy) {
    std::uint32_t sum = 0;
    Reader rd(copy);
    for (std::size_t at = 4; at + 4 <= copy.size(); at += 4) sum += rd.u32(at);
    return sum;
}

std::vector<std::uint8_t> write_file(std::span<const std::uint8_t> header, std::span<const Record> records) {
    std::vector<std::uint8_t> file(kFileSize, 0);
    std::copy_n(header.begin(), std::min(header.size(), kHeaderSize), file.begin());
    std::vector<std::uint8_t> copy(kCopySize, 0);
    Writer w(copy);
    w.u32(4, kCopyVersion);
    for (std::size_t i = 0; i < std::min(records.size(), kRecordsPerCopy); ++i) {
        const auto bytes = encode(records[i]);
        std::copy(bytes.begin(), bytes.end(), copy.begin() + std::ptrdiff_t(8 + i * kRecordSize));
    }
    w.u32(0, checksum(copy));
    for (const auto offset : kCopyOffsets) std::copy(copy.begin(), copy.end(), file.begin() + std::ptrdiff_t(offset));
    return file;
}

std::optional<std::vector<Record>> read_file(std::span<const std::uint8_t> file) {
    if (file.size() < kFileSize) return std::nullopt;
    for (const auto offset : kCopyOffsets) {
        const auto copy = file.subspan(offset, kCopySize);
        Reader rd(copy);
        if (rd.u32(4) != kCopyVersion || rd.u32(0) != checksum(copy)) continue;
        std::vector<Record> records;
        for (std::size_t i = 0; i < kRecordsPerCopy; ++i) records.push_back(decode(copy.subspan(8 + i * kRecordSize, kRecordSize)));
        return records;
    }
    return std::nullopt;
}

std::array<std::uint8_t, kGciEntrySize> encode_gci_entry(const GciEntry& e) {
    std::array<std::uint8_t, kGciEntrySize> out{};
    Writer w(out);
    std::memcpy(&out[0], e.game.data(), 4);
    std::memcpy(&out[4], e.maker.data(), 2);
    out[6] = 0xFF;
    out[7] = e.banner_format;
    std::memcpy(&out[8], e.file_name.data(), std::min<std::size_t>(e.file_name.size(), 31));
    w.u32(40, e.modified);
    w.u32(44, e.image_offset);
    w.u16(48, e.icon_format);
    w.u16(50, e.icon_speed);
    out[52] = e.permissions;
    out[53] = e.copies;
    w.u16(54, e.first_block);
    w.u16(56, e.blocks);
    w.u16(58, 0xFFFF);
    w.u32(60, e.comment_offset);
    return out;
}

GciEntry decode_gci_entry(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kGciEntrySize) throw std::runtime_error("GCI entry: short data");
    Reader rd(bytes);
    GciEntry e;
    std::memcpy(e.game.data(), &bytes[0], 4);
    std::memcpy(e.maker.data(), &bytes[4], 2);
    e.banner_format = bytes[7];
    const auto* name = reinterpret_cast<const char*>(&bytes[8]);
    e.file_name.assign(name, bounded_length(name, 32));
    e.modified = rd.u32(40);
    e.image_offset = rd.u32(44);
    e.icon_format = rd.u16(48);
    e.icon_speed = rd.u16(50);
    e.permissions = bytes[52];
    e.copies = bytes[53];
    e.first_block = rd.u16(54);
    e.blocks = rd.u16(56);
    e.comment_offset = rd.u32(60);
    return e;
}

} // namespace dw4::save
