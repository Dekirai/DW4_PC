#pragma once

#include "animation_fixture.hpp"
#include "dw4/animation_hierarchy.hpp"

namespace hierarchy_fixture {
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

inline AnimationMatrix identity() {
    AnimationMatrix result{}; result[0]=result[5]=result[10]=result[15]=1; return result;
}
inline AnimationMatrix translation(float x,float y,float z) {
    auto result=identity(); result[12]=x; result[13]=y; result[14]=z; return result;
}

enum Type : std::uint32_t { object, object_dir, memory_dir, object_list, skeleton_type, bone_list, bone_type };
struct Schema { std::string name; std::uint32_t parent, slots; std::vector<IgbObjectField> fields; };
inline const std::vector<Schema> schemas{
    {"igObject",null_ref,2,{}},
    {"igObjectDirEntry",object,13,{{0,2,4},{1,11,4},{1,12,4}}},
    {"igMemoryDirEntry",object,14,{{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
    {"igObjectList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkeleton",object,7,{{2,4,4},{3,5,4},{1,6,4}}},
    {"igSkeletonBoneInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSkeletonBoneInfo",object,5,{{0,2,4},{1,3,4},{1,4,4}}}
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
    void text(std::uint32_t ref,std::uint16_t slot,const std::string& value) {
        auto bytes=words({static_cast<std::uint32_t>(value.size()+1)}); name(bytes,value); records.at(ref).values.at(slot)=std::move(bytes);
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
            "igMemoryRefMetaField","igBoolMetaField"};
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
    std::uint32_t skeleton,bones_list,inverse_memory;
    std::vector<std::uint32_t> bones;
    std::vector<AnimationMatrix> inverses;
    Fixture() {
        skeleton=add(skeleton_type);
        const std::int32_t parents[]{-1,0,1,2,0},joints[]{-1,2,-1,0,1};
        for (std::size_t i=0;i<5;++i) {
            const auto bone=add(bone_type); bones.push_back(bone); text(bone,2,"Bone "+std::to_string(i));
            scalar(bone,3,std::bit_cast<std::uint32_t>(parents[i])); scalar(bone,4,std::bit_cast<std::uint32_t>(joints[i]));
        }
        bones_list=list(bone_list,bones); scalar(skeleton,4,bones_list);
        inverses={translation(10,0,0),translation(0,0,7),translation(9,-1,0)}; inverses[1][0]=3;
        Bytes bytes; for (const auto& matrix:inverses) for (const auto element:matrix) push32(bytes,std::bit_cast<std::uint32_t>(element));
        inverse_memory=memory(std::move(bytes)); scalar(skeleton,5,inverse_memory); scalar(skeleton,6,3);
    }
    AnimationHierarchyDefinition definition() const { return decode_animation_hierarchy(document(),skeleton); }
};
} // namespace hierarchy_fixture
