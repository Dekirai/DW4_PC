#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace dw4 {

class IgbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class IgbByteOrder { little, big };

struct IgbHeader {
    IgbByteOrder byte_order{};
    std::uint16_t version{};
    std::uint32_t flags{}; // High 16 bits of the version/flags word.
    std::uint32_t entry_buffer_size{};
    std::uint32_t entry_count{};
    std::uint32_t object_type_buffer_size{};
    std::uint32_t object_type_count{};
    std::uint32_t object_buffer_size{};
    std::uint32_t object_count{};
    std::uint32_t memory_reference_buffer_size{};
    std::uint32_t memory_reference_count{};
    std::uint32_t field_type_buffer_size{};
    std::uint32_t field_type_count{};
};

struct IgbFieldType {
    std::string name;
    std::uint32_t major{};
    std::uint32_t minor{};
};

struct IgbObjectField {
    std::uint16_t type_index{};
    std::uint16_t slot{};
    std::uint16_t size{};
};

struct IgbObjectType {
    std::string name;
    std::uint32_t major{};
    std::uint32_t minor{};
    std::int32_t parent_index{-1};
    std::uint32_t slots{};
    std::vector<IgbObjectField> fields;
};

struct IgbMetadata {
    IgbHeader header;
    std::vector<IgbFieldType> field_types;
    std::vector<IgbObjectType> object_types;
    std::size_t alignment_bytes{};
    std::size_t metadata_bytes{}; // End of the object-type metadata block.
};

// Reads only IGB v6 header/type metadata, in either byte order. It does not
// decode or validate object instances, references, textures, or mesh payloads.
// A span containing just the complete metadata prefix is sufficient.
// Defensive limits: 16 MiB metadata, 4,096 field types, 65,536 object types,
// 1,048,576 total object fields, and 4,096 bytes per type name.
IgbMetadata read_igb_metadata(std::span<const std::uint8_t> bytes);
// Reads only the required metadata prefix from the local file.
IgbMetadata read_igb_metadata(const std::filesystem::path& path);

} // namespace dw4
