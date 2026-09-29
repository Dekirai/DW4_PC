#include "dw4/engine/igb_view.hpp"

#include <bit>
#include <cstring>
#include <stdexcept>

namespace dw4::engine {
namespace {
std::uint32_t load_u32(std::span<const std::uint8_t> bytes, std::size_t offset, IgbByteOrder order) {
    if (offset + 4 > bytes.size()) throw std::runtime_error("IGB value read outside its field");
    if (order == IgbByteOrder::little)
        return bytes[offset] | (bytes[offset + 1] << 8) | (bytes[offset + 2] << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
    return (std::uint32_t(bytes[offset]) << 24) | (bytes[offset + 1] << 16) | (bytes[offset + 2] << 8) | bytes[offset + 3];
}
} // namespace

std::uint32_t read_u32le(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return load_u32(bytes, offset, IgbByteOrder::little);
}
std::uint16_t read_u16le(std::span<const std::uint8_t> bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) throw std::runtime_error("IGB value read outside its memory block");
    return static_cast<std::uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}
float read_f32le(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return std::bit_cast<float>(read_u32le(bytes, offset));
}

ObjectView::ObjectView(const IgbDocument* document, std::uint32_t reference)
    : document_(document), object_(&document->object(reference)), reference_(reference) {}

const std::string& ObjectView::type() const {
    return document_->metadata.object_types[object_->type_index].name;
}

bool ObjectView::is_a(std::string_view type_name) const {
    return document_ && document_->is_a(*object_, type_name);
}

bool ObjectView::has(std::uint16_t slot) const {
    return document_ && document_->field(*object_, slot) != nullptr;
}

std::span<const std::uint8_t> ObjectView::raw(std::uint16_t slot) const {
    if (!document_) return {};
    const auto* field = document_->field(*object_, slot);
    if (!field) return {};
    return document_->field_bytes(*field);
}

std::uint32_t ObjectView::u32(std::uint16_t slot, std::uint32_t fallback) const {
    const auto bytes = raw(slot);
    if (bytes.size() < 4) return fallback;
    return load_u32(bytes, 0, document_->metadata.header.byte_order);
}

std::int32_t ObjectView::i32(std::uint16_t slot, std::int32_t fallback) const {
    const auto bytes = raw(slot);
    if (bytes.size() < 4) return fallback;
    return std::bit_cast<std::int32_t>(load_u32(bytes, 0, document_->metadata.header.byte_order));
}

std::int16_t ObjectView::i16(std::uint16_t slot, std::int16_t fallback) const {
    const auto bytes = raw(slot);
    if (bytes.size() < 2) return fallback;
    if (document_->metadata.header.byte_order == IgbByteOrder::little)
        return static_cast<std::int16_t>(bytes[0] | (bytes[1] << 8));
    return static_cast<std::int16_t>((bytes[0] << 8) | bytes[1]);
}

float ObjectView::f32(std::uint16_t slot, float fallback) const {
    const auto bytes = raw(slot);
    if (bytes.size() < 4) return fallback;
    return std::bit_cast<float>(load_u32(bytes, 0, document_->metadata.header.byte_order));
}

bool ObjectView::boolean(std::uint16_t slot, bool fallback) const {
    const auto bytes = raw(slot);
    if (bytes.empty()) return fallback;
    return bytes[0] != 0;
}

std::int64_t ObjectView::i64(std::uint16_t slot, std::int64_t fallback) const {
    const auto bytes = raw(slot);
    if (bytes.size() < 8) return fallback;
    const auto order = document_->metadata.header.byte_order;
    const std::uint64_t a = load_u32(bytes, 0, order), b = load_u32(bytes, 4, order);
    const std::uint64_t value = order == IgbByteOrder::little ? (b << 32) | a : (a << 32) | b;
    return std::bit_cast<std::int64_t>(value);
}

std::string_view ObjectView::str(std::uint16_t slot) const {
    if (!document_) return {};
    const auto* field = document_->field(*object_, slot);
    if (!field) return {};
    if (document_->metadata.field_types[field->type_index].name != "igStringMetaField") return {};
    return document_->field_string(*object_, slot);
}

std::optional<Mat4> ObjectView::mat4(std::uint16_t slot) const {
    const auto bytes = raw(slot);
    if (bytes.size() < 64) return std::nullopt;
    Mat4 result;
    for (std::size_t i = 0; i < 16; ++i)
        result.m[i] = std::bit_cast<float>(load_u32(bytes, i * 4, document_->metadata.header.byte_order));
    return result;
}

std::optional<Vec4f> ObjectView::vec4(std::uint16_t slot) const {
    const auto bytes = raw(slot);
    if (bytes.size() < 16) return std::nullopt;
    const auto order = document_->metadata.header.byte_order;
    return Vec4f{std::bit_cast<float>(load_u32(bytes, 0, order)), std::bit_cast<float>(load_u32(bytes, 4, order)),
                 std::bit_cast<float>(load_u32(bytes, 8, order)), std::bit_cast<float>(load_u32(bytes, 12, order))};
}

std::optional<Vec3f> ObjectView::vec3(std::uint16_t slot) const {
    const auto bytes = raw(slot);
    if (bytes.size() < 12) return std::nullopt;
    const auto order = document_->metadata.header.byte_order;
    return Vec3f{std::bit_cast<float>(load_u32(bytes, 0, order)), std::bit_cast<float>(load_u32(bytes, 4, order)),
                 std::bit_cast<float>(load_u32(bytes, 8, order))};
}

ObjectView ObjectView::ref(std::uint16_t slot) const {
    if (!document_) return {};
    const auto* field = document_->field(*object_, slot);
    if (!field) return {};
    const auto& type_name = document_->metadata.field_types[field->type_index].name;
    if (type_name != "igObjectRefMetaField") return {};
    const auto value = load_u32(document_->field_bytes(*field), 0, document_->metadata.header.byte_order);
    if (value == 0xffffffffu) return {};
    return ObjectView(document_, value);
}

std::span<const std::uint8_t> ObjectView::memory(std::uint16_t slot) const {
    if (!document_) return {};
    const auto* field = document_->field(*object_, slot);
    if (!field) return {};
    const auto& type_name = document_->metadata.field_types[field->type_index].name;
    if (type_name != "igMemoryRefMetaField") return {};
    const auto value = load_u32(document_->field_bytes(*field), 0, document_->metadata.header.byte_order);
    if (value == 0xffffffffu) return {};
    return document_->memory(value);
}

std::vector<ObjectView> ObjectView::elements() const {
    std::vector<ObjectView> result;
    if (!document_ || !document_->is_a(*object_, "igObjectList")) return result;
    for (const auto& element : document_->object_list(reference_))
        result.push_back(element ? ObjectView(document_, *element) : ObjectView{});
    return result;
}

std::vector<ObjectView> ObjectView::list(std::uint16_t slot) const {
    const auto target = ref(slot);
    if (!target) return {};
    return target.elements();
}

} // namespace dw4::engine
