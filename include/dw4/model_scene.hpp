#pragma once

#include "dw4/actor_scene.hpp"
#include "dw4/model_player_work.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace dw4 {
class ActorGeometryAttribute;
class ActorVertexArray;
class ActorIndexArray;
class ActorPrimitiveLengths;
class ModelSceneError : public std::runtime_error { public: using std::runtime_error::runtime_error; };

// A source-qualified physical object, or a fresh runtime allocation with no
// serialized reference. Retaining it retains the document, never its resource
// facade. Equal paths and occurrence numbers do not establish owner equality.
class ModelSceneObject {
public:
    virtual ~ModelSceneObject()=default;
    ModelSceneObject(const ModelSceneObject&)=delete;
    ModelSceneObject& operator=(const ModelSceneObject&)=delete;
    std::shared_ptr<const IgbDocument> document_owner() const noexcept { return document_; }
    const std::string& source() const noexcept { return source_; }
    std::uint32_t source_occurrence() const noexcept { return occurrence_; }
    std::optional<std::uint32_t> reference() const noexcept { return reference_; }
    const std::string& type() const noexcept { return type_; }
    bool is_a(std::string_view type) const;
    std::optional<std::string> name;
protected:
    explicit ModelSceneObject(std::string type):type_(std::move(type)) {}
private:
    friend class ModelSceneObjects;
    std::shared_ptr<const IgbDocument> document_;
    std::string source_,type_;
    std::uint32_t occurrence_{};
    std::optional<std::uint32_t> reference_;
};

// Count, capacity and the backing allocation have independent identities.
// The backing represents all serialized slots, including unused capacity.
// Consumers validate only the live prefix; null/repeated entries stay intact.
class ModelSceneList final : public ModelSceneObject {
public:
    using Storage=std::vector<std::shared_ptr<ModelSceneObject>>;
    explicit ModelSceneList(std::string type="beBaseInfoDataList") :ModelSceneObject(std::move(type)) {}
    std::int32_t count{},capacity{};
    std::shared_ptr<Storage> backing;
    std::span<const std::shared_ptr<ModelSceneObject>> values() const;
    void append(std::shared_ptr<ModelSceneObject>);
};

class ModelSceneData : public ModelSceneObject {
public:
    explicit ModelSceneData(std::string type="beBaseInfoData"):ModelSceneObject(std::move(type)) {}
    std::int32_t value{};
};
class ModelSceneOne final : public ModelSceneData {
public:
    ModelSceneOne():ModelSceneData("beModelCtrlInfoRamData") {}
    std::shared_ptr<ActorSceneNode> node;
    std::shared_ptr<ModelPlayerWork> work;
};
class ModelSceneTimer final : public ModelSceneData {
public:
    ModelSceneTimer():ModelSceneData("beBaseInfoRamTimer") {}
    MotionTimer timer;
};
class ModelSceneSeq final : public ModelSceneData {
public:
    ModelSceneSeq():ModelSceneData("beModelCtrlInfoDataSeq") {}
    std::shared_ptr<ModelSceneList> sequences;
};
class ModelSceneSequence final : public ModelSceneObject {
public:
    ModelSceneSequence():ModelSceneObject("beModelCtrlData") {}
    std::shared_ptr<ModelSceneList> commands;
};
// One physical command allocation. The inherited nullable name and these raw
// fields remain authoritative; dispatch snapshots only the reached command.
class ModelSceneCommand final : public ModelSceneObject {
public:
    explicit ModelSceneCommand(std::string type):ModelSceneObject(std::move(type)) {}
    float value{},frames{},end_time{},stop_value{},stop_frames{};
    std::uint8_t stop_on_sequence{};
    std::optional<std::string> scope; // LABELCTRL/LUACALL field 3.
    ModelHitboxSubcommand hitbox;
};
class ModelSceneHit final : public ModelSceneData {
public:
    ModelSceneHit():ModelSceneData("beModelCtrlInfoDataHit") {}
    std::uint8_t hit_player_bullet{},hit_enemy_bullet{},hit_player_talk{};
    std::shared_ptr<ModelSceneList> boxes;
};
class ModelSceneBox final : public ModelSceneObject {
public:
    ModelSceneBox():ModelSceneObject("igAABox") {}
    // The same box allocation is used by aliased node-bound fields.
    std::shared_ptr<CollisionBox> box;
};
class ModelSceneInfo final : public ModelSceneObject {
public:
    ModelSceneInfo():ModelSceneObject("beModelCtrlInfo") {}
    std::uint8_t active{};
    std::shared_ptr<ModelSceneList> data,rams;
    std::optional<std::string> kind;
};
class ModelHitLandInfo final : public ModelSceneObject {
public:
    ModelHitLandInfo():ModelSceneObject("beHitLandDelivInfo") {}
    std::uint8_t active=1;
    std::shared_ptr<ModelSceneList> data,rams;
};
// Distinct HITLANDMODEL geometry category, not a HITLANDDELIVER recipient.
// These are decoded authored fields, not a complete reflected fresh factory.
class ModelHitLandGeometryInfo final : public ModelSceneObject {
public:
    ModelHitLandGeometryInfo():ModelSceneObject("beHitLandModelInfo") {}
    std::uint8_t active=1;
    std::shared_ptr<ModelSceneList> data,rams;
    std::shared_ptr<ActorSceneNode> switch_graph,check_switch; // +20/+24.
    std::shared_ptr<ModelSceneList> names; // +1C _nameList.
    std::int32_t kind{}; // +28 _kind; retain every raw bit.
};
struct ModelPadManager;
struct ViewerSceneInfoManager;
struct ModelSceneManager;
struct ModelManagerFrameExecutionState;
struct MessengerCallResult;
using ModelManagerRamCall=std::function<MessengerCallResult()>;
using ModelManagerRamFactory=std::function<ModelManagerRamCall(
    const std::shared_ptr<ModelSceneManager>&,const std::shared_ptr<ModelSceneObject>&)>;
struct ModelSceneContext { std::uint64_t world_time{}; };
struct ModelSceneSystem {
    float frame_time_f{1}; // +28, explicit beSystem metadata default.
    std::uint64_t saved_world_time{},frame_time{}; // +18/+20.
    std::shared_ptr<ModelSceneContext> context; // Inherited manager +0C.
    std::uint8_t is_active{},is_first_time{},is_frame_skip{}; // +2C/+2D/+2E, reflected zero defaults.
    std::uint8_t reset_enabled{1}; // +5F, shared reset/storage-error gate; reflected default one.
};
struct ModelSceneManager {
    virtual ~ModelSceneManager()=default;
    std::shared_ptr<ModelSceneContext> context;
    std::shared_ptr<ModelSceneSystem> system; // Manager +10, system +28 cached uncapped frame delta.
    std::shared_ptr<ModelSceneList> infos=std::make_shared<ModelSceneList>("beBaseInfoList"); // +18 reflected owner.
    std::uint8_t update_enabled{}; // +1C _isUpdateRun, reflected zero default.
    ModelManagerRamFactory ram_frame_factory; // Actual virtual+84 binding; selected per reached RAM.
    // Native continuation state only, separate from the original manager fields.
    std::shared_ptr<ModelManagerFrameExecutionState> manager_frame_execution;
    std::shared_ptr<ModelPadManager> pads; // Player manager +30, read at each reached pad lookup.
    // Player +2C. Disengaged means setup is not represented; engaged null is
    // an actual null pointer and fails if the original input path reaches it.
    std::optional<std::shared_ptr<ViewerSceneInfoManager>> viewer;
};
class ModelSceneRam final : public ModelSceneObject {
public:
    ModelSceneRam():ModelSceneObject("beBaseInfoRam") {}
    std::int32_t number{};
    std::uint8_t active{};
    std::array<std::uint32_t,4> words{};
    std::shared_ptr<ModelSceneList> data;
    // Original registration establishes these links; serialization does not.
    std::weak_ptr<ModelSceneInfo> parent;
    std::weak_ptr<const ModelSceneManager> manager;
};
class ModelHitLandRam final : public ModelSceneObject {
public:
    ModelHitLandRam():ModelSceneObject("beHitLandDelivInfoRam") {}
    std::int32_t number{};
    std::uint8_t active=1;
    std::array<std::uint32_t,4> words{};
    std::shared_ptr<ModelSceneList> data;
    std::weak_ptr<ModelHitLandInfo> parent;
    std::weak_ptr<const ModelSceneManager> manager;
    std::shared_ptr<ActorSceneNode> node; // +2C _transform, shared with MODELCTRL ONE.
    std::int32_t check_flags{}; // +30 _checkFlag.
    float half_width=3.0f,up_range=4.0f,down_range=4.0f;
    std::array<float,3> position{},previous_position{},land_position{}; // +40/+4C/+58.
    std::uint8_t land_position_ok{}; // +64 _landPosOK.
};

// One registry, owned lazily by ActorSceneResource. The nonowning resource
// reference is used only while loading; captured graph owners outlive it.
class ModelSceneObjects {
public:
    ~ModelSceneObjects();
    ModelSceneObjects(const ModelSceneObjects&)=delete;
    ModelSceneObjects& operator=(const ModelSceneObjects&)=delete;
    std::shared_ptr<ModelSceneObject> object(std::uint32_t reference);
    std::shared_ptr<ModelSceneInfo> info(std::uint32_t reference);
    std::shared_ptr<ModelSceneRam> ram(std::uint32_t reference);
    std::shared_ptr<ModelHitLandInfo> hit_land_info(std::uint32_t reference);
    std::shared_ptr<ModelHitLandRam> hit_land_ram(std::uint32_t reference);
    std::shared_ptr<ModelHitLandGeometryInfo> hit_land_geometry_info(std::uint32_t reference);
    std::shared_ptr<ActorGeometryAttribute> geometry_attribute(std::uint32_t reference);
    std::shared_ptr<ActorVertexArray> vertex_array(std::uint32_t reference);
    std::shared_ptr<ActorIndexArray> index_array(std::uint32_t reference);
    std::shared_ptr<ActorPrimitiveLengths> primitive_lengths(std::uint32_t reference);
    std::shared_ptr<ModelSceneData> data(std::uint32_t reference);
    std::shared_ptr<ModelSceneList> list(std::uint32_t reference);
private:
    friend class ActorSceneResource;
    explicit ModelSceneObjects(ActorSceneResource&);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Ordered first-name lookup happens before the requested concrete type check.
// A wrong-type match shadows later records. Reached null entries and a null
// list are unsafe original dereferences; count<=0 does not inspect backing.
std::shared_ptr<ModelSceneObject> find_model_named(const std::shared_ptr<ModelSceneList>&,std::string_view name);
// A null requested name faults only when strcmp is reached for a non-null
// entry name. Empty/nonpositive lists and null names do not consume it.
std::shared_ptr<ModelSceneObject> find_model_named_nullable(const std::shared_ptr<ModelSceneList>&,
    std::optional<std::string_view> name);
std::shared_ptr<ModelSceneData> find_model_data(const std::shared_ptr<ModelSceneList>&,std::string_view name);
std::shared_ptr<ModelSceneList> ensure_model_data(ModelSceneRam&);
std::shared_ptr<ModelSceneOne> find_model_one(ModelSceneRam&);
std::shared_ptr<ModelSceneSeq> find_model_seq(const ModelSceneInfo&);
std::shared_ptr<ModelSceneSequence> find_model_sequence(const ModelSceneSeq&,std::string_view name);
std::shared_ptr<ModelSceneSequence> find_model_sequence_nullable(const ModelSceneSeq&,
    std::optional<std::string_view> name);
// Consumed-command adapter, not an immutable whole-program cache. Null name
// remains distinct in the owner; existing command handlers receive their
// established non-null empty-name fallback in this detached request.
ModelSequenceCommand model_scene_command(const ModelSceneCommand&);
std::shared_ptr<ModelSceneTimer> find_model_timer(ModelSceneRam&,std::string_view name);
// 8031DE44: ensure the data list; reuse the first matching timer, or allocate,
// name and append one; then restart its existing fields at the supplied clock.
std::shared_ptr<ModelSceneTimer> start_model_timer(ModelSceneRam&,std::string_view name,
    std::uint64_t world_time,std::int64_t end_time=0);
// 8031E4C4: ensure the data list but require an existing named timer.
float model_timer_frames(ModelSceneRam&,std::string_view name,std::uint64_t world_time);

// The registration caller supplies the actual manager owner and original index.
// This does not append, sort, deduplicate, activate or invent a GETROOT result.
void register_model_ram(const std::shared_ptr<ModelSceneInfo>&,
    const std::shared_ptr<ModelSceneRam>&,std::int32_t number,
    const std::shared_ptr<const ModelSceneManager>& actual_manager);
} // namespace dw4
