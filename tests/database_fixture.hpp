#pragma once

#include "dw4/database.hpp"
#include "script_fixtures.hpp"

#include <array>
#include <bit>

// Synthetic IGB graph only. The extra types/fields are attached to owned
// serialized storage so malformed tests can alter actual field metadata.
namespace database_fixture {
using namespace dw4;
using Bytes=dw4::test::Bytes;
inline constexpr auto null_ref=dw4::test::null_ref;
struct Fixture : dw4::test::ScriptFixture {
    struct Group {std::uint32_t columns,data;};
    std::uint32_t database,database_storage,info,info_storage,ram_list;
    std::array<Group,6> groups;
    std::vector<std::uint32_t> rams,ram_storage;
    Fixture() {
        database=add(dw4::test::object);Bytes fields;
        for(auto& group:groups) {
            group.columns=list(dw4::test::named_list,{});
            group.data=list(dw4::test::object_list,{});
            scalar(group.columns,4,null_ref);scalar(group.data,4,null_ref);
            dw4::test::push32(fields,group.columns);dw4::test::push32(fields,group.data);
        }
        database_storage=memory(std::move(fields));
        add_ram(0,1);ram_list=list(dw4::test::object_list,rams);
        info=add(dw4::test::scene_info);text(info,2,"synthetic_db");scalar(info,4,1);scalar(info,7,database);
        info_storage=memory(dw4::test::words({ram_list}));exported_info_list=list(dw4::test::info_list,{info});
    }
    std::uint32_t named(std::optional<std::string> value) {
        const auto ref=add(dw4::test::named_object);if(value)text(ref,2,*value);return ref;
    }
    std::uint32_t add_ram(std::int32_t index,std::uint8_t active=1) {
        const auto ref=add(dw4::test::object);rams.push_back(ref);
        ram_storage.push_back(memory(dw4::test::words({std::bit_cast<std::uint32_t>(index),active,0,0,0,0,null_ref})));
        return ref;
    }
    void columns(std::size_t group,const std::vector<std::uint32_t>& values) {contents(groups.at(group).columns,values);}
    void columns(std::size_t group,const std::vector<std::optional<std::string>>& values) {
        std::vector<std::uint32_t> refs;for(const auto& value:values)refs.push_back(named(value));columns(group,refs);
    }
    void data(std::size_t group,Bytes bytes,std::uint32_t count) {
        const auto ref=groups.at(group).data;scalar(ref,2,count);scalar(ref,3,count);
        scalar(ref,4,bytes.empty()?null_ref:memory(std::move(bytes)));
    }
    void chars(const std::vector<std::optional<std::string>>& names,const std::vector<std::int8_t>& values) {
        columns(0,names);Bytes bytes;for(const auto value:values)bytes.push_back(std::bit_cast<std::uint8_t>(value));
        data(0,std::move(bytes),static_cast<std::uint32_t>(values.size()));
    }
    void shorts(const std::vector<std::optional<std::string>>& names,const std::vector<std::int16_t>& values) {
        columns(1,names);Bytes bytes;for(const auto value:values)dw4::test::push16(bytes,std::bit_cast<std::uint16_t>(value));
        data(1,std::move(bytes),static_cast<std::uint32_t>(values.size()));
    }
    void ints(const std::vector<std::optional<std::string>>& names,const std::vector<std::int32_t>& values) {
        columns(2,names);Bytes bytes;for(const auto value:values)dw4::test::push32(bytes,std::bit_cast<std::uint32_t>(value));
        data(2,std::move(bytes),static_cast<std::uint32_t>(values.size()));
    }
    void floats(const std::vector<std::optional<std::string>>& names,const std::vector<float>& values) {
        columns(3,names);Bytes bytes;for(const auto value:values)dw4::test::push32(bytes,std::bit_cast<std::uint32_t>(value));
        data(3,std::move(bytes),static_cast<std::uint32_t>(values.size()));
    }
    void bools(const std::vector<std::optional<std::string>>& names,const std::vector<std::uint8_t>& values) {
        columns(4,names);data(4,values,static_cast<std::uint32_t>(values.size()));
    }
    void strings(const std::vector<std::optional<std::string>>& names,const std::vector<std::optional<std::string>>& values) {
        columns(5,names);std::vector<std::uint32_t> refs;for(const auto& value:values)refs.push_back(named(value));contents(groups[5].data,refs);
    }
    IgbDocument document() const {
        auto doc=dw4::test::ScriptFixture::document();
        auto object=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
        auto storage=[&](std::uint32_t ref) {return doc.memories.at(doc.references.at(ref).value_index).offset;};
        auto type=[&](const char* name,std::int32_t parent,std::uint32_t slots) {
            auto value=doc.metadata.object_types.at(dw4::test::object);value.name=name;value.parent_index=parent;value.slots=slots;
            const auto result=static_cast<std::uint32_t>(doc.metadata.object_types.size());doc.metadata.object_types.push_back(std::move(value));return result;
        };
        const auto list_base=type("igDataList",dw4::test::object,5);
        const std::array<const char*,6> names{"igCharList","igShortList","igIntList","igFloatList","igBoolList","igNamedObjectList"};
        for(std::size_t i=0;i<groups.size();++i)
            object(groups[i].data).type_index=i==5?dw4::test::named_list:type(names[i],static_cast<std::int32_t>(list_base),5);
        auto& database_object=object(database);database_object.type_index=type("beDataBase",dw4::test::object,14);
        for(std::uint16_t slot=2;slot<=13;++slot)database_object.fields.push_back({slot,2,storage(database_storage)+(slot-2)*4,4});
        const auto base_info=type("beBaseInfo",dw4::test::info_base,7);
        auto& info_object=object(info);info_object.type_index=type("beDBManagerInfo",static_cast<std::int32_t>(base_info),8);
        info_object.fields.push_back({6,2,storage(info_storage),4});
        object(ram_list).type_index=type("beBaseInfoRamList",dw4::test::object_list,5);
        const auto ram_type=type("beBaseInfoRam",dw4::test::object,11);
        for(std::size_t i=0;i<rams.size();++i) {
            auto& ram=object(rams[i]);ram.type_index=ram_type;const auto at=storage(ram_storage[i]);
            ram.fields={{2,1,at,4},{5,4,at+4,1},{6,1,at+8,4},{7,1,at+12,4},{8,1,at+16,4},{9,1,at+20,4},{10,2,at+24,4}};
        }
        return doc;
    }
    DatabaseDefinition definition(std::string source="synthetic/database.igb",std::size_t occurrence=7) const {
        return decode_database(document(),database,std::move(source),occurrence);
    }
};
} // namespace database_fixture
