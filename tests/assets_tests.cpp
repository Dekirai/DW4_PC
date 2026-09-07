#include "dw4/assets.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
using namespace dw4::assets;
int checks = 0;

void check(bool value, const std::string& message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}

void rejects(const std::function<void()>& operation, const std::string& message) {
    bool rejected = false;
    try { operation(); } catch (const AssetError&) { rejected = true; }
    check(rejected, message);
}

void le(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8));
}

void be(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value >> ((3 - i) * 8));
}

void str(Bytes& bytes, std::size_t offset, const std::string& value) {
    for (std::size_t i = 0; i < value.size(); ++i) bytes.at(offset + i) = static_cast<std::uint8_t>(value[i]);
}

struct Fixtures {
    std::filesystem::path directory;
    std::vector<std::filesystem::path> files;
    Fixtures() {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto temp = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            directory = temp / ("dw4_assets_tests_" + std::to_string(stamp) + "_" + std::to_string(attempt));
            if (std::filesystem::create_directory(directory)) return;
        }
        throw std::runtime_error("Cannot create unique fixture directory");
    }
    ~Fixtures() {
        std::error_code ec;
        for (const auto& path : files) std::filesystem::remove(path, ec);
        std::filesystem::remove(directory, ec);
    }
    std::filesystem::path write(const std::string& name, const Bytes& bytes) {
        const auto path = directory / name;
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) throw std::runtime_error("Cannot write fixture");
        files.push_back(path);
        return path;
    }
};

Bytes boot() {
    Bytes result(0x440);
    str(result, 0, "GDJEB2");
    str(result, 0x20, "Synthetic fixture only");
    be(result, 0x1c, 0xc2339f3d);
    return result;
}

Bytes dol() {
    Bytes result(0x130);
    be(result, 0, 0x100);
    be(result, 0x48, 0x80003100);
    be(result, 0x90, 0x20);
    be(result, 7 * 4, 0x120);
    be(result, 0x48 + 7 * 4, 0x80004000);
    be(result, 0x90 + 7 * 4, 0x10);
    be(result, 0xd8, 0x80004000); // A legal envelope overlapping initialized data.
    be(result, 0xdc, 0x100);
    be(result, 0xe0, 0x80003100);
    return result;
}

Bytes afs(bool names = true) {
    Bytes result(names ? 224 : 96);
    str(result, 0, "AFS");
    le(result, 4, 2);
    le(result, 8, 64);
    le(result, 12, 4);
    le(result, 16, 80);
    le(result, 20, 2);
    str(result, 64, "ABCD");
    str(result, 80, "EF");
    if (names) {
        le(result, 24, 128);
        le(result, 28, 96);
        str(result, 128, "first.bin");
        str(result, 176, "second.bin");
    }
    return result;
}

void boot_tests(Fixtures& fixtures) {
    auto bytes = boot();
    auto header = read_boot_header(fixtures.write("boot.bin", bytes));
    check(header.is_dw4_usa(), "USA header should match");
    check(header.title == "Synthetic fixture only", "Title decoding");
    bytes[7] = 1;
    check(!read_boot_header(fixtures.write("boot.bin", bytes)).is_dw4_usa(), "Different revisions must not match");
    bytes = boot(); bytes[6] = 1;
    check(!read_boot_header(fixtures.write("boot.bin", bytes)).is_dw4_usa(), "Different discs must not match");
    bytes = boot(); bytes[0] = 1;
    rejects([&] { read_boot_header(fixtures.write("boot.bin", bytes)); }, "Invalid game ID should fail");
    bytes = boot(); bytes[0x1c] = 0;
    rejects([&] { read_boot_header(fixtures.write("boot.bin", bytes)); }, "Invalid GameCube magic should fail");
    bytes.resize(0x43f);
    rejects([&] { read_boot_header(fixtures.write("boot.bin", bytes)); }, "Truncated boot header should fail");
    rejects([&] { read_boot_header(fixtures.directory / "missing.bin"); }, "Missing file should fail");
}

void dol_tests(Fixtures& fixtures) {
    auto bytes = dol();
    const auto info = inspect_dol(fixtures.write("main.dol", bytes));
    check(info.sections.size() == 2, "Two DOL sections expected");
    check(info.sections[0].executable && !info.sections[1].executable, "Text/data classification");
    check(info.sections[1].index == 0, "Data section index starts at zero");
    check(info.entry_point == 0x80003100, "Big-endian DOL entry point");
    const auto invalid = [&](Bytes input, const std::string& message) {
        rejects([&] { inspect_dol(fixtures.write("main.dol", input)); }, message);
    };
    bytes = dol(); bytes.resize(0xff); invalid(bytes, "Truncated DOL header");
    bytes = dol(); bytes.resize(0x12f); invalid(bytes, "Truncated DOL section");
    bytes = dol(); be(bytes, 0, 0x80); invalid(bytes, "DOL section in header");
    bytes = dol(); be(bytes, 0, 0xfffffff0); invalid(bytes, "DOL offset overflow");
    bytes = dol(); be(bytes, 0x90, 0xffffffff); invalid(bytes, "DOL section size overflow");
    bytes = dol(); be(bytes, 7 * 4, 0x110); invalid(bytes, "Overlapping DOL file sections");
    bytes = dol(); be(bytes, 0x48 + 7 * 4, 0x80003110); invalid(bytes, "Overlapping DOL memory sections");
    bytes = dol(); be(bytes, 0x48, 0x817ffff0); invalid(bytes, "DOL section crossing MEM1 boundary");
    bytes = dol(); be(bytes, 0x48, 0x100); invalid(bytes, "DOL section below cached MEM1");
    bytes = dol(); be(bytes, 0xe0, 0x80004000); invalid(bytes, "DOL entry point in data");
    bytes = dol(); be(bytes, 0xe0, 0x80003101); invalid(bytes, "Unaligned DOL entry point");
    bytes = dol(); be(bytes, 0x90, 0); invalid(bytes, "DOL without text");
    bytes = dol(); be(bytes, 0xd8, 0xfffffff0); invalid(bytes, "DOL BSS range overflow");
}

void afs_tests(Fixtures& fixtures) {
    const auto path = fixtures.write("archive.afs", afs());
    auto archive = AfsArchive::open(path);
    check(archive.entries().size() == 2, "AFS entry count");
    check(archive.has_names(), "AFS filename table detected");
    check(archive.entries()[1].name == "second.bin", "AFS filename record stride");
    check(archive.read(0) == Bytes({'A', 'B', 'C', 'D'}), "AFS exact entry reading");
    check(archive.read_prefix(0, 2) == Bytes({'A', 'B'}), "AFS bounded prefix");
    check(archive.read_prefix(1, 100).size() == 2, "AFS prefix capped to entry size");
    check(archive.read_prefix(0, 0).empty(), "AFS zero prefix");
    rejects([&] { archive.read(0, 3); }, "AFS full reads honor allocation limit");
    rejects([&] { archive.read(2); }, "AFS invalid entry index");
    rejects([&] { archive.read_prefix(static_cast<std::size_t>(-1)); }, "AFS huge entry index");
    auto bytes = afs(); bytes.resize(bytes.size() - 1);
    fixtures.write("archive.afs", bytes);
    rejects([&] { archive.read_prefix(0); }, "AFS detects file size change after indexing");
    archive = AfsArchive::open(fixtures.write("archive.afs", afs(false)));
    check(!archive.has_names() && archive.entries()[0].name.empty(), "AFS unnamed archive supported");
    bytes = afs(); le(bytes, 24, 0); le(bytes, 28, 0); le(bytes, 56, 128); le(bytes, 60, 96);
    archive = AfsArchive::open(fixtures.write("archive.afs", bytes));
    check(archive.has_names() && archive.entries()[0].name == "first.bin", "AFS legacy filename pointer supported");
    bytes = afs(false); le(bytes, 8, 24); str(bytes, 24, "DATA");
    archive = AfsArchive::open(fixtures.write("archive.afs", bytes));
    check(!archive.has_names() && archive.read(0) == Bytes({'D', 'A', 'T', 'A'}), "AFS payload directly after index");
    bytes = afs(false); le(bytes, 8, 0); le(bytes, 12, 0);
    archive = AfsArchive::open(fixtures.write("archive.afs", bytes));
    check(archive.read(0).empty(), "AFS empty entries supported");
    bytes = Bytes(8); str(bytes, 0, "AFS");
    check(AfsArchive::open(fixtures.write("archive.afs", bytes)).entries().empty(), "Empty AFS supported");
    const auto invalid = [&](Bytes input, const std::string& message) {
        rejects([&] { AfsArchive::open(fixtures.write("archive.afs", input)); }, message);
    };
    bytes = afs(); bytes.resize(7); invalid(bytes, "Truncated AFS header");
    bytes = afs(); bytes[3] = 1; invalid(bytes, "Bad AFS signature");
    bytes = afs(); le(bytes, 4, 1000001); invalid(bytes, "AFS count allocation safety limit");
    bytes = afs(); bytes.resize(23); invalid(bytes, "Truncated AFS index");
    bytes = afs(); le(bytes, 8, 0xfffffff0); invalid(bytes, "AFS entry offset outside file");
    bytes = afs(); le(bytes, 12, 0xffffffff); invalid(bytes, "AFS entry size overflow");
    bytes = afs(); le(bytes, 8, 16); invalid(bytes, "AFS entry inside index");
    bytes = afs(); le(bytes, 24, 224); invalid(bytes, "AFS filename offset outside file");
    bytes = afs(); le(bytes, 28, 95); invalid(bytes, "AFS filename table too short");
    bytes = afs(); le(bytes, 24, 64); invalid(bytes, "AFS filename table overlapping data");
    bytes = afs(); bytes.resize(223); invalid(bytes, "Truncated AFS filename table");
}

void banner_tests(Fixtures& fixtures) {
    Bytes bytes(0x1960);
    str(bytes, 0, "BNR1");
    const auto pixel = [&](std::size_t n, std::uint16_t value) {
        bytes.at(0x20 + n * 2) = static_cast<std::uint8_t>(value >> 8);
        bytes.at(0x20 + n * 2 + 1) = static_cast<std::uint8_t>(value);
    };
    pixel(0, 0xfc00); // Opaque RGB555 red.
    pixel(1, 0x43a5); // Alpha=4/7, RGB444=(3,10,5).
    pixel(4, 0x83e0); // First pixel of next row inside the tile: green.
    pixel(16, 0x801f); // First pixel of next tile: blue.
    const auto banner = decode_banner(fixtures.write("opening.bnr", bytes));
    check(banner.width == 96 && banner.height == 32 && banner.rgba.size() == 12288, "Banner dimensions");
    check(banner.rgba[0] == 255 && banner.rgba[1] == 0 && banner.rgba[2] == 0 && banner.rgba[3] == 255, "RGB5 decoding");
    check(banner.rgba[4] == 51 && banner.rgba[5] == 170 && banner.rgba[6] == 85 && banner.rgba[7] == 146, "RGB4A3 decoding");
    check(banner.rgba[96 * 4 + 1] == 255, "Banner tile row decoding");
    check(banner.rgba[4 * 4 + 2] == 255, "Banner tile column decoding");
    bytes[3] = '2'; bytes.resize(0x1fa0);
    check(decode_banner(fixtures.write("opening.bnr", bytes)).rgba == banner.rgba, "BNR2 uses same image format");
    bytes[0] = 'X';
    rejects([&] { decode_banner(fixtures.write("opening.bnr", bytes)); }, "Invalid banner signature");
    bytes[0] = 'B'; bytes.resize(0x195f); bytes[3] = '1';
    rejects([&] { decode_banner(fixtures.write("opening.bnr", bytes)); }, "Truncated banner metadata");
    bytes.resize(0x181f);
    rejects([&] { decode_banner(fixtures.write("opening.bnr", bytes)); }, "Truncated banner pixels");
}

void disc_tests(Fixtures& fixtures) {
    fixtures.write("boot.bin", boot());
    fixtures.write("main.dol", dol());
    fixtures.write("archive.afs", afs());
    Bytes banner(0x1960); str(banner, 0, "BNR1");
    fixtures.write("opening.bnr", banner);
    const auto disc = inspect_extracted(fixtures.directory);
    check(disc.header.is_dw4_usa() && disc.dol.sections.size() == 2, "Flat disc inspection");
    check(disc.archives.size() == 1 && disc.archives[0].entry_count == 2, "Disc archive enumeration");
    check(!disc.banner_path.empty() && disc.warnings.empty(), "Disc banner and clean warnings");
    banner[0] = 'X'; fixtures.write("opening.bnr", banner);
    const auto damaged_banner = inspect_extracted(fixtures.directory);
    check(damaged_banner.banner_path.empty() && damaged_banner.warnings.size() == 1, "Invalid optional banner produces warning");
    auto other_boot = boot(); other_boot[7] = 3; fixtures.write("boot.bin", other_boot);
    check(inspect_extracted(fixtures.directory).warnings.size() == 2, "Different game revision produces warning");
}
} // namespace

int main() {
    try {
        Fixtures fixtures;
        boot_tests(fixtures);
        dol_tests(fixtures);
        afs_tests(fixtures);
        banner_tests(fixtures);
        disc_tests(fixtures);
        std::cout << "Passed " << checks << " synthetic asset checks; no game fixtures used.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Asset test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
