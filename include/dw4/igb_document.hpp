#pragma once

#include "dw4/igb.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4 {

struct IgbFieldValue {
    std::uint16_t slot{};
    std::uint16_t type_index{}; // Index into metadata.field_types.
    std::size_t offset{};      // Offset into the document's owned bytes.
    std::size_t size{};        // Serialized bytes, including a string's length word.
};

struct IgbEntry {
    std::uint32_t type_index{};
    std::vector<IgbFieldValue> fields;
};

struct IgbObject {
    std::uint32_t reference_index{};
    std::uint32_t type_index{};
    std::vector<IgbFieldValue> fields;
};

struct IgbMemory {
    std::uint32_t reference_index{};
    std::int32_t memory_type_index{-1};
    std::int32_t alignment_type_index{-1};
    std::int32_t pool_index{-1};
    bool reference_counted{};
    std::size_t offset{};
    std::size_t size{};
};

enum class IgbReferenceKind { object, memory };

struct IgbReference {
    IgbReferenceKind kind{};
    std::uint32_t entry_index{};
    std::size_t value_index{}; // Index into objects or memories, according to kind.
};

// A serialized graph with its own backing storage; no guest pointers are used.
// Objects are dense; serialized references use the separate references table.
class IgbDocument {
public:
    IgbMetadata metadata;
    std::vector<std::string> external_directories;
    std::vector<std::string> memory_pool_names;
    std::vector<IgbEntry> entries;
    std::vector<IgbReference> references;
    std::vector<IgbObject> objects;
    std::vector<IgbMemory> memories;
    std::optional<std::uint32_t> info_list_reference;
    std::size_t consumed_bytes{};

    const IgbObject& object(std::uint32_t reference) const;
    std::span<const std::uint8_t> memory(std::uint32_t reference) const;
    const IgbFieldValue* field(const IgbObject& object, std::uint16_t slot) const noexcept;
    std::span<const std::uint8_t> field_bytes(const IgbFieldValue& field) const;
    std::uint32_t field_u32(const IgbObject& object, std::uint16_t slot) const;
    std::int32_t field_i32(const IgbObject& object, std::uint16_t slot) const;
    std::uint16_t field_u16(const IgbObject& object, std::uint16_t slot) const;
    std::int16_t field_i16(const IgbObject& object, std::uint16_t slot) const;
    float field_f32(const IgbObject& object, std::uint16_t slot) const;
    // Returns original string bytes with terminator/padding removed; character
    // encoding is not inferred. Presentation layers must handle encoding.
    std::string_view field_string(const IgbObject& object, std::uint16_t slot) const;
    std::optional<std::uint32_t> field_reference(const IgbObject& object,
        std::uint16_t slot) const;
    bool is_a(const IgbObject& object, std::string_view type_name) const;
    // Decodes the referenced igObjectList's count/capacity/data fields. Every
    // non-null element is checked as an object reference. Null elements remain.
    std::vector<std::optional<std::uint32_t>> object_list(std::uint32_t reference) const;
    std::span<const std::uint8_t> bytes() const noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
    friend IgbDocument read_igb_document(std::vector<std::uint8_t> bytes);
};

// Supported: v6, either byte order, shared entries, inline strings, local object
// and memory references, optional external-directory and memory-pool names.
// External files are never opened by this parser. Limit: 256 MiB input,
// 1,048,576 references/objects/memory records, 4,194,304 field values.
IgbDocument read_igb_document(std::vector<std::uint8_t> bytes);
IgbDocument read_igb_document(const std::filesystem::path& path);

} // namespace dw4
