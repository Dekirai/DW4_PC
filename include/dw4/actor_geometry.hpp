#pragma once

#include "dw4/model_scene.hpp"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace dw4 {
class ActorGeometryBuffer {
public:
    std::vector<std::uint8_t> bytes;
    std::shared_ptr<const IgbDocument> document_owner() const noexcept { return document_; }
    std::optional<std::uint32_t> reference() const noexcept { return reference_; }
private:
    friend class ModelSceneObjects;
    std::shared_ptr<const IgbDocument> document_;
    std::optional<std::uint32_t> reference_;
};
// Retained physical mapped pointer. Replacing an array's current backing does
// not retarget this captured address; scalar reads check only reached bytes.
struct ActorGeometryPointer {
    std::shared_ptr<ActorGeometryBuffer> buffer;
    std::uint32_t byte_offset{};
    explicit operator bool() const noexcept { return bool(buffer) || byte_offset!=0; }
    std::uint16_t read_u16(std::uint32_t offset=0) const;
    std::uint32_t read_u32(std::uint32_t offset=0) const;
    float read_float(std::uint32_t offset=0) const;
    std::array<float,3> read_vec3(std::uint32_t offset=0) const;
};
struct ActorVertexFormat { std::uint32_t word{}; };
struct ActorVertexMapping {
    std::uint32_t unit{},selector{},first{},length{};
    ActorGeometryPointer position;
    std::uint32_t stride{};
    std::uint32_t state{};
};
struct ActorVertexStreams {
    std::vector<std::shared_ptr<ActorGeometryBuffer>> slots;
    std::shared_ptr<const IgbDocument> document;
    std::optional<std::uint32_t> reference;
};
class ActorVertexArray final : public ModelSceneObject {
public:
    explicit ActorVertexArray(std::string type="igVertexArray1_1"):ModelSceneObject(std::move(type)) {}
    std::uint32_t count{},offset{},flags{};
    std::uint32_t unmap_state{}; // Concrete GameCube array +30, set by +70.
    std::shared_ptr<ActorVertexFormat> format=std::make_shared<ActorVertexFormat>();
    std::shared_ptr<ActorVertexStreams> streams;
    std::shared_ptr<ActorGeometryBuffer> blend_weights,blend_indices,extra_data;
    std::shared_ptr<ActorVertexFormat> get_format();
    void map(ActorVertexMapping&,std::int32_t mode=0);
    void unmap(ActorVertexMapping&,std::int32_t mode=1);
    std::function<std::shared_ptr<ActorVertexFormat>(ActorVertexArray&)> format_service;
    std::function<void(ActorVertexArray&,ActorVertexMapping&,std::int32_t)> map_service,unmap_service;
    // 801008C0 reaches the GPU synchronization service after mapping stores
    // when flags bit 2 is set. An absent reached service is a checked boundary.
    std::function<void(ActorVertexArray&)> synchronize_service;
};
class ActorIndexArray final : public ModelSceneObject {
public:
    explicit ActorIndexArray(std::string type="igIndexArray"):ModelSceneObject(std::move(type)) {}
    std::shared_ptr<ActorGeometryBuffer> backing;
    std::uint32_t count{},format{},offset{};
    ActorGeometryPointer map(std::int32_t mode=0,std::uint32_t first=0,std::int32_t length=-1);
    void unmap(const ActorGeometryPointer&,std::int32_t mode=1,std::uint32_t first=0,std::int32_t length=-1);
    std::function<ActorGeometryPointer(ActorIndexArray&,std::int32_t,std::uint32_t,std::int32_t)> map_service;
    std::function<void(ActorIndexArray&,const ActorGeometryPointer&,std::int32_t,std::uint32_t,std::int32_t)> unmap_service;
};
class ActorPrimitiveLengths final : public ModelSceneObject {
public:
    explicit ActorPrimitiveLengths(std::string type="igPrimLengthArray1_1"):ModelSceneObject(std::move(type)) {}
    std::shared_ptr<ActorGeometryBuffer> backing;
    std::uint32_t count{},element_bits{};
    std::uint32_t element(std::uint32_t index);
    std::function<std::uint32_t(ActorPrimitiveLengths&,std::uint32_t)> element_service;
};
class ActorGeometryAttribute : public ModelSceneObject {
public:
    explicit ActorGeometryAttribute(std::string type="igGeometryAttr1_5"):ModelSceneObject(std::move(type)) {}
    std::shared_ptr<ActorVertexArray> vertices;
    std::shared_ptr<ActorIndexArray> index;
    std::shared_ptr<ActorPrimitiveLengths> primitive_lengths;
    std::uint32_t primitive_mode{},primitive_count{},vertex_base{};
    std::int32_t state{};
    bool is_primary() const { return is_a("igGeometryAttr1_5"); }
    bool is_alternate() const { return is_a("igGeometryAttr2"); }
    std::shared_ptr<ActorPrimitiveLengths> groups();
    std::function<std::shared_ptr<ActorPrimitiveLengths>(ActorGeometryAttribute&)> groups_service;
    // Alternate igGeometryAttr2 uses a different vertex/semantic graph. This
    // reached provider represents its preparation up to 801F69FC: true is the
    // semantic6/5 rejection; false reaches the original null primary +AC call.
    std::function<bool(ActorGeometryAttribute&)> alternate_prefix_service;
    bool prepare_alternate();
};
} // namespace dw4
