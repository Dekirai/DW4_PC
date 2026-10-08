#pragma once

#include "dw4/engine/actor.hpp"
#include "dw4/engine/igb_view.hpp"
#include "dw4/engine/math.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace dw4::engine {

// A process-unique id that a copy does not share, so host caches keyed by a
// geometry's address can tell a new object at a reused address apart.
struct GeometrySerial {
    std::uint64_t value = next();
    GeometrySerial() = default;
    GeometrySerial(const GeometrySerial&) : value(next()) {}
    GeometrySerial& operator=(const GeometrySerial&) {
        value = next();
        return *this;
    }
    static std::uint64_t next() {
        static std::uint64_t counter = 0;
        return ++counter;
    }
};

// Decoded igGeometryAttr1_5 vertex/index data, expanded to an indexed
// triangle list. Runtime geometry (text, numbers) is replaced often; the
// serial identifies each instance.
struct GeometryData {
    GeometrySerial serial;
    std::vector<Vec3f> positions;
    std::vector<Vec3f> normals;
    std::vector<std::uint32_t> colors; // Original packed RGBA8 bytes in memory order.
    std::vector<float> texcoords;      // u,v pairs.
    std::vector<float> weights;        // weight_count floats per vertex.
    std::vector<std::uint8_t> blend_indices; // weight_count indices per vertex.
    std::uint32_t weight_count{};
    std::vector<std::uint32_t> indices; // Triangles.
    Vec3f bounds_min{}, bounds_max{};
    bool valid{};
    std::string diagnostic;
};

GeometryData decode_geometry(const ObjectView& geometry_attr);

enum class AttrKind : std::uint8_t {
    unknown, texture_bind, texture_state, texture_matrix_state, color, material, blend_function, blend_state,
    alpha_function, alpha_state, depth_state, depth_write_state, depth_function, cull_face, lighting_state,
    light, light_state, fog, fog_state, shade_model, model_view_matrix, projection_matrix, viewport, color_mask,
    vertex_blend_state, normalize_normals, geometry
};

struct TextureSampler {
    int mag_filter = 1, min_filter = 1, wrap_s = 1, wrap_t = 1;
};

struct Attribute {
    AttrKind kind = AttrKind::unknown;
    ObjectView object;
    std::int32_t unit{};
    bool enabled{};
    std::int32_t e0{}, e1{}, e2{};
    float f0{}, f1{};
    Vec4f color{1, 1, 1, 1};
    Vec4f ambient{}, emission{}, specular{};
    Mat4 matrix{};
    ObjectView image;        // texture binding image
    ObjectView texture;      // igTextureAttr
    TextureSampler sampler;
    std::int32_t viewport[4]{};
    bool mask[4]{true, true, true, true};
    const Attribute* light{}; // igLightStateAttr: the light it switches on
};

enum class NodeKind : std::uint8_t {
    unknown, group, attr_set, switch_node, transform, geometry, actor, billboard, blend_matrix_select,
    light_set, light_state_set, camera, model_node, lod, shadow_shader, bone_select, copy_model, particle_timer,
    transform_recorder, node_anim
};

// beNodeAnim (fields 802C48A4, clock 80311230): the children run on its own
// clock, an intro from `start` to `loop_end`, then looping back to
// `loop_start`; a nonzero `fixed` time holds it. beLoadingNode adds the lock
// time it jumps to once loading is done and the minimum play time.
struct NodeAnim {
    std::int64_t start{}, loop_start{}, loop_end{}, fixed{}; // +20 / +28 / +30 / +38
    bool first = true;                                       // +40
    float scale = 1.0f;                                      // +50
    std::int64_t bias{}, before{}, played{};                 // +58 / +60 / +68
    bool loading{};
    std::int64_t lock{}, play_limit{};                       // beLoadingNode +70 / +78
    std::int64_t time(std::int64_t now);
    bool finished() const { return play_limit < played; }    // 80311674
};

// igTransformSequence1_5 (time 80209B74, blend 80209578, single key 80208084)
// and igCompressedAnimationSequenceQS (time 801E2FF0, keys 801E32B8),
// evaluated into a local matrix.
struct TransformSequence {
    // _drivenChannels (+0x48, QS +0x38; enum DRIVEN_CHANNEL 804AB6D0).
    enum Channel : std::uint8_t { translation = 1, quaternion = 2, euler = 4, scale = 8 };
    // _interpolationMethod (+0x49..+0x4B, QS +0x39..+0x3B) for T, R, S
    // (enum INTERPOLATION_METHOD 804AB700).
    enum Method : std::uint8_t { none = 0, linear = 1, bezier = 2, slerp = 3 };
    // _playMode (+0x24, QS +0x30; enum PLAY_MODE 804AB758).
    enum PlayMode : std::int32_t { repeat = 0, clamp = 1, bounce = 2, loop = 3 };
    std::vector<Vec3f> translations;
    std::vector<Vec4f> rotations; // quaternions, or Euler degrees in xyz
    std::vector<Vec3f> scales;
    // _interpolationData{Translation,Rotation,Scale}: two Bezier control
    // points per key.
    std::vector<Vec3f> translation_tangents, rotation_tangents, scale_tangents;
    std::vector<std::int64_t> times; // nanoseconds
    std::uint8_t driven_channels = translation | quaternion | scale;
    std::uint8_t methods[3]{linear, linear, linear};
    std::int32_t play_mode = repeat;
    bool uniform{};    // _uniformTimeEnabled (+0x20): times[0] is the key spacing
    bool compressed{}; // QS: no time offset, Euler, centre or scale channel
    std::int64_t time_offset{}, duration{};
    Vec3f center{};
    bool valid{};
    Mat4 evaluate(std::int64_t time) const;
    // One key without interpolation (80208084; QS 801E32B8 outside the keys).
    Mat4 key(std::size_t index) const;
    // Keys i and j blended by alpha (80209578; QS 801E32B8).
    Mat4 blend(std::size_t i, std::size_t j, float alpha) const;
};

// Runtime mirror of one serialized igNode. Shared DAG nodes stay shared, so a
// manager state change affects every placement, as in the original engine.
struct Node {
    NodeKind kind = NodeKind::unknown;
    ObjectView object;
    std::string name;
    std::vector<Node*> children;
    std::vector<const Attribute*> attributes;      // igAttrSet state attributes
    std::vector<const Attribute*> geometries;      // igGeometry payloads
    Mat4 matrix{};
    bool has_matrix{};
    std::shared_ptr<TransformSequence> sequence;
    bool texture_transform{}; // igTransform flag 8: a texture coordinate transform
    std::vector<std::uint32_t> mask;               // igSwitch child bits
    // beModelCtrlNode2 node clock (original slots 12..14).
    float time_scale = 1.0f;
    std::int64_t time_bias{};
    bool time_override{};
    // beMeterCtrlNode: children are evaluated at this time (METERCTRL).
    std::optional<std::int64_t> fixed_time;
    // Runtime overrides applied by managers (ACTION2 appearance, MODELCTRL).
    std::optional<Vec4f> color_override;
    std::optional<bool> blend_override, texture_override, lighting_override;
    bool hidden{};
    // Extra runtime transform post-multiplied onto the node's own matrix.
    std::optional<Mat4> runtime_matrix;
    // Billboard axis mode etc.
    std::int32_t billboard_mode{};
    // igLod child ranges.
    std::vector<float> lod_ranges;
    // igActor appearance skin graph roots and actor transform.
    std::vector<Node*> appearance_roots;
    Mat4 actor_matrix{};
    // igBlendMatrixSelect palette (joint indices) / igModelViewMatrixBoneSelect bone.
    std::vector<std::int32_t> blend_indices;
    std::int32_t bone_index = -1;
    // Runtime geometry generated by managers (text glyph quads).
    std::shared_ptr<GeometryData> runtime_geometry;
    const Attribute* runtime_texture{};
    // beCopyModelCtrl2 [12]: the model instance whose matrix and actor pose
    // the children follow (shadow proxies); its first actor, found once.
    Node* copy_source{};
    Node* copy_actor{};
    bool copy_resolved{};
    // ParticleTimer: its igParticleAttr list ([8]) and the time its effect
    // started (-1: not running; beParticleCtrl2 starts and stops it); the
    // quads rebuilt every frame.
    std::vector<ObjectView> particles;
    std::int64_t particle_start = -1;
    std::int64_t particle_paused = -1; // ONFREEZE: the time the timer stopped at
    std::vector<std::shared_ptr<GeometryData>> particle_geometry;
    // igTransformRecorder: the world matrix it was last drawn with (TRC data).
    std::optional<Mat4> recorded;
    std::optional<NodeAnim> anim;

    bool child_enabled(std::size_t index) const;
    void set_child_enabled(std::size_t index, bool enabled);
    void set_all_children(bool enabled);
};

struct RenderState {
    const Attribute* texture{};
    bool texture_enabled{};
    bool blend_enabled{};
    // igBlendFunctionAttr defaults (0x8055E6AC..B4, registered by 800BA89C):
    // SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ADD.
    std::int32_t blend_src = 4, blend_dst = 5, blend_equation = 0;
    // The original's GX alpha compare is in effect GREATER 0.1 wherever no
    // igAlphaStateAttr / igAlphaFunctionAttr says otherwise. Deferred render
    // packages record only attributes that differ from the stack defaults
    // (801D44A8 via 80205FAC), so the window particle layer's GREATER 0.1
    // (WN04 attr set of every stage) stays set in the GX context for later
    // packages: with the compare forced off (flag 0x8056350D read at
    // 0x800F8F4C) Dolphin shows the fade steps at alpha <= 25/255, the font
    // fringes and, behind the j_digifacewin portrait, the 3D background.
    bool alpha_test = true;
    std::int32_t alpha_func = 4; // IG_GFX_ALPHA_FUNCTION_GREATER
    float alpha_ref = 0.1f;
    bool depth_test = true, depth_write = true;
    std::int32_t depth_func = 3;
    bool cull_enabled{};
    std::int32_t cull_mode = 0; // IG_GFX_CULL_FACE_MODE_BACK
    bool lighting = true; // Alchemy default: only igLightingStateAttr turns it off
    Vec4f color{1, 1, 1, 1};
    std::optional<Vec4f> modulate; // runtime ACTION2 color (multiplied)
    // Set by runtime ACTION2 overrides: deeper attributes of the kind no
    // longer change the value (override push, 801D5190).
    bool texture_forced{}, lighting_forced{}, blend_forced{};
    // igTransform flag 8: the transform (sequence) applies to the texture coordinates.
    std::optional<Mat4> texture_matrix;
    Vec4f diffuse{1, 1, 1, 1}, ambient{0.2f, 0.2f, 0.2f, 1}, emission{0, 0, 0, 1};
    bool fog{};
    const Attribute* fog_params{};
    std::vector<const Attribute*> lights;
    std::optional<Mat4> projection;
    std::optional<Mat4> view;
    std::optional<std::array<std::int32_t, 4>> viewport;
    bool color_write = true;
    bool vertex_blend{};
};

struct DrawItem {
    const GeometryData* geometry{};
    const Node* node{};
    Mat4 model{};
    RenderState state;
    std::uint32_t layer{};
    std::uint32_t order{};
    // Optional skinning palette (row-vector matrices) for weighted geometry.
    std::shared_ptr<const std::vector<Mat4>> palette;
};

struct FrameCamera {
    Mat4 view{};
    Mat4 projection{};
};

// igProjectiveShadowShader state for one frame: the casters drawn from the
// light into a size x size texture that is cleared white (0x938), the casters
// in the grey _shadowFactor (801FDCCC: colour (f, f, f, 1)); the receivers
// (the shader's children, simplified ground meshes outside the visible scene)
// multiply the map over the scene (801FE190: MODULATE, blend ZERO/SRC_COLOR).
struct ShadowFrame {
    bool active{};
    Mat4 light_view{}, light_projection{};
    float factor = 0.4f; // _shadowFactor: the multiplier inside a shadow
    int size = 256;
    // The casters fill the inner viewport (border, border, size - 2 border,
    // ...); the receivers' texture matrix maps the light box into it.
    int border = 7;
    std::vector<DrawItem> casters, receivers;
};

class SceneCache {
public:
    Node* node(const IgbDocument* document, std::uint32_t reference);
    Node* node(const ObjectView& view) { return view ? node(view.document(), view.reference()) : nullptr; }
    const GeometryData* geometry(const Attribute* attribute);
    // A cached attribute a manager changes at run time (LIGHTCTRL colours).
    Attribute* edit_attribute(const ObjectView& view) { return const_cast<Attribute*>(attribute(view)); }
    Node* find(const IgbDocument* document, std::uint32_t reference) const;
    void release_document(const IgbDocument* document);

private:
    const Attribute* attribute(const ObjectView& view);
    struct Key {
        const IgbDocument* document;
        std::uint32_t reference;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const noexcept {
            return std::hash<const void*>{}(key.document) ^ (std::size_t(key.reference) * 0x9e3779b97f4a7c15ull);
        }
    };
    std::unordered_map<Key, std::unique_ptr<Node>, KeyHash> nodes_;
    std::unordered_map<Key, std::unique_ptr<Attribute>, KeyHash> attributes_;
    std::unordered_map<const Attribute*, std::unique_ptr<GeometryData>> geometry_;
};

struct TraversalContext {
    SceneCache* cache{};
    std::int64_t time{};            // world time in nanoseconds
    FrameCamera camera;             // defaults when a scene supplies no matrices
    std::vector<DrawItem>* output{};
    std::uint32_t layer{};
    std::uint32_t order{};
    std::function<void(const Node&, const Mat4& model, TraversalContext&)> actor_hook;
    // Pose provider for igActor nodes (null or nullptr result: bind pose).
    std::function<const ActorPose*(const Node& actor)> actor_pose;
    Vec3f camera_position{};
};

void traverse(Node* root, TraversalContext& context);
void traverse(Node* root, TraversalContext& context, const Mat4& model, const RenderState& state, std::int64_t time);

} // namespace dw4::engine
