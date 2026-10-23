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
    vertex_blend_state, normalize_normals, geometry, texcoord_source
};

// The GX texture object state: IG_GFX_TEXTURE_FILTER values (0 NEAREST,
// 1 LINEAR, 2..5 the mipmap modes) and GX wraps (0 CLAMP, 1 REPEAT).
struct TextureSampler {
    int mag_filter = 1, min_filter = 1, wrap_s = 1, wrap_t = 1;
};

// What the original's texture object samples with for an igTextureAttr
// (igTextureAttr::apply, 800FE744 and the bind loop 800FCA9C).
TextureSampler texture_sampler(const ObjectView& texture);

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
    transform_recorder, node_anim, reset_node
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

// ParticleTimer (fields 802E80A0, traversal 80319F90 -> 8031A0A4): the
// effect time of its igParticleAttr draws is _time - _timeBias, and only the
// timer's own traversal advances it: a visit while _timeRunning moves _time
// to the traversal time, a visit while stopped moves _timeBias along (the
// time stands), and _time == -1 (a start, 0x80312334) makes the first visit
// the time's origin. A timer the traversal does not reach (culled, switched
// off) keeps its fields.
struct ParticleTimerState {
    bool running = true;     // _timeRunning (+0x2C, stored in the file)
    std::int64_t bias{};     // _timeBias (+0x30, not stored: 0)
    std::int64_t time{};     // _time (+0x38, not stored: 0)
    // The effect time after a visit at traversal time `now` (8031A0A4).
    std::int64_t visit(std::int64_t now);
    // The same without changing the fields.
    std::int64_t peek(std::int64_t now) const;
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
    // igNode _bound (+0xC): the igAABox the stage camera's frustum cull
    // tests (801E9AB4). The data has no other bound volume; a node without
    // one is never tested.
    bool has_bound{};
    Vec3f bound_min{}, bound_max{};
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
    // igBillboard: _mode (+0x20, [8]), _axis (+0x24, [9]) and _center
    // (+0x30, [10]); see billboard_matrix.
    std::int32_t billboard_mode{};
    Vec3f billboard_axis{0, 0, 1};
    Vec3f billboard_center{};
    // igLod: _rangeList ([8], SQUARED eye-space distances), _center ([13])
    // and _hysteresisList ([14]); the selection cache _lastRange (+0x24, here
    // the child index or -1), _lastIndex (+0x28), _lastLower / _lastUpper
    // (+0x2C / +0x30), with the defaults of the registration 801BB474.
    std::vector<float> lod_ranges;
    std::vector<float> lod_hysteresis;
    Vec3f lod_center{};
    std::int32_t lod_child = -1, lod_index = -1;
    float lod_lower{}, lod_upper{};
    // 801F799C: the child for this squared distance (nullptr: none).
    Node* select_lod(float distance_squared);
    // 801F7CB8 (vtable +0x7C): forget the cached selection.
    void reset_lod();
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
    // ParticleTimer: its igParticleAttr list ([8]), its timer, whether
    // beParticleCtrl2 has started its effect (from its start to its stop;
    // the port draws only those), and the quads rebuilt every frame.
    std::vector<ObjectView> particles;
    ParticleTimerState timer;
    bool particle_live{};
    std::vector<std::shared_ptr<GeometryData>> particle_geometry;
    // igTransformRecorder: the world matrix it was last drawn with (TRC data).
    std::optional<Mat4> recorded;
    // beResetNode (802C0B64, fields 802C0C00): _isResetTime (+0x20) runs the
    // children on the traversal's base time, _isResetRotAndScale (+0x21)
    // keeps only the accumulated translation (traversal 80312C28).
    bool reset_time{}, reset_rot_scale{};
    std::optional<NodeAnim> anim;

    // (Inline: the collision queries read the switch bits of every moving
    // child they consider.)
    bool child_enabled(std::size_t index) const {
        if (kind != NodeKind::switch_node) return true;
        const std::size_t word = index / 32;
        if (word >= mask.size()) return false;
        return (mask[word] >> (index % 32)) & 1u;
    }
    void set_child_enabled(std::size_t index, bool enabled);
    void set_all_children(bool enabled);
};

struct RenderState {
    const Attribute* texture{};
    bool texture_enabled{};
    bool blend_enabled{};
    // igBlendFunctionAttr defaults (0x8055E6AC..B4, registered by 800BA89C):
    // SRC_ALPHA, ONE_MINUS_SRC_ALPHA (and the equation ADD, which the
    // GameCube context ignores; see the blend_function case of apply).
    std::int32_t blend_src = 4, blend_dst = 5;
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
    // Back faces are culled wherever no igCullFaceAttr says otherwise: the
    // visual context's cull state (+0x4B0 enabled, +0x4B4 mode) starts
    // enabled with IG mode 0 (BACK); only an igCullFaceAttr apply (800BDDDC
    // -> 800F93B0 / 800F93DC) changes it and sets the dirty bit 0x20 for the
    // flush 800F93F0 (GXSetCullMode through the table 0x8055EE98). Dolphin
    // with GXSetCullMode forced to NONE shows the single-sided reeds of
    // stag0301 (TA3BJ10A) from behind exactly as the port drew them; the
    // stages' igCullFaceAttr (enabled 0) make only their subtrees two-sided.
    bool cull_enabled = true;
    std::int32_t cull_mode = 0; // IG_GFX_CULL_FACE_MODE_BACK
    bool lighting = true; // Alchemy default: only igLightingStateAttr turns it off
    Vec4f color{1, 1, 1, 1};
    std::optional<Vec4f> modulate; // runtime ACTION2 color: the outermost override (801D5190)
    // Set by runtime ACTION2 overrides: deeper attributes of the kind no
    // longer change the value (override push, 801D5190).
    bool texture_forced{}, lighting_forced{}, blend_forced{};
    // igTransform flag 8: the transform (sequence) applies to the texture coordinates.
    std::optional<Mat4> texture_matrix;
    // igTextureCoordSourceAttr of unit 0 (_source): 0 the vertices' texture
    // coordinates, 2 a sphere map from the view-space normals (800FCEA8
    // case 2; only the beWaterPlain surfaces use it, always with 2).
    std::int32_t texcoord_source{};
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
// light into a width x height texture that is cleared white (0x938), the casters
// in the grey _shadowFactor (801FDCCC: colour (f, f, f, 1)); the receivers
// (the shader's children, simplified ground meshes outside the visible scene)
// multiply the map over the scene (801FE190: MODULATE, blend ZERO/SRC_COLOR).
struct ShadowFrame {
    bool active{};
    Mat4 light_view{}, light_projection{};
    float factor = 0.3f; // _shadowFactor: the multiplier inside a shadow
    int width = 128, height = 128;
    // The casters fill the inner viewport (border, border, width - 2 border,
    // height - 2 border); the receivers' texture matrix maps the light box
    // into it.
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
    // `traversed`: the actor's own traversal (its combiner evaluation,
    // 801D0C14); false: a picture of an actor the original's cull skips,
    // which changes nothing.
    std::function<const ActorPose*(const Node& actor, bool traversed)> actor_pose;
    Vec3f camera_position{};
    // igFrustCullTraversal (8040B3EC creates it for the stage camera, whose
    // traversal is the whole frame: scenes, 2D layers, the shadow shader's
    // passes): a node whose bound lies outside the frustum of the current
    // modelview and projection is skipped with its subtree, its updates
    // included (NodeAnim and ParticleTimer clocks, actor combiners, LOD
    // caches, transform recorders). `cull_camera` is the game's camera; a
    // debug camera only moves the picture.
    bool cull{};
    FrameCamera cull_camera;
    // The picture's own cull (Engine::render only): the renderer widens
    // perspective projections by 1 / picture_widen in x (widescreen), so a
    // subtree outside the original's frustum but inside the picture is drawn
    // without its updates; false (a debug camera) draws every such subtree.
    bool picture_cull = true;
    float picture_widen = 1.0f;
    // The projection under the modelview_base of the shadow caster pass
    // (the light's, pushed by 801FDCCC with its view).
    std::optional<Mat4> projection_base;
    // The base of the modelview stack when it is not the traversal's view:
    // the shadow caster pass (801FDCCC) pushes the shader's light view (its
    // igModelViewMatrixAttr +0x58, filled by 801FE5E4) with 801D4F14 and
    // traverses the casters (801FE498) on the scene traversal, whose view
    // and inverse view (+0x64 / +0xA4, set from the camera by 801DB8F0 in
    // 8040B8C0) stay the camera's. Only the billboards use it: the casters'
    // subtrees in the data hold no igLod or igTransformRecorder, the other
    // nodes that read the modelview stack top.
    std::optional<Mat4> modelview_base;
};

// igBillboard (801D5ADC + 801D53EC): the world matrix that replaces the
// parent's world matrix for the billboard's children. It is an orthonormal
// basis (no scale or shear of the parent chain survives) at the parent's
// world position plus center * rotation, with Y the mode's up vector and Z
// towards the eye: mode 0 (and any value outside 1..3) world Y and mode 1
// the axis through the parent's rotation, both kept as they are, so the face
// only turns about them; mode 2 world Y and mode 3 camera Y, each made
// orthogonal to the eye direction first, so the face looks straight at the eye.
Mat4 billboard_matrix(std::int32_t mode, const Vec3f& axis, const Vec3f& center, const Mat4& world,
                      const Mat4& inverse_view);

// igFrustCullTraversal's box test (801E97C8: _compMat = modelview *
// projection, 801E898C: the eight corners in clip space, 801E8BF0): 2 outside
// (every corner behind the eye, or no corner inside one of the six planes
// x > -w, x < w, y > -w, y < w, z > -w, z < w), 1 inside (every corner inside
// every plane), 5 inside the frustum widened by _outerFrustCullScale 1.1
// (x, y and z scaled by 1 / 1.1, set by 801E9BE0 in 8040B3EC), else 3.
int frustum_cull_code(const Mat4& modelview, const Mat4& projection, const Vec3f& box_min, const Vec3f& box_max);

void traverse(Node* root, TraversalContext& context);
void traverse(Node* root, TraversalContext& context, const Mat4& model, const RenderState& state, std::int64_t time);

} // namespace dw4::engine
