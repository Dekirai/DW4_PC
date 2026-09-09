#pragma once

#include "animation_fixture.hpp"
#include "dw4/actor_skin.hpp"

namespace actor_skin_fixture {
using namespace dw4;
using animation_fixture::Bytes;
using animation_fixture::word;
using animation_fixture::words;
using animation_fixture::floats;
using animation_fixture::push16;
using animation_fixture::push32;
using animation_fixture::append;
using animation_fixture::name;
using animation_fixture::align;
using animation_fixture::null_ref;
inline constexpr const char* source_name="synthetic/actor-skin.igb";
inline AnimationMatrix identity() { return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; }
inline AnimationMatrix translated(float x,float y=0,float z=0) { auto result=identity(); result[12]=x; result[13]=y; result[14]=z; return result; }
inline Bytes matrix_bytes(const AnimationMatrix& matrix) {
    Bytes result; for (const auto value:matrix) push32(result,std::bit_cast<std::uint32_t>(value)); return result;
}
enum Type : std::uint32_t {
    object, object_dir, memory_dir, attr, geometry_attr, vertex_array, object_list,
    attr_list, node_list, skin_list, skeleton_list, bone_list, node, group,
    attr_set, geometry_node, selector_type, blend_state, skin_type, database_type,
    skeleton_type, bone_type, actor_type, appearance_type, int_list, bone_select_list, string_list, actor_list, ram_type, combiner_type
};
struct Schema { std::string name; std::uint32_t parent, slots; std::vector<IgbObjectField> fields; };
inline const std::vector<Schema> schemas{
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
    {"igAppearance",object,8,{{2,3,4},{2,4,4},{2,5,4},{2,6,4},{2,7,4}}},
    {"igIntList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igModelViewMatrixBoneSelectList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igStringObjList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igActorList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beAction2InfoRam",object,14,{{2,13,4}}},
    {"igAnimationCombiner",object,4,{{2,3,4}}}
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
    std::uint32_t geometry,vertices,owner,selector,skin,database,skeleton,appearance,actor,ram,combiner;
    std::uint32_t palette,palette_memory,inverse_memory,actor_entries;
    std::vector<std::uint32_t> bones,extras;
    Fixture() {
        geometry=add(geometry_attr); vertices=add(vertex_array); scalar(geometry,4,vertices); scalar(geometry,6,3); scalar(geometry,7,1);
        scalar(vertices,3,3); scalar(vertices,6,0x10113);
        const auto positions=memory(floats({2,0,0,4,1,0,1,1,0})); Bytes table(80,0xff); word(table,0,positions);
        scalar(vertices,2,memory(table)); scalar(vertices,7,memory(floats({1,1,1}))); scalar(vertices,8,memory(Bytes{0,1,2}));
        owner=add(geometry_node); scalar(owner,8,list(attr_list,{geometry}));
        selector=add(selector_type); scalar(selector,7,list(node_list,{owner}));
        records[selector].values[11]=matrix_bytes(identity()); records[selector].values[12]=matrix_bytes(identity());
        const auto blend=add(blend_state); scalar(blend,4,1); scalar(selector,8,list(attr_list,{blend}));
        palette=add(int_list); palette_memory=memory(words({1,0,1})); scalar(palette,2,3); scalar(palette,3,3); scalar(palette,4,palette_memory);
        scalar(selector,10,palette);
        skin=add(skin_type); text(skin,2,"Main skin"); scalar(skin,3,selector);
        skeleton=add(skeleton_type); text(skeleton,2,"Skeleton"); scalar(skeleton,6,2);
        for (int i=0;i<3;++i) {
            const auto bone=add(bone_type); text(bone,2,"Bone "+std::to_string(i));
            scalar(bone,3,i ? std::uint32_t(i-1) : null_ref); scalar(bone,4,i ? std::uint32_t(2-i) : null_ref); bones.push_back(bone);
        }
        scalar(skeleton,4,list(bone_list,bones));
        auto inverse=matrix_bytes(translated(-3)); append(inverse,matrix_bytes(translated(0,-5)));
        inverse_memory=memory(inverse); scalar(skeleton,5,inverse_memory);
        database=add(database_type); scalar(database,7,list(skin_list,{skin})); scalar(database,5,list(skeleton_list,{skeleton}));
        appearance=add(appearance_type); scalar(appearance,3,skin);
        const std::array<std::uint32_t,4> types{skin_list,bone_select_list,string_list,node_list};
        for (std::uint16_t i=0;i<types.size();++i) { const auto extra=list(types[i],{}); extras.push_back(extra); scalar(appearance,std::uint16_t(4+i),extra); }
        actor=add(actor_type); scalar(actor,11,appearance); ram=add(ram_type); actor_entries=list(actor_list,{actor}); scalar(ram,13,actor_entries);
        combiner=add(combiner_type); scalar(combiner,3,skeleton);
    }
    SkinningData decoded_skin() const { return decode_igb_skinning(document(),geometry); }
    ActionActorGroup group(std::string source=source_name) const { return {std::move(source),ram,combiner,{{actor,appearance,3,2}}}; }
    AnimationSourceRuntime library(std::string source_name_value=source_name) const {
        // Three identity tracks with no deltas, encoded independently of the
        // geometry/palette graph. They are validated but never sampled by capture.
        std::array<Bytes,14> streams; streams[0].resize(6); streams[1]={1,1,1}; streams[9]={64}; streams[10]={100}; streams[13]={0,0,0};
        for (const auto component:{3u,10u,17u}) streams[0][component/4]|=std::uint8_t(1u<<(6-2*(component%4)));
        Bytes bytes(80); word(bytes,0,0x002abf84); word(bytes,4,3); word(bytes,8,std::bit_cast<std::uint32_t>(1.f));
        word(bytes,12,std::bit_cast<std::uint32_t>(1.f)); word(bytes,16,1);
        for (std::size_t i=0;i<streams.size();++i) word(bytes,20+4*i,static_cast<std::uint32_t>(streams[i].size()));
        for (const auto i:{3,8,12,2,7,11,0,1,4,5,6,9,10,13}) append(bytes,streams[i]);
        AnimationClip clip; clip.info.supported=true; clip.info.reference_index=900; clip.info.name="Identity";
        clip.info.duration_seconds=1; clip.info.duration_ticks=1000000000; clip.info.track_count=3;
        clip.info.source_reference=901; clip.info.skeleton_reference=skeleton; clip.channels=decode_enbaya(bytes);
        clip.bone_references=bones; clip.constant_translations.resize(3); clip.use_sample_translation.resize(3);
        return {std::move(source_name_value),{std::move(clip)}};
    }
    AnimationCombiner caches(const AnimationSourceRuntime& sources,std::optional<std::uint32_t> identity_ref) const {
        return {sources,std::vector<AnimationMatrix>(3,identity()),decode_animation_hierarchy(document(),skeleton),
            {translated(10),translated(20,21),translated(30,31)},{translated(100),translated(200)},identity_ref};
    }
};
} // namespace actor_skin_fixture
