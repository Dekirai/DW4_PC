#pragma once

#include "dw4/igb_document.hpp"

#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Synthetic serialized IGB graphs only; this fixture contains no game bytes.
namespace dw4::test {
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::uint32_t null_ref = 0xffffffffu;
inline void word(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (i * 8));
}
inline void push32(Bytes& bytes, std::uint32_t value) {
    const auto at = bytes.size(); bytes.resize(at + 4); word(bytes, at, value);
}
inline void push16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
inline void append(Bytes& bytes, const Bytes& source) { bytes.insert(bytes.end(), source.begin(), source.end()); }
inline Bytes words(std::initializer_list<std::uint32_t> values) {
    Bytes result; for (auto value : values) push32(result, value); return result;
}
inline void name(Bytes& bytes, const std::string& text) {
    bytes.insert(bytes.end(), text.begin(), text.end()); bytes.push_back(0);
}
inline void align(Bytes& bytes) { bytes.resize((bytes.size() + 3) & ~std::size_t(3)); }

enum Type : std::uint32_t {
    object, object_dir, memory_dir, object_list, named_object, info_base, info_list,
    lua_info, lua_list, lua_data, lua_object, derived_info, derived_data, derived_object,
    node, camera, camera_list, scene_info, derived_scene,
    file_info, named_list, test_controller
};
struct Schema { std::string name; std::uint32_t parent, slots; std::vector<IgbObjectField> fields; };
inline const std::vector<Schema> schemas{
    {"igObject",null_ref,2,{}},
    {"igObjectDirEntry",object,13,{{0,2,4},{1,11,4},{1,12,4}}},
    {"igMemoryDirEntry",object,14,{{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
    {"igObjectList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igNamedObject",object,3,{{0,2,4}}},
    {"igInfo",named_object,3,{{0,2,4}}},
    {"igInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beLuaInfo",info_base,8,{{0,2,4},{2,7,4}}},
    {"beLuaDataList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beLuaData",named_object,4,{{0,2,4},{2,3,4}}},
    {"beLuaDataObject",object,4,{{1,2,4},{3,3,4}}},
    {"DerivedLuaInfo",lua_info,8,{{0,2,4},{2,7,4}}},
    {"DerivedLuaData",lua_data,4,{{0,2,4},{2,3,4}}},
    {"DerivedLuaDataObject",lua_object,4,{{1,2,4},{3,3,4}}},
    {"igNode",named_object,3,{{0,2,4}}},
    {"igCamera",node,3,{{0,2,4}}},
    {"igCameraList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSceneInfo",info_base,8,{{0,2,4},{4,4,1},{2,5,4},{2,7,4}}},
    {"DerivedSceneInfo",scene_info,8,{{0,2,4},{4,4,1},{2,5,4},{2,7,4}}},
    {"beFileListInfo",info_base,7,{{0,2,4},{4,4,1},{0,5,4},{2,6,4}}},
    {"igNamedObjectList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beTestCtrlInfo",info_base,3,{{0,2,4}}}
};
struct ScriptFixture {
    struct Record {
        bool memory{};
        std::uint32_t type{};
        std::map<std::uint16_t, Bytes> values;
        Bytes data;
    };
    std::vector<Record> records;
    std::uint32_t exported_info_list{null_ref};
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
            "igMemoryRefMetaField","igBoolMetaField","igUnsignedIntMetaField","igEnumMetaField","igMatrix44fMetaField","igVec3fMetaField"};
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
            static_cast<std::uint32_t>(field_names.size()),0xfada,exported_info_list == null_ref ? 0x20000006u : 0xa0000006u});
        append(bytes,fields); push32(bytes,4); append(bytes,types); append(bytes,entries);
        append(bytes,words({8 + 2 * count,count}));
        for (std::uint32_t i = 0; i < count; ++i) push16(bytes,static_cast<std::uint16_t>(i));
        if (exported_info_list != null_ref) push32(bytes,exported_info_list);
        append(bytes,objects); append(bytes,memories); return read_igb_document(std::move(bytes));
    }

    struct Script { std::string name; Bytes bytes; };
    struct Info {
        std::uint32_t reference{}, list_reference{};
        std::vector<std::uint32_t> data_references, data_objects, memories;
    };
    std::vector<std::uint32_t> exports;
    Info add_info(const std::vector<Script>& scripts, const std::string& label = {}, bool exported = true) {
        Info info; info.reference = add(lua_info); text(info.reference,2,label);
        for (const auto& script : scripts) {
            const auto data = add(lua_data); text(data,2,script.name);
            const auto payload = add(lua_object);
            const auto buffer = memory(script.bytes);
            scalar(payload,2,static_cast<std::uint32_t>(script.bytes.size())); scalar(payload,3,buffer);
            scalar(data,3,payload);
            info.data_references.push_back(data); info.data_objects.push_back(payload); info.memories.push_back(buffer);
        }
        info.list_reference = list(lua_list,info.data_references); scalar(info.reference,7,info.list_reference);
        if (exported) {
            exports.push_back(info.reference);
            if (exported_info_list == null_ref) exported_info_list = list(info_list,exports);
            else contents(exported_info_list,exports);
        }
        return info;
    }
};
} // namespace dw4::test
