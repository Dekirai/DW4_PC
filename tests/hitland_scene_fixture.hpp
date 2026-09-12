#pragma once

#include "dw4/model_scene.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <algorithm>

namespace hitland_scene_fixture {
using namespace dw4;
namespace pf=player_runtime_fixture;
struct Fixture {
    pf::RuntimeFixture source;
    std::uint32_t info,outer,inner,other_switch,dynamic,mask,other_mask,mask_memory,children,other_children,names,scalars,box,box_data,alias_word;
    Fixture() {
        info=source.add(pf::model_type);source.records[info]=source.records[source.model];
        source.scalar(info,5,pf::null_ref);source.scalar(info,6,source.list(pf::ram_list,{}));
        outer=source.add(pf::geometry_node);inner=source.add(pf::geometry_node);other_switch=source.add(pf::geometry_node);
        dynamic=source.add(pf::model_node);source.records[dynamic]=source.records[source.nodes[0]];
        source.scalar(dynamic,7,source.list(pf::node_list,{source.owner}));
        children=source.list(pf::node_list,{source.owner,pf::null_ref,dynamic,dynamic});
        other_children=source.list(pf::node_list,{});source.records[other_children]=source.records[children];
        source.scalar(outer,7,source.list(pf::node_list,{inner}));source.scalar(inner,7,children);
        source.scalar(other_switch,7,other_children);
        mask_memory=source.memory(pf::words({0x8000000du,0x12345678u}));
        mask=source.add(pf::palette_type);other_mask=source.add(pf::palette_type);
        for(auto ref:{mask,other_mask}) {source.scalar(ref,2,0xfffffffeu);source.scalar(ref,3,0);source.scalar(ref,4,mask_memory);}
        names=source.list(pf::named_list,{source.bones[0],pf::null_ref,source.bones[0]});
        box=source.add(pf::object);box_data=source.memory(pf::floats({-2,-3,-4,5,6,7}));
        scalars=source.memory(pf::words({outer,inner,names,0x80001c03u,mask,other_mask,source.nodes[1],pf::null_ref,1,box}));
        alias_word=source.memory(pf::words({static_cast<std::uint32_t>(source.records.size()+1)}));
    }
    IgbDocument document() const {
        auto doc=source.document();
        doc.references.push_back(doc.references.at(mask_memory));
        auto object=[&](std::uint32_t ref)->IgbObject& {return doc.objects.at(doc.references.at(ref).value_index);};
        const auto offset=doc.memories.at(doc.references.at(scalars).value_index).offset;
        const auto field=[&](std::uint16_t slot,std::uint16_t type,std::size_t index) {return IgbFieldValue{slot,type,offset+index*4,4};};
        const auto type=[&](std::string name,std::int32_t parent,std::uint16_t slots) {
            const auto index=static_cast<std::uint32_t>(doc.metadata.object_types.size());
            IgbObjectType schema;schema.name=std::move(name);schema.parent_index=parent;schema.slots=slots;
            doc.metadata.object_types.push_back(std::move(schema));return index;
        };
        const auto info_type=type("beHitLandModelInfo",pf::info_type,11);
        const auto switch_type=type("igSwitch",pf::group,9);
        const auto mask_type=type("igBitMask",pf::object,6);
        const auto transform_type=type("igTransform",pf::group,12);
        const auto copy_type=type("beCopyTransform",static_cast<std::int32_t>(transform_type),13);
        auto& land=object(info);land.type_index=info_type;
        std::erase_if(land.fields,[](const auto& f){return f.slot==7;});
        auto active=*doc.field(doc.object(source.state),4);active.slot=4;land.fields.push_back(active);
        land.fields.push_back(field(7,2,0));land.fields.push_back(field(8,2,1));land.fields.push_back(field(9,2,2));
        land.fields.push_back(field(10,1,3));
        for(auto ref:{outer,inner,other_switch}) {
            auto& sw=object(ref);sw.type_index=switch_type;
            std::erase_if(sw.fields,[](const auto& f){return f.slot==8 || f.slot==9;});
            sw.fields.push_back(field(3,2,7));sw.fields.push_back(field(8,2,ref==other_switch ? 5 : 4));
        }
        for(auto ref:{mask,other_mask}) {object(ref).type_index=mask_type;object(ref).fields.push_back(field(5,5,8));}
        for(auto& f:object(other_mask).fields)if(f.slot==4)
            f.offset=doc.memories.at(doc.references.at(alias_word).value_index).offset;
        auto& copy=object(dynamic);copy.type_index=copy_type;
        std::erase_if(copy.fields,[](const auto& f){return f.slot>=12;});copy.fields.push_back(field(12,2,6));
        copy.fields.push_back(field(3,2,7));
        auto& geom=object(source.owner);geom.fields.push_back(field(3,2,9));
        const auto vector_type=static_cast<std::uint16_t>(doc.metadata.field_types.size());
        doc.metadata.field_types.push_back({"igVec3fMetaField",1,0});
        const auto box_type=type("igAABox",pf::object,4);
        const auto vector_offset=doc.memories.at(doc.references.at(box_data).value_index).offset;
        object(box).type_index=box_type;object(box).fields={{2,vector_type,vector_offset,12},{3,vector_type,vector_offset+12,12}};
        return doc;
    }
    std::shared_ptr<ActorSceneResource> scene() const {return std::make_shared<ActorSceneResource>(document(),"synthetic/hitland-geometry",4);}
};
} // namespace hitland_scene_fixture
