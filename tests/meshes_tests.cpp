#include "dw4/meshes.hpp"
#include "dw4/assets.hpp"
#include "dw4/igb_document.hpp"

#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {
using namespace dw4;
using Bytes = std::vector<std::uint8_t>;
int checks = 0;

void check(bool condition, const std::string& reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

void word(Bytes& bytes, std::size_t offset, std::uint32_t value, bool big = false) {
    for (std::size_t i = 0; i < 4; ++i)
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> ((big ? 3 - i : i) * 8));
}
void push32(Bytes& bytes, std::uint32_t value, bool big = false) {
    const auto offset = bytes.size(); bytes.resize(offset + 4); word(bytes, offset, value, big);
}
void push16(Bytes& bytes, std::uint16_t value, bool big = false) {
    bytes.push_back(static_cast<std::uint8_t>(big ? value >> 8 : value));
    bytes.push_back(static_cast<std::uint8_t>(big ? value : value >> 8));
}
void name(Bytes& bytes, const std::string& string) {
    bytes.insert(bytes.end(), string.begin(), string.end()); bytes.push_back(0);
}
void words(Bytes& bytes, std::initializer_list<std::uint32_t> values, bool big = false) {
    for (const auto value : values) push32(bytes, value, big);
}
void append(Bytes& target, const Bytes& source) { target.insert(target.end(), source.begin(), source.end()); }

struct Options {
    std::uint32_t vertex_count{3};
    std::uint32_t triangle_count{1};
    std::uint32_t topology{3};
    std::uint32_t vertex_ref{1};
    std::uint32_t index_ref{0xffffffffu};
    std::uint32_t primitive_ref{0xffffffffu};
    std::uint32_t position_ref{3};
    std::uint32_t extra_stream_ref{0xffffffffu};
    std::size_t table_size{80};
    std::size_t position_size{36};
    float first_coordinate{-2.0f};
    bool big{};
    bool strip{};
    bool null_index{};
    bool null_lengths{};
    std::uint32_t geometry_state{};
    std::uint32_t geometry_offset{};
    std::uint32_t vertex_state{};
    std::vector<std::uint16_t> index_values{0,1,2,3};
    std::vector<std::uint32_t> group_lengths{4};
    std::optional<std::uint32_t> index_count;
    std::optional<std::uint32_t> group_count;
    std::optional<std::size_t> index_size;
    std::optional<std::size_t> length_size;
    std::uint32_t index_format{};
    std::uint32_t index_offset{};
    std::uint32_t length_format{32};
};

// A compact, generated IGB with one geometry, one vertex-array object, and
// two memory records. No original file bytes or vertex positions are used.
IgbDocument fixture(Options options = {}) {
    const auto big = options.big;
    const std::vector<std::string> fields{"igStringMetaField", "igIntMetaField", "igObjectRefMetaField",
        "igMemoryRefMetaField", "igBoolMetaField", "igUnsignedIntMetaField", "igEnumMetaField"};
    Bytes field_block(fields.size() * 12);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        word(field_block, i * 12, static_cast<std::uint32_t>(fields[i].size() + 1), big);
        word(field_block, i * 12 + 4, 1, big); name(field_block, fields[i]);
    }
    struct Type { std::string name; std::uint32_t slots; std::vector<IgbObjectField> fields; };
    const std::vector<Type> types{
        {"igObject", 2, {}},
        {"igObjectDirEntry", 13, {{0,2,4},{1,11,4},{1,12,4}}},
        {"igMemoryDirEntry", 14, {{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
        {"igGeometryAttr1_5", 14, {{2,4,4},{2,5,4},{6,6,4},{5,7,4},{5,8,4},{1,10,4},{2,13,4}}},
        {"igVertexArray1_1", 12, {{3,2,4},{5,3,4},{5,4,4}}},
        {"igIndexArray", 7, {{3,2,4},{5,3,4},{6,4,4},{5,5,4}}},
        {"igPrimLengthArray1_1", 5, {{3,2,4},{5,3,4},{5,4,4}}}
    };
    Bytes type_block(types.size() * 24);
    for (std::size_t i = 0; i < types.size(); ++i) {
        word(type_block, i * 24, static_cast<std::uint32_t>(types[i].name.size() + 1), big);
        word(type_block, i * 24 + 4, 1, big);
        word(type_block, i * 24 + 12, static_cast<std::uint32_t>(types[i].fields.size()), big);
        word(type_block, i * 24 + 16, i == 0 ? 0xffffffffu : 0u, big);
        word(type_block, i * 24 + 20, types[i].slots, big);
        name(type_block, types[i].name);
        for (const auto& field : types[i].fields) {
            push16(type_block, field.type_index, big); push16(type_block, field.slot, big); push16(type_block, field.size, big);
        }
    }
    Bytes entries;
    words(entries, {1,20,0,3,0xffffffffu}, big);
    words(entries, {1,20,0,4,0xffffffffu}, big);
    words(entries, {2,32,0,static_cast<std::uint32_t>(options.table_size),0xffffffffu,0,0xffffffffu,0xffffffffu}, big);
    words(entries, {2,32,0,static_cast<std::uint32_t>(options.position_size),0xffffffffu,0,0xffffffffu,0xffffffffu}, big);
    Bytes encoded_indices, encoded_lengths;
    for (auto value : options.index_values) push16(encoded_indices, value, big);
    for (auto value : options.group_lengths) push32(encoded_lengths, value, big);
    if (options.index_size) encoded_indices.resize(*options.index_size);
    if (options.length_size) encoded_lengths.resize(*options.length_size);
    if (options.strip) {
        words(entries, {1,20,0,5,0xffffffffu}, big);
        words(entries, {1,20,0,6,0xffffffffu}, big);
        words(entries, {2,32,0,static_cast<std::uint32_t>(encoded_indices.size()),0xffffffffu,0,0xffffffffu,0xffffffffu}, big);
        words(entries, {2,32,0,static_cast<std::uint32_t>(encoded_lengths.size()),0xffffffffu,0,0xffffffffu,0xffffffffu}, big);
    }
    Bytes objects;
    const auto index_ref = options.strip && !options.null_index && options.index_ref == 0xffffffffu ? 4u : options.index_ref;
    const auto primitive_ref = options.strip && !options.null_lengths && options.primitive_ref == 0xffffffffu ? 5u : options.primitive_ref;
    words(objects, {3,36,options.vertex_ref,index_ref,options.topology,options.triangle_count,
        options.geometry_state,options.geometry_offset,primitive_ref}, big);
    words(objects, {4,20,2,options.vertex_count,options.vertex_state}, big);
    if (options.strip) {
        words(objects, {5,24,6,options.index_count.value_or(static_cast<std::uint32_t>(options.index_values.size())),options.index_format,options.index_offset}, big);
        words(objects, {6,20,7,options.group_count.value_or(static_cast<std::uint32_t>(options.group_lengths.size())),options.length_format}, big);
    }
    Bytes memories((options.table_size + 3) & ~std::size_t(3), 0xff);
    if (options.table_size >= 4) word(memories, 0, options.position_ref, big);
    if (options.table_size >= 8) word(memories, 4, options.extra_stream_ref, big);
    Bytes positions((options.position_size + 3) & ~std::size_t(3));
    const float values[]{options.first_coordinate, 1, 3, 4, -5, 6, 0, 2, -7, 7, 8, 9};
    for (std::size_t i = 0; i < 12 && i * 4 + 4 <= positions.size(); ++i)
        word(positions, i * 4, std::bit_cast<std::uint32_t>(values[i]), big);
    append(memories, positions);
    if (options.strip) {
        encoded_indices.resize((encoded_indices.size() + 3) & ~std::size_t(3));
        encoded_lengths.resize((encoded_lengths.size() + 3) & ~std::size_t(3));
        append(memories, encoded_indices); append(memories, encoded_lengths);
    }
    Bytes bytes;
    const auto references = options.strip ? 8u : 4u;
    const auto object_count = options.strip ? 4u : 2u;
    words(bytes, {static_cast<std::uint32_t>(entries.size()),references,static_cast<std::uint32_t>(type_block.size()),static_cast<std::uint32_t>(types.size()),
        static_cast<std::uint32_t>(objects.size()),object_count,static_cast<std::uint32_t>(memories.size()),object_count,
        static_cast<std::uint32_t>(field_block.size()),static_cast<std::uint32_t>(fields.size()),0xfada,0x20000006}, big);
    append(bytes, field_block); push32(bytes, 4, big); append(bytes, type_block); append(bytes, entries);
    words(bytes, {8 + references * 2,references}, big);
    for (std::uint16_t i = 0; i < references; ++i) push16(bytes, i, big);
    append(bytes, objects); append(bytes, memories);
    return read_igb_document(std::move(bytes));
}

template<class F> void rejects(F&& operation, const std::string& reason) {
    bool rejected = false;
    try { operation(); } catch (const MeshError&) { rejected = true; }
    check(rejected, reason);
}

void synthetic() {
    const auto document = fixture();
    const auto infos = list_igb_meshes(document);
    check(infos.size() == 1 && infos[0].supported && infos[0].reference_index == 0, "List geometry by global reference");
    check(infos[0].vertex_count == 3 && infos[0].triangle_count == 1, "Geometry descriptor counts");
    const auto mesh = decode_igb_mesh(document, 0);
    check(mesh.positions.size() == 3 && mesh.indices == std::vector<std::uint32_t>{0,1,2}, "Unindexed triangle reconstruction");
    check(mesh.positions[0].x == -2 && mesh.positions[1].y == -5 && mesh.positions[2].z == -7, "Little-endian stored coordinates");
    check(mesh.bounds.min.x == -2 && mesh.bounds.min.y == -5 && mesh.bounds.min.z == -7, "Mesh minimum bounds");
    check(mesh.bounds.max.x == 4 && mesh.bounds.max.y == 2 && mesh.bounds.max.z == 6, "Mesh maximum bounds");
    rejects([&] { decode_igb_mesh(document, 1); }, "Vertex array is not geometry");
    rejects([&] { decode_igb_mesh(document, 2); }, "Memory is not geometry");
    rejects([&] { decode_igb_mesh(document, 99); }, "Unknown geometry reference");
    const auto unsupported = [&](Options options, const std::string& reason) {
        const auto doc = fixture(options);
        const auto list = list_igb_meshes(doc);
        check(list.size() == 1 && !list[0].supported && !list[0].diagnostic.empty(), reason + " remains visible");
        rejects([&] { decode_igb_mesh(doc, 0); }, reason + " rejected by decoder");
    };
    auto options = Options{}; options.vertex_count = 0; options.triangle_count = 0; unsupported(options, "Empty geometry");
    options = {}; options.vertex_count = 1048577; unsupported(options, "Vertex allocation cap");
    options = {}; options.vertex_count = 0xffffffff; options.triangle_count = 0xffffffff; unsupported(options, "Count overflow");
    options = {}; options.triangle_count = 2; unsupported(options, "Inconsistent triangle count");
    options = {}; options.topology = 5; unsupported(options, "Unverified topology");
    options = {}; options.index_ref = 1; unsupported(options, "Unverified indexed geometry");
    options = {}; options.primitive_ref = 1; unsupported(options, "Unverified primitive grouping");
    options = {}; options.vertex_ref = 0xffffffff; unsupported(options, "Missing vertex array");
    options = {}; options.vertex_ref = 0; unsupported(options, "Wrong vertex object type");
    options = {}; options.table_size = 76; unsupported(options, "Short stream-reference table");
    options = {}; options.table_size = 84; unsupported(options, "Oversized stream-reference table");
    options = {}; options.position_ref = 0xffffffff; unsupported(options, "Missing position stream");
    options = {}; options.position_ref = 1; unsupported(options, "Nested stream selects object");
    options = {}; options.position_ref = 99; unsupported(options, "Nested stream reference out of bounds");
    options = {}; options.extra_stream_ref = 0; unsupported(options, "Unused populated stream selects object");
    options = {}; options.position_size = 35; unsupported(options, "Truncated position stream");
    options = {}; options.position_size = 40; unsupported(options, "Position stream stride mismatch");
    options = {}; options.big = true; unsupported(options, "Unverified big-endian stream profile");
    options = {}; options.vertex_state = 1; unsupported(options, "Nonzero vertex-array state");
    for (const auto bad : {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        options = {}; options.first_coordinate = bad;
        const auto doc = fixture(options);
        rejects([&] { decode_igb_mesh(doc, 0); }, "Non-finite stored position");
    }
    auto copied = document;
    auto moved = std::move(copied);
    check(decode_igb_mesh(moved, 0).positions[0].x == -2, "Mesh references survive owned-document move");
    check(list_igb_meshes(IgbDocument{}).empty(), "Empty document lists no fabricated meshes");
}

void strip_synthetic() {
    Options base;
    base.strip = true; base.topology = 4; base.vertex_count = 4; base.position_size = 48;
    const auto doc = fixture(base);
    const auto infos = list_igb_meshes(doc);
    check(infos.size() == 1 && infos[0].supported && infos[0].triangle_count == 2, "Strip listing reports expanded triangle count");
    const auto mesh = decode_igb_mesh(doc, 0);
    check(mesh.indices == std::vector<std::uint32_t>{0,1,2,2,1,3}, "Strip alternating winding");
    check(mesh.positions.size() == 4 && mesh.bounds.max.x == 7 && mesh.bounds.max.z == 9, "Strip retains indexed vertex buffer and bounds");
    auto options = base;
    options.index_values = {0,1,2,0,1,2,3}; options.group_lengths = {3,4}; options.triangle_count = 2;
    check(decode_igb_mesh(fixture(options), 0).indices == std::vector<std::uint32_t>{0,1,2,0,1,2,2,1,3}, "Strip winding resets at each explicit group");
    options = base;
    options.index_values = {0,1,2,2,1,3}; options.group_lengths = {6};
    check(decode_igb_mesh(fixture(options), 0).indices == std::vector<std::uint32_t>{0,1,2,1,2,3}, "Repeated-index connectors removed without changing strip parity");
    const auto unsupported = [&](Options bad, const std::string& reason) {
        const auto document = fixture(bad);
        check(!list_igb_meshes(document).at(0).supported, reason + " listed unsupported");
        rejects([&] { decode_igb_mesh(document, 0); }, reason + " decoder rejection");
    };
    options = base; options.null_index = true; unsupported(options, "Strip missing index array");
    options = base; options.null_lengths = true; unsupported(options, "Strip missing primitive lengths");
    options = base; options.index_ref = 1; unsupported(options, "Wrong index-array class");
    options = base; options.primitive_ref = 1; unsupported(options, "Wrong length-array class");
    options = base; options.index_count = 4194305; unsupported(options, "Strip index allocation limit");
    options = base; options.index_count = 2; unsupported(options, "Strip too few indices");
    options = base; options.index_size = 7; unsupported(options, "Truncated index data");
    options = base; options.index_size = 10; unsupported(options, "Index byte-size mismatch");
    options = base; options.index_format = 1; unsupported(options, "Unverified index format");
    options = base; options.index_offset = 1; unsupported(options, "Nonzero index offset");
    options = base; options.geometry_state = 1; unsupported(options, "Nonzero geometry state");
    options = base; options.geometry_offset = 1; unsupported(options, "Nonzero geometry offset");
    options = base; options.index_values[3] = 4; unsupported(options, "Index equals vertex count");
    options = base; options.index_values[3] = 0xffff; unsupported(options, "Unverified restart marker");
    options = base; options.group_count = 2; unsupported(options, "Group count disagrees with geometry");
    options = base; options.triangle_count = 0; unsupported(options, "Zero geometry group count");
    options = base; options.length_size = 3; unsupported(options, "Truncated primitive lengths");
    options = base; options.length_format = 16; unsupported(options, "Unverified primitive-length width");
    options = base; options.group_lengths[0] = 2; unsupported(options, "Short primitive group");
    options = base; options.group_lengths[0] = 3; unsupported(options, "Primitive lengths leave unused indices");
    options = base; options.group_lengths[0] = 5; unsupported(options, "Primitive lengths exceed indices");
    options = base; options.group_lengths[0] = 0xffffffff; unsupported(options, "Primitive-length overflow");
    options = base; options.index_values = {0,0,0,0}; unsupported(options, "All-degenerate strip");
    options = base; options.index_values.resize(1048579);
    for (std::size_t i = 0; i < options.index_values.size(); ++i) options.index_values[i] = static_cast<std::uint16_t>(i % 3);
    options.group_lengths = {static_cast<std::uint32_t>(options.index_values.size())};
    unsupported(options, "Expanded triangle allocation limit");
}

void sample(const std::filesystem::path& path, std::size_t entry) {
    const auto archive = assets::AfsArchive::open(path);
    const auto document = read_igb_document(archive.read(entry));
    std::size_t supported = 0, unsupported = 0;
    for (const auto& info : list_igb_meshes(document)) {
        if (!info.supported) { ++unsupported; continue; }
        const auto mesh = decode_igb_mesh(document, info.reference_index);
        if (supported < 5) std::cout << "Mesh ref=" << info.reference_index << " vertices=" << mesh.positions.size()
            << " triangles=" << mesh.indices.size() / 3 << " bounds=(" << mesh.bounds.min.x << ',' << mesh.bounds.min.y << ',' << mesh.bounds.min.z
            << ")..(" << mesh.bounds.max.x << ',' << mesh.bounds.max.y << ',' << mesh.bounds.max.z << ")\n";
        ++supported;
    }
    check(supported > 0, "Original sample has no supported meshes");
    std::cout << "Original sample: " << supported << " decoded meshes, " << unsupported << " unsupported geometries.\n";
}

void archives(const std::filesystem::path& directory) {
    for (const auto& name : {"area.afs", "ndmw.afs"}) {
        const auto archive = assets::AfsArchive::open(directory / name);
        std::size_t documents = 0, supported = 0, unsupported = 0, triangles = 0;
        std::map<std::string, std::size_t> reasons;
        for (std::size_t i = 0; i < archive.entries().size(); ++i) {
            const auto header = archive.read_prefix(i, 48);
            if (header.size() < 48 || header[40] != 0xda || header[41] != 0xfa || header[42] != 0 || header[43] != 0) continue;
            try {
                const auto document = read_igb_document(archive.read(i));
                ++documents;
                for (const auto& info : list_igb_meshes(document)) {
                    if (!info.supported) { ++unsupported; ++reasons[info.diagnostic]; continue; }
                    const auto mesh = decode_igb_mesh(document, info.reference_index);
                    ++supported; triangles += mesh.indices.size() / 3;
                }
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string(name) + " entry " + std::to_string(i) + ": " + error.what());
            }
        }
        check(documents > 0 && supported > 0, "Corpus contains no supported meshes");
        std::cout << name << ": documents=" << documents << " decoded=" << supported << " triangles=" << triangles << " unsupported=" << unsupported << '\n';
        for (const auto& [reason, count] : reasons) std::cout << "  " << count << " " << reason << '\n';
    }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    try {
        synthetic();
        strip_synthetic();
        std::cout << "Passed " << checks << " synthetic mesh checks.\n";
        if (argc == 3 && std::filesystem::path(argv[1]) == "--archives") archives(argv[2]);
        else if (argc == 3) sample(argv[1], static_cast<std::size_t>(std::stoull(std::filesystem::path(argv[2]).string())));
        else if (argc != 1) throw std::runtime_error("Usage: dw4_meshes_tests [archive.afs entry_index | --archives extracted/files]");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Mesh tests failed: " << error.what() << '\n';
        return 1;
    }
}
