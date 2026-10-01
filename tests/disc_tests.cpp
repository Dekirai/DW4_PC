#include "dw4/disc.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks{};
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void put32(std::vector<std::uint8_t>& d, std::size_t at, std::uint32_t v) {
    d[at] = std::uint8_t(v >> 24);
    d[at + 1] = std::uint8_t(v >> 16);
    d[at + 2] = std::uint8_t(v >> 8);
    d[at + 3] = std::uint8_t(v);
}

// A synthetic header and FST: root { ndmw.igb, ndmw.afs, sub { Area.afs } }.
std::vector<std::uint8_t> header() {
    std::vector<std::uint8_t> h(0x440);
    const std::string id = "GDJE52";
    for (std::size_t i = 0; i < id.size(); ++i) h[i] = std::uint8_t(id[i]);
    put32(h, 0x1C, 0xC2339F3D);
    put32(h, 0x420, 0x1000);
    return h;
}

std::vector<std::uint8_t> fst() {
    const std::vector<std::string> names{"ndmw.igb", "ndmw.afs", "sub", "Area.afs"};
    std::vector<std::uint8_t> d(5 * 12);
    std::vector<std::uint32_t> offsets;
    std::string table;
    for (const auto& name : names) {
        offsets.push_back(std::uint32_t(table.size()));
        table += name;
        table.push_back('\0');
    }
    put32(d, 0, 0x01000000);
    put32(d, 8, 5);
    put32(d, 12, offsets[0]);
    put32(d, 16, 0x2000);
    put32(d, 20, 0x100);
    put32(d, 24, offsets[1]);
    put32(d, 28, 0x3000);
    put32(d, 32, 0x800);
    put32(d, 36, 0x01000000 | offsets[2]);
    put32(d, 40, 0);
    put32(d, 44, 5);
    put32(d, 48, offsets[3]);
    put32(d, 52, 0x4000);
    put32(d, 56, 0x400);
    d.insert(d.end(), table.begin(), table.end());
    return d;
}

void test_parse() {
    const auto disc = dw4::DiscImage::parse(header(), fst(), 0x10000);
    check(disc.game_id() == "GDJE52", "The game id is read from the header");
    check(disc.dol_offset() == 0x1000, "The DOL offset is read from the header");
    check(disc.files().size() == 3, "Directories are not files");
    const auto* igb = disc.find("NDMW.IGB");
    check(igb && igb->offset == 0x2000 && igb->size == 0x100, "Files are found without case");
    const auto* area = disc.find("sub/area.afs");
    check(area && area->path == "sub/Area.afs" && area->offset == 0x4000, "Paths include their directories");
    check(!disc.find("area.afs"), "A file below a directory needs its path");
}

void test_rejects() {
    auto bad = header();
    bad[0x1C] = 0;
    bool thrown = false;
    try {
        dw4::DiscImage::parse(bad, fst(), 0x10000);
    } catch (const std::exception&) {
        thrown = true;
    }
    check(thrown, "A header without the disc magic is rejected");
    thrown = false;
    try {
        dw4::DiscImage::parse(header(), fst(), 0x3000);
    } catch (const std::exception&) {
        thrown = true;
    }
    check(thrown, "A file outside the image is rejected");
}
} // namespace

int main() {
    try {
        test_parse();
        test_rejects();
    } catch (const std::exception& error) {
        std::cerr << "disc tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "disc tests passed (" << checks << " checks)\n";
    return 0;
}
