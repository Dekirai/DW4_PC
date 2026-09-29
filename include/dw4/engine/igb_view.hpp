#pragma once

#include "dw4/engine/math.hpp"
#include "dw4/igb_document.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::engine {

// Lightweight read access to one serialized IGB object. Slots are the
// original reflected field slots (inherited slots included), as recovered
// from the executable's registration tables.
class ObjectView {
public:
    ObjectView() = default;
    ObjectView(const IgbDocument* document, std::uint32_t reference);

    explicit operator bool() const noexcept { return document_ != nullptr; }
    const IgbDocument* document() const noexcept { return document_; }
    std::uint32_t reference() const noexcept { return reference_; }
    const IgbObject& object() const { return *object_; }
    const std::string& type() const;
    bool is_a(std::string_view type_name) const;

    bool has(std::uint16_t slot) const;
    std::int32_t i32(std::uint16_t slot, std::int32_t fallback = 0) const;
    std::uint32_t u32(std::uint16_t slot, std::uint32_t fallback = 0) const;
    std::int16_t i16(std::uint16_t slot, std::int16_t fallback = 0) const;
    float f32(std::uint16_t slot, float fallback = 0) const;
    bool boolean(std::uint16_t slot, bool fallback = false) const;
    std::int64_t i64(std::uint16_t slot, std::int64_t fallback = 0) const;
    std::string_view str(std::uint16_t slot) const;
    std::optional<Mat4> mat4(std::uint16_t slot) const;
    std::optional<Vec4f> vec4(std::uint16_t slot) const;
    std::optional<Vec3f> vec3(std::uint16_t slot) const;
    std::span<const std::uint8_t> raw(std::uint16_t slot) const;
    ObjectView ref(std::uint16_t slot) const;
    std::span<const std::uint8_t> memory(std::uint16_t slot) const;
    // Elements of an igObjectList-derived object referenced by slot.
    std::vector<ObjectView> list(std::uint16_t slot) const;
    // Elements when this object itself is an igObjectList-derived object.
    std::vector<ObjectView> elements() const;
    // igNamedObject name (slot 2) for named objects, otherwise empty.
    std::string_view name() const { return str(2); }

private:
    const IgbDocument* document_{};
    const IgbObject* object_{};
    std::uint32_t reference_{};
};

// Memory-record helpers with explicit little-endian decoding.
std::uint32_t read_u32le(std::span<const std::uint8_t> bytes, std::size_t offset);
std::uint16_t read_u16le(std::span<const std::uint8_t> bytes, std::size_t offset);
float read_f32le(std::span<const std::uint8_t> bytes, std::size_t offset);

} // namespace dw4::engine
