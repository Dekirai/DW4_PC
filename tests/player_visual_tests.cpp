#include "dw4/player_visual.hpp"
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
    bool caught = false; try { operation(); } catch (const PlayerVisualError&) { caught = true; }
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
    skeleton_type, bone_type, actor_type, appearance_type, palette_type, info_type, info_list, model_type, ram_type, ram_list, one_type, data_list, model_node, lod_type, modifiers_list, matrix_bone_list, string_list, unknown_node
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
    {"igGeometry",attr_set,10,{{0,2,4},{1,5,4},{2,7,4},{2,8,4},{4,9,1}}},
    {"igBlendMatrixSelect",attr_set,13,{{0,2,4},{1,5,4},{2,7,4},{2,8,4},{4,9,1},{2,10,4},{7,11,64},{7,12,64}}},
    {"igVertexBlendStateAttr",attr,5,{{4,4,1}}},
    {"igSkin",object,4,{{0,2,4},{2,3,4}}},
    {"igAnimationDatabase",object,8,{{2,5,4},{2,7,4}}},
    {"igSkeleton",object,7,{{0,2,4},{2,4,4},{3,5,4},{1,6,4}}},
    {"igSkeletonBoneInfo",object,6,{{0,2,4},{1,3,4},{1,4,4},{1,5,4}}},
    {"igActor",group,15,{{0,2,4},{1,5,4},{2,7,4},{2,8,4},{3,9,4},{3,10,4},{2,11,4},{2,12,4},{2,13,4},{7,14,64}}},
    {"igAppearance",object,8,{{2,3,4},{2,4,4},{2,5,4},{2,6,4},{2,7,4}}},
    {"igIntList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igInfo",object,3,{{0,2,4}}},
    {"igInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beModelCtrlInfo",info_type,8,{{0,2,4},{2,6,4},{0,7,4}}},
    {"beBaseInfoRam",object,11,{{2,10,4}}},
    {"beBaseInfoRamList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beModelCtrlInfoRamData",object,6,{{0,2,4},{1,3,4},{2,4,4},{2,5,4}}},
    {"beBaseInfoDataList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beModelCtrlNode2",group,16,{{0,2,4},{1,5,4},{2,7,4},{7,8,64},{1,9,4},{2,11,4},{2,15,4}}},
    {"igLod",group,15,{{0,2,4},{1,5,4},{2,7,4},{2,14,4}}},
    {"igAnimationModifierList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igModelViewMatrixBoneSelectList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igStringObjList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"UnknownNode",group,8,{{0,2,4},{2,7,4}}}
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
    std::uint32_t info_reference{null_ref};
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
            static_cast<std::uint32_t>(field_names.size()),0xfada,0xa0000006});
        append(bytes,fields); push32(bytes,4); append(bytes,types); append(bytes,entries);
        append(bytes,words({8 + 2 * count,count}));
        for (std::uint32_t i = 0; i < count; ++i) push16(bytes,static_cast<std::uint16_t>(i));
        push32(bytes,info_reference); append(bytes,objects); append(bytes,memories); return read_igb_document(std::move(bytes));
    }
};




struct SkinFixture : Graph {
    std::uint32_t geom, vertices, owner, selector, skin, database, skeleton;
    std::uint32_t weights, indices, palette, palette_memory, inverse_matrices;
    std::uint32_t owner_attrs, selector_attrs, selector_children, state, database_skins, database_skeletons, bones_list;
    std::vector<std::uint32_t> bones;
    std::uint32_t actor, appearance;
    SkinFixture() {
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
        appearance=add(appearance_type); scalar(appearance,3,skin);
        actor=add(actor_type); scalar(actor,11,appearance);
    }
    std::uint32_t add_int_list() { return add(palette_type); }
    SkinningData decode() const { return decode_igb_skinning(document(),geom); }
};

struct Fixture : SkinFixture {
    std::uint32_t model, ram, one, modelnode, lod, branches, instances, data, alternate_actor;
    Fixture() {
        scalar(owner,7,list(node_list,{})); scalar(owner,9,1);
        scalar(actor,7,list(node_list,{})); scalar(actor,13,list(modifiers_list,{}));
        records[actor].values[14]=matrix_bytes(identity());
        scalar(appearance,4,list(skin_list,{})); scalar(appearance,5,list(matrix_bone_list,{}));
        scalar(appearance,6,list(string_list,{})); scalar(appearance,7,list(node_list,{}));
        alternate_actor=add(actor_type); records[alternate_actor]=records[actor];
        lod=add(lod_type); branches=list(node_list,{actor,alternate_actor}); scalar(lod,7,branches);
        modelnode=add(model_node); scalar(modelnode,7,list(node_list,{lod}));
        auto authored=identity(); authored[12]=123; records[modelnode].values[8]=matrix_bytes(authored);
        scalar(modelnode,15,list(attr_list,{}));
        one=add(one_type); text(one,2,"ONE"); scalar(one,4,modelnode);
        ram=add(ram_type); data=list(data_list,{one}); scalar(ram,10,data);
        model=add(model_type); text(model,2,"Synthetic player"); text(model,7,"KIND_PLAYER");
        instances=list(ram_list,{ram}); scalar(model,6,instances);
        info_reference=list(info_list,{model});
    }
    PlayerVisualRequest request() const { return {model,0,1}; }
};

void rejected(const Fixture& fixture, PlayerVisualRequest request, const std::string& reason) {
    const auto document=fixture.document();
    const auto info=inspect_igb_player_visual(document,request);
    check(!info.supported && !info.diagnostic.empty(),reason+" diagnostic");
    rejects([&] { decode_igb_player_visual(document,request); },reason);
}

void synthetic() {
    Fixture f;
    const auto models=list_igb_player_models(f.document());
    check(models.size()==1 && models[0].supported && models[0].reference_index==f.model && models[0].instance_count==1,
        "Enumerate explicit KIND_PLAYER instances");
    const auto visual=decode_igb_player_visual(f.document(),f.request());
    check(visual.info.supported && visual.info.model_node_reference==f.modelnode && visual.info.lod_reference==f.lod,
        "Preserve player instance node and LOD provenance");
    check(visual.info.actor_reference==f.alternate_actor && visual.info.appearance_reference==f.appearance,
        "Use explicit selected LOD branch");
    check(visual.info.skin_reference==f.skin && visual.info.geometry_reference==f.geom && visual.info.branch_count==2,
        "Follow appearance skin into its exact geometry");
    check(visual.mesh.positions.size()==3 && near(visual.mesh.positions[0],{2,0,0}), "Authored model transform leaked into source vertices");
    check(visual.skin.info.actor_references.size()==2 && visual.skin.info.skin_reference==f.skin && !visual.info.provenance.empty(),
        "Preserve existing skin and actor ownership validation");
    const auto other=decode_igb_player_visual(f.document(),{f.model,0,0});
    check(other.info.actor_reference==f.actor && other.info.geometry_reference==f.geom,"Alternative branch selected a guessed actor");
    rejected(f,{f.model,1,0},"Invalid instance index");
    rejected(f,{f.model,0,2},"Invalid branch index");
    rejected(f,{f.model,0,0xffffffffu},"Overflowing branch index");
    auto bad=f; bad.info_reference=null_ref;
    check(list_igb_player_models(bad.document()).empty(),"Missing exports caused a player object scan");
    rejected(bad,bad.request(),"Unexported player model");
    bad=f; bad.contents(bad.info_reference,{});
    rejected(bad,bad.request(),"Hidden model in otherwise valid document");
    bad=f; bad.contents(bad.info_reference,{bad.model,bad.model});
    rejects([&] { list_igb_player_models(bad.document()); },"Repeated model export accepted");
    rejected(bad,bad.request(),"Ambiguous model export");
    bad=f; bad.contents(bad.info_reference,{bad.actor});
    rejects([&] { list_igb_player_models(bad.document()); },"Non-info export accepted");
    bad=f; bad.contents(bad.info_reference,{null_ref});
    rejects([&] { list_igb_player_models(bad.document()); },"Null export accepted");
    bad=f; bad.text(bad.model,7,"KIND_ENEMY");
    check(list_igb_player_models(bad.document()).empty(),"Non-player model was listed");
    rejected(bad,bad.request(),"Non-player request");
    bad=f; bad.contents(bad.instances,{});
    const auto empty=list_igb_player_models(bad.document());
    check(empty.size()==1 && !empty[0].supported && !empty[0].diagnostic.empty(),"Missing instances have no diagnostic");
    rejected(bad,bad.request(),"Empty RAM list");
    bad=f; bad.contents(bad.instances,{bad.model});
    rejected(bad,bad.request(),"Wrong RAM member type");
    bad=f; bad.contents(bad.data,{bad.one,bad.one});
    rejected(bad,bad.request(),"Duplicate ONE record");
    bad=f; bad.contents(bad.data,{});
    rejected(bad,bad.request(),"Missing ONE record");
    bad=f; bad.text(bad.one,2,"OTHER");
    rejected(bad,bad.request(),"Wrong visual record label");
    bad=f; bad.scalar(bad.one,4,null_ref);
    rejected(bad,bad.request(),"Null ONE node reference");
    bad=f; bad.scalar(bad.one,5,bad.modelnode);
    rejected(bad,bad.request(),"Additional ONE node state");
    bad=f; bad.scalar(bad.modelnode,7,bad.branches);
    rejected(bad,bad.request(),"Multiple model-node children");
    bad=f; bad.scalar(bad.modelnode,7,bad.selector_children);
    rejected(bad,bad.request(),"Missing direct LOD");
    bad=f; bad.contents(bad.branches,{bad.actor,null_ref});
    rejected(bad,bad.request(),"Null branch child");
    bad=f; bad.contents(bad.branches,{bad.actor,bad.modelnode});
    rejected(bad,bad.request(),"Non-actor branch or cycle");
    bad=f; bad.scalar(bad.lod,14,bad.actor);
    rejected(bad,bad.request(),"Additional LOD node state");
    auto matrix=identity(); matrix[12]=1;
    bad=f; bad.records[bad.alternate_actor].values[14]=matrix_bytes(matrix);
    rejected(bad,bad.request(),"Nonidentity actor transform");
    matrix=identity(); matrix[0]=std::numeric_limits<double>::quiet_NaN();
    bad=f; bad.records[bad.modelnode].values[8]=matrix_bytes(matrix);
    rejected(bad,bad.request(),"Nonfinite authored model matrix");
    matrix=identity(); matrix[3]=1;
    bad=f; bad.records[bad.modelnode].values[8]=matrix_bytes(matrix);
    rejected(bad,bad.request(),"Nonaffine authored model matrix");
    bad=f; bad.scalar(bad.modelnode,9,1);
    rejected(bad,bad.request(),"Unsupported model transform mode");
    bad=f; bad.scalar(bad.modelnode,11,bad.actor);
    rejected(bad,bad.request(),"Model transform attachment");
    for (const auto slot : {8u,9u,10u,12u}) {
        bad=f; bad.scalar(bad.alternate_actor,static_cast<std::uint16_t>(slot),slot==9 || slot==10 ? bad.weights : bad.modelnode);
        rejected(bad,bad.request(),"Actor additional state slot "+std::to_string(slot));
    }
    bad=f; bad.scalar(bad.alternate_actor,7,bad.branches);
    rejected(bad,bad.request(),"Actor attachment children");
    bad=f; bad.scalar(bad.alternate_actor,13,bad.list(modifiers_list,{bad.actor}));
    rejected(bad,bad.request(),"Actor modifier list");
    for (const auto [slot,type] : {std::pair{4u,skin_list},std::pair{5u,matrix_bone_list},std::pair{6u,string_list},std::pair{7u,node_list}}) {
        bad=f; bad.scalar(bad.appearance,static_cast<std::uint16_t>(slot),bad.list(type,{bad.actor}));
        rejected(bad,bad.request(),"Appearance attachment slot "+std::to_string(slot));
    }
    bad=f; bad.scalar(bad.appearance,3,bad.skeleton);
    rejected(bad,bad.request(),"Appearance skin reference has wrong type");
    bad=f; bad.contents(bad.selector_children,{bad.owner,bad.owner});
    rejected(bad,bad.request(),"Multiple skin geometry children");
    bad=f; bad.contents(bad.owner_attrs,{bad.geom,bad.geom});
    rejected(bad,bad.request(),"Duplicate geometry attribute");
    bad=f; bad.contents(bad.owner_attrs,{bad.state});
    rejected(bad,bad.request(),"Missing geometry attribute");
    bad=f; bad.contents(bad.selector_attrs,{bad.actor});
    rejected(bad,bad.request(),"Unsupported selector state");
    bad=f; bad.contents(bad.selector_attrs,{bad.state,bad.geom});
    rejected(bad,bad.request(),"Additional selector-scope geometry");
    bad=f; bad.scalar(bad.owner,9,0);
    rejected(bad,bad.request(),"Disabled geometry trigger");
    for (const auto reference : {f.modelnode,f.lod,f.alternate_actor,f.selector,f.owner}) {
        bad=f; bad.scalar(reference,5,0x20);
        rejected(bad,bad.request(),"Invisible or dynamic node flags");
    }
    bad=f; bad.text(bad.model,2,std::string(8192,'\0'));
    rejected(bad,bad.request(),"Oversized serialized name hidden by NUL padding");
    bad=f; bad.scalar(bad.owner,7,bad.branches);
    rejected(bad,bad.request(),"Geometry children silently discarded");
    bad=f; bad.scalar(bad.instances,3,4097);
    rejected(bad,bad.request(),"Oversized instance capacity");
    bad=f; bad.scalar(bad.instances,3,0);
    rejected(bad,bad.request(),"List count greater than capacity");
    bad=f; bad.records[bad.weights].data=floats({1,0.5f,1});
    rejected(bad,bad.request(),"Invalid skin weights bypass binding validation");
    bad=f; const auto second_skin=bad.add(skin_type); bad.text(second_skin,2,"Conflicting skin"); bad.scalar(second_skin,3,bad.selector);
    rejected(bad,bad.request(),"Ambiguous global skin ownership");
    bad=f; bad.contents(bad.instances,std::vector<std::uint32_t>(4096,bad.ram));
    std::vector<std::uint32_t> many_models{bad.model};
    for (std::uint32_t i=1;i<1025;++i) { const auto model=bad.add(model_type); bad.records[model]=bad.records[bad.model]; many_models.push_back(model); }
    bad.contents(bad.info_reference,many_models);
    const auto many=list_igb_player_models(bad.document());
    check(many.size()==1025 && many[0].supported && !many.back().supported &&
        many.back().diagnostic.find("aggregate")!=std::string::npos,"Repeated-list expansion bypassed aggregate work bound");
}

void original(const std::filesystem::path& root) {
    const auto archive=assets::AfsArchive::open(root/"files"/"ndmw.afs");
    const auto doc=read_igb_document(archive.read(850));
    const auto models=list_igb_player_models(doc);
    check(models.size()==1 && models[0].reference_index==1386 && models[0].instance_count==4 && models[0].supported,
        "Original Agumon player export changed");
    for (std::uint32_t branch=0;branch<3;++branch) {
        const auto visual=decode_igb_player_visual(doc,{1386,0,branch});
        check(visual.info.branch_count==3 && visual.info.model_node_reference==6390 && visual.info.lod_reference==6393,
            "Original player visual root chain changed");
        check(visual.info.geometry_reference==(branch==1 ? 5665u : branch==2 ? 262u : 413u),
            "Original explicit appearance selected wrong geometry");
        check(visual.mesh.positions.size()==visual.skin.source_positions.size(),"Original source/skin vertex count mismatch");
        if (branch==1) check(visual.info.actor_reference==6443 && visual.info.appearance_reference==6444 && visual.info.skin_reference==404,
            "Original high-appearance provenance changed");
    }
    const auto fourth=decode_igb_player_visual(doc,{1386,3,1});
    check(fourth.info.model_node_reference==6590 && fourth.info.geometry_reference==5665,
        "Original fourth player instance selected wrong visual");
    std::cout<<"Original Agumon: 4 instances, 3 explicit appearance branches, selected high geometry 5665\n";
}
} // namespace

int main(int argc,char**argv) {
    try {
        synthetic();
        if(argc==2) original(std::filesystem::path(argv[1]));
        else if(argc!=1) throw std::runtime_error("Usage: dw4_player_visual_tests [extracted-data-root]");
        std::cout<<"Player visual tests passed: "<<checks<<" checks\n"; return 0;
    } catch(const std::exception&error) { std::cerr<<"Player visual test failure: "<<error.what()<<'\n'; return 1; }
}
