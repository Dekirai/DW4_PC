#include "dw4/igb_document.hpp"
#include "dw4/assets.hpp"

#include <bit>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void word(Bytes& bytes, std::size_t at, std::uint32_t value, bool big) {
    check(at <= bytes.size() && bytes.size() - at >= 4, "fixture word out of range");
    for (std::size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(
        (value >> (static_cast<unsigned>(big ? 3 - i : i) * 8u)) & 0xffu);
}
void push32(Bytes& bytes, std::uint32_t value, bool big) {
    const auto at = bytes.size(); bytes.resize(at + 4); word(bytes, at, value, big);
}
void push16(Bytes& bytes, std::uint16_t value, bool big) {
    bytes.push_back(static_cast<std::uint8_t>(big ? value >> 8u : value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>(big ? value & 0xffu : value >> 8u));
}
void name(Bytes& bytes, const std::string& value) {
    bytes.insert(bytes.end(), value.begin(), value.end()); bytes.push_back(0);
}
Bytes words(std::initializer_list<std::uint32_t> values, bool big) {
    Bytes result; for (auto value : values) push32(result, value, big); return result;
}
void append(Bytes& target, const Bytes& bytes) { target.insert(target.end(), bytes.begin(), bytes.end()); }

struct Type {
    std::string name;
    std::uint32_t slots;
    std::vector<dw4::IgbObjectField> fields;
};
struct Fixture {
    Bytes bytes;
    std::size_t object_reference_metadata_size{};
    std::size_t memory_reference_metadata_size{};
    std::size_t external{};
    std::size_t pools{};
    std::size_t entries{};
    std::size_t index{};
    std::size_t info{};
    std::size_t object_buffer{};
    std::size_t object_reference{};
    std::size_t memory_reference{};
    std::size_t short_padding{};
    std::size_t list_count{};
    std::size_t memory_buffer{};
};

Fixture fixture(bool big, bool optional_blocks = true) {
    const std::vector<std::string> field_names{"igStringMetaField", "igIntMetaField",
        "igObjectRefMetaField", "igMemoryRefMetaField", "igBoolMetaField",
        "igShortMetaField", "igFloatMetaField"};
    Bytes field_block(field_names.size() * 12, 0);
    for (std::size_t i = 0; i < field_names.size(); ++i) {
        word(field_block, i * 12, static_cast<std::uint32_t>(field_names[i].size() + 1), big);
        word(field_block, i * 12 + 4, 1, big);
        name(field_block, field_names[i]);
    }
    const std::vector<Type> types{
        {"igObject", 2, {}},
        {"igObjectDirEntry", 13, {{0,2,4},{1,11,4},{1,12,4}}},
        {"igMemoryDirEntry", 14, {{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
        {"igObjectList", 5, {{1,2,4},{1,3,4},{3,4,4}}},
        {"ExampleNode", 7, {{0,2,4},{2,3,4},{3,4,4},{5,5,2},{6,6,4}}}
    };
    Bytes type_block(types.size() * 24, 0);
    std::size_t object_reference_metadata_size = 0, memory_reference_metadata_size = 0;
    for (std::size_t i = 0; i < types.size(); ++i) {
        word(type_block, i * 24, static_cast<std::uint32_t>(types[i].name.size() + 1), big);
        word(type_block, i * 24 + 4, 1, big);
        word(type_block, i * 24 + 12, static_cast<std::uint32_t>(types[i].fields.size()), big);
        word(type_block, i * 24 + 16, i == 0 ? 0xffffffffu : 0, big);
        word(type_block, i * 24 + 20, types[i].slots, big);
        name(type_block, types[i].name);
        for (const auto& field : types[i].fields) {
            if (i == 4 && field.slot == 3) object_reference_metadata_size = type_block.size() + 4;
            if (i == 4 && field.slot == 4) memory_reference_metadata_size = type_block.size() + 4;
            push16(type_block, field.type_index, big); push16(type_block, field.slot, big);
            push16(type_block, field.size, big);
        }
    }
    Bytes entry_block;
    append(entry_block, words({1,20,0,3,0xffffffffu}, big));
    append(entry_block, words({1,20,0,4,0xffffffffu}, big));
    append(entry_block, words({2,32,0,8,0xffffffffu,0,0xffffffffu,0xffffffffu}, big));
    Bytes node = words({4,36,8}, big);
    name(node, "Example");
    append(node, words({2,1}, big));
    push16(node, 0xfff9u, big); push16(node, 0, big);
    push32(node, std::bit_cast<std::uint32_t>(1.25f), big);
    check(node.size() == 36, "node fixture has wrong record size");
    Bytes objects = node;
    append(objects, words({3,20,2,2,1}, big));
    Fixture result;
    result.bytes.resize(48);
    const std::uint32_t flags = optional_blocks ? 0xf0000006u : 0xa0000006u;
    const auto header = words({static_cast<std::uint32_t>(entry_block.size()),3,
        static_cast<std::uint32_t>(type_block.size()),static_cast<std::uint32_t>(types.size()),
        static_cast<std::uint32_t>(objects.size()),2,8,1,
        static_cast<std::uint32_t>(field_block.size()),static_cast<std::uint32_t>(field_names.size()),
        0xfadau,flags}, big);
    result.bytes = header; append(result.bytes, field_block); append(result.bytes, words({4}, big));
    result.object_reference_metadata_size = result.bytes.size() + object_reference_metadata_size;
    result.memory_reference_metadata_size = result.bytes.size() + memory_reference_metadata_size;
    append(result.bytes, type_block);
    if (optional_blocks) {
        result.external = result.bytes.size();
        Bytes external = words({36,1,2,7,9}, big); name(external, "models"); name(external, "textures");
        check(external.size() == 36, "external fixture has wrong size"); append(result.bytes, external);
        result.pools = result.bytes.size();
        Bytes pools = words({16,1}, big); name(pools, "default"); append(result.bytes, pools);
    }
    result.entries = result.bytes.size(); append(result.bytes, entry_block);
    result.index = result.bytes.size(); append(result.bytes, words({14,3}, big));
    push16(result.bytes, 1, big); push16(result.bytes, 2, big); push16(result.bytes, 0, big);
    result.info = result.bytes.size(); push32(result.bytes, 2, big);
    result.object_buffer = result.bytes.size(); append(result.bytes, objects);
    result.object_reference = result.object_buffer + 20;
    result.memory_reference = result.object_buffer + 24;
    result.short_padding = result.object_buffer + 30;
    result.list_count = result.object_buffer + 36 + 8;
    result.memory_buffer = result.bytes.size(); append(result.bytes, words({0,0xffffffffu}, big));
    return result;
}

template<class F> void rejects(F&& operation, const char* message) {
    bool rejected = false;
    try { operation(); } catch (const dw4::IgbError&) { rejected = true; }
    check(rejected, message);
}

void check_valid(bool big, bool optional_blocks) {
    auto input = fixture(big, optional_blocks);
    auto doc = dw4::read_igb_document(input.bytes);
    check(doc.objects.size() == 2 && doc.memories.size() == 1 && doc.references.size() == 3,
        "wrong graph record counts");
    check(doc.info_list_reference == 2 && doc.consumed_bytes == input.bytes.size(), "wrong root or stream end");
    check(doc.external_directories.size() == (optional_blocks ? 2u : 0u) &&
        doc.memory_pool_names.size() == (optional_blocks ? 1u : 0u), "wrong optional tables");
    if (optional_blocks) check(doc.external_directories[1] == "textures" &&
        doc.memory_pool_names[0] == "default", "wrong optional strings");
    const auto& node = doc.object(0);
    check(node.type_index == 4 && doc.field_string(node,2) == "Example", "wrong object or inline string");
    check(doc.field_reference(node,3) == 2 && doc.field_reference(node,4) == 1, "wrong typed references");
    check(doc.field_i16(node,5) == -7 && doc.field_f32(node,6) == 1.25f, "wrong typed scalars");
    check(doc.is_a(node,"igObject") && doc.is_a(node,"ExampleNode") && !doc.is_a(node,"igObjectList"),
        "wrong type ancestry");
    check(doc.memory(1).size() == 8 && doc.field(node,63) == nullptr, "wrong memory or missing field");
    const auto list = doc.object_list(2);
    check(list.size() == 2 && list[0] == 0 && !list[1], "object list did not preserve nullable references");
    rejects([&] { static_cast<void>(doc.object(1)); }, "memory accepted as object");
    rejects([&] { static_cast<void>(doc.memory(0)); }, "object accepted as memory");
    rejects([&] { static_cast<void>(doc.field_u32(node,5)); }, "short accepted as 32-bit scalar");
    rejects([&] { static_cast<void>(doc.field_reference(node,6)); }, "float accepted as reference");
    rejects([&] { static_cast<void>(doc.field_string(node,3)); }, "reference accepted as string");
    rejects([&] { static_cast<void>(doc.object_list(0)); }, "node accepted as object list");
    // The copied/moved document must refer to its own storage, not the old vector.
    auto copied = doc; doc = dw4::IgbDocument{};
    auto moved = std::move(copied);
    check(moved.field_string(moved.object(0),2) == "Example", "backing storage invalid after copy/move");

    // Two indexed objects can reuse one directory-entry template without
    // becoming the same host record. Replace the list with another node.
    auto reused = input.bytes;
    const auto last_index = input.index + 12;
    reused[last_index] = big ? 0 : 1;
    reused[last_index + 1] = big ? 1 : 0;
    const Bytes node_record(input.bytes.begin() + static_cast<std::ptrdiff_t>(input.object_buffer),
        input.bytes.begin() + static_cast<std::ptrdiff_t>(input.object_buffer + 36));
    reused.erase(reused.begin() + static_cast<std::ptrdiff_t>(input.object_buffer + 36),
        reused.begin() + static_cast<std::ptrdiff_t>(input.memory_buffer));
    reused.insert(reused.begin() + static_cast<std::ptrdiff_t>(input.object_buffer + 36),
        node_record.begin(), node_record.end());
    word(reused,16,72,big);
    const auto shared = dw4::read_igb_document(std::move(reused));
    check(shared.references[0].entry_index == shared.references[2].entry_index &&
        &shared.object(0) != &shared.object(2) && shared.object(2).reference_index == 2,
        "shared directory entry was not expanded into distinct records");
}

void check_invalid(bool big) {
    const auto input = fixture(big);
    for (std::size_t size = 0; size < input.bytes.size(); ++size) {
        Bytes cut(input.bytes.begin(), input.bytes.begin() + static_cast<std::ptrdiff_t>(size));
        rejects([&] { static_cast<void>(dw4::read_igb_document(std::move(cut))); }, "truncated graph accepted");
    }
    auto mutate = [&](std::size_t at, std::uint32_t value) {
        auto bytes = input.bytes; word(bytes, at, value, big);
        rejects([&] { static_cast<void>(dw4::read_igb_document(std::move(bytes))); }, "invalid graph word accepted");
    };
    mutate(44,0xd0000006u); // Non-shared encoding.
    mutate(input.external,8); mutate(input.external+8,0xffffffffu);
    mutate(input.external+12,0xffffffffu); mutate(input.pools+4,0xffffffffu);
    mutate(input.entries+4,4); mutate(input.entries,99);
    mutate(input.index,8); mutate(input.index+4,99);
    mutate(input.info,1); // Root cannot be a memory reference.
    mutate(input.object_buffer,3); // Record type mismatch.
    mutate(input.object_buffer+8,0xffffffffu); // Inline string length.
    mutate(input.object_reference,1); // Object reference points to memory.
    mutate(input.memory_reference,0); // Memory reference points to object.
    mutate(input.object_reference,99); mutate(input.memory_reference,99);
    mutate(20,3); mutate(28,2); // Header count mismatch.
    auto malformed = input.bytes; malformed[input.short_padding] = 1;
    rejects([&] { static_cast<void>(dw4::read_igb_document(malformed)); }, "nonzero field padding accepted");
    malformed = input.bytes; malformed.push_back(0);
    rejects([&] { static_cast<void>(dw4::read_igb_document(malformed)); }, "extra bytes accepted");
    malformed = input.bytes; word(malformed,input.object_reference,0xffffffffu,big);
    auto nullable = dw4::read_igb_document(malformed);
    check(!nullable.field_reference(nullable.object(0),3), "null object reference not preserved");
    malformed = input.bytes; word(malformed,input.list_count,3,big);
    auto oversized_list = dw4::read_igb_document(malformed);
    rejects([&] { static_cast<void>(oversized_list.object_list(2)); }, "count larger than capacity accepted");
    malformed = input.bytes; word(malformed,input.memory_buffer,1,big);
    auto invalid_list = dw4::read_igb_document(malformed);
    rejects([&] { static_cast<void>(invalid_list.object_list(2)); }, "list memory element accepted as object");

    // Keep all block/record lengths valid so these regressions specifically
    // exercise scalar references being mistaken for arrays of zero/two words.
    malformed = input.bytes;
    malformed[input.object_reference_metadata_size] = big ? 0 : 8;
    malformed[input.object_reference_metadata_size + 1] = big ? 8 : 0;
    word(malformed,input.object_buffer+4,40,big); word(malformed,16,60,big);
    malformed.insert(malformed.begin() + static_cast<std::ptrdiff_t>(input.object_reference+4),4,0);
    rejects([&] { static_cast<void>(dw4::read_igb_document(malformed)); }, "two-word single object reference accepted");
    malformed = input.bytes;
    malformed[input.memory_reference_metadata_size] = 0;
    malformed[input.memory_reference_metadata_size + 1] = 0;
    word(malformed,input.object_buffer+4,32,big); word(malformed,16,52,big);
    malformed.erase(malformed.begin() + static_cast<std::ptrdiff_t>(input.memory_reference),
        malformed.begin() + static_cast<std::ptrdiff_t>(input.memory_reference+4));
    rejects([&] { static_cast<void>(dw4::read_igb_document(malformed)); }, "zero-word single memory reference accepted");
}

void integration(const std::filesystem::path& path) {
    const auto doc = dw4::read_igb_document(path);
    std::size_t lists = 0;
    for (const auto& object : doc.objects) if (doc.is_a(object,"igObjectList")) {
        static_cast<void>(doc.object_list(object.reference_index)); ++lists;
    }
    std::cout << "Local document: " << doc.objects.size() << " objects, " << doc.memories.size()
        << " memories, " << doc.external_directories.size() << " external directories, "
        << doc.memory_pool_names.size() << " pool names, " << lists << " validated object lists.\n";
}

void integration_archive(const std::filesystem::path& path) {
    const auto archive = dw4::assets::AfsArchive::open(path);
    std::size_t files = 0, errors = 0, objects = 0, memories = 0;
    for (std::size_t i = 0; i < archive.entries().size(); ++i) {
        const auto prefix = archive.read_prefix(i,48);
        if (prefix.size() < 48 || !((prefix[40]==0xda && prefix[41]==0xfa && prefix[42]==0 && prefix[43]==0) ||
            (prefix[40]==0 && prefix[41]==0 && prefix[42]==0xfa && prefix[43]==0xda))) continue;
        ++files;
        try {
            const auto doc = dw4::read_igb_document(archive.read(i,256u*1024u*1024u));
            objects += doc.objects.size(); memories += doc.memories.size();
            for (const auto& object : doc.objects) if (doc.is_a(object,"igObjectList"))
                static_cast<void>(doc.object_list(object.reference_index));
        } catch (const std::exception& error) {
            ++errors; std::cerr << "IGB document entry " << i << ": " << error.what() << '\n';
        }
    }
    std::cout << "Archive documents: " << files << " inputs, " << errors << " failures, "
        << objects << " objects, " << memories << " memories.\n";
    check(files != 0 && errors == 0,"original archive graph integration failed");
}
} // namespace

int main(int argc,char** argv) {
    try {
        for (bool big : {false,true}) { check_valid(big,true); check_valid(big,false); check_invalid(big); }
        if (argc >= 3 && std::string(argv[1]) == "--archives")
            for (int i=2;i<argc;++i) integration_archive(std::filesystem::path(argv[i]));
        else if (argc==2) integration(std::filesystem::path(argv[1]));
        std::cout << "IGB document tests passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "IGB document test failure: " << error.what() << '\n'; return 1; }
}
