#include "dw4/skinning.hpp"
#include "dw4/assets.hpp"
#include "dw4/igb_document.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4;
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t null_ref = 0xffffffffu;
int checks{};
void check(bool condition, const std::string& reason) {
    ++checks; if (!condition) throw std::runtime_error(reason);
}
bool near(float a, float b, float tolerance = 1e-5f) { return std::abs(a-b) <= tolerance; }
bool near(Vec3 a, Vec3 b) { return near(a.x,b.x) && near(a.y,b.y) && near(a.z,b.z); }
template<class F> void rejects(F&& operation, const std::string& reason) {
    bool caught = false; try { operation(); } catch (const SkinningError&) { caught = true; }
    check(caught,reason);
}
void word(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes.at(at+i) = static_cast<std::uint8_t>(value >> (i*8));
}
void push32(Bytes& bytes, std::uint32_t value) {
    const auto at = bytes.size(); bytes.resize(at+4); word(bytes,at,value);
}
void push16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void append(Bytes& bytes, const Bytes& source) { bytes.insert(bytes.end(),source.begin(),source.end()); }
Bytes words(std::initializer_list<std::uint32_t> values) {
    Bytes result; for (auto value : values) push32(result,value); return result;
}
Bytes floats(std::initializer_list<float> values) {
    Bytes result; for (auto value : values) push32(result,std::bit_cast<std::uint32_t>(value)); return result;
}
void name(Bytes& bytes, const std::string& text) { bytes.insert(bytes.end(),text.begin(),text.end()); bytes.push_back(0); }
void align(Bytes& bytes) { bytes.resize((bytes.size()+3) & ~std::size_t(3)); }
SkinMatrix identity() { SkinMatrix result{}; result[0]=result[5]=result[10]=result[15]=1; return result; }
Bytes matrix_bytes(const SkinMatrix& matrix) {
    Bytes bytes; for (auto value : matrix) push32(bytes,std::bit_cast<std::uint32_t>(static_cast<float>(value))); return bytes;
}

enum Type : std::uint32_t {
    object, object_dir, memory_dir, attr, geometry_attr, vertex_array, object_list,
    attr_list, node_list, skin_list, skeleton_list, bone_list, node, group,
    attr_set, geometry_node, selector_type, blend_state, skin_type, database_type,
    skeleton_type, bone_type, actor_type, appearance_type
};
struct Schema { std::string name; std::uint32_t parent, slots; std::vector<IgbObjectField> fields; };
const std::vector<Schema> schemas{
    {"igObject",null_ref,2,{}},
    {"igObjectDirEntry",object,13,{{0,2,4},{1,11,4},{1,12,4}}},
    {"igMemoryDirEntry",object,14,{{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
    {"igAttr",object,2,{}},
    {"igGeometryAttr1_5",attr,14,{{2,4,4},{2,5,4},{6,6,4},{5,7,4},{5,8,4},{1,10,4},{2,13,4}}},
    {"igVertexArray1_1",object,11,{{3,2,4},{5,3,4},{5,4,4},{5,5,4},{5,6,4},{3,7,4},{3,8,4},{3,10,4}}},
    {"igObjectList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAttrList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igNodeList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkinList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkeletonList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkeletonBoneInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igNode",object,3,{{0,2,4}}},
    {"igGroup",node,8,{{0,2,4},{2,7,4}}},
    {"igAttrSet",group,10,{{0,2,4},{2,7,4},{2,8,4},{4,9,1}}},
    {"igGeometry",attr_set,10,{{0,2,4},{2,7,4},{2,8,4},{4,9,1}}},
    {"igBlendMatrixSelect",attr_set,13,{{0,2,4},{2,7,4},{2,8,4},{4,9,1},{2,10,4},{7,11,64},{7,12,64}}},
    {"igVertexBlendStateAttr",attr,5,{{4,4,1}}},
    {"igSkin",object,4,{{0,2,4},{2,3,4}}},
    {"igAnimationDatabase",object,8,{{2,5,4},{2,7,4}}},
    {"igSkeleton",object,7,{{0,2,4},{2,4,4},{3,5,4},{1,6,4}}},
    {"igSkeletonBoneInfo",object,6,{{0,2,4},{1,3,4},{1,4,4},{1,5,4}}},
    {"igActor",node,12,{{0,2,4},{2,11,4}}},
    {"igAppearance",object,4,{{2,3,4}}},
    {"igIntList",object,5,{{1,2,4},{1,3,4},{3,4,4}}}
};

// Generated IGB records and local reference graph; no original game bytes.
struct Graph {
    struct Record {
        bool memory{};
        std::uint32_t type{};
        std::map<std::uint16_t, Bytes> values;
        Bytes data;
    };
    std::vector<Record> records;
    std::uint32_t add(std::uint32_t type) {
        const auto ref = static_cast<std::uint32_t>(records.size());
        Record record; record.type = type;
        for (const auto& field : schemas.at(type).fields) {
            record.values[field.slot] = Bytes(field.type_index == 0 ? 4 : field.size);
            if (field.type_index == 2 || field.type_index == 3) word(record.values[field.slot], 0, null_ref);
        }
        records.push_back(std::move(record)); return ref;
    }
    std::uint32_t memory(Bytes data) {
        const auto ref = static_cast<std::uint32_t>(records.size());
        Record record; record.memory = true; record.data = std::move(data);
        records.push_back(std::move(record)); return ref;
    }
    void scalar(std::uint32_t ref, std::uint16_t slot, std::uint32_t value) {
        auto& bytes = records.at(ref).values.at(slot);
        if (bytes.size() == 1) bytes[0] = static_cast<std::uint8_t>(value);
        else word(bytes, 0, value);
    }
    void text(std::uint32_t ref, std::uint16_t slot, const std::string& value) {
        auto bytes = words({static_cast<std::uint32_t>(value.size() + 1)});
        name(bytes, value); records.at(ref).values.at(slot) = std::move(bytes);
    }
    void contents(std::uint32_t ref, const std::vector<std::uint32_t>& values) {
        Bytes data; for (auto value : values) push32(data, value);
        const auto memory_ref = memory(std::move(data));
        scalar(ref, 2, static_cast<std::uint32_t>(values.size()));
        scalar(ref, 3, static_cast<std::uint32_t>(values.size())); scalar(ref, 4, memory_ref);
    }
    std::uint32_t list(std::uint32_t type, const std::vector<std::uint32_t>& values) {
        const auto ref = add(type); contents(ref, values); return ref;
    }
    IgbDocument document() const {
        const std::vector<std::string> field_names{"igStringMetaField","igIntMetaField","igObjectRefMetaField",
            "igMemoryRefMetaField","igBoolMetaField","igUnsignedIntMetaField","igEnumMetaField","igMatrix44fMetaField"};
        Bytes fields(field_names.size() * 12);
        for (std::size_t i = 0; i < field_names.size(); ++i) {
            word(fields, i * 12, static_cast<std::uint32_t>(field_names[i].size() + 1));
            word(fields, i * 12 + 4, 1); name(fields, field_names[i]);
        }
        Bytes types(schemas.size() * 24);
        for (std::size_t i = 0; i < schemas.size(); ++i) {
            const auto& schema = schemas[i];
            word(types,i * 24,static_cast<std::uint32_t>(schema.name.size() + 1)); word(types,i * 24 + 4,1);
            word(types,i * 24 + 12,static_cast<std::uint32_t>(schema.fields.size()));
            word(types,i * 24 + 16,schema.parent); word(types,i * 24 + 20,schema.slots); name(types,schema.name);
            for (const auto& field : schema.fields) {
                push16(types,field.type_index); push16(types,field.slot); push16(types,field.size);
            }
        }
        Bytes entries, objects, memories; std::uint32_t object_count{}, memory_count{};
        for (const auto& record : records) {
            if (record.memory) {
                append(entries, words({memory_dir,32,0,static_cast<std::uint32_t>(record.data.size()),null_ref,0,null_ref,null_ref}));
                append(memories,record.data); align(memories); ++memory_count;
            } else {
                append(entries, words({object_dir,20,0,record.type,null_ref}));
                auto object_bytes = words({record.type,0});
                for (const auto& field : schemas.at(record.type).fields) {
                    append(object_bytes,record.values.at(field.slot)); align(object_bytes);
                }
                word(object_bytes,4,static_cast<std::uint32_t>(object_bytes.size())); append(objects,object_bytes); ++object_count;
            }
        }
        const auto count = static_cast<std::uint32_t>(records.size());
        auto bytes = words({static_cast<std::uint32_t>(entries.size()),count,static_cast<std::uint32_t>(types.size()),
            static_cast<std::uint32_t>(schemas.size()),static_cast<std::uint32_t>(objects.size()),object_count,
            static_cast<std::uint32_t>(memories.size()),memory_count,static_cast<std::uint32_t>(fields.size()),
            static_cast<std::uint32_t>(field_names.size()),0xfada,0x20000006});
        append(bytes,fields); push32(bytes,4); append(bytes,types); append(bytes,entries);
        append(bytes,words({8 + 2 * count,count}));
        for (std::uint32_t i = 0; i < count; ++i) push16(bytes,static_cast<std::uint16_t>(i));
        append(bytes,objects); append(bytes,memories); return read_igb_document(std::move(bytes));
    }
};




struct Fixture : Graph {
    std::uint32_t geom, vertices, owner, selector, skin, database, skeleton;
    std::uint32_t weights, indices, palette, palette_memory, inverse_matrices;
    std::uint32_t owner_attrs, selector_attrs, selector_children, state, database_skins, database_skeletons, bones_list;
    std::vector<std::uint32_t> bones;
    Fixture() {
        geom=add(geometry_attr); vertices=add(vertex_array); scalar(geom,4,vertices); scalar(geom,6,3); scalar(geom,7,1);
        scalar(vertices,3,3); scalar(vertices,6,0x10113);
        const auto positions=memory(floats({2,0,0,4,1,0,1,1,0})); Bytes table(80,0xff); word(table,0,positions);
        const auto streams=memory(table); scalar(vertices,2,streams);
        weights=memory(floats({1,1,1})); indices=memory(Bytes{0,1,1}); scalar(vertices,7,weights); scalar(vertices,8,indices);
        owner=add(geometry_node); owner_attrs=list(attr_list,{geom}); scalar(owner,8,owner_attrs);
        selector=add(selector_type); selector_children=list(node_list,{owner}); scalar(selector,7,selector_children);
        records[selector].values[11]=matrix_bytes(identity()); records[selector].values[12]=matrix_bytes(identity());
        state=add(blend_state); scalar(state,4,1); selector_attrs=list(attr_list,{state}); scalar(selector,8,selector_attrs);
        // An igIntList is intentionally a distinct type from object-reference lists.
        palette=add_int_list(); palette_memory=memory(words({0,1})); scalar(palette,2,2); scalar(palette,3,2); scalar(palette,4,palette_memory);
        scalar(selector,10,palette);
        skin=add(skin_type); text(skin,2,"Synthetic skin"); scalar(skin,3,selector);
        skeleton=add(skeleton_type); text(skeleton,2,"Synthetic skeleton"); scalar(skeleton,6,2);
        for (int i=0;i<4;++i) { const auto bone=add(bone_type); text(bone,2,"Bone " + std::to_string(i)); bones.push_back(bone); }
        scalar(bones[0],3,null_ref); scalar(bones[0],4,null_ref);
        scalar(bones[1],3,0); scalar(bones[1],4,0);
        scalar(bones[2],3,1); scalar(bones[2],4,null_ref);
        scalar(bones[3],3,2); scalar(bones[3],4,1);
        bones_list=list(bone_list,bones); scalar(skeleton,4,bones_list);
        auto root_inverse=identity(); root_inverse[12]=-1;
        auto child_inverse=identity(); child_inverse[0]=child_inverse[5]=0; child_inverse[1]=-1; child_inverse[4]=1; child_inverse[13]=3;
        auto matrix_data=matrix_bytes(root_inverse); append(matrix_data,matrix_bytes(child_inverse));
        inverse_matrices=memory(matrix_data); scalar(skeleton,5,inverse_matrices);
        database=add(database_type); database_skins=list(skin_list,{skin}); database_skeletons=list(skeleton_list,{skeleton});
        scalar(database,7,database_skins); scalar(database,5,database_skeletons);
        const auto appearance=add(appearance_type); scalar(appearance,3,skin);
        const auto actor=add(actor_type); scalar(actor,11,appearance);
    }
    std::uint32_t add_int_list() { return add(static_cast<std::uint32_t>(schemas.size()-1)); }
    SkinningData decode() const { return decode_igb_skinning(document(),geom); }
};

void rejected(const Fixture& fixture, const std::string& reason) {
    const auto document=fixture.document();
    const auto info=inspect_igb_skinning(document,fixture.geom);
    check(!info.supported && !info.diagnostic.empty(),reason + " diagnostic");
    rejects([&] { decode_igb_skinning(document,fixture.geom); },reason);
}

void synthetic() {
    Fixture f; const auto skin=f.decode();
    check(skin.info.supported && skin.info.geometry_reference==f.geom,"Decode generated complete skin graph");
    check(skin.info.geometry_node_reference==f.owner && skin.info.selector_reference==f.selector,"Preserve geometry/selector chain");
    check(skin.info.skin_reference==f.skin && skin.info.database_reference==f.database && skin.info.skeleton_reference==f.skeleton,"Preserve database/skeleton provenance");
    check(skin.info.actor_references.size()==1 && !skin.info.provenance.empty(),"Report explicit appearance actor provenance");
    check(skin.bones.size()==4 && skin.joints.size()==2 && skin.influences.size()==3,"Decode skeleton and vertex counts");
    check(skin.joints[1].bone_index==3 && skin.joints[1].parent_joint==0,"Joint hierarchy skips helper bones");
    check(skin.bones[2].joint_index==-1 && skin.palette_to_joint==std::vector<std::uint32_t>{0,1},"Keep bone ordinals distinct from joint/palette indices");
    const auto zero=evaluate_skinning(skin);
    check(near(zero.positions[0],{2,0,0}) && near(zero.positions[1],{4,1,0}) && near(zero.positions[2],{1,1,0}),"Inverse-bind round trip restores all stored positions");
    check(near(zero.joint_positions[0],{1,0,0}) && near(zero.joint_positions[1],{3,0,0}),"Recovered bind-world joint origins");
    const auto root_pose=evaluate_skinning(skin,BoneRotation{1,{0,0,90}});
    check(near(root_pose.positions[0],{1,1,0}) && near(root_pose.positions[1],{0,3,0}),"Parent rotation carries weighted descendant vertices");
    check(near(root_pose.joint_positions[1],{1,2,0}),"Parent rotation carries child joint pivot");
    const auto child_pose=evaluate_skinning(skin,BoneRotation{3,{90,0,0}});
    check(near(child_pose.positions[0],{2,0,0}) && near(child_pose.positions[1],{3,1,-1}) && near(child_pose.positions[2],{3,1,2}),"Noncommuting child rotation acts in recovered local bind frame");
    check(near(child_pose.joint_positions[1],{3,0,0}),"Child local rotation preserves its own pivot");
    check(near(evaluate_skinning(skin).positions[1],{4,1,0}),"Manual evaluation leaves immutable source/reset intact");
    rejects([&]{ evaluate_skinning(skin,BoneRotation{0,{0,0,10}}); },"Reject helper-only manual bone");
    rejects([&]{ evaluate_skinning(skin,BoneRotation{99,{0,0,10}}); },"Reject out-of-range manual bone");
    for (auto invalid : {361.f,-361.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        rejects([&]{ evaluate_skinning(skin,BoneRotation{1,{invalid,0,0}}); },"Reject invalid manual rotation");

    auto bad=f; bad.scalar(bad.vertices,6,0x10223); rejected(bad,"Reject unverified two-influence format");
    bad=f; bad.records[bad.weights].data.resize(8); rejected(bad,"Reject truncated weights");
    bad=f; bad.records[bad.indices].data.resize(4); rejected(bad,"Reject unexpected index stride");
    for (auto weight : {0.f,.5f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        bad=f; word(bad.records[bad.weights].data,0,std::bit_cast<std::uint32_t>(weight)); rejected(bad,"Reject non-unit or non-finite rigid weight");
    }
    bad=f; bad.records[bad.indices].data[0]=2; rejected(bad,"Reject vertex outside palette");
    bad=f; word(bad.records[bad.palette_memory].data,0,99); rejected(bad,"Reject palette outside joint array");
    bad=f; bad.scalar(bad.palette,3,257); rejected(bad,"Reject oversized palette capacity");
    bad=f; bad.scalar(bad.palette,2,0); rejected(bad,"Reject empty palette");
    bad=f; bad.records[bad.palette_memory].data.resize(4); rejected(bad,"Reject palette capacity byte mismatch");
    bad=f; bad.scalar(bad.vertices,7,null_ref); rejected(bad,"Reject missing influence memory");
    bad=f; bad.scalar(bad.vertices,5,1); rejected(bad,"Reject additional vertex state");
    bad=f; bad.scalar(bad.vertices,10,bad.weights); rejected(bad,"Reject extra point sprite state");
    bad=f; bad.scalar(bad.state,4,0); rejected(bad,"Reject disabled vertex blending");
    bad=f; bad.contents(bad.selector_attrs,{}); rejected(bad,"Reject missing blend state");
    bad=f; bad.contents(bad.selector_attrs,{bad.state,bad.state}); rejected(bad,"Reject duplicate blend state");
    bad=f; bad.contents(bad.owner_attrs,{bad.geom,bad.state}); rejected(bad,"Reject child blend override");
    bad=f; bad.contents(bad.owner_attrs,{}); rejected(bad,"Reject missing owner");
    bad=f; const auto owner2=bad.add(geometry_node); bad.scalar(owner2,8,bad.owner_attrs); rejected(bad,"Reject ambiguous geometry ownership");
    bad=f; const auto parent2=bad.add(group); bad.scalar(parent2,7,bad.selector_children); rejected(bad,"Reject ambiguous selector parents");
    bad=f; bad.scalar(bad.selector,7,null_ref); rejected(bad,"Reject missing direct selector parent");
    bad=f; auto matrix=identity(); matrix[12]=1; bad.records[bad.selector].values[11]=matrix_bytes(matrix); rejected(bad,"Reject non-identity selector transform");
    bad=f; bad.records[bad.selector].values[12]=matrix_bytes(matrix); rejected(bad,"Reject non-identity selector inverse transform");
    bad=f; const auto skin2=bad.add(skin_type); bad.scalar(skin2,3,bad.selector); rejected(bad,"Reject ambiguous skin ownership");
    bad=f; bad.contents(bad.database_skins,{}); rejected(bad,"Reject skin outside explicit database");
    bad=f; const auto db2=bad.add(database_type); bad.scalar(db2,7,bad.database_skins); rejected(bad,"Reject multiple containing databases");
    bad=f; bad.contents(bad.database_skeletons,{}); rejected(bad,"Reject missing skeleton association");
    bad=f; bad.contents(bad.database_skeletons,{bad.skeleton,bad.skeleton}); rejected(bad,"Reject multiple database skeletons");
    bad=f; bad.contents(bad.database_skeletons,{bad.skin}); rejected(bad,"Reject wrong skeleton type");
    bad=f; bad.contents(bad.database_skins,{bad.skin,bad.skeleton}); rejected(bad,"Reject non-skin member");
    bad=f; bad.scalar(bad.skeleton,6,257); rejected(bad,"Reject joint allocation cap");
    bad=f; bad.contents(bad.bones_list,{}); rejected(bad,"Reject empty bone list");
    bad=f; bad.contents(bad.bones_list,std::vector<std::uint32_t>(1025,bad.bones[0])); rejected(bad,"Reject bone allocation cap");
    bad=f; bad.scalar(bad.bones[0],3,3); rejected(bad,"Reject bone parent cycle");
    bad=f; bad.scalar(bad.bones[2],3,99); rejected(bad,"Reject out-of-range helper parent");
    bad=f; bad.scalar(bad.bones[1],4,1); rejected(bad,"Reject duplicate joint ownership");
    bad=f; bad.scalar(bad.bones[3],4,null_ref); rejected(bad,"Reject unmapped joint matrix");
    bad=f; bad.scalar(bad.bones[3],4,2); rejected(bad,"Reject joint index outside matrix array");
    bad=f; bad.records[bad.inverse_matrices].data.resize(64); rejected(bad,"Reject truncated inverse matrices");
    bad=f; word(bad.records[bad.inverse_matrices].data,0,0); rejected(bad,"Reject singular inverse matrix");
    bad=f; word(bad.records[bad.inverse_matrices].data,12,std::bit_cast<std::uint32_t>(1.f)); rejected(bad,"Reject non-affine inverse matrix");
    bad=f; word(bad.records[bad.inverse_matrices].data,0,std::bit_cast<std::uint32_t>(std::numeric_limits<float>::infinity())); rejected(bad,"Reject non-finite inverse matrix");
    check(!inspect_igb_skinning(f.document(),f.skin).supported,"Reject non-geometry selection");
    check(!inspect_igb_skinning(f.document(),99999).supported,"Reject invalid geometry reference");

    auto invalid=skin; invalid.joints[0].bind_local[12]+=1;
    rejects([&]{ evaluate_skinning(invalid); },"Reject inconsistent public root local cache");
    invalid=skin; invalid.joints[1].bind_local[12]+=1;
    rejects([&]{ evaluate_skinning(invalid); },"Reject inconsistent public child local cache");
    invalid=skin; invalid.palette_to_joint.push_back(99);
    rejects([&]{ evaluate_skinning(invalid); },"Reject unused invalid public palette entry");
    invalid=skin; invalid.joints[0].parent_joint=1;
    rejects([&]{ evaluate_skinning(invalid); },"Reject public joint parent cycle");
    invalid=skin; invalid.influences[0].joint_index=1;
    rejects([&]{ evaluate_skinning(invalid); },"Reject inconsistent public influence mapping");
    invalid=skin; invalid.source_positions[0].x=std::numeric_limits<float>::quiet_NaN();
    rejects([&]{ evaluate_skinning(invalid); },"Reject public non-finite source position");
    invalid=skin; invalid.influences.clear();
    rejects([&]{ evaluate_skinning(invalid); },"Reject public source/influence mismatch");
    invalid=skin; invalid.joints[0].inverse_bind[12]+=1;
    rejects([&]{ evaluate_skinning(invalid); },"Reject public non-cancelling inverse bind");
}

void absolute_bone_poses() {
    const auto skin = Fixture{}.decode();
    std::vector<SkinMatrix> locals{identity(),skin.joints[0].bind_world,identity(),skin.joints[1].bind_local};
    const auto reset = evaluate_bone_transforms(skin,locals);
    check(near(reset.positions[0],{2,0,0}) && near(reset.positions[1],{4,1,0}) && near(reset.positions[2],{1,1,0}),
        "Complete explicit locals reconstruct the synthetic bind reference");
    // The root helper has no joint, but its rotation and translation carry both joints.
    locals[0][0] = locals[0][5] = 0; locals[0][1] = 1; locals[0][4] = -1; locals[0][12] = 2;
    const auto root = evaluate_bone_transforms(skin,locals);
    check(near(root.positions[0],{2,2,0}) && near(root.positions[1],{1,4,0}),"Absolute root helper moves all weighted branches");
    check(near(root.joint_positions[0],{2,1,0}) && near(root.joint_positions[1],{2,3,0}),"Joint output remains joint ordinal order after helper motion");
    locals[0] = identity();
    // The intervening helper rotates X, while the child's bind-local rotates Z.
    // Their noncommuting product is independently checked by concrete points.
    locals[2][5] = locals[2][10] = 0; locals[2][6] = 1; locals[2][9] = -1; locals[2][13] = 2;
    const auto helper = evaluate_bone_transforms(skin,locals);
    check(near(helper.positions[0],{2,0,0}) && near(helper.positions[1],{4,2,1}) && near(helper.positions[2],{1,2,1}),
        "Noncommuting intervening helper affects only its descendants in parent-times-local order");
    check(near(helper.joint_positions[1],{3,2,0}),"Helper translation precedes descendant local rotation");
    check(near(evaluate_skinning(skin).positions[1],{4,1,0}),"Full-bone pose leaves manual reset and original data intact");
    auto bad_locals = locals; bad_locals.pop_back();
    rejects([&]{ evaluate_bone_transforms(skin,bad_locals); },"Reject missing absolute bone local");
    bad_locals = locals; bad_locals.push_back(identity());
    rejects([&]{ evaluate_bone_transforms(skin,bad_locals); },"Reject excess absolute bone local");
    bad_locals = locals; bad_locals[2][0] = std::numeric_limits<double>::quiet_NaN();
    rejects([&]{ evaluate_bone_transforms(skin,bad_locals); },"Reject non-finite helper matrix");
    bad_locals = locals; bad_locals[0][3] = 1;
    rejects([&]{ evaluate_bone_transforms(skin,bad_locals); },"Reject projective root matrix");
    bad_locals = locals; bad_locals[0][12] = std::numeric_limits<double>::max();
    rejects([&]{ evaluate_bone_transforms(skin,bad_locals); },"Reject full-bone posed float overflow");
    auto invalid = skin; invalid.bones[0].parent = 3;
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject a cycle through joint-free helpers");
    invalid = skin; invalid.bones[2].parent = 99;
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject full-bone parent outside array");
    invalid = skin; invalid.bones[2].parent = -2;
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject invalid negative full-bone parent");
    invalid = skin; invalid.bones[2].joint_index = 0;
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject duplicate full-bone joint owner");
    invalid = skin; invalid.bones[3].joint_index = -1;
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject missing full-bone joint owner");
    invalid = skin; invalid.joints[1].bone_index = 2;
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject mismatched full-bone joint mapping");
    invalid = skin; invalid.joints[0].inverse_bind[12] += 1;
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject inconsistent full-bone inverse reference");
    invalid = skin; invalid.palette_to_joint.push_back(99);
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject unused invalid full-bone palette entry");
    invalid = skin; invalid.influences[0].weight = .5f;
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject unsupported full-bone weight");
    invalid = skin; invalid.influences[0].joint_index = 1;
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject inconsistent full-bone influence mapping");
    invalid = skin; invalid.source_positions[0].x = std::numeric_limits<float>::infinity();
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject non-finite full-bone source position");
    invalid = skin; invalid.influences.clear();
    rejects([&]{ evaluate_bone_transforms(invalid,locals); },"Reject mismatched full-bone vertex count");
    invalid = skin; invalid.bones.resize(1025); bad_locals.resize(1025,identity());
    rejects([&]{ evaluate_bone_transforms(invalid,bad_locals); },"Reject oversized full-bone hierarchy before traversal");
    // Full-bone evaluation intentionally uses its explicit local transforms,
    // rather than the helper-skipping caches used by manual inspection.
    invalid = skin; invalid.joints[0].bind_local[12] += 100;
    check(near(evaluate_bone_transforms(invalid,locals).positions[1],{4,2,1}),"Explicit locals do not reuse manual bind-local caches");

    const auto identity_transform = original_bone_transform({0,0,0,1},{2,3,4});
    check(identity_transform[0] == 1 && identity_transform[5] == 1 && identity_transform[10] == 1 &&
        identity_transform[12] == 2 && identity_transform[13] == 3 && identity_transform[14] == 4,
        "Original identity quaternion preserves absolute translation");
    const auto cyclic = original_bone_transform({.5f,.5f,.5f,.5f},{0,0,0});
    check(cyclic[0] == 0 && cyclic[1] == 0 && cyclic[2] == 1 && cyclic[4] == 1 && cyclic[5] == 0 &&
        cyclic[6] == 0 && cyclic[8] == 0 && cyclic[9] == 1 && cyclic[10] == 0,
        "Original XYZW matrix signs send X to Z, Y to X, Z to Y");
    const auto opposite = original_bone_transform({-.5f,-.5f,-.5f,-.5f},{0,0,0});
    check(cyclic == opposite,"Opposite quaternion signs preserve the original matrix");
    const auto nonunit = original_bone_transform({0,0,.5f,.5f},{0,0,0});
    check(nonunit[0] == .5 && nonunit[1] == -.5 && nonunit[4] == .5 && nonunit[5] == .5 && nonunit[10] == 1,
        "Original quaternion conversion does not silently normalize a non-unit sample");
    for (const auto value : {std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),std::numeric_limits<float>::max()})
        rejects([&]{ original_bone_transform({value,0,0,1},{0,0,0}); },"Reject invalid original quaternion component or output overflow");
    rejects([&]{ original_bone_transform({0,0,0,1},{std::numeric_limits<float>::infinity(),0,0}); },"Reject invalid original absolute translation");
}

void inspect_original(const IgbDocument& document, std::uint32_t geometry, bool required) {
    const auto info=inspect_igb_skinning(document,geometry);
    std::cout<<"geometry="<<geometry<<" supported="<<info.supported<<" diagnostic="<<info.diagnostic<<'\n';
    if (!info.supported) { check(!required,"Requested original skin must be supported"); return; }
    const auto skin=decode_igb_skinning(document,geometry); const auto zero=evaluate_skinning(skin);
    double error{};
    for (std::size_t i=0;i<zero.positions.size();++i) {
        const auto a=zero.positions[i],b=skin.source_positions[i];
        error=std::max(error,double(std::max({std::abs(a.x-b.x),std::abs(a.y-b.y),std::abs(a.z-b.z)})));
    }
    check(error<=1e-5,"Original inverse-bind zero-pose round trip");
    std::cout<<info.provenance<<"\nbones="<<skin.bones.size()<<" joints="<<skin.joints.size()<<" vertices="<<skin.source_positions.size()<<" reset_max_error="<<error<<'\n';
    for (std::size_t i=0;i<skin.bones.size();++i)
        std::cout<<"bone="<<i<<" name="<<skin.bones[i].name<<" parent="<<skin.bones[i].parent<<" joint="<<skin.bones[i].joint_index<<'\n';
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        synthetic(); absolute_bone_poses();
        if (args.size()==3 || args.size()==4) {
            const auto archive=assets::AfsArchive::open(args[1]);
            const auto document=read_igb_document(archive.read(std::stoul(args[2].string())));
            if (args.size()==4) inspect_original(document,static_cast<std::uint32_t>(std::stoul(args[3].string())),true);
            else for (const auto& mesh:list_igb_meshes(document)) inspect_original(document,mesh.reference_index,false);
        } else if (args.size()!=1) throw std::runtime_error("Usage: dw4_skinning_tests [archive.afs entry [geometry-reference]]");
        std::cout<<checks<<" skinning checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<"Skinning test failure: "<<error.what()<<'\n'; return 1; }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::vector<std::filesystem::path> args; for (int i=0;i<argc;++i) args.emplace_back(argv[i]); return run(args);
}

