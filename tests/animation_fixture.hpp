#pragma once

// Asset-free IGB/Enbaya graph writer shared by animation parser/runtime tests.
#include "dw4/animations.hpp"
#include "dw4/igb_document.hpp"
#include <bit>
#include <map>

namespace animation_fixture {
using namespace dw4;
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t null_ref = 0xffffffffu;
inline void word(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i=0; i<4; ++i) bytes.at(at+i)=static_cast<std::uint8_t>(value >> (8*i));
}
inline void push32(Bytes& bytes, std::uint32_t value) { const auto at=bytes.size(); bytes.resize(at+4); word(bytes,at,value); }
inline void push16(Bytes& bytes, std::uint16_t value) { bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value>>8)); }
inline void append(Bytes& bytes, const Bytes& source) { bytes.insert(bytes.end(),source.begin(),source.end()); }
inline Bytes words(std::initializer_list<std::uint32_t> values) { Bytes bytes; for (auto value:values) push32(bytes,value); return bytes; }
inline Bytes floats(std::initializer_list<float> values) { Bytes bytes; for (auto value:values) push32(bytes,std::bit_cast<std::uint32_t>(value)); return bytes; }
inline void name(Bytes& bytes, const std::string& value) { bytes.insert(bytes.end(),value.begin(),value.end()); bytes.push_back(0); }
inline void align(Bytes& bytes) { bytes.resize((bytes.size()+3)&~std::size_t(3)); }

enum Type : std::uint32_t { object, object_dir, memory_dir, object_list, skin_list, skeleton_list,
    bone_list, animation_list, binding_list, track_list, database_type, skin_type, skeleton_type,
    bone_type, animation_type, binding_type, track_type, transform_type, source_type, mask_type,
    transition_list, combiner_type, int_list, state_list, combiner_lists, combiner_tracks,
    action_info, action_ram, action_node, action_data, action_data_list, named_list,
    action_ram_list, action_combiner_list, action_motion, action_joint, action_fade, action_wait, action_flash };
struct Schema { std::string name; std::uint32_t parent, slots; std::vector<IgbObjectField> fields; };
const std::vector<Schema> schemas{
    {"igObject",null_ref,2,{}},
    {"igObjectDirEntry",object,13,{{0,2,4},{1,11,4},{1,12,4}}},
    {"igMemoryDirEntry",object,14,{{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
    {"igObjectList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkinList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkeletonList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkeletonBoneInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationBindingList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationTrackList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationDatabase",object,10,{{2,5,4},{2,6,4},{2,7,4},{2,9,4}}},
    {"igSkin",object,2,{}},
    {"igSkeleton",object,7,{{3,3,4},{2,4,4},{3,5,4},{1,6,4}}},
    {"igSkeletonBoneInfo",object,5,{{0,2,4},{1,3,4},{1,4,4}}},
    {"igAnimation",object,11,{{0,2,4},{1,3,4},{2,4,4},{2,5,4},{2,6,4},{7,7,8},{7,8,8},{7,9,8},{2,10,4}}},
    {"igAnimationBinding",object,7,{{2,2,4},{3,3,4},{1,4,4},{2,5,4},{2,6,4}}},
    {"igAnimationTrack",object,4,{{0,2,4},{2,3,4}}},
    {"igEnbayaTransformSource",object,4,{{1,2,4},{2,3,4}}},
    {"igEnbayaAnimationSource",object,6,{{3,2,4},{4,3,3},{4,4,1},{6,5,4}}},
    {"igBitMask",object,6,{{1,2,4},{1,3,4},{3,4,4},{5,5,4}}},
    {"igAnimationTransitionDefinitionList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationCombiner",object,15,{{2,3,4},{2,4,4},{2,5,4},{2,6,4},{3,8,4},{7,9,8},{4,10,1},{4,11,1},{3,12,4},{3,13,4},{7,14,8}}},
    {"igIntList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationStateList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationCombinerBoneInfoListList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationCombinerBoneInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beAction2Info",object,10,{{0,2,4},{2,6,4},{2,8,4},{2,9,4}}},
    {"beAction2InfoRam",object,15,{{2,10,4},{2,11,4},{2,14,4}}},
    {"beModelCtrlNode2",object,15,{{8,13,4},{7,14,8}}},
    {"beAction2Data",object,4,{{0,2,4},{2,3,4}}},
    {"beAction2DataList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igNamedObjectList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beBaseInfoRamList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAnimationCombinerList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beAction2MOTIONSET",object,6,{{0,2,4},{0,3,4},{8,4,4},{0,5,4}}},
    {"beAction2JOINTSET",object,6,{{0,2,4},{8,3,4},{8,4,4},{0,5,4}}},
    {"beAction2FADESET",object,5,{{0,2,4},{8,3,4},{8,4,4}}},
    {"beAction2WAITCHECK",object,4,{{0,2,4},{8,3,4}}},
    {"beAction2FLASHSET",object,3,{{0,2,4}}}
};
// These synthetic records share only the test writer with the skinning tests.
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
            "igMemoryRefMetaField","igBoolMetaField","igUnsignedIntMetaField","igEnumMetaField","igLongMetaField","igFloatMetaField"};
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



inline Bytes constant_stream() {
    Bytes bytes(80); word(bytes,0,0x002abf84); word(bytes,4,2);
    word(bytes,8,std::bit_cast<std::uint32_t>(0.5f)); word(bytes,12,std::bit_cast<std::uint32_t>(0.1f)); word(bytes,16,30);
    const std::uint32_t lengths[]{4,2,0,0,0,0,0,0,0,1,1,0,0,2};
    for (std::size_t i=0;i<14;++i) word(bytes,20+4*i,lengths[i]);
    append(bytes,Bytes{1,0,4,0,2,2,0x40,100,0,0}); return bytes;
}
struct Fixture : Graph {
    std::uint32_t database, skeleton, skin, clip, binding, source, compressed, offsets, mask, mask_memory, mapping;
    std::uint32_t clips_list, skeletons_list, bindings_list, tracks_list, bones_list;
    std::vector<std::uint32_t> bones, tracks, transforms;
    SkinningData data;
    Fixture() {
        database=add(database_type); skeleton=add(skeleton_type); skin=add(skin_type);
        for (int i=0;i<2;++i) {
            const auto bone=add(bone_type); text(bone,2,"Bone "+std::to_string(i));
            scalar(bone,3,i ? 0 : null_ref); scalar(bone,4,i ? 0 : null_ref); bones.push_back(bone);
            data.bones.push_back({bone,"Bone "+std::to_string(i),i ? 0 : -1,i ? 0 : -1});
        }
        bones_list=list(bone_list,bones); scalar(skeleton,4,bones_list);
        scalar(skeleton,5,memory(floats({1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}))); scalar(skeleton,6,1);
        offsets=memory(floats({10,0,0,0,2,0})); scalar(skeleton,3,offsets);
        scalar(database,7,list(skin_list,{skin})); skeletons_list=list(skeleton_list,{skeleton}); scalar(database,5,skeletons_list);
        data.info.supported=true; data.info.skin_reference=skin; data.info.database_reference=database; data.info.skeleton_reference=skeleton;
        clip=add(animation_type); text(clip,2,"Synthetic clip");
        records[clip].values[9]=words({100000001,0});
        binding=add(binding_type); scalar(binding,2,skeleton); scalar(binding,4,2);
        mapping=memory(words({0,1})); scalar(binding,3,mapping);
        bindings_list=list(binding_list,{binding}); scalar(clip,4,bindings_list);
        source=add(source_type); compressed=memory(constant_stream()); scalar(source,2,compressed);
        records[source].values[3]=Bytes{1,3,1}; scalar(source,4,3); scalar(source,5,0);
        for (int i=0;i<2;++i) {
            const auto transform=add(transform_type); scalar(transform,2,i); scalar(transform,3,source); transforms.push_back(transform);
            const auto track=add(track_type); text(track,2,"Bone "+std::to_string(i)); scalar(track,3,transform); tracks.push_back(track);
        }
        tracks_list=list(track_list,tracks); scalar(clip,5,tracks_list);
        mask=add(mask_type); scalar(mask,2,1); scalar(mask,3,1); scalar(mask,5,2);
        mask_memory=memory(words({2})); scalar(mask,4,mask_memory); scalar(clip,10,mask);
        clips_list=list(animation_list,{clip}); scalar(database,6,clips_list);
    }
};

} // namespace animation_fixture
