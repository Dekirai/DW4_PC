#pragma once

// Generated combined MODELCTRL/ACTION/skin graph; no original game bytes.
#include "dw4/player_runtime_binding.hpp"
#include "dw4/player_visual.hpp"
#include <bit>
#include <map>

namespace player_runtime_fixture {
using namespace dw4;
using Bytes=std::vector<std::uint8_t>;
constexpr std::uint32_t null_ref=0xffffffffu;
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
    skeleton_type, bone_type, actor_type, appearance_type, palette_type, info_type, info_list, model_type, ram_type, ram_list, base_data_type, one_type, data_list, model_node, lod_type, modifiers_list, matrix_bone_list, string_list, unknown_node, sequence_type, sequence_data_type, named_list, action_info, action_ram, action_data_list, animation_type, animation_list, action_combiner_list, combiner_type, actor_list, appearance_list
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
    {"igAnimationDatabase",object,10,{{2,5,4},{2,6,4},{2,7,4},{2,9,4}}},
    {"igSkeleton",object,7,{{0,2,4},{2,4,4},{3,5,4},{1,6,4}}},
    {"igSkeletonBoneInfo",object,6,{{0,2,4},{1,3,4},{1,4,4},{1,5,4}}},
    {"igActor",group,15,{{0,2,4},{1,5,4},{2,7,4},{2,8,4},{3,9,4},{3,10,4},{2,11,4},{2,12,4},{2,13,4},{7,14,64}}},
    {"igAppearance",object,8,{{2,3,4},{2,4,4},{2,5,4},{2,6,4},{2,7,4}}},
    {"igIntList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igInfo",object,3,{{0,2,4}}},
    {"igInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beModelCtrlInfo",info_type,8,{{0,2,4},{2,5,4},{2,6,4},{0,7,4}}},
    {"beBaseInfoRam",object,11,{{2,10,4}}},
    {"beBaseInfoRamList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beBaseInfoData",object,4,{{0,2,4},{1,3,4}}},
    {"beModelCtrlInfoRamData",base_data_type,6,{{0,2,4},{1,3,4},{2,4,4},{2,5,4}}},
    {"beBaseInfoDataList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beModelCtrlNode2",group,16,{{0,2,4},{1,5,4},{2,7,4},{7,8,64},{1,9,4},{2,11,4},{8,13,4},{9,14,8},{2,15,4}}},
    {"igLod",group,15,{{0,2,4},{1,5,4},{2,7,4},{2,14,4}}},
    {"igAnimationModifierList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igModelViewMatrixBoneSelectList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igStringObjList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"UnknownNode",group,8,{{0,2,4},{2,7,4}}},
    {"beModelCtrlInfoDataSeq",base_data_type,5,{{0,2,4},{1,3,4},{2,4,4}}},
    {"beModelCtrlData",object,4,{{0,2,4},{2,3,4}}},
    {"igNamedObjectList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beAction2Info",info_type,10,{{0,2,4},{2,6,4},{2,8,4},{2,9,4}}},
    {"beAction2InfoRam",object,15,{{2,10,4},{2,11,4},{2,12,4},{2,13,4},{2,14,4}}},
    {"beAction2DataList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimation",object,3,{{0,2,4}}},
    {"igAnimationList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationCombinerList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationCombiner",object,4,{{2,3,4}}},
    {"igActorList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAppearanceList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}}
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
            "igMemoryRefMetaField","igBoolMetaField","igUnsignedIntMetaField","igEnumMetaField","igMatrix44fMetaField","igFloatMetaField","igLongMetaField"};
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

struct RuntimeFixture : Fixture {
    std::uint32_t action,sequence,model_data,action_instances,combiner_instances;
    std::vector<std::uint32_t> model_rams,ones,nodes,lods,branch_lists,action_rams,combiners,actor_lists,appearance_lists;
    std::vector<std::vector<std::uint32_t>> ordered_actors;
    RuntimeFixture() {
        // Three LOD actors share immutable skin data but own distinct appearances.
        const auto alternate_appearance=add(appearance_type); records[alternate_appearance]=records[appearance];
        scalar(alternate_actor,11,alternate_appearance);
        const auto third=add(actor_type); records[third]=records[actor];
        const auto third_appearance=add(appearance_type); records[third_appearance]=records[appearance]; scalar(third,11,third_appearance);
        contents(branches,{actor,alternate_actor,third});
        model_rams.push_back(ram); ones.push_back(one); nodes.push_back(modelnode); lods.push_back(lod); branch_lists.push_back(branches);
        const auto second_ram=add(ram_type),second_one=add(one_type),second_node=add(model_node),second_lod=add(lod_type);
        records[second_one]=records[one]; records[second_node]=records[modelnode]; records[second_lod]=records[lod];
        scalar(second_one,4,second_node); scalar(second_ram,10,list(data_list,{second_one}));
        scalar(second_node,7,list(node_list,{second_lod}));
        std::vector<std::uint32_t> second_actors;
        for (const auto original : std::vector<std::uint32_t>{actor,alternate_actor,third}) {
            const auto next=add(actor_type),next_appearance=add(appearance_type);
            records[next]=records[original]; records[next_appearance]=records[appearance]; scalar(next,11,next_appearance); second_actors.push_back(next);
        }
        const auto second_branches=list(node_list,second_actors); scalar(second_lod,7,second_branches);
        model_rams.push_back(second_ram); ones.push_back(second_one); nodes.push_back(second_node); lods.push_back(second_lod); branch_lists.push_back(second_branches);
        contents(instances,model_rams);
        sequence=add(sequence_type); text(sequence,2,"SEQ");
        const auto wait=add(sequence_data_type); text(wait,2,"SQ_WAIT"); scalar(wait,3,list(named_list,{})); scalar(sequence,4,list(named_list,{wait}));
        model_data=list(data_list,{sequence}); scalar(model,5,model_data);
        action=add(action_info); text(action,2,"Synthetic player"); scalar(action,8,database); scalar(action,9,list(action_data_list,{}));
        const auto animation=add(animation_type); text(animation,2,"waitS"); scalar(database,6,list(animation_list,{animation}));
        // ACTION order intentionally differs from the LOD's branch order.
        ordered_actors={{alternate_actor,third,actor},{second_actors[2],second_actors[0],second_actors[1]}};
        for (std::size_t i=0;i<model_rams.size();++i) {
            records[nodes[i]].values[13]=floats({1});
            const auto next_ram=add(action_ram),combiner=add(combiner_type); scalar(next_ram,11,nodes[i]); scalar(combiner,3,skeleton);
            action_rams.push_back(next_ram); combiners.push_back(combiner);
            const auto actors_list=list(actor_list,ordered_actors[i]); scalar(next_ram,13,actors_list); actor_lists.push_back(actors_list);
            std::vector<std::uint32_t> appearances;
            for (const auto selected:ordered_actors[i]) {
                const auto& bytes=records[selected].values[11];
                appearances.push_back(std::uint32_t(bytes[0]) | std::uint32_t(bytes[1])<<8 | std::uint32_t(bytes[2])<<16 | std::uint32_t(bytes[3])<<24);
            }
            const auto appearances_list=list(appearance_list,appearances); scalar(next_ram,12,appearances_list); appearance_lists.push_back(appearances_list);
        }
        action_instances=list(ram_list,action_rams); scalar(action,6,action_instances);
        combiner_instances=list(action_combiner_list,combiners); scalar(database,9,combiner_instances);
        contents(info_reference,{model,action});
    }
    PlayerRuntimeRequest runtime_request(std::uint32_t instance=0,std::uint32_t branch=1) const { return {model,action,instance,branch}; }
    PlayerRuntimeBinding binding(std::size_t occurrence=3) const {
        const auto doc=document(); return decode_player_runtime_binding(doc,runtime_request(),"synthetic/player.igb",occurrence);
    }
};
} // namespace player_runtime_fixture
