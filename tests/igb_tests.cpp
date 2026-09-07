#include "dw4/igb.hpp"
#include "dw4/assets.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void put32(Bytes& bytes, std::size_t at, std::uint32_t value, bool big) {
    require(at + 4 <= bytes.size(), "fixture write outside buffer");
    for (std::size_t i = 0; i < 4; ++i) {
        const auto shift = static_cast<unsigned>(big ? 3 - i : i) * 8u;
        bytes[at + i] = static_cast<std::uint8_t>((value >> shift) & 0xffu);
    }
}

void append16(Bytes& bytes, std::uint16_t value, bool big) {
    if (big) {
        bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
        bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    } else {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
        bytes.push_back(static_cast<std::uint8_t>(value >> 8u));
    }
}

std::uint32_t append_name(Bytes& bytes, const std::string& name) {
    for (const char c : name) {
        bytes.push_back(static_cast<std::uint8_t>(c));
    }
    bytes.push_back(0);
    return static_cast<std::uint32_t>(name.size() + 1);
}

struct Fixture {
    Bytes bytes;
    std::size_t alignment_offset{};
    std::size_t object_block{};
    std::size_t first_object_field{};
    std::size_t first_object_name{};
};

// Entirely synthetic metadata: no bytes copied from the game or other parsers.
Fixture make_fixture(bool big) {
    Fixture fixture;
    Bytes fields(24, 0);
    put32(fields, 0, append_name(fields, "NumberField"), big);
    put32(fields, 4, 1, big);
    put32(fields, 8, 7, big);
    put32(fields, 12, append_name(fields, "ReferenceField"), big);
    put32(fields, 16, 2, big);
    put32(fields, 20, 3, big);

    Bytes types(48, 0);
    const auto first_name_at = types.size();
    put32(types, 0, append_name(types, "BaseType"), big);
    put32(types, 4, 1, big);
    put32(types, 8, 2, big);
    put32(types, 12, 1, big);
    put32(types, 16, 0xffffffffu, big);
    put32(types, 20, 3, big);
    const auto first_field_at = types.size();
    append16(types, 0, big);
    append16(types, 2, big);
    append16(types, 4, big);

    put32(types, 24, append_name(types, "ChildType"), big);
    put32(types, 28, 0, big); // Preserve an on-disk zero major version.
    put32(types, 32, 4, big);
    put32(types, 36, 2, big);
    put32(types, 40, 0, big);
    put32(types, 44, 5, big);
    append16(types, 0, big);
    append16(types, 2, big);
    append16(types, 4, big);
    append16(types, 1, big);
    append16(types, 4, big);
    append16(types, 4, big);

    fixture.bytes.resize(48, 0);
    put32(fixture.bytes, 0, 100, big);
    put32(fixture.bytes, 4, 3, big);
    put32(fixture.bytes, 8, static_cast<std::uint32_t>(types.size()), big);
    put32(fixture.bytes, 12, 2, big);
    put32(fixture.bytes, 16, 200, big);
    put32(fixture.bytes, 20, 9, big);
    put32(fixture.bytes, 24, 50, big);
    put32(fixture.bytes, 28, 4, big);
    put32(fixture.bytes, 32, static_cast<std::uint32_t>(fields.size()), big);
    put32(fixture.bytes, 36, 2, big);
    put32(fixture.bytes, 40, 0xfadau, big);
    put32(fixture.bytes, 44, 0xf0000006u, big);
    fixture.bytes.insert(fixture.bytes.end(), fields.begin(), fields.end());
    fixture.alignment_offset = fixture.bytes.size();
    fixture.bytes.resize(fixture.bytes.size() + 8, 0);
    put32(fixture.bytes, fixture.alignment_offset, 8, big);
    fixture.object_block = fixture.bytes.size();
    fixture.first_object_field = fixture.object_block + first_field_at;
    fixture.first_object_name = fixture.object_block + first_name_at;
    fixture.bytes.insert(fixture.bytes.end(), types.begin(), types.end());
    return fixture;
}

void rejected(const Bytes& bytes, const char* message) {
    bool threw = false;
    try {
        static_cast<void>(dw4::read_igb_metadata(std::span<const std::uint8_t>(bytes)));
    } catch (const dw4::IgbError&) {
        threw = true;
    }
    require(threw, message);
}

void test_valid_and_invalid(bool big) {
    const auto fixture = make_fixture(big);
    const auto result = dw4::read_igb_metadata(std::span<const std::uint8_t>(fixture.bytes));
    require(result.header.byte_order == (big ? dw4::IgbByteOrder::big : dw4::IgbByteOrder::little),
        "wrong byte order");
    require(result.header.version == 6 && result.header.flags == 0xf0000000u,
        "version/flags were not preserved");
    require(result.header.entry_count == 3 && result.header.object_count == 9 &&
        result.header.memory_reference_count == 4, "wrong payload counts");
    require(result.field_types.size() == 2 && result.object_types.size() == 2,
        "wrong type counts");
    require(result.field_types[0].name == "NumberField" &&
        result.field_types[0].major == 1 && result.field_types[0].minor == 7,
        "wrong field type descriptor");
    require(result.object_types[0].name == "BaseType" &&
        result.object_types[0].parent_index == -1 && result.object_types[0].slots == 3,
        "wrong root type descriptor");
    const auto& child = result.object_types[1];
    require(child.name == "ChildType" && child.parent_index == 0 &&
        child.major == 0 && child.minor == 4 && child.fields.size() == 2,
        "wrong child type descriptor");
    require(child.fields[1].type_index == 1 && child.fields[1].slot == 4 &&
        child.fields[1].size == 4, "wrong object field descriptor");
    require(result.metadata_bytes == fixture.bytes.size() && result.alignment_bytes == 8,
        "wrong metadata block end");

    // Every truncated prefix must fail before yielding any partial metadata.
    for (std::size_t length = 0; length < fixture.bytes.size(); ++length) {
        rejected(Bytes(fixture.bytes.begin(), fixture.bytes.begin() +
            static_cast<Bytes::difference_type>(length)), "accepted truncated metadata");
    }
    auto trailing_payload = fixture.bytes;
    trailing_payload.insert(trailing_payload.end(), {0xff, 0xaa, 0x12, 0x34});
    require(dw4::read_igb_metadata(std::span<const std::uint8_t>(trailing_payload)).metadata_bytes ==
        fixture.bytes.size(), "metadata parser consumed payload bytes");

    auto mutate_word = [&](std::size_t at, std::uint32_t value, const char* message) {
        auto changed = fixture.bytes;
        put32(changed, at, value, big);
        rejected(changed, message);
    };
    mutate_word(40, 0, "accepted an invalid cookie");
    mutate_word(44, 0xf0000007u, "accepted unsupported version 7");
    mutate_word(44, 0xf0000005u, "accepted unsupported version 5");
    mutate_word(36, 0xffffffffu, "accepted an excessive field type count");
    mutate_word(12, 0xffffffffu, "accepted an excessive object type count");
    mutate_word(32, 0xffffffffu, "accepted oversized field type block");
    mutate_word(8, 0xffffffffu, "accepted oversized object type block");
    mutate_word(32, 1, "accepted field descriptors outside their block");
    mutate_word(8, 1, "accepted object descriptors outside their block");
    mutate_word(48, 0, "accepted a zero-length name");
    mutate_word(48, 4097, "accepted an excessive name length");
    mutate_word(48, 300, "accepted a name outside its block");
    mutate_word(fixture.alignment_offset, 0, "accepted missing alignment length");
    mutate_word(fixture.alignment_offset, 3, "accepted undersized alignment block");
    mutate_word(fixture.alignment_offset, 0xffffffffu, "accepted oversized alignment block");
    mutate_word(fixture.object_block + 16, 0, "accepted self-parenting root");
    mutate_word(fixture.object_block + 16, 0xfffffffeu, "accepted invalid negative parent");
    mutate_word(fixture.object_block + 40, 2, "accepted a forward parent");
    mutate_word(fixture.object_block + 12, 0xffffffffu, "accepted excessive object field count");
    mutate_word(fixture.object_block + 12, 20, "accepted fields outside their block");
    mutate_word(fixture.object_block + 20, 2, "accepted object slot outside its type");
    auto invalid_type = fixture.bytes;
    invalid_type[fixture.first_object_field + (big ? 1u : 0u)] = 2;
    rejected(invalid_type, "accepted an unknown field type index");
    auto invalid_name = fixture.bytes;
    invalid_name[fixture.first_object_name + 8] = 'X';
    rejected(invalid_name, "accepted an unterminated name");
    invalid_name = fixture.bytes;
    invalid_name[fixture.first_object_name + 2] = 0;
    rejected(invalid_name, "accepted an embedded NUL in a name");
}

void test_file_reader() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() /
        ("dw4-igb-test-" + std::to_string(stamp));
    require(std::filesystem::create_directory(directory), "synthetic fixture directory already exists");
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(directory / "synthetic.igb", ec);
            std::filesystem::remove(directory, ec);
        }
    } cleanup{directory};
    const auto path = directory / "synthetic.igb";
    const auto fixture = make_fixture(false);
    {
        std::ofstream output(path, std::ios::binary);
        output.write(reinterpret_cast<const char*>(fixture.bytes.data()),
            static_cast<std::streamsize>(fixture.bytes.size()));
        require(static_cast<bool>(output), "cannot write synthetic fixture");
    }
    const auto metadata = dw4::read_igb_metadata(path);
    require(metadata.metadata_bytes == fixture.bytes.size(), "file reader returned wrong block end");
    require(metadata.object_types[1].name == "ChildType", "file reader returned wrong types");
    std::filesystem::resize_file(path, fixture.bytes.size() - 1);
    bool threw = false;
    try {
        static_cast<void>(dw4::read_igb_metadata(path));
    } catch (const dw4::IgbError&) {
        threw = true;
    }
    require(threw, "file reader accepted truncated metadata");
    threw = false;
    try {
        static_cast<void>(dw4::read_igb_metadata(directory / "missing.igb"));
    } catch (const dw4::IgbError&) {
        threw = true;
    }
    require(threw, "file reader accepted a missing file");
}

void inspect_archive(const std::filesystem::path& path) {
    const auto archive = dw4::assets::AfsArchive::open(path);
    std::size_t inspected = 0;
    std::size_t errors = 0;
    std::size_t field_type_max = 0;
    std::size_t object_type_max = 0;
    std::size_t metadata_max = 0;
    for (std::size_t i = 0; i < archive.entries().size(); ++i) {
        const auto prefix = archive.read_prefix(i, 48);
        if (prefix.size() < 48) {
            continue;
        }
        const bool little_cookie = prefix[40] == 0xda && prefix[41] == 0xfa &&
            prefix[42] == 0 && prefix[43] == 0;
        const bool big_cookie = prefix[40] == 0 && prefix[41] == 0 &&
            prefix[42] == 0xfa && prefix[43] == 0xda;
        if (!little_cookie && !big_cookie) {
            continue;
        }
        ++inspected;
        try {
            const auto bytes = archive.read_prefix(i, 16u * 1024u * 1024u);
            const auto metadata = dw4::read_igb_metadata(std::span<const std::uint8_t>(bytes));
            field_type_max = (std::max)(field_type_max, metadata.field_types.size());
            object_type_max = (std::max)(object_type_max, metadata.object_types.size());
            metadata_max = (std::max)(metadata_max, metadata.metadata_bytes);
        } catch (const std::exception& error) {
            ++errors;
            std::cerr << "Archive IGB index " << i << ": " << error.what() << '\n';
        }
    }
    std::cout << "Archive IGB integration: " << inspected << " signatures, " << errors
        << " failures; max field types " << field_type_max << ", max object types "
        << object_type_max << ", max metadata bytes " << metadata_max << '\n';
    require(inspected != 0, "archive contains no recognized IGB inputs");
    require(errors == 0, "one or more original archive entries failed metadata parsing");
}

} // namespace

int main(int argc, char** argv) {
    try {
        test_valid_and_invalid(false);
        test_valid_and_invalid(true);
        test_file_reader();
        // Optional private, read-only integration input; never required by CI.
        if (argc >= 3 && std::string(argv[1]) == "--archives") {
            for (int i = 2; i < argc; ++i) {
                inspect_archive(std::filesystem::path(argv[i]));
            }
        } else if (argc == 2) {
            const auto metadata = dw4::read_igb_metadata(std::filesystem::path(argv[1]));
            std::cout << "Local IGB: version " << metadata.header.version
                << ", field types " << metadata.field_types.size()
                << ", object types " << metadata.object_types.size()
                << ", declared objects " << metadata.header.object_count
                << ", metadata bytes " << metadata.metadata_bytes << '\n';
        }
        std::cout << "IGB metadata tests passed (both byte orders, all truncated prefixes, "
                     "invalid references, limits, and file reads).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "IGB test failure: " << error.what() << '\n';
        return 1;
    }
}
