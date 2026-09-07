#include "dw4/assets.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <system_error>

namespace dw4::assets {
namespace {

constexpr std::uint64_t mem1_begin = 0x80000000ull;
constexpr std::uint64_t mem1_end = 0x81800000ull;

std::uint32_t le32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
        (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) |
        (std::uint32_t(p[2]) << 8) | std::uint32_t(p[3]);
}

bool fits(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

bool overlaps(std::uint64_t a, std::uint64_t as, std::uint64_t b, std::uint64_t bs) {
    return as != 0 && bs != 0 && a < b + bs && b < a + as;
}

std::uint64_t size_of(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec)
        throw AssetError("Asset is missing or is not a regular file");
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
        throw AssetError("Cannot determine a supported asset file size");
    return size;
}

std::ifstream open_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw AssetError("Cannot open asset for reading");
    return file;
}

void read_at(std::ifstream& file, std::uint64_t offset, std::uint8_t* out, std::size_t size) {
    if (size == 0) return;
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
        size > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max()))
        throw AssetError("Asset read exceeds stream limits");
    file.seekg(static_cast<std::streamoff>(offset));
    if (!file) throw AssetError("Cannot seek within asset");
    file.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
    if (!file || static_cast<std::size_t>(file.gcount()) != size)
        throw AssetError("Asset is truncated or changed while reading");
}

std::string text_field(const std::uint8_t* data, std::size_t count) {
    std::string result;
    for (std::size_t i = 0; i < count && data[i] != 0; ++i)
        result.push_back(data[i] >= 32 && data[i] < 127 ? static_cast<char>(data[i]) : '?');
    return result;
}

bool mem1_range(std::uint32_t address, std::uint32_t size) {
    return address >= mem1_begin && fits(address, size, mem1_end);
}

std::string lower_ascii(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

} // namespace

bool GameCubeHeader::is_dw4_usa() const noexcept {
    return game_id == "GDJEB2" && disc_number == 0 && revision == 0;
}

GameCubeHeader read_boot_header(const std::filesystem::path& path) {
    if (size_of(path) < 0x440) throw AssetError("GameCube boot.bin is shorter than 0x440 bytes");
    auto file = open_file(path);
    std::array<std::uint8_t, 0x440> bytes{};
    read_at(file, 0, bytes.data(), bytes.size());
    if (be32(bytes.data() + 0x1c) != 0xc2339f3d)
        throw AssetError("boot.bin has no GameCube disc magic");
    for (std::size_t i = 0; i < 6; ++i)
        if (!((bytes[i] >= 'A' && bytes[i] <= 'Z') || (bytes[i] >= '0' && bytes[i] <= '9')))
            throw AssetError("boot.bin contains an invalid six-character disc ID");
    GameCubeHeader result;
    result.game_id = text_field(bytes.data(), 6);
    result.title = text_field(bytes.data() + 0x20, 0x3e0);
    result.disc_number = bytes[6];
    result.revision = bytes[7];
    return result;
}

DolInfo inspect_dol(const std::filesystem::path& path) {
    DolInfo result;
    result.file_size = size_of(path);
    if (result.file_size < 0x100) throw AssetError("DOL header is truncated");
    auto file = open_file(path);
    std::array<std::uint8_t, 0x100> bytes{};
    read_at(file, 0, bytes.data(), bytes.size());
    result.bss_address = be32(bytes.data() + 0xd8);
    result.bss_size = be32(bytes.data() + 0xdc);
    result.entry_point = be32(bytes.data() + 0xe0);
    for (std::size_t i = 0; i < 18; ++i) {
        const auto offset = be32(bytes.data() + i * 4);
        const auto address = be32(bytes.data() + 0x48 + i * 4);
        const auto size = be32(bytes.data() + 0x90 + i * 4);
        if (size == 0) continue;
        if (offset < 0x100 || !fits(offset, size, result.file_size))
            throw AssetError("DOL section points outside its file payload");
        if (!mem1_range(address, size))
            throw AssetError("DOL section is outside GameCube cached MEM1");
        for (const auto& previous : result.sections) {
            if (overlaps(offset, size, previous.file_offset, previous.size))
                throw AssetError("DOL file sections overlap");
            if (overlaps(address, size, previous.address, previous.size))
                throw AssetError("DOL loaded sections overlap in memory");
        }
        result.sections.push_back({i < 7, i < 7 ? i : i - 7, offset, address, size});
    }
    // The BSS envelope may include initialized sections in valid GameCube DOLs.
    // A future loader must preserve those sections when zeroing this range.
    if (result.bss_size && !mem1_range(result.bss_address, result.bss_size))
        throw AssetError("DOL BSS is outside GameCube cached MEM1");
    const bool executable_entry = std::any_of(result.sections.begin(), result.sections.end(),
        [&](const DolSection& section) {
            return section.executable && result.entry_point >= section.address &&
                fits(result.entry_point, 4, std::uint64_t(section.address) + section.size);
        });
    if ((result.entry_point & 3u) != 0 || !executable_entry)
        throw AssetError("DOL entry point is not an aligned instruction inside a text section");
    return result;
}

AfsArchive AfsArchive::open(const std::filesystem::path& path) {
    AfsArchive result;
    result.path_ = std::filesystem::absolute(path);
    result.file_size_ = size_of(path);
    if (result.file_size_ < 8) throw AssetError("AFS header is truncated");
    auto file = open_file(path);
    std::array<std::uint8_t, 8> header{};
    read_at(file, 0, header.data(), header.size());
    if (header[0] != 'A' || header[1] != 'F' || header[2] != 'S' || header[3] != 0)
        throw AssetError("Archive does not have an AFS signature");
    const auto count = le32(header.data() + 4);
    if (count > 1000000 || !fits(8, std::uint64_t(count) * 8, result.file_size_))
        throw AssetError("AFS index count exceeds its file or the supported safety limit");
    const std::uint64_t table_end = 8 + std::uint64_t(count) * 8;
    std::vector<std::uint8_t> table(static_cast<std::size_t>(count) * 8);
    read_at(file, 8, table.data(), table.size());
    result.entries_.reserve(count);
    std::uint64_t first_payload = result.file_size_;
    for (std::size_t i = 0; i < count; ++i) {
        const auto offset = le32(table.data() + i * 8);
        const auto size = le32(table.data() + i * 8 + 4);
        if (!fits(offset, size, result.file_size_) || (size && offset < table_end))
            throw AssetError("AFS entry points outside its file payload");
        if (size) first_payload = std::min(first_payload, std::uint64_t(offset));
        result.entries_.push_back({offset, size, {}});
    }
    if (!count) return result;

    // Most AFS files place this pointer after the index; older variants place
    // it in the final eight bytes of padding before the first payload.
    std::array<std::uint8_t, 8> name_pointer{};
    if (fits(table_end, 8, first_payload))
        read_at(file, table_end, name_pointer.data(), name_pointer.size());
    auto names_offset = le32(name_pointer.data());
    auto names_size = le32(name_pointer.data() + 4);
    if (!names_offset && !names_size && first_payload >= table_end + 8 && first_payload != table_end + 8) {
        read_at(file, first_payload - 8, name_pointer.data(), name_pointer.size());
        names_offset = le32(name_pointer.data());
        names_size = le32(name_pointer.data() + 4);
    }
    if (!names_offset && !names_size) return result;
    const auto required_names_size = std::uint64_t(count) * 48;
    if (names_offset < table_end || names_size < required_names_size ||
        !fits(names_offset, names_size, result.file_size_))
        throw AssetError("AFS optional filename table is invalid or truncated");
    for (const auto& entry : result.entries_)
        if (overlaps(names_offset, names_size, entry.offset, entry.size))
            throw AssetError("AFS filename table overlaps a payload entry");
    std::array<std::uint8_t, 48> name_record{};
    for (std::size_t i = 0; i < count; ++i) {
        read_at(file, std::uint64_t(names_offset) + i * 48, name_record.data(), name_record.size());
        result.entries_[i].name = text_field(name_record.data(), 32);
    }
    result.has_names_ = true;
    return result;
}

std::vector<std::uint8_t> AfsArchive::read(std::size_t index, std::size_t max_bytes) const {
    if (index >= entries_.size()) throw AssetError("AFS entry index is out of range");
    if (entries_[index].size > max_bytes) throw AssetError("AFS entry exceeds the requested read limit");
    return read_prefix(index, entries_[index].size);
}

std::vector<std::uint8_t> AfsArchive::read_prefix(std::size_t index, std::size_t max_bytes) const {
    if (index >= entries_.size()) throw AssetError("AFS entry index is out of range");
    if (size_of(path_) != file_size_) throw AssetError("AFS file size changed since indexing");
    const auto& entry = entries_[index];
    const auto count = std::min<std::size_t>(entry.size, max_bytes);
    std::vector<std::uint8_t> bytes(count);
    auto file = open_file(path_);
    read_at(file, entry.offset, bytes.data(), bytes.size());
    return bytes;
}

BannerImage decode_banner(const std::filesystem::path& path) {
    const auto size = size_of(path);
    if (size < 0x1820) throw AssetError("GameCube banner image is truncated");
    auto file = open_file(path);
    std::array<std::uint8_t, 0x1820> bytes{};
    read_at(file, 0, bytes.data(), bytes.size());
    const bool bnr1 = bytes[0] == 'B' && bytes[1] == 'N' && bytes[2] == 'R' && bytes[3] == '1';
    const bool bnr2 = bytes[0] == 'B' && bytes[1] == 'N' && bytes[2] == 'R' && bytes[3] == '2';
    if (!bnr1 && !bnr2) throw AssetError("File has no BNR1 or BNR2 banner signature");
    if (size < (bnr1 ? 0x1960u : 0x1fa0u)) throw AssetError("GameCube banner metadata is truncated");
    BannerImage result;
    result.rgba.resize(static_cast<std::size_t>(result.width) * result.height * 4);
    std::size_t source = 0x20;
    for (std::uint32_t tile_y = 0; tile_y < result.height; tile_y += 4)
        for (std::uint32_t tile_x = 0; tile_x < result.width; tile_x += 4)
            for (std::uint32_t y = 0; y < 4; ++y)
                for (std::uint32_t x = 0; x < 4; ++x) {
                    const auto pixel = (std::uint16_t(bytes[source]) << 8) | bytes[source + 1];
                    source += 2;
                    const auto dest = (static_cast<std::size_t>(tile_y + y) * result.width + tile_x + x) * 4;
                    if (pixel & 0x8000) {
                        const auto expand5 = [](int v) { return static_cast<std::uint8_t>((v << 3) | (v >> 2)); };
                        result.rgba[dest] = expand5((pixel >> 10) & 31);
                        result.rgba[dest + 1] = expand5((pixel >> 5) & 31);
                        result.rgba[dest + 2] = expand5(pixel & 31);
                        result.rgba[dest + 3] = 255;
                    } else {
                        const auto a = (pixel >> 12) & 7;
                        result.rgba[dest] = static_cast<std::uint8_t>(((pixel >> 8) & 15) * 17);
                        result.rgba[dest + 1] = static_cast<std::uint8_t>(((pixel >> 4) & 15) * 17);
                        result.rgba[dest + 2] = static_cast<std::uint8_t>((pixel & 15) * 17);
                        result.rgba[dest + 3] = static_cast<std::uint8_t>((a << 5) | (a << 2) | (a >> 1));
                    }
                }
    return result;
}

DiscInspection inspect_extracted(const std::filesystem::path& root) {
    std::error_code ec;
    if (!std::filesystem::is_directory(root, ec) || ec)
        throw AssetError("Extracted game directory does not exist");
    DiscInspection result;
    result.root = std::filesystem::absolute(root);
    const auto sys = std::filesystem::is_directory(result.root / "sys") ? result.root / "sys" : result.root;
    const auto files = std::filesystem::is_directory(result.root / "files") ? result.root / "files" : result.root;
    result.header = read_boot_header(sys / "boot.bin");
    result.dol_path = sys / "main.dol";
    result.dol = inspect_dol(result.dol_path);
    if (!result.header.is_dw4_usa())
        result.warnings.push_back("This is not the supported GDJEB2 disc 0 revision 0 (Digimon World 4 USA).");
    if (std::filesystem::is_regular_file(files / "opening.bnr")) {
        result.banner_path = files / "opening.bnr";
        try { (void)decode_banner(result.banner_path); }
        catch (const AssetError& error) {
            result.warnings.push_back(std::string("Banner: ") + error.what());
            result.banner_path.clear();
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator(files)) {
        if (!entry.is_regular_file() || lower_ascii(entry.path().extension().string()) != ".afs") continue;
        const auto archive = AfsArchive::open(entry.path());
        result.archives.push_back({entry.path(), archive.file_size(), archive.entries().size(), archive.has_names()});
    }
    std::sort(result.archives.begin(), result.archives.end(), [](const ArchiveSummary& a, const ArchiveSummary& b) {
        return a.path < b.path;
    });
    if (result.archives.empty()) result.warnings.push_back("No AFS archives were found in the extracted files directory.");
    return result;
}

} // namespace dw4::assets
