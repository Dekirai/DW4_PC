#pragma once

#include "animation_fixture.hpp"
#include "dw4/action_actors.hpp"

namespace action_actor_fixture {
using animation_fixture::Bytes;
using animation_fixture::word;
using animation_fixture::words;
using animation_fixture::push16;
using animation_fixture::push32;
using animation_fixture::append;
using animation_fixture::name;
using animation_fixture::align;
using animation_fixture::null_ref;
using namespace dw4;
enum Type : std::uint32_t { object, object_dir, memory_dir, object_list, ram_type, node_type,
    actor_list, appearance_list, actor_type, appearance_type, modifier_list, attribute_list,
    skin_type, combiner_type, skeleton_type, bone_list, bone_type };
struct Schema { std::string name; std::uint32_t parent, slots; std::vector<IgbObjectField> fields; };
inline const std::vector<Schema> schemas{
    {"igObject",null_ref,2,{}},
    {"igObjectDirEntry",object,13,{{0,2,4},{1,11,4},{1,12,4}}},
    {"igMemoryDirEntry",object,14,{{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
    {"igObjectList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beAction2InfoRam",object,14,{{2,11,4},{2,12,4},{2,13,4}}},
    {"beModelCtrlNode2",object,16,{{2,15,4}}},
    {"igActorList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAppearanceList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igActor",object,21,{{2,8,4},{3,9,4},{3,10,4},{2,11,4},{2,12,4},{2,13,4},{5,14,64},
        {4,15,1},{4,16,1},{4,19,1},{4,20,1}}},
    {"igAppearance",object,4,{{2,3,4}}},
    {"igAnimationModifierList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAttrList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkin",object,2,{}},
    {"igAnimationCombiner",object,4,{{2,3,4}}},
    {"igSkeleton",object,7,{{2,4,4},{1,6,4}}},
    {"igSkeletonBoneInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkeletonBoneInfo",object,5,{{1,4,4}}}
};

struct Graph {
    struct Record { bool memory{}; std::uint32_t type{}; std::map<std::uint16_t,Bytes> values; Bytes data; };
    std::vector<Record> records;
    std::uint32_t add(Type type) {
        Record value; value.type=type;
        for (const auto& field:schemas.at(type).fields) {
            value.values[field.slot]=Bytes(field.size);
            if (field.type_index==2 || field.type_index==3) word(value.values[field.slot],0,null_ref);
        }
        const auto ref=static_cast<std::uint32_t>(records.size()); records.push_back(std::move(value)); return ref;
    }
    std::uint32_t memory(Bytes bytes) {
        Record value; value.memory=true; value.data=std::move(bytes);
        const auto ref=static_cast<std::uint32_t>(records.size()); records.push_back(std::move(value)); return ref;
    }
    void scalar(std::uint32_t ref,std::uint16_t slot,std::uint32_t value) {
        auto& bytes=records.at(ref).values.at(slot);
        if (bytes.size()==1) bytes[0]=static_cast<std::uint8_t>(value); else word(bytes,0,value);
    }
    void contents(std::uint32_t ref,const std::vector<std::uint32_t>& values) {
        Bytes bytes; for (const auto value:values) push32(bytes,value);
        const auto data=memory(std::move(bytes));
        scalar(ref,2,static_cast<std::uint32_t>(values.size())); scalar(ref,3,static_cast<std::uint32_t>(values.size())); scalar(ref,4,data);
    }
    std::uint32_t list(Type type,const std::vector<std::uint32_t>& values) {
        const auto ref=add(type); contents(ref,values); return ref;
    }
    IgbDocument document() const {
        const std::vector<std::string> names{"igStringMetaField","igIntMetaField","igObjectRefMetaField",
            "igMemoryRefMetaField","igBoolMetaField","igMatrix44fMetaField"};
        Bytes fields(names.size()*12);
        for (std::size_t i=0;i<names.size();++i) {
            word(fields,i*12,static_cast<std::uint32_t>(names[i].size()+1)); word(fields,i*12+4,1); name(fields,names[i]);
        }
        Bytes types(schemas.size()*24);
        for (std::size_t i=0;i<schemas.size();++i) {
            const auto& schema=schemas[i];
            word(types,i*24,static_cast<std::uint32_t>(schema.name.size()+1)); word(types,i*24+4,1);
            word(types,i*24+12,static_cast<std::uint32_t>(schema.fields.size()));
            word(types,i*24+16,schema.parent); word(types,i*24+20,schema.slots); name(types,schema.name);
            for (const auto& field:schema.fields) { push16(types,field.type_index); push16(types,field.slot); push16(types,field.size); }
        }
        Bytes entries,objects,memories; std::uint32_t object_count=0,memory_count=0;
        for (const auto& record:records) {
            if (record.memory) {
                append(entries,words({memory_dir,32,0,static_cast<std::uint32_t>(record.data.size()),null_ref,0,null_ref,null_ref}));
                append(memories,record.data); align(memories); ++memory_count;
            } else {
                append(entries,words({object_dir,20,0,record.type,null_ref}));
                auto bytes=words({record.type,0});
                for (const auto& field:schemas.at(record.type).fields) { append(bytes,record.values.at(field.slot)); align(bytes); }
                word(bytes,4,static_cast<std::uint32_t>(bytes.size())); append(objects,bytes); ++object_count;
            }
        }
        const auto count=static_cast<std::uint32_t>(records.size());
        auto bytes=words({static_cast<std::uint32_t>(entries.size()),count,static_cast<std::uint32_t>(types.size()),
            static_cast<std::uint32_t>(schemas.size()),static_cast<std::uint32_t>(objects.size()),object_count,
            static_cast<std::uint32_t>(memories.size()),memory_count,static_cast<std::uint32_t>(fields.size()),
            static_cast<std::uint32_t>(names.size()),0xfada,0x20000006});
        append(bytes,fields); push32(bytes,4); append(bytes,types); append(bytes,entries); append(bytes,words({8+2*count,count}));
        for (std::uint32_t ref=0;ref<count;++ref) push16(bytes,static_cast<std::uint16_t>(ref));
        append(bytes,objects); append(bytes,memories); return read_igb_document(std::move(bytes));
    }
};

struct Fixture : Graph {
    std::uint32_t ram,node,combiner,skeleton,bones_list,actors_list,appearances_list,attributes;
    std::vector<std::uint32_t> bones,actors,appearances,modifiers;
    Fixture() {
        ram=add(ram_type); node=add(node_type); combiner=add(combiner_type); skeleton=add(skeleton_type);
        scalar(ram,11,node); scalar(combiner,3,skeleton); scalar(skeleton,6,1);
        attributes=list(attribute_list,{}); scalar(node,15,attributes);
        for (std::uint32_t i=0;i<3;++i) { const auto bone=add(bone_type); scalar(bone,4,i==2 ? 0 : null_ref); bones.push_back(bone); }
        bones_list=list(bone_list,bones); scalar(skeleton,4,bones_list);
        for (int i=0;i<3;++i) {
            const auto actor=add(actor_type),appearance=add(appearance_type),skin=add(skin_type);
            const auto modifier=list(modifier_list,{}); modifiers.push_back(modifier);
            scalar(actor,11,appearance); scalar(actor,13,modifier); scalar(appearance,3,skin);
            auto& matrix=records[actor].values[14]; for (std::size_t at=0;at<16;++at) word(matrix,at*4,std::bit_cast<std::uint32_t>(at%5==0 ? 1.0f : 0.0f));
            scalar(actor,19,1); scalar(actor,20,1); actors.push_back(actor); appearances.push_back(appearance);
        }
        actors_list=list(actor_list,actors); appearances_list=list(appearance_list,appearances);
        scalar(ram,12,appearances_list); scalar(ram,13,actors_list);
    }
    ActionActorGroup group() const { return decode_action_actor_group(document(),ram,combiner,"synthetic/action-actors.igb"); }
};
} // namespace action_actor_fixture
