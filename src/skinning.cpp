#include "dw4/skinning.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>
#include <span>

namespace dw4 {
namespace {
constexpr std::size_t max_bones = 1024;
constexpr std::size_t max_joints = 256;
constexpr std::size_t max_vertices = 1024u * 1024u;
constexpr std::size_t max_links = 4u * 1024u * 1024u;
constexpr double identity_tolerance = 1e-6;

[[noreturn]] void fail(const std::string& reason) { throw SkinningError("Skinning: " + reason); }

SkinMatrix identity() {
    SkinMatrix result{};
    result[0] = result[5] = result[10] = result[15] = 1;
    return result;
}

bool finite(const Vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

void validate_matrix(const SkinMatrix& matrix) {
    for (const auto value : matrix) if (!std::isfinite(value)) fail("matrix contains a non-finite value");
    if (matrix[3] != 0 || matrix[7] != 0 || matrix[11] != 0 || matrix[15] != 1)
        fail("matrix is not an affine transform");
}

SkinMatrix multiply(const SkinMatrix& a, const SkinMatrix& b) {
    SkinMatrix result{};
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row)
            for (std::size_t term = 0; term < 4; ++term)
                result[column * 4 + row] += a[term * 4 + row] * b[column * 4 + term];
    validate_matrix(result);
    return result;
}

bool is_identity(const SkinMatrix& matrix, double tolerance = identity_tolerance) {
    const auto expected = identity();
    for (std::size_t i = 0; i < 16; ++i) if (std::abs(matrix[i] - expected[i]) > tolerance) return false;
    return true;
}

SkinMatrix inverse(const SkinMatrix& matrix) {
    validate_matrix(matrix);
    // Invert the linear part with scaled pivot selection; translation is then
    // -inverse(linear) * translation. This preserves the affine last row.
    double rows[3][6]{};
    double scale[3]{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            rows[row][column] = matrix[column * 4 + row];
            scale[row] = std::max(scale[row],std::abs(rows[row][column]));
        }
        rows[row][row + 3] = 1;
        if (scale[row] == 0) fail("inverse-bind matrix is singular");
    }
    for (std::size_t column = 0; column < 3; ++column) {
        auto pivot = column;
        for (std::size_t row = column + 1; row < 3; ++row)
            if (std::abs(rows[row][column]) / scale[row] > std::abs(rows[pivot][column]) / scale[pivot]) pivot = row;
        if (std::abs(rows[pivot][column]) / scale[pivot] <= 1e-12) fail("inverse-bind matrix is singular or ill-conditioned");
        if (pivot != column) {
            for (std::size_t i = 0; i < 6; ++i) std::swap(rows[pivot][i],rows[column][i]);
            std::swap(scale[pivot],scale[column]);
        }
        const auto divisor = rows[column][column];
        for (auto& value : rows[column]) value /= divisor;
        for (std::size_t row = 0; row < 3; ++row) {
            if (row == column) continue;
            const auto factor = rows[row][column];
            for (std::size_t i = 0; i < 6; ++i) rows[row][i] -= factor * rows[column][i];
        }
    }
    auto result = identity();
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) result[column * 4 + row] = rows[row][column + 3];
        result[12 + row] = -(rows[row][3] * matrix[12] + rows[row][4] * matrix[13] + rows[row][5] * matrix[14]);
    }
    validate_matrix(result);
    if (!is_identity(multiply(matrix,result)) || !is_identity(multiply(result,matrix)))
        fail("inverse-bind inversion fails the identity tolerance");
    return result;
}

Vec3 transform(const SkinMatrix& matrix, const Vec3& point) {
    if (!finite(point)) fail("source position is non-finite");
    Vec3 result;
    const double values[]{matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
        matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14]};
    for (const auto value : values)
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) fail("posed position exceeds finite float range");
    result = {static_cast<float>(values[0]),static_cast<float>(values[1]),static_cast<float>(values[2])};
    return result;
}

std::uint32_t little_word(std::span<const std::uint8_t> bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) fail("truncated skin data word");
    return std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at + 1]) << 8) |
        (std::uint32_t(bytes[at + 2]) << 16) | (std::uint32_t(bytes[at + 3]) << 24);
}

SkinMatrix read_matrix(std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 64) fail("matrix byte count is not 64");
    SkinMatrix result;
    for (std::size_t i = 0; i < 16; ++i) result[i] = std::bit_cast<float>(little_word(bytes,i * 4));
    validate_matrix(result);
    return result;
}

std::uint32_t required_ref(const IgbDocument& document, const IgbObject& object, std::uint16_t slot) {
    const auto ref = document.field_reference(object,slot);
    if (!ref) fail("required skin reference is null");
    return *ref;
}

SkinMatrix matrix_field(const IgbDocument& document, const IgbObject& object, std::uint16_t slot) {
    const auto* field = document.field(object,slot);
    if (!field) fail("required selector matrix is missing");
    return read_matrix(document.field_bytes(*field));
}

void prepare_joints(SkinningData& result) {
    if (result.bones.empty() || result.bones.size() > max_bones || result.joints.empty() || result.joints.size() > max_joints)
        fail("skeleton size exceeds the 1,024-bone / 256-joint profile");
    std::vector<bool> mapped(result.joints.size());
    for (std::size_t i = 0; i < result.bones.size(); ++i) {
        const auto& bone = result.bones[i];
        if (bone.parent < -1 || (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) >= result.bones.size()))
            fail("bone parent is outside the skeleton");
        auto parent = static_cast<std::int32_t>(i);
        for (std::size_t depth = 0; parent >= 0; ++depth) {
            if (depth >= result.bones.size()) fail("bone hierarchy contains a cycle");
            if (static_cast<std::size_t>(parent) >= result.bones.size()) fail("bone ancestor is outside the skeleton");
            parent = result.bones[static_cast<std::size_t>(parent)].parent;
            if (parent < -1) fail("bone ancestor index is invalid");
        }
        if (bone.joint_index < -1 || (bone.joint_index >= 0 && static_cast<std::size_t>(bone.joint_index) >= result.joints.size()))
            fail("bone blend-matrix index is outside the joint array");
        if (bone.joint_index < 0) continue;
        const auto joint_index = static_cast<std::size_t>(bone.joint_index);
        if (mapped[joint_index]) fail("multiple bones select the same joint matrix");
        mapped[joint_index] = true;
        auto& joint = result.joints[joint_index]; joint.bone_index = static_cast<std::uint32_t>(i);
        joint.parent_joint = -1;
        for (parent = bone.parent; parent >= 0; parent = result.bones[static_cast<std::size_t>(parent)].parent) {
            const auto candidate = result.bones[static_cast<std::size_t>(parent)].joint_index;
            if (candidate >= 0) { joint.parent_joint = candidate; break; }
        }
        joint.bind_world = inverse(joint.inverse_bind);
    }
    if (std::find(mapped.begin(),mapped.end(),false) != mapped.end()) fail("joint matrix has no matching skeleton bone");
    for (auto& joint : result.joints)
        joint.bind_local = joint.parent_joint < 0 ? joint.bind_world :
            multiply(result.joints[static_cast<std::size_t>(joint.parent_joint)].inverse_bind,joint.bind_world);
}

class Decoder {
public:
    Decoder(const IgbDocument& document, std::uint32_t geometry) : doc(document) { result.info.geometry_reference = geometry; }
    SkinningData decode() {
        if (doc.metadata.header.byte_order != IgbByteOrder::little) fail("big-endian skin streams have not been verified");
        const auto& geometry = doc.object(result.info.geometry_reference);
        if (!doc.is_a(geometry,"igGeometryAttr1_5")) fail("selected reference is not an igGeometryAttr1_5");
        const auto mesh = decode_igb_mesh(doc,result.info.geometry_reference);
        result.source_positions = mesh.positions;
        result.info.vertex_count = static_cast<std::uint32_t>(mesh.positions.size());
        const auto& vertices = doc.object(required_ref(doc,geometry,4));
        const auto format = doc.field_u32(vertices,6);
        if (format != 0x10111u && format != 0x10113u) fail("vertex format is outside the verified single-influence profile");
        if (doc.field_u32(vertices,5) != 0 || doc.field_reference(vertices,10)) fail("additional vertex state is outside the skin profile");
        const auto weights = doc.memory(required_ref(doc,vertices,7));
        const auto indices = doc.memory(required_ref(doc,vertices,8));
        if (weights.size() != std::uint64_t(result.info.vertex_count) * 4 || indices.size() != result.info.vertex_count)
            fail("single-influence buffers must contain one float weight and one byte index per vertex");
        resolve_graph();
        read_skeleton();
        const auto& selector = doc.object(*result.info.selector_reference);
        const auto palette_ref = required_ref(doc,selector,10);
        const auto& palette = doc.object(palette_ref);
        if (!doc.is_a(palette,"igIntList")) fail("blend-matrix palette is not an igIntList");
        const auto count = doc.field_i32(palette,2), capacity = doc.field_i32(palette,3);
        if (count <= 0 || count > 256 || capacity < count || capacity > 256) fail("palette count/capacity is outside 1..256");
        const auto palette_bytes = doc.memory(required_ref(doc,palette,4));
        if (palette_bytes.size() != std::uint64_t(capacity) * 4) fail("palette memory does not match its capacity");
        for (std::int32_t i = 0; i < count; ++i) {
            const auto value = little_word(palette_bytes,static_cast<std::size_t>(i) * 4);
            if (value >= result.joints.size()) fail("palette entry is outside the joint array");
            result.palette_to_joint.push_back(value);
        }
        result.influences.reserve(indices.size());
        for (std::size_t i = 0; i < indices.size(); ++i) {
            const auto weight = std::bit_cast<float>(little_word(weights,i * 4));
            if (!std::isfinite(weight) || weight != 1.f) fail("single-influence weight must equal one exactly");
            if (indices[i] >= result.palette_to_joint.size()) fail("vertex blend index is outside the palette");
            result.influences.push_back({indices[i],result.palette_to_joint[indices[i]],weight});
        }
        result.info.supported = true;
        result.info.provenance = "Geometry " + std::to_string(result.info.geometry_reference) + " -> node " +
            std::to_string(*result.info.geometry_node_reference) + " -> selector " + std::to_string(*result.info.selector_reference) +
            " -> skin " + std::to_string(*result.info.skin_reference) + " -> database " + std::to_string(*result.info.database_reference) +
            " -> skeleton " + std::to_string(*result.info.skeleton_reference);
        return std::move(result);
    }
    SkinningInfo info() const { return result.info; }
private:
    const IgbDocument& doc;
    SkinningData result;
    std::size_t links{};
    std::vector<std::optional<std::uint32_t>> list(std::uint32_t ref, std::string_view expected) {
        const auto& object = doc.object(ref);
        if (!doc.is_a(object,expected)) fail("skin graph list has an unexpected type");
        const auto count = doc.field_i32(object,2);
        if (count < 0 || static_cast<std::size_t>(count) > max_links - links) fail("skin graph exceeds the 4,194,304-link limit");
        links += static_cast<std::size_t>(count);
        return doc.object_list(ref);
    }
    void resolve_graph() {
        std::vector<std::uint32_t> owners;
        for (const auto& object : doc.objects) if (doc.is_a(object,"igGeometry")) {
            const auto attrs = doc.field_reference(object,8); if (!attrs) continue;
            const auto contents = list(*attrs,"igAttrList");
            if (std::find(contents.begin(),contents.end(),result.info.geometry_reference) != contents.end()) owners.push_back(object.reference_index);
        }
        if (owners.size() != 1) fail("geometry must have exactly one explicit scene owner");
        result.info.geometry_node_reference = owners[0];
        std::vector<std::uint32_t> parents;
        for (const auto& object : doc.objects) if (doc.is_a(object,"igGroup")) {
            const auto children = doc.field_reference(object,7); if (!children) continue;
            const auto contents = list(*children,"igNodeList");
            for (const auto child : contents) if (child && !doc.is_a(doc.object(*child),"igNode")) fail("node list contains a non-node");
            if (std::find(contents.begin(),contents.end(),owners[0]) != contents.end()) parents.push_back(object.reference_index);
        }
        if (parents.size() != 1 || !doc.is_a(doc.object(parents[0]),"igBlendMatrixSelect"))
            fail("geometry must have one direct igBlendMatrixSelect parent");
        result.info.selector_reference = parents[0]; const auto& selector = doc.object(parents[0]);
        if (!is_identity(matrix_field(doc,selector,11)) || !is_identity(matrix_field(doc,selector,12)))
            fail("non-identity skeleton selector transforms are outside the verified profile");
        const auto state_list = list(required_ref(doc,selector,8),"igAttrList");
        std::size_t states{};
        for (const auto ref : state_list) if (ref) {
            const auto& attribute = doc.object(*ref);
            if (!doc.is_a(attribute,"igAttr")) fail("attribute list contains a non-attribute");
            if (!doc.is_a(attribute,"igVertexBlendStateAttr")) continue;
            ++states; const auto* field = doc.field(attribute,4);
            if (!field || doc.field_bytes(*field).size() != 1 || doc.field_bytes(*field)[0] != 1)
                fail("vertex blending must be explicitly enabled");
        }
        if (states != 1) fail("selector must contain exactly one vertex-blend enable state");
        const auto owner_attrs = list(required_ref(doc,doc.object(owners[0]),8),"igAttrList");
        for (const auto ref : owner_attrs) if (ref) {
            const auto& attribute = doc.object(*ref);
            if (!doc.is_a(attribute,"igAttr")) fail("geometry attribute list contains a non-attribute");
            if (doc.is_a(attribute,"igVertexBlendStateAttr")) fail("child vertex-blend overrides are outside the verified profile");
        }
        std::vector<std::uint32_t> skins;
        for (const auto& object : doc.objects) if (doc.is_a(object,"igSkin") && doc.field_reference(object,3) == parents[0]) skins.push_back(object.reference_index);
        if (skins.size() != 1) fail("selector must be owned by exactly one igSkin");
        result.info.skin_reference = skins[0]; result.info.skin_name = std::string(doc.field_string(doc.object(skins[0]),2));
        std::vector<std::uint32_t> databases;
        for (const auto& object : doc.objects) if (doc.is_a(object,"igAnimationDatabase")) {
            const auto skin_list = doc.field_reference(object,7); if (!skin_list) continue;
            const auto contents = list(*skin_list,"igSkinList");
            for (const auto skin : contents) if (skin && !doc.is_a(doc.object(*skin),"igSkin")) fail("skin list contains a non-skin");
            if (std::find(contents.begin(),contents.end(),skins[0]) != contents.end()) databases.push_back(object.reference_index);
        }
        if (databases.size() != 1) fail("skin must belong to exactly one explicit animation database");
        result.info.database_reference = databases[0];
        const auto skeletons = list(required_ref(doc,doc.object(databases[0]),5),"igSkeletonList");
        if (skeletons.size() != 1 || !skeletons[0] || !doc.is_a(doc.object(*skeletons[0]),"igSkeleton"))
            fail("owning database must have exactly one explicit skeleton");
        result.info.skeleton_reference = *skeletons[0];
        for (const auto& object : doc.objects) if (doc.is_a(object,"igActor")) {
            const auto appearance_ref = doc.field_reference(object,11); if (!appearance_ref) continue;
            const auto& appearance = doc.object(*appearance_ref);
            if (doc.is_a(appearance,"igAppearance") && doc.field_reference(appearance,3) == skins[0])
                result.info.actor_references.push_back(object.reference_index);
        }
    }
    void read_skeleton() {
        const auto& skeleton = doc.object(*result.info.skeleton_reference);
        result.info.skeleton_name = std::string(doc.field_string(skeleton,2));
        const auto count = doc.field_i32(skeleton,6);
        if (count <= 0 || count > static_cast<std::int32_t>(max_joints)) fail("joint count is outside 1..256");
        const auto bone_refs = list(required_ref(doc,skeleton,4),"igSkeletonBoneInfoList");
        if (bone_refs.empty() || bone_refs.size() > max_bones) fail("bone count is outside 1..1,024");
        for (const auto ref : bone_refs) {
            if (!ref || !doc.is_a(doc.object(*ref),"igSkeletonBoneInfo")) fail("bone list contains a missing or wrong-type bone");
            const auto& bone = doc.object(*ref);
            result.bones.push_back({*ref,std::string(doc.field_string(bone,2)),doc.field_i32(bone,3),doc.field_i32(bone,4)});
        }
        const auto matrices = doc.memory(required_ref(doc,skeleton,5));
        if (matrices.size() != std::uint64_t(count) * 64) fail("inverse-joint byte count does not match the joint count");
        result.joints.resize(static_cast<std::size_t>(count));
        for (std::size_t i = 0; i < result.joints.size(); ++i) result.joints[i].inverse_bind = read_matrix(matrices.subspan(i * 64,64));
        prepare_joints(result);
        result.info.bone_count = static_cast<std::uint32_t>(result.bones.size());
        result.info.joint_count = static_cast<std::uint32_t>(result.joints.size());
    }
};

SkinMatrix rotation_matrix(Vec3 degrees) {
    if (!finite(degrees) || std::abs(degrees.x) > 360 || std::abs(degrees.y) > 360 || std::abs(degrees.z) > 360)
        fail("manual rotations must be finite and within -360..360 degrees");
    const double scale = std::numbers::pi / 180;
    auto x = identity(), y = identity(), z = identity();
    const double sx = std::sin(degrees.x * scale), cx = std::cos(degrees.x * scale);
    const double sy = std::sin(degrees.y * scale), cy = std::cos(degrees.y * scale);
    const double sz = std::sin(degrees.z * scale), cz = std::cos(degrees.z * scale);
    x[5] = x[10] = cx; x[6] = sx; x[9] = -sx;
    y[0] = y[10] = cy; y[2] = -sy; y[8] = sy;
    z[0] = z[5] = cz; z[1] = sz; z[4] = -sz;
    return multiply(z,multiply(y,x));
}
} // namespace

SkinningData decode_igb_skinning(const IgbDocument& document, std::uint32_t geometry_reference) {
    try { return Decoder(document,geometry_reference).decode(); }
    catch (const IgbError& error) { throw SkinningError(error.what()); }
    catch (const MeshError& error) { throw SkinningError(error.what()); }
}

SkinningInfo inspect_igb_skinning(const IgbDocument& document, std::uint32_t geometry_reference) {
    Decoder decoder(document,geometry_reference);
    try { return decoder.decode().info; }
    catch (const std::exception& error) {
        auto info = decoder.info(); info.supported = false; info.diagnostic = error.what(); return info;
    }
}

SkinPose evaluate_skinning(const SkinningData& data, std::optional<BoneRotation> rotation) {
    if (data.source_positions.empty() || data.source_positions.size() > max_vertices || data.influences.size() != data.source_positions.size())
        fail("source position/influence counts are invalid");
    if (data.bones.empty() || data.bones.size() > max_bones || data.joints.empty() || data.joints.size() > max_joints ||
        data.palette_to_joint.empty() || data.palette_to_joint.size() > max_joints) fail("pose skeleton/palette counts are invalid");
    for (const auto index : data.palette_to_joint) if (index >= data.joints.size()) fail("pose palette entry is outside the joint array");
    std::optional<std::uint32_t> selected_joint;
    auto delta = identity();
    if (rotation) {
        if (rotation->bone_index >= data.bones.size() || data.bones[rotation->bone_index].joint_index < 0)
            fail("manual pose requires a joint-bearing bone");
        selected_joint = static_cast<std::uint32_t>(data.bones[rotation->bone_index].joint_index);
        if (*selected_joint >= data.joints.size()) fail("selected bone joint is outside the joint array");
        delta = rotation_matrix(rotation->degrees);
    }
    std::vector<SkinMatrix> world(data.joints.size()), skin(data.joints.size());
    std::vector<std::uint8_t> visited(data.joints.size());
    std::function<void(std::size_t)> evaluate = [&](std::size_t index) {
        if (visited[index] == 2) return;
        if (visited[index] == 1) fail("pose joint hierarchy contains a cycle");
        visited[index] = 1;
        const auto& joint = data.joints[index];
        if (joint.bone_index >= data.bones.size() || data.bones[joint.bone_index].joint_index != static_cast<std::int32_t>(index))
            fail("pose bone-to-joint mapping is inconsistent");
        if (joint.parent_joint < -1 || (joint.parent_joint >= 0 && static_cast<std::size_t>(joint.parent_joint) >= data.joints.size()))
            fail("pose joint parent is outside the array");
        validate_matrix(joint.bind_local); validate_matrix(joint.inverse_bind); validate_matrix(joint.bind_world);
        if (!is_identity(multiply(joint.bind_world,joint.inverse_bind))) fail("pose bind matrices do not cancel");
        const auto expected_world = joint.parent_joint < 0 ? joint.bind_local :
            multiply(data.joints[static_cast<std::size_t>(joint.parent_joint)].bind_world,joint.bind_local);
        for (std::size_t element = 0; element < 16; ++element)
            if (std::abs(expected_world[element] - joint.bind_world[element]) > identity_tolerance)
                fail("pose local bind matrix is inconsistent with its parent and world bind matrix");
        auto local = joint.bind_local;
        if (selected_joint == index) local = multiply(local,delta);
        if (joint.parent_joint >= 0) {
            const auto parent = static_cast<std::size_t>(joint.parent_joint); evaluate(parent);
            world[index] = multiply(world[parent],local);
        } else world[index] = local;
        skin[index] = multiply(world[index],joint.inverse_bind); visited[index] = 2;
    };
    for (std::size_t i = 0; i < data.joints.size(); ++i) evaluate(i);
    SkinPose result; result.positions.reserve(data.source_positions.size()); result.joint_positions.reserve(world.size());
    for (const auto& matrix : world) result.joint_positions.push_back(transform(matrix,{0,0,0}));
    for (std::size_t i = 0; i < data.source_positions.size(); ++i) {
        const auto& influence = data.influences[i];
        if (influence.weight != 1.f || influence.palette_index >= data.palette_to_joint.size() ||
            influence.joint_index >= skin.size() || data.palette_to_joint[influence.palette_index] != influence.joint_index)
            fail("pose influence is outside the single-weight palette profile");
        result.positions.push_back(transform(skin[influence.joint_index],data.source_positions[i]));
    }
    return result;
}

SkinMatrix original_bone_transform(const std::array<float, 4>& quaternion, Vec3 translation) {
    for (const auto value : quaternion) if (!std::isfinite(value)) fail("bone quaternion contains a non-finite value");
    if (!finite(translation)) fail("bone translation contains a non-finite value");
    const double x = quaternion[0], y = quaternion[1], z = quaternion[2], w = quaternion[3];
    auto result = identity();
    // Preserve the flat matrix indexing and signs of 0x8012bcdc. In our host
    // column-vector convention this differs from a Hamilton active rotation.
    result[0] = 1 - 2*y*y - 2*z*z;
    result[1] = 2*x*y - 2*z*w;
    result[2] = 2*x*z + 2*y*w;
    result[4] = 2*x*y + 2*z*w;
    result[5] = 1 - 2*x*x - 2*z*z;
    result[6] = 2*y*z - 2*x*w;
    result[8] = 2*x*z - 2*y*w;
    result[9] = 2*y*z + 2*x*w;
    result[10] = 1 - 2*x*x - 2*y*y;
    result[12] = translation.x; result[13] = translation.y; result[14] = translation.z;
    validate_matrix(result);
    for (const auto value : result)
        if (std::abs(value) > std::numeric_limits<float>::max()) fail("bone transform exceeds finite float range");
    return result;
}

SkinPose evaluate_bone_transforms(const SkinningData& data, std::span<const SkinMatrix> local_transforms) {
    if (data.source_positions.empty() || data.source_positions.size() > max_vertices || data.influences.size() != data.source_positions.size())
        fail("source position/influence counts are invalid");
    if (data.bones.empty() || data.bones.size() > max_bones || data.joints.empty() || data.joints.size() > max_joints ||
        data.palette_to_joint.empty() || data.palette_to_joint.size() > max_joints) fail("pose skeleton/palette counts are invalid");
    if (local_transforms.size() != data.bones.size()) fail("absolute local transform count must equal the full bone count");
    for (const auto index : data.palette_to_joint) if (index >= data.joints.size()) fail("pose palette entry is outside the joint array");
    std::vector<bool> mapped(data.joints.size());
    for (std::size_t index = 0; index < data.bones.size(); ++index) {
        const auto& bone = data.bones[index];
        if (bone.parent < -1 || (bone.parent >= 0 && static_cast<std::size_t>(bone.parent) >= data.bones.size()))
            fail("pose bone parent is outside the skeleton");
        if (bone.joint_index < -1 || (bone.joint_index >= 0 && static_cast<std::size_t>(bone.joint_index) >= data.joints.size()))
            fail("pose bone joint index is outside the joint array");
        validate_matrix(local_transforms[index]);
        if (bone.joint_index < 0) continue;
        const auto joint_index = static_cast<std::size_t>(bone.joint_index);
        if (mapped[joint_index] || data.joints[joint_index].bone_index != index)
            fail("pose bone-to-joint mapping is inconsistent");
        mapped[joint_index] = true;
        const auto& joint = data.joints[joint_index];
        validate_matrix(joint.inverse_bind); validate_matrix(joint.bind_world);
        if (!is_identity(multiply(joint.bind_world,joint.inverse_bind)) || !is_identity(multiply(joint.inverse_bind,joint.bind_world)))
            fail("pose bind matrices do not cancel");
    }
    if (std::find(mapped.begin(),mapped.end(),false) != mapped.end()) fail("pose joint matrix has no matching bone");
    std::vector<SkinMatrix> bone_world(data.bones.size());
    std::vector<std::uint8_t> visited(data.bones.size());
    std::function<void(std::size_t)> evaluate = [&](std::size_t index) {
        if (visited[index] == 2) return;
        if (visited[index] == 1) fail("pose bone hierarchy contains a cycle");
        visited[index] = 1;
        const auto parent = data.bones[index].parent;
        if (parent >= 0) {
            evaluate(static_cast<std::size_t>(parent));
            bone_world[index] = multiply(bone_world[static_cast<std::size_t>(parent)],local_transforms[index]);
        } else bone_world[index] = local_transforms[index];
        visited[index] = 2;
    };
    for (std::size_t index = 0; index < data.bones.size(); ++index) evaluate(index);
    std::vector<SkinMatrix> skin(data.joints.size());
    SkinPose result;
    result.joint_positions.reserve(data.joints.size()); result.positions.reserve(data.source_positions.size());
    for (std::size_t index = 0; index < data.joints.size(); ++index) {
        const auto& world = bone_world[data.joints[index].bone_index];
        skin[index] = multiply(world,data.joints[index].inverse_bind);
        result.joint_positions.push_back(transform(world,{0,0,0}));
    }
    for (std::size_t index = 0; index < data.source_positions.size(); ++index) {
        const auto& influence = data.influences[index];
        if (influence.weight != 1.f || influence.palette_index >= data.palette_to_joint.size() ||
            influence.joint_index >= skin.size() || data.palette_to_joint[influence.palette_index] != influence.joint_index)
            fail("pose influence is outside the single-weight palette profile");
        result.positions.push_back(transform(skin[influence.joint_index],data.source_positions[index]));
    }
    return result;
}

} // namespace dw4
