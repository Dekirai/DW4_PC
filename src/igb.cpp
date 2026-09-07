#include "dw4/igb.hpp"

#include <array>
#include <bit>
#include <fstream>
#include <limits>
#include <utility>

namespace dw4 {
namespace {

constexpr std::size_t kHeaderBytes = 48;
constexpr std::size_t kMaxMetadataBytes = 16u * 1024u * 1024u;
constexpr std::uint32_t kMaxFieldTypes = 4096;
constexpr std::uint32_t kMaxObjectTypes = 65536;
constexpr std::size_t kMaxObjectFields = 1024u * 1024u;
constexpr std::size_t kMaxNameBytes = 4096;

// Format observations, independently implemented here:
// https://gist.github.com/mateon1/b123a53d824305767989ebc2ba3e5814
// Verified against the user's local GDJEB2 ndmw.igb (version 6). No code from
// that exploratory parser or original game data is included in this file.

[[noreturn]] void fail(const std::string& message) {
    throw IgbError("IGB metadata: " + message);
}

std::span<const std::uint8_t> slice(std::span<const std::uint8_t> bytes,
    std::size_t offset, std::size_t length, const char* context) {
    if (offset > bytes.size() || length > bytes.size() - offset) {
        fail(std::string("truncated ") + context);
    }
    return bytes.subspan(offset, length);
}

std::uint32_t read_u32(std::span<const std::uint8_t> bytes,
    std::size_t offset, IgbByteOrder order) {
    const auto word = slice(bytes, offset, 4, "32-bit value");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto index = order == IgbByteOrder::little ? 3 - i : i;
        value = (value << 8u) | word[index];
    }
    return value;
}

std::uint16_t read_u16(std::span<const std::uint8_t> bytes,
    std::size_t offset, IgbByteOrder order) {
    const auto word = slice(bytes, offset, 2, "16-bit value");
    const auto high = order == IgbByteOrder::little ? word[1] : word[0];
    const auto low = order == IgbByteOrder::little ? word[0] : word[1];
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8u) | low);
}

std::size_t checked_end(std::size_t offset, std::size_t length) {
    if (offset > kMaxMetadataBytes || length > kMaxMetadataBytes - offset) {
        fail("metadata exceeds the 16 MiB limit");
    }
    return offset + length;
}

IgbHeader read_header(std::span<const std::uint8_t> bytes) {
    slice(bytes, 0, kHeaderBytes, "header");
    IgbByteOrder order;
    if (read_u32(bytes, 40, IgbByteOrder::little) == 0xfadau) {
        order = IgbByteOrder::little;
    } else if (read_u32(bytes, 40, IgbByteOrder::big) == 0xfadau) {
        order = IgbByteOrder::big;
    } else {
        fail("invalid 0xFADA magic cookie");
    }

    const auto version_flags = read_u32(bytes, 44, order);
    IgbHeader header;
    header.byte_order = order;
    header.version = static_cast<std::uint16_t>(version_flags & 0xffffu);
    header.flags = version_flags & 0xffff0000u;
    if (header.version != 6) {
        fail("only format version 6 is supported (found " +
            std::to_string(header.version) + ")");
    }
    header.entry_buffer_size = read_u32(bytes, 0, order);
    header.entry_count = read_u32(bytes, 4, order);
    header.object_type_buffer_size = read_u32(bytes, 8, order);
    header.object_type_count = read_u32(bytes, 12, order);
    header.object_buffer_size = read_u32(bytes, 16, order);
    header.object_count = read_u32(bytes, 20, order);
    header.memory_reference_buffer_size = read_u32(bytes, 24, order);
    header.memory_reference_count = read_u32(bytes, 28, order);
    header.field_type_buffer_size = read_u32(bytes, 32, order);
    header.field_type_count = read_u32(bytes, 36, order);
    if (header.field_type_count > kMaxFieldTypes ||
        header.object_type_count > kMaxObjectTypes) {
        fail("type count exceeds the configured limit");
    }
    if (header.field_type_count > header.field_type_buffer_size / 12u ||
        header.object_type_count > header.object_type_buffer_size / 24u) {
        fail("type descriptor count does not fit its block");
    }
    checked_end(kHeaderBytes, header.field_type_buffer_size);
    checked_end(kHeaderBytes, header.object_type_buffer_size);
    return header;
}

std::string read_name(std::span<const std::uint8_t> block,
    std::size_t& cursor, std::uint32_t length) {
    if (length == 0 || length > kMaxNameBytes) {
        fail("invalid type name length");
    }
    const auto encoded = slice(block, cursor, length, "type name");
    cursor += length;
    if (encoded.back() != 0) {
        fail("type name is not NUL-terminated");
    }
    std::size_t text_length = encoded.size();
    while (text_length != 0 && encoded[text_length - 1] == 0) {
        --text_length;
    }
    if (text_length == 0) {
        fail("empty type name");
    }
    std::string name;
    name.reserve(text_length);
    for (std::size_t i = 0; i < text_length; ++i) {
        if (encoded[i] == 0) {
            fail("type name contains an embedded NUL");
        }
        name.push_back(static_cast<char>(encoded[i]));
    }
    return name;
}

// Padding is not interpreted as another record. Its size is already included
// in the block boundary from the header, so it cannot shift later tables.
void read_field_types(IgbMetadata& result, std::span<const std::uint8_t> block) {
    const auto count = result.header.field_type_count;
    const auto order = result.header.byte_order;
    std::size_t names = static_cast<std::size_t>(count) * 12u;
    result.field_types.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto descriptor = static_cast<std::size_t>(i) * 12u;
        IgbFieldType type;
        type.name = read_name(block, names, read_u32(block, descriptor, order));
        type.major = read_u32(block, descriptor + 4, order);
        type.minor = read_u32(block, descriptor + 8, order);
        result.field_types.push_back(std::move(type));
    }
}

void read_object_types(IgbMetadata& result, std::span<const std::uint8_t> block) {
    const auto count = result.header.object_type_count;
    const auto order = result.header.byte_order;
    std::size_t dynamic = static_cast<std::size_t>(count) * 24u;
    std::size_t total_fields = 0;
    result.object_types.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto descriptor = static_cast<std::size_t>(i) * 24u;
        IgbObjectType type;
        type.name = read_name(block, dynamic, read_u32(block, descriptor, order));
        type.major = read_u32(block, descriptor + 4, order);
        type.minor = read_u32(block, descriptor + 8, order);
        const auto field_count = read_u32(block, descriptor + 12, order);
        type.parent_index = std::bit_cast<std::int32_t>(read_u32(block, descriptor + 16, order));
        type.slots = read_u32(block, descriptor + 20, order);
        if (type.parent_index < -1 ||
            (type.parent_index >= 0 && static_cast<std::uint32_t>(type.parent_index) >= i)) {
            fail("object type parent must precede its child");
        }
        if (field_count > kMaxObjectFields - total_fields) {
            fail("object field count exceeds the configured limit");
        }
        if (dynamic > block.size() || field_count > (block.size() - dynamic) / 6u) {
            fail("object fields do not fit their metadata block");
        }
        total_fields += field_count;
        type.fields.reserve(field_count);
        for (std::uint32_t j = 0; j < field_count; ++j) {
            IgbObjectField field;
            field.type_index = read_u16(block, dynamic, order);
            field.slot = read_u16(block, dynamic + 2, order);
            field.size = read_u16(block, dynamic + 4, order);
            if (field.type_index >= result.field_types.size()) {
                fail("object field references an unknown field type");
            }
            if (field.slot >= type.slots) {
                fail("object field slot is outside its object type");
            }
            dynamic += 6;
            type.fields.push_back(field);
        }
        result.object_types.push_back(std::move(type));
    }
}

void append_from_file(std::ifstream& file, std::vector<std::uint8_t>& bytes,
    std::size_t amount) {
    const auto offset = bytes.size();
    bytes.resize(checked_end(offset, amount));
    if (amount == 0) {
        return;
    }
    file.read(reinterpret_cast<char*>(bytes.data() + offset),
        static_cast<std::streamsize>(amount));
    if (!file || file.gcount() != static_cast<std::streamsize>(amount)) {
        fail("file ended before its declared metadata");
    }
}

} // namespace

IgbMetadata read_igb_metadata(std::span<const std::uint8_t> bytes) {
    IgbMetadata result;
    result.header = read_header(bytes);
    const auto fields_end = checked_end(kHeaderBytes, result.header.field_type_buffer_size);
    const auto field_block = slice(bytes, kHeaderBytes,
        result.header.field_type_buffer_size, "field-type block");
    read_field_types(result, field_block);

    const auto alignment_size = read_u32(bytes, fields_end, result.header.byte_order);
    if (alignment_size < 4) {
        fail("alignment block is smaller than its length word");
    }
    const auto objects_start = checked_end(fields_end, alignment_size);
    slice(bytes, fields_end, alignment_size, "alignment block");
    result.metadata_bytes = checked_end(objects_start, result.header.object_type_buffer_size);
    const auto object_block = slice(bytes, objects_start,
        result.header.object_type_buffer_size, "object-type block");
    result.alignment_bytes = alignment_size;
    read_object_types(result, object_block);
    return result;
}

IgbMetadata read_igb_metadata(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        fail("cannot open the input file");
    }
    std::vector<std::uint8_t> bytes;
    append_from_file(file, bytes, kHeaderBytes);
    const auto header = read_header(bytes);
    append_from_file(file, bytes, static_cast<std::size_t>(header.field_type_buffer_size) + 4u);
    const auto alignment_size = read_u32(bytes, bytes.size() - 4, header.byte_order);
    if (alignment_size < 4) {
        fail("alignment block is smaller than its length word");
    }
    append_from_file(file, bytes, alignment_size - 4u);
    append_from_file(file, bytes, header.object_type_buffer_size);
    return read_igb_metadata(std::span<const std::uint8_t>(bytes));
}

} // namespace dw4
