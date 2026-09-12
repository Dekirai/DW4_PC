#pragma once

#include "dw4/controllers.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/meshes.hpp"

#include <array>
#include <memory>
#include <optional>

namespace dw4 {
class ActionEventRuntime;
class NumberControlError : public std::runtime_error {public:using std::runtime_error::runtime_error;};
struct NumberListIdentity {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::uint32_t> memory_reference,memory_owner_reference;
    std::uint32_t count{},capacity{};
};
struct NumberMappingDefinition {
    std::uint32_t reference{},owner_reference{};
    std::int32_t index{},kind{};
};
struct NumberDigitDefinition {
    std::uint32_t reference{},owner_reference{};
    std::int32_t display{};
    NumberListIdentity mappings_source;
    // The helper consumes the first six entries; extra authored entries survive.
    std::vector<NumberMappingDefinition> mappings;
};
struct NumberWorkDefinition {
    std::uint32_t reference{},owner_reference{};
    std::int32_t current{},target{},step{};
};
struct NumberUvDefinition {
    std::uint32_t vertex_array_reference{},vertex_array_owner_reference{};
    std::uint32_t memory_reference{},memory_owner_reference{};
    std::vector<Vec2> texcoords;
};
struct NumberRamDefinition {
    std::uint32_t reference{},owner_reference{};
    std::int32_t authored_index{};
    std::uint8_t active{};
    std::array<std::int32_t,4> authored_words{};
    std::optional<std::uint32_t> data_reference,geometry_reference,geometry_owner_reference,attribute_reference;
    std::optional<std::uint32_t> vertex_array_reference,vertex_array_owner_reference;
    float offset_next{};
    std::int32_t maximum{},mode{};
    std::optional<NumberListIdentity> digits_source;
    std::vector<NumberDigitDefinition> digits;
    std::optional<NumberWorkDefinition> work;
    std::shared_ptr<const NumberUvDefinition> uv;
};
struct NumberInfoDefinition {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name;
    std::uint8_t active{};
    std::optional<std::uint32_t> data_reference;
    NumberListIdentity rams_source;
    std::vector<NumberRamDefinition> rams;
};
class NumberControlProgram {
public:
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    const std::vector<NumberInfoDefinition>& infos() const noexcept;
private:
    struct Data;
    std::shared_ptr<const Data> data_;
    explicit NumberControlProgram(std::shared_ptr<const Data> data):data_(std::move(data)) {}
    friend NumberControlProgram decode_number_controls(const IgbDocument&,std::string,std::uint32_t);
};
// Exact exported beNumberCtrlInfo owners, including ordered RAM aliases. The
// supported mutable geometry is the original little-endian float2 UV stream 11.
// Null geometry/digit/work references remain null; no invented HUD is generated.
NumberControlProgram decode_number_controls(const IgbDocument&,std::string source,std::uint32_t source_occurrence);

struct NumberWorkState {
    ControllerObjectId id;
    std::int32_t current{},target{},step{};
};
struct NumberDigitState {
    ControllerObjectId id;
    std::int32_t display{};
    NumberListIdentity mappings_source;
    std::vector<NumberMappingDefinition> mappings;
};
struct NumberUvState {
    ControllerObjectId memory;
    std::vector<Vec2> texcoords;
    std::uint64_t revision{};
};
struct NumberRamState {
    ControllerObjectId id,manager,info;
    std::int32_t index{};
    std::uint8_t active{};
    std::array<std::int32_t,4> words{};
    std::optional<ControllerObjectId> geometry,vertex_array;
    float offset_next{};
    std::int32_t maximum{},mode{};
    std::optional<NumberListIdentity> digits_source;
    std::vector<std::shared_ptr<const NumberDigitState>> digits;
    std::shared_ptr<const NumberWorkState> work;
    // This is the authoritative native UV owner, shared by physical memory
    // aliases. A future scene renderer must submit this owner and its revision.
    std::shared_ptr<const NumberUvState> uv;
};
// Original NUMBERCTRL structured commands GETVALUE, SETVALUE and ADDVALUE,
// including actual digit/UV mutation. SETVALUEDIRECT is originally declined.
// This owns prepared RAMs and geometry data, not HUD draw or frame scheduling.
class NumberControlRuntime {
public:
    explicit NumberControlRuntime(ActionEventRuntime&);
    ~NumberControlRuntime();
    NumberControlRuntime(const NumberControlRuntime&)=delete;
    NumberControlRuntime& operator=(const NumberControlRuntime&)=delete;
    void register_source(NumberControlProgram);
    ControllerObjectId manager_id() const noexcept;
    const std::vector<NumberControlProgram>& sources() const noexcept;
    const NumberRamState& ram(ControllerObjectId) const;
    void set_active(ControllerObjectId,std::uint8_t);
    // One explicit original manager update (8031AD7C), without time inference
    // or an activity gate. Missing required geometry is a checked host error.
    void update(ControllerObjectId);
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
    ActionEventRuntime& runtime_;
};
} // namespace dw4
