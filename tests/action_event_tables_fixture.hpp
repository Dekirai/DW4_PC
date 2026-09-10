#pragma once

#include "animation_fixture.hpp"
#include "dw4/action_event_tables.hpp"

namespace action_event_fixture {
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
enum Type : std::uint32_t { object,object_dir,memory_dir,object_list,info_type,info_list,ram_type,ram_list,
    base_data_type,base_data_list,se_info,se_model,se_action,se_actions,starter_info,starter_model,
    starter_models,starter_action,starter_action2,starter_derived,starter_actions,derived_se_info };
struct Schema { std::string name; std::uint32_t parent,slots; std::vector<IgbObjectField> fields; };
inline const std::vector<Schema> schemas{
    {"igObject",null_ref,2,{}},
    {"igObjectDirEntry",object,13,{{0,2,4},{1,11,4},{1,12,4}}},
    {"igMemoryDirEntry",object,14,{{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
    {"igObjectList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igInfo",object,3,{{0,2,4}}},
    {"igInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beBaseInfoRam",object,11,{{1,2,4},{4,5,1},{1,6,4},{1,7,4},{1,8,4},{1,9,4},{2,10,4}}},
    {"beBaseInfoRamList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beBaseInfoData",object,4,{{0,2,4},{1,3,4}}},
    {"beBaseInfoDataList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beSeInfo",info_type,7,{{0,2,4},{4,4,1},{2,5,4},{2,6,4}}},
    {"beSeInfoData",base_data_type,5,{{0,2,4},{1,3,4},{2,4,4}}},
    {"beSeData",object,5,{{0,2,4},{0,3,4},{8,4,2}}},
    {"beSeDataList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beActionStarterInfo",info_type,8,{{0,2,4},{4,4,1},{2,5,4},{2,6,4},{2,7,4}}},
    {"beAsTargetModelData",object,4,{{0,2,4},{2,3,4}}},
    {"beAsTargetModelDataList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beActionStarterData",object,8,{{0,2,4},{0,3,4},{1,4,4},{9,5,12},{4,6,1},{4,7,1}}},
    {"beActionStarterData2",starter_action,9,{{0,2,4},{0,3,4},{1,4,4},{9,5,12},{4,6,1},{4,7,1},{9,8,12}}},
    {"SyntheticStarterData2Child",starter_action2,9,{{0,2,4},{0,3,4},{1,4,4},{9,5,12},{4,6,1},{4,7,1},{9,8,12}}},
    {"beActionStarterDataList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"SyntheticSeInfoChild",se_info,7,{{0,2,4},{4,4,1},{2,5,4},{2,6,4}}}
};

// Synthetic local references and inline strings; no original assets.
struct Graph {
    struct Record {
        bool memory{};
        std::uint32_t type{};
        std::map<std::uint16_t, Bytes> values;
        Bytes data;
    };
    std::vector<Record> records;
    std::uint32_t info_reference=null_ref;
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
        if (bytes.size() < 4) { for (std::size_t i=0;i<bytes.size();++i) bytes[i]=static_cast<std::uint8_t>(value>>(8*i)); }
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
            "igMemoryRefMetaField","igBoolMetaField","igUnsignedIntMetaField","igEnumMetaField","igMatrix44fMetaField","igShortMetaField","igVec3fMetaField"};
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




struct Fixture : Graph {
    std::uint32_t se,starter,rams,se_models,starter_model_list,se_last_actions,starter_first_actions;
    std::vector<std::uint32_t> ram_refs,se_rows,starter_rows,sounds,effects;
    Fixture() {
        ram_refs={add(ram_type),add(ram_type)}; rams=list(ram_list,{ram_refs[0],ram_refs[1],ram_refs[0]});
        se=add(se_info); text(se,2,"Synthetic sound consumer"); scalar(se,4,1); scalar(se,6,rams);
        starter=add(starter_info); scalar(starter,4,1); scalar(starter,6,rams); // Null info name is meaningful.
        for (std::size_t i=0;i<4;++i) {
            const auto sound=add(se_action); text(sound,2,"RUN"); scalar(sound,4,i==0 ? 0xffff8000u : i==1 ? 32767u : 6u);
            if (i==1) text(sound,3,""); else if (i>1) text(sound,3,i==2 ? "foot_lp1" : "ignored"); sounds.push_back(sound);
        }
        const auto null_key=add(se_action); text(null_key,3,"empty key"); scalar(null_key,4,1);
        for (std::size_t i=0;i<3;++i) {
            const auto row=add(se_model); if (i!=1) text(row,2,"p");
            const auto entries=i==0 ? std::vector<std::uint32_t>{sounds[3]} : i==1 ? std::vector<std::uint32_t>{null_key} :
                std::vector<std::uint32_t>{sounds[0],sounds[1],sounds[2],sounds[2]};
            const auto data=list(se_actions,entries); scalar(row,4,data); if(i==2)se_last_actions=data; se_rows.push_back(row);
        }
        se_models=list(base_data_list,se_rows); scalar(se,5,se_models);
        for (const auto type : {starter_action,starter_action2,starter_derived}) {
            const auto effect=add(type); text(effect,2,"RUN");
            if (type==starter_action2) text(effect,3,""); else if (type==starter_derived)text(effect,3,"SQ_ATCK");
            scalar(effect,4,type==starter_action ? 16777217u : type==starter_action2 ? 0xffffffffu : 0u);
            records[effect].values[5]=floats({1,2,3}); scalar(effect,6,1); scalar(effect,7,type==starter_action2 ? 1u : 0u);
            if(type!=starter_action) records[effect].values[8]=floats({4,5,6}); effects.push_back(effect);
        }
        const auto empty_effect=add(starter_action); // Null model/action DATA keys use empty strings.
        for (std::size_t i=0;i<3;++i) {
            const auto row=add(starter_model); if(i!=1)text(row,2,"p");
            const auto entries=i==0 ? std::vector<std::uint32_t>{effects[0],effects[1],effects[2],effects[0]} :
                i==1 ? std::vector<std::uint32_t>{empty_effect} : std::vector<std::uint32_t>{effects[1]};
            const auto data=list(starter_actions,entries); scalar(row,3,data); if(i==0)starter_first_actions=data; starter_rows.push_back(row);
        }
        starter_model_list=list(starter_models,starter_rows); scalar(starter,7,starter_model_list);
        info_reference=list(info_list,{se,starter,starter});
    }
    ActionEventTableProgram sound_program() const { return decode_action_event_tables(document(),se,"synthetic/events.igb",7); }
    ActionEventTableProgram effect_program() const { return decode_action_event_tables(document(),starter,"synthetic/events.igb",7); }
};
} // namespace action_event_fixture
