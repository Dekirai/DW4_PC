#pragma once

#include "dw4/animation_hierarchy.hpp"
#include "dw4/collision_math.hpp"
#include "dw4/model_placement.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dw4 {
class ActorSceneError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class ActorSceneResource;
class ModelSceneObjects;
class ModelSceneList;
class ActorSceneNodeList;
class ActorSceneNodeStorage;
struct ActorSceneSource;

// Actual igBitMask fields. Logical size is independent of its physical word
// allocation; collision traversal reads a reached word without checking either
// the signed list count/capacity or the declared number of bits.
class ActorSceneBitMask {
public:
    using Storage=std::vector<std::uint32_t>;
    std::int32_t count{},capacity{};
    std::uint32_t bit_count{};
    std::shared_ptr<Storage> backing;
    std::uint32_t backing_word(std::size_t index) const;
    std::shared_ptr<const IgbDocument> document_owner() const noexcept;
    std::optional<std::uint32_t> reference() const noexcept { return reference_; }
private:
    friend class ActorSceneResource;
    std::shared_ptr<const ActorSceneSource> source_;
    std::optional<std::uint32_t> reference_;
};

// Canonical loaded node identity and mutable original node fields. Attribute
// lists share the resource's object registry; this is not a baked world-space mesh.
class ActorSceneNode : public std::enable_shared_from_this<ActorSceneNode> {
public:
    ActorSceneNode(const ActorSceneNode&)=delete;
    ActorSceneNode& operator=(const ActorSceneNode&)=delete;
    const IgbDocument& document() const noexcept;
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    std::uint32_t reference() const noexcept { return reference_; }
    const std::string& type() const noexcept { return type_; }
    const std::optional<std::string>& name() const noexcept { return name_; }
    std::uint32_t flags() const noexcept { return flags_; }
    bool is_a(std::string_view type) const;
    std::optional<std::uint32_t> runtime_dispatch_index() const noexcept { return runtime_dispatch_index_; }
    void set_runtime_dispatch_index(std::optional<std::uint32_t> value) noexcept { runtime_dispatch_index_=value; }
    bool attribute_field_available() const noexcept { return attributes_available_; }
    std::shared_ptr<ModelSceneList> attribute_list_owner() const;
    void set_attribute_list_owner(std::shared_ptr<ModelSceneList>);
    std::uint8_t attributes_enabled() const;
    void set_attributes_enabled(std::uint8_t);
    std::span<const std::shared_ptr<const ActorSceneNode>> children() const;
    bool matrix_field_available() const noexcept { return bool(matrix_); }
    std::shared_ptr<AnimationMatrix> matrix_owner();
    std::shared_ptr<const AnimationMatrix> matrix_owner() const;
    bool bound_field_available() const noexcept { return bound_available_; }
    std::shared_ptr<CollisionBox> box_owner();
    std::shared_ptr<const CollisionBox> box_owner() const;
    void set_box_owner(std::shared_ptr<CollisionBox> box);
    bool child_field_available() const noexcept { return children_available_; }
    std::shared_ptr<ActorSceneNodeList> child_list_owner();
    std::shared_ptr<const ActorSceneNodeList> child_list_owner() const;
    void set_child_list_owner(std::shared_ptr<ActorSceneNodeList> children);
    bool switch_mask_field_available() const noexcept { return mask_available_; }
    std::shared_ptr<ActorSceneBitMask> switch_mask_owner();
    std::shared_ptr<const ActorSceneBitMask> switch_mask_owner() const;
    void set_switch_mask_owner(std::shared_ptr<ActorSceneBitMask>);
    bool source_transform_field_available() const noexcept { return source_transform_available_; }
    std::shared_ptr<ActorSceneNode> source_transform_owner();
    std::shared_ptr<const ActorSceneNode> source_transform_owner() const;
    void set_source_transform_owner(std::shared_ptr<ActorSceneNode>);
    std::shared_ptr<ModelPlacementNode> placement_node();
private:
    friend class ActorSceneResource;
    friend class ActorSceneNodeList;
    ActorSceneNode()=default;
    std::shared_ptr<const ActorSceneSource> source_;
    std::uint32_t reference_{},flags_{};
    std::string type_;
    std::optional<std::string> name_;
    std::shared_ptr<AnimationMatrix> matrix_;
    bool bound_available_{},children_available_{};
    std::shared_ptr<CollisionBox> box_;
    std::shared_ptr<ActorSceneNodeList> children_;
    bool mask_available_{},source_transform_available_{};
    std::shared_ptr<ActorSceneBitMask> mask_;
    std::shared_ptr<ActorSceneNode> source_transform_;
    std::optional<std::uint32_t> runtime_dispatch_index_;
    bool attributes_available_{};
    std::uint8_t attributes_enabled_{};
    std::shared_ptr<ModelSceneList> attributes_;
    mutable std::vector<std::weak_ptr<ActorSceneNodeStorage>> incoming_;
    mutable std::vector<std::weak_ptr<ActorSceneNode>> incoming_sources_;
};

// One physical nullable pointer backing allocation, independently shared by
// serialized list objects. Logical counts and capacities belong to each list.
class ActorSceneNodeStorage {
public:
    using Values=std::vector<std::shared_ptr<const ActorSceneNode>>;
    const Values& values() const noexcept { return values_; }
private:
    friend class ActorSceneNodeList;
    friend class ActorSceneResource;
    Values values_;
    std::vector<std::weak_ptr<ActorSceneNodeList>> lists_;
};

// Ordered strong node references. Checked mutation preserves physical memory
// aliases and rejects cycles through capacity slots, including inactive ones.
class ActorSceneNodeList : public std::enable_shared_from_this<ActorSceneNodeList> {
public:
    using Values=ActorSceneNodeStorage::Values;
    ActorSceneNodeList()=default;
    ActorSceneNodeList(const ActorSceneNodeList&)=delete;
    ActorSceneNodeList& operator=(const ActorSceneNodeList&)=delete;
    std::span<const std::shared_ptr<const ActorSceneNode>> values() const;
    std::int32_t count() const noexcept { return count_; }
    std::int32_t capacity() const noexcept { return capacity_; }
    void clear() noexcept { count_=0; }
    std::shared_ptr<const ActorSceneNode> first_backing_node() const;
    std::shared_ptr<ActorSceneNode> backing_node(std::size_t index) const;
    std::shared_ptr<const ActorSceneNodeStorage> storage_owner() const noexcept { return storage_; }
    void replace(std::vector<std::shared_ptr<const ActorSceneNode>> values);
    void append(std::shared_ptr<const ActorSceneNode> value);
private:
    friend class ActorSceneNode;
    friend class ActorSceneResource;
    static void validate(const ActorSceneNode* changed_node,const ActorSceneNodeList* changed_list,
        const ActorSceneNodeStorage* changed_storage,
        const std::vector<std::shared_ptr<const ActorSceneNode>>& candidate,
        const std::vector<std::shared_ptr<const ActorSceneNode>>& roots,
        const ActorSceneNode* changed_source=nullptr,
        const std::shared_ptr<ActorSceneNode>& source_value={});
    void attach(const std::shared_ptr<ActorSceneNode>& node);
    void detach(const ActorSceneNode* node);
    void bind_storage(std::shared_ptr<ActorSceneNodeStorage> storage);
    void publish(Values values,std::int32_t count);
    static void write_storage(const std::shared_ptr<ActorSceneNodeStorage>&,Values values);
    std::int32_t count_{},capacity_{};
    std::shared_ptr<ActorSceneNodeStorage> storage_;
    std::vector<std::weak_ptr<ActorSceneNode>> holders_;
};

// Newly allocated original selector's represented fields. Each allocation has
// its own shared identity; all appearances may retain that same selector.
// General inherited render/compiler state is a separate traversal boundary.
class ActorBoneSelector {
public:
    ActorBoneSelector(std::int32_t matrix_index,std::vector<std::shared_ptr<const ActorSceneNode>> children);
    std::int32_t matrix_index() const noexcept { return matrix_index_; }
    const std::vector<std::shared_ptr<const ActorSceneNode>>& children() const noexcept { return children_; }
private:
    std::int32_t matrix_index_{};
    std::vector<std::shared_ptr<const ActorSceneNode>> children_;
};

class ActorSceneSkeleton {
public:
    ActorSceneSkeleton(const ActorSceneSkeleton&)=delete;
    ActorSceneSkeleton& operator=(const ActorSceneSkeleton&)=delete;
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    std::uint32_t reference() const noexcept { return reference_; }
    const AnimationHierarchyDefinition& hierarchy() const noexcept { return hierarchy_; }
    std::int32_t find_bone(std::string_view name) const;
private:
    friend class ActorSceneResource;
    friend class ActorSceneAppearanceList;
    ActorSceneSkeleton(std::shared_ptr<const ActorSceneSource>,std::uint32_t,AnimationHierarchyDefinition);
    std::shared_ptr<const ActorSceneSource> source_;
    std::uint32_t reference_{};
    AnimationHierarchyDefinition hierarchy_;
};

class ActorBoneSelectorList {
public:
    const auto& values() const noexcept { return values_; }
    void append(std::shared_ptr<const ActorBoneSelector> selector);
    // Original remove-all pointer equality, preserving the other entries.
    std::size_t remove(const std::shared_ptr<const ActorBoneSelector>& selector);
private:
    std::vector<std::shared_ptr<const ActorBoneSelector>> values_;
};

class ActorSceneAppearance {
public:
    ActorSceneAppearance(const ActorSceneAppearance&)=delete;
    ActorSceneAppearance& operator=(const ActorSceneAppearance&)=delete;
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    std::uint32_t reference() const noexcept { return reference_; }
    std::uint32_t skin_reference() const noexcept { return skin_reference_; }
    std::uint32_t bone_select_list_reference() const noexcept { return bone_select_list_reference_; }
    const std::shared_ptr<ActorBoneSelectorList>& bone_selectors() const noexcept { return selectors_; }
private:
    friend class ActorSceneResource;
    ActorSceneAppearance()=default;
    std::shared_ptr<const ActorSceneSource> source_;
    std::uint32_t reference_{},skin_reference_{},bone_select_list_reference_{};
    std::shared_ptr<ActorBoneSelectorList> selectors_;
};
class ActorSceneAppearanceList {
public:
    ActorSceneAppearanceList(const ActorSceneAppearanceList&)=delete;
    ActorSceneAppearanceList& operator=(const ActorSceneAppearanceList&)=delete;
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    std::uint32_t reference() const noexcept { return reference_; }
    bool shares_source(const ActorSceneSkeleton& skeleton) const noexcept;
    const std::vector<std::shared_ptr<ActorSceneAppearance>>& values() const noexcept { return values_; }
private:
    friend class ActorSceneResource;
    ActorSceneAppearanceList()=default;
    std::shared_ptr<const ActorSceneSource> source_;
    std::uint32_t reference_{};
    std::vector<std::shared_ptr<ActorSceneAppearance>> values_;
};

class ActorSceneAction {
public:
    std::uint32_t info_reference() const noexcept { return info_reference_; }
    std::uint32_t ram_reference() const noexcept { return ram_reference_; }
    std::uint32_t actor_list_reference() const noexcept { return actor_list_reference_; }
    std::uint32_t database_reference() const noexcept { return database_reference_; }
    const auto& actor_references() const noexcept { return actor_references_; }
    const auto& actor_appearance_references() const noexcept { return actor_appearance_references_; }
    const auto& appearances() const noexcept { return appearances_; }
    const auto& skeleton() const noexcept { return skeleton_; }
private:
    friend class ActorSceneResource;
    ActorSceneAction()=default;
    std::uint32_t info_reference_{},ram_reference_{},actor_list_reference_{},database_reference_{};
    // Serialized appearance order is independent of the actor-list order.
    std::vector<std::uint32_t> actor_references_,actor_appearance_references_;
    std::shared_ptr<ActorSceneAppearanceList> appearances_;
    std::shared_ptr<const ActorSceneSkeleton> skeleton_;
};

// One actual loaded source occurrence. Canonical physical references share
// owners across exports and callers. Passing a different resource is a different
// load even when its path and numeric references happen to look identical.
// Per resource: 65,536 cached owners, 262,144 copied child/list/hierarchy
// entries, 16 MiB of copied text, and 128 edges on every node path. Cached
// subtrees count toward path depth. Source-document storage has its own bounds.
class ActorSceneResource {
public:
    ActorSceneResource(IgbDocument document,std::string source,std::uint32_t source_occurrence);
    ~ActorSceneResource();
    ActorSceneResource(const ActorSceneResource&)=delete;
    ActorSceneResource& operator=(const ActorSceneResource&)=delete;
    const IgbDocument& document() const noexcept;
    std::shared_ptr<const IgbDocument> document_owner() const noexcept;
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    std::uint32_t canonical_reference(std::uint32_t reference) const;
    std::shared_ptr<const ActorSceneNode> node(std::uint32_t reference);
    std::shared_ptr<ActorSceneNode> mutable_node(std::uint32_t reference);
    std::shared_ptr<CollisionBox> box(std::uint32_t reference);
    std::shared_ptr<ActorSceneBitMask> bit_mask(std::uint32_t reference);
    ModelSceneObjects& models();
    std::shared_ptr<const ActorSceneSkeleton> skeleton(std::uint32_t reference);
    std::shared_ptr<ActorSceneAppearance> appearance(std::uint32_t reference);
    std::shared_ptr<ActorSceneAppearanceList> appearance_list(std::uint32_t reference);
    ActorSceneAction action(std::uint32_t info_reference,std::uint32_t ram_reference);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
