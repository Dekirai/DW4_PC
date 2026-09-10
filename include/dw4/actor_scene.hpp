#pragma once

#include "dw4/animation_hierarchy.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dw4 {
class ActorSceneError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class ActorSceneResource;
struct ActorSceneSource;

// Immutable loaded node identity and child graph. Geometry/material fields stay
// in the owned original document; this is not a baked world-space mesh or actor.
class ActorSceneNode {
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
    const std::vector<std::shared_ptr<const ActorSceneNode>>& children() const noexcept { return children_; }
private:
    friend class ActorSceneResource;
    ActorSceneNode()=default;
    std::shared_ptr<const ActorSceneSource> source_;
    std::uint32_t reference_{},flags_{};
    std::string type_;
    std::optional<std::string> name_;
    std::vector<std::shared_ptr<const ActorSceneNode>> children_;
    std::size_t subtree_height_{};
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
    const std::string& source() const noexcept;
    std::uint32_t source_occurrence() const noexcept;
    std::uint32_t canonical_reference(std::uint32_t reference) const;
    std::shared_ptr<const ActorSceneNode> node(std::uint32_t reference);
    std::shared_ptr<const ActorSceneSkeleton> skeleton(std::uint32_t reference);
    std::shared_ptr<ActorSceneAppearance> appearance(std::uint32_t reference);
    std::shared_ptr<ActorSceneAppearanceList> appearance_list(std::uint32_t reference);
    ActorSceneAction action(std::uint32_t info_reference,std::uint32_t ram_reference);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dw4
