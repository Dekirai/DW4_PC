#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <fstream>
#include <limits>
#include <utility>

namespace dw4 {
namespace {

constexpr std::size_t max_input = 256u * 1024u * 1024u;
constexpr std::size_t max_records = 1024u * 1024u;
constexpr std::size_t max_fields = 4u * 1024u * 1024u;
constexpr std::size_t max_string = 1024u * 1024u;
constexpr std::uint32_t null_reference = 0xffffffffu;

[[noreturn]] void invalid(const std::string& message) {
    throw IgbError("IGB document: " + message);
}

std::span<const std::uint8_t> region(std::span<const std::uint8_t> bytes,
    std::size_t offset, std::size_t size) {
    if (offset > bytes.size() || size > bytes.size() - offset) {
        invalid("a record exceeds its declared block or input");
    }
    return bytes.subspan(offset, size);
}

std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t offset,
    IgbByteOrder order) {
    const auto data = region(bytes, offset, 4);
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value = (value << 8u) | data[order == IgbByteOrder::little ? 3 - i : i];
    }
    return value;
}

std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t offset,
    IgbByteOrder order) {
    const auto data = region(bytes, offset, 2);
    const auto high = data[order == IgbByteOrder::little ? 1 : 0];
    const auto low = data[order == IgbByteOrder::little ? 0 : 1];
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(high) << 8u) | low);
}

std::size_t padded(std::size_t size) {
    if (size > max_input) invalid("field or memory payload exceeds input limit");
    return (size + 3u) & ~std::size_t{3u};
}

std::string_view string_bytes(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return {};
    if (bytes.size() > max_string || bytes.back() != 0) {
        invalid("string is too large or not NUL-terminated");
    }
    while (!bytes.empty() && bytes.back() == 0) bytes = bytes.first(bytes.size() - 1);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

const IgbFieldValue& require_field(const IgbDocument& doc, const IgbObject& object,
    std::uint16_t slot) {
    const auto* found = doc.field(object, slot);
    if (!found) invalid("object has no field at slot " + std::to_string(slot));
    return *found;
}

const IgbFieldValue& entry_field(const IgbEntry& entry, std::uint16_t slot) {
    for (const auto& field : entry.fields) if (field.slot == slot) return field;
    invalid("directory entry is missing required slot " + std::to_string(slot));
}

std::uint32_t entry_word(const IgbDocument& doc, const IgbEntry& entry, std::uint16_t slot) {
    const auto data = doc.field_bytes(entry_field(entry, slot));
    if (data.size() != 4) invalid("directory scalar field is not 32-bit");
    return u32(data, 0, doc.metadata.header.byte_order);
}

class Reader {
public:
    explicit Reader(IgbDocument& document) : doc(document), data(document.bytes()),
        order(document.metadata.header.byte_order), cursor(document.metadata.metadata_bytes) {}

    void read() {
        const auto& header = doc.metadata.header;
        if (!(header.flags & 0x20000000u)) invalid("non-shared directory entries are not supported");
        if (header.entry_count > max_records || header.object_count > max_records ||
            header.memory_reference_count > max_records) invalid("record count exceeds the limit");
        if (header.flags & 0x40000000u) external_dirs();
        if (header.flags & 0x10000000u) pool_names();
        entries();
        index();
        if (header.flags & 0x80000000u) {
            const auto root = u32(take(4), 0, order);
            if (root != null_reference) {
                require_reference(root, IgbReferenceKind::object);
                doc.info_list_reference = root;
            }
        }
        objects();
        memories();
        doc.consumed_bytes = cursor;
        if (cursor != data.size()) invalid("unrecognized bytes after the memory-reference buffer");
        validate_links();
    }

private:
    IgbDocument& doc;
    std::span<const std::uint8_t> data;
    IgbByteOrder order;
    std::size_t cursor;
    std::size_t fields_used{};

    std::span<const std::uint8_t> take(std::size_t size) {
        const auto bytes = region(data, cursor, size);
        cursor += size;
        return bytes;
    }

    std::span<const std::uint8_t> sized_block(std::size_t minimum) {
        const auto size = u32(data, cursor, order);
        if (size < minimum) invalid("length-prefixed block is shorter than its header");
        return take(size);
    }

    void external_dirs() {
        const auto block = sized_block(12);
        const auto count = u32(block, 8, order);
        if (count > max_records || count > (block.size() - 12) / 4u) {
            invalid("external-directory count exceeds its block");
        }
        std::size_t names = 12u + std::size_t{count} * 4u;
        doc.external_directories.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto length = u32(block, 12u + std::size_t{i} * 4u, order);
            const auto value = string_bytes(region(block, names, length));
            names += length;
            doc.external_directories.emplace_back(value);
        }
        if (names != block.size()) invalid("external-directory block contains unparsed bytes");
    }

    void pool_names() {
        const auto block = sized_block(8);
        const auto count = u32(block, 4, order);
        if (count > max_records || count > block.size() - 8) {
            invalid("memory-pool count exceeds its block");
        }
        std::size_t position = 8;
        doc.memory_pool_names.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto rest = block.subspan(position);
            const auto end = std::find(rest.begin(), rest.end(), 0);
            if (end == rest.end()) invalid("unterminated memory-pool name");
            const auto length = static_cast<std::size_t>(end - rest.begin()) + 1;
            doc.memory_pool_names.emplace_back(string_bytes(rest.first(length)));
            position += length;
        }
        if (position != block.size()) invalid("memory-pool block contains unparsed bytes");
    }

    IgbEntry record(std::span<const std::uint8_t> block, std::size_t block_offset,
        std::size_t& position) {
        const auto type = u32(block, position, order);
        const auto length = u32(block, position + 4, order);
        if (length < 8 || type >= doc.metadata.object_types.size()) {
            invalid("invalid record length or object type");
        }
        const auto bytes = region(block, position, length);
        const auto& schema = doc.metadata.object_types[type];
        if (schema.fields.size() > max_fields - fields_used) invalid("too many field values");
        fields_used += schema.fields.size();
        IgbEntry result;
        result.type_index = type;
        result.fields.reserve(schema.fields.size());
        std::size_t field_offset = 8;
        for (const auto& field : schema.fields) {
            std::size_t size = field.size;
            if (doc.metadata.field_types[field.type_index].name == "igStringMetaField") {
                if (size != 4) invalid("string field does not have a 32-bit length word");
                const auto string_size = u32(bytes, field_offset, order);
                if (string_size > max_string) invalid("inline string exceeds the limit");
                size += string_size;
                static_cast<void>(string_bytes(region(bytes, field_offset + 4, string_size)));
            }
            const auto storage_size = padded(size);
            const auto stored = region(bytes, field_offset, storage_size);
            if (std::any_of(stored.begin() + static_cast<std::ptrdiff_t>(size), stored.end(),
                [](std::uint8_t byte) { return byte != 0; })) {
                invalid("nonzero field alignment padding");
            }
            for (const auto& previous : result.fields) {
                if (previous.slot == field.slot) invalid("duplicate object field slot");
            }
            result.fields.push_back({field.slot, field.type_index,
                block_offset + position + field_offset, size});
            field_offset += storage_size;
        }
        if (field_offset != bytes.size()) invalid("record size disagrees with its field layout");
        position += length;
        return result;
    }

    void entries() {
        const auto offset = cursor;
        const auto block = take(doc.metadata.header.entry_buffer_size);
        if (doc.metadata.header.entry_count > block.size() / 8u) {
            invalid("directory record count exceeds its buffer");
        }
        std::size_t position = 0;
        doc.entries.reserve(doc.metadata.header.entry_count);
        for (std::uint32_t i = 0; i < doc.metadata.header.entry_count; ++i) {
            doc.entries.push_back(record(block, offset, position));
        }
        if (position != block.size()) invalid("directory buffer contains unparsed records");
    }

    void index() {
        const auto block = sized_block(8);
        const auto count = u32(block, 4, order);
        if (count > max_records || count != (block.size() - 8) / 2u ||
            (block.size() - 8) % 2u != 0) invalid("invalid reference index size or count");
        if (std::uint64_t{doc.metadata.header.object_count} +
            doc.metadata.header.memory_reference_count != count) {
            invalid("reference count disagrees with object and memory counts");
        }
        doc.references.reserve(count);
        doc.objects.reserve(doc.metadata.header.object_count);
        doc.memories.reserve(doc.metadata.header.memory_reference_count);
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto entry_index = u16(block, 8u + std::size_t{i} * 2u, order);
            if (entry_index >= doc.entries.size()) invalid("reference index selects an unknown entry");
            const auto& entry = doc.entries[entry_index];
            const auto& type_name = doc.metadata.object_types[entry.type_index].name;
            if (type_name == "igObjectDirEntry") {
                const auto object_type = entry_word(doc, entry, 11);
                if (object_type >= doc.metadata.object_types.size()) invalid("unknown object instance type");
                doc.references.push_back({IgbReferenceKind::object, entry_index, doc.objects.size()});
                doc.objects.push_back({i, object_type, {}});
            } else if (type_name == "igMemoryDirEntry") {
                IgbMemory memory;
                memory.reference_index = i;
                memory.size = entry_word(doc, entry, 7);
                if (memory.size > max_input) invalid("memory reference exceeds the input limit");
                memory.memory_type_index = std::bit_cast<std::int32_t>(entry_word(doc, entry, 10));
                const auto flag = doc.field_bytes(entry_field(entry, 11));
                if (flag.size() != 1 || flag[0] > 1) invalid("invalid reference-counted boolean");
                memory.reference_counted = flag[0] != 0;
                memory.alignment_type_index = std::bit_cast<std::int32_t>(entry_word(doc, entry, 12));
                memory.pool_index = std::bit_cast<std::int32_t>(entry_word(doc, entry, 13));
                doc.references.push_back({IgbReferenceKind::memory, entry_index, doc.memories.size()});
                doc.memories.push_back(memory);
            } else {
                invalid("unsupported referenced directory type: " + type_name);
            }
        }
        if (doc.objects.size() != doc.metadata.header.object_count ||
            doc.memories.size() != doc.metadata.header.memory_reference_count) {
            invalid("directory kinds disagree with header object/memory counts");
        }
    }

    void objects() {
        const auto offset = cursor;
        const auto block = take(doc.metadata.header.object_buffer_size);
        if (doc.objects.size() > block.size() / 8u) invalid("object count exceeds its buffer");
        std::size_t position = 0;
        for (auto& object : doc.objects) {
            auto value = record(block, offset, position);
            if (value.type_index != object.type_index) invalid("object record type disagrees with its directory");
            object.fields = std::move(value.fields);
        }
        if (position != block.size()) invalid("object buffer contains unparsed records");
    }

    void memories() {
        const auto offset = cursor;
        const auto block = take(doc.metadata.header.memory_reference_buffer_size);
        std::size_t position = 0;
        for (auto& memory : doc.memories) {
            static_cast<void>(region(block, position, padded(memory.size)));
            memory.offset = offset + position;
            position += padded(memory.size);
        }
        if (position != block.size()) invalid("memory buffer size disagrees with its directory");
    }

    void require_reference(std::uint32_t index, IgbReferenceKind kind) const {
        if (index >= doc.references.size() || doc.references[index].kind != kind) {
            invalid("reference index is out of range or has the wrong kind");
        }
    }

    void validate_links() const {
        for (const auto& object : doc.objects) {
            for (const auto& field : object.fields) {
                const auto& name = doc.metadata.field_types[field.type_index].name;
                const bool object_ref = name == "igObjectRefMetaField" || name == "igObjectRefArrayMetaField";
                const bool memory_ref = name == "igMemoryRefMetaField" || name == "igMemoryRefArrayMetaField";
                if (!object_ref && !memory_ref) continue;
                const auto bytes = doc.field_bytes(field);
                if ((name == "igObjectRefMetaField" || name == "igMemoryRefMetaField") &&
                    bytes.size() != 4) invalid("single reference field must occupy exactly one word");
                if (bytes.size() % 4u != 0) invalid("reference field has a non-word size");
                for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
                    const auto index = u32(bytes, offset, order);
                    if (index != null_reference) require_reference(index,
                        object_ref ? IgbReferenceKind::object : IgbReferenceKind::memory);
                }
            }
        }
    }
};

} // namespace

const IgbObject& IgbDocument::object(std::uint32_t reference) const {
    if (reference >= references.size() || references[reference].kind != IgbReferenceKind::object ||
        references[reference].value_index >= objects.size()) invalid("expected an object reference");
    return objects[references[reference].value_index];
}

std::span<const std::uint8_t> IgbDocument::memory(std::uint32_t reference) const {
    if (reference >= references.size() || references[reference].kind != IgbReferenceKind::memory ||
        references[reference].value_index >= memories.size()) invalid("expected a memory reference");
    const auto& value = memories[references[reference].value_index];
    return region(bytes_, value.offset, value.size);
}

const IgbFieldValue* IgbDocument::field(const IgbObject& object, std::uint16_t slot) const noexcept {
    for (const auto& value : object.fields) if (value.slot == slot) return &value;
    return nullptr;
}

std::span<const std::uint8_t> IgbDocument::field_bytes(const IgbFieldValue& value) const {
    return region(bytes_, value.offset, value.size);
}

std::uint32_t IgbDocument::field_u32(const IgbObject& object, std::uint16_t slot) const {
    const auto data = field_bytes(require_field(*this, object, slot));
    if (data.size() != 4) invalid("requested field is not a 32-bit scalar");
    return u32(data, 0, metadata.header.byte_order);
}

std::int32_t IgbDocument::field_i32(const IgbObject& object, std::uint16_t slot) const {
    return std::bit_cast<std::int32_t>(field_u32(object, slot));
}

std::uint16_t IgbDocument::field_u16(const IgbObject& object, std::uint16_t slot) const {
    const auto data = field_bytes(require_field(*this, object, slot));
    if (data.size() != 2) invalid("requested field is not a 16-bit scalar");
    return u16(data, 0, metadata.header.byte_order);
}

std::int16_t IgbDocument::field_i16(const IgbObject& object, std::uint16_t slot) const {
    return std::bit_cast<std::int16_t>(field_u16(object, slot));
}

float IgbDocument::field_f32(const IgbObject& object, std::uint16_t slot) const {
    return std::bit_cast<float>(field_u32(object, slot));
}

std::string_view IgbDocument::field_string(const IgbObject& object, std::uint16_t slot) const {
    const auto& value = require_field(*this, object, slot);
    if (value.type_index >= metadata.field_types.size() ||
        metadata.field_types[value.type_index].name != "igStringMetaField") {
        invalid("requested field is not an inline string");
    }
    const auto data = field_bytes(value);
    const auto length = u32(data, 0, metadata.header.byte_order);
    if (length != data.size() - 4) invalid("inline string length disagrees with its field");
    return string_bytes(data.subspan(4));
}

std::optional<std::uint32_t> IgbDocument::field_reference(const IgbObject& object,
    std::uint16_t slot) const {
    const auto& field_value = require_field(*this, object, slot);
    if (field_value.type_index >= metadata.field_types.size()) invalid("unknown field type");
    const auto& name = metadata.field_types[field_value.type_index].name;
    if (name != "igObjectRefMetaField" && name != "igMemoryRefMetaField") {
        invalid("requested field is not a single reference");
    }
    const auto reference = field_u32(object, slot);
    if (reference == null_reference) return std::nullopt;
    if (name == "igObjectRefMetaField") static_cast<void>(this->object(reference));
    else static_cast<void>(memory(reference));
    return reference;
}

bool IgbDocument::is_a(const IgbObject& object, std::string_view type_name) const {
    std::size_t type = object.type_index;
    std::size_t traversed = 0;
    while (type < metadata.object_types.size()) {
        if (++traversed > metadata.object_types.size()) invalid("cyclic object type ancestry");
        const auto& value = metadata.object_types[type];
        if (value.name == type_name) return true;
        if (value.parent_index < 0) return false;
        type = static_cast<std::size_t>(value.parent_index);
    }
    invalid("object ancestry references an unknown type");
}

std::vector<std::optional<std::uint32_t>> IgbDocument::object_list(std::uint32_t reference) const {
    const auto& list = object(reference);
    if (!is_a(list, "igObjectList")) invalid("requested object is not an object list");
    const auto count = field_u32(list, 2);
    const auto capacity = field_u32(list, 3);
    if (count > capacity || count > max_records) invalid("invalid object-list count or capacity");
    const auto data_reference = field_reference(list, 4);
    if (!data_reference) {
        if (count != 0) invalid("nonempty object list has no data reference");
        return {};
    }
    const auto data = memory(*data_reference);
    if (capacity > data.size() / 4u) invalid("object-list capacity exceeds its memory block");
    std::vector<std::optional<std::uint32_t>> result;
    result.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto index = u32(data, std::size_t{i} * 4u, metadata.header.byte_order);
        if (index == null_reference) result.push_back(std::nullopt);
        else {
            static_cast<void>(object(index));
            result.push_back(index);
        }
    }
    return result;
}

IgbDocument read_igb_document(std::vector<std::uint8_t> bytes) {
    if (bytes.size() > max_input) invalid("input exceeds the 256 MiB limit");
    IgbDocument result;
    result.bytes_ = std::move(bytes);
    result.metadata = read_igb_metadata(std::span<const std::uint8_t>(result.bytes_));
    Reader(result).read();
    return result;
}

IgbDocument read_igb_document(const std::filesystem::path& path) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) invalid("input is not a regular file");
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > max_input) invalid("input is unavailable or exceeds the 256 MiB limit");
    std::ifstream file(path, std::ios::binary);
    if (!file) invalid("cannot open input file");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || (!bytes.empty() && static_cast<std::size_t>(file.gcount()) != bytes.size())) {
        invalid("input was truncated while reading");
    }
    return read_igb_document(std::move(bytes));
}

} // namespace dw4
