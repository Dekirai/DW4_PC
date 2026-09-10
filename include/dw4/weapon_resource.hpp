#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace dw4 {
class ActorSceneResource;
class ActorSceneNode;
class WeaponResourceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
struct WeaponListIdentity {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::uint32_t> memory_reference,memory_owner_reference;
    std::uint32_t count{},capacity{};
    bool operator==(const WeaponListIdentity&) const = default;
};
struct WeaponRamDefinition {
    std::uint32_t reference{},owner_reference{};
    std::int32_t authored_index{};
    std::uint8_t active{};
    std::array<std::int32_t,4> authored_words{};
    std::optional<std::uint32_t> data_reference;
};
struct WeaponNodeDefinition {
    std::uint32_t reference{},owner_reference{};
    std::shared_ptr<const ActorSceneNode> node;
};
struct WeaponBoneDefinition {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name;
};
struct WeaponSequenceDefinition {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name,label;
};
struct WeaponInfoDefinition {
    std::uint32_t reference{},owner_reference{};
    std::optional<std::string> name;
    std::uint8_t active{};
    std::optional<std::uint32_t> data_reference;
    WeaponListIdentity ram_list,node_list,bone_name_list,sequence_list;
    std::vector<WeaponRamDefinition> rams;
    std::vector<WeaponNodeDefinition> nodes;
    std::vector<WeaponBoneDefinition> bone_names;
    std::vector<WeaponSequenceDefinition> sequences;
};

// Immutable metadata for every exact beWeaponInfo export, in export order.
// Raw occurrences remain distinct from canonical object owners. Registration
// applies the final live RAM ordinal/parent writes; decoding never does so.
class WeaponProgram {
public:
    const std::shared_ptr<ActorSceneResource>& resource() const noexcept;
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    const std::vector<WeaponInfoDefinition>& infos() const noexcept;
private:
    struct Data;
    explicit WeaponProgram(std::shared_ptr<const Data> data):data_(std::move(data)) {}
    std::shared_ptr<const Data> data_;
    friend WeaponProgram decode_weapons(std::shared_ptr<ActorSceneResource>);
};
WeaponProgram decode_weapons(std::shared_ptr<ActorSceneResource> resource);
} // namespace dw4
