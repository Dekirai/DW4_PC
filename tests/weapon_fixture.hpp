#pragma once

#include "script_fixtures.hpp"
#include <bit>

namespace weapon_fixture {
using namespace dw4;
using namespace dw4::test;
inline IgbObject& obj(IgbDocument& doc,std::uint32_t ref) {return doc.objects.at(doc.references.at(ref).value_index);}

// This fixture serializes synthetic storage first, then attaches the exact
// extra metadata fields. No original bytes or native-only weapon constructor.
struct Fixture : ScriptFixture {
    std::uint32_t info,other_info,ram_list,node_list,bone_list,sequence_list,info_storage;
    std::vector<std::uint32_t> rams,ram_storage,nodes,node_storage,children,attributes,bones,sequences,labels;
    Fixture() {
        for(unsigned i=0;i<2;++i) {
            rams.push_back(add(object));
            ram_storage.push_back(memory(words({std::bit_cast<std::uint32_t>(-7+static_cast<std::int32_t>(i)),255,
                11,0xfffffffeu,13,14,null_ref})));
            nodes.push_back(add(node));text(nodes.back(),2,i==0?"right":"left");
            children.push_back(list(object_list,{}));attributes.push_back(list(object_list,{}));
            node_storage.push_back(memory(words({null_ref,i==0?0u:2u,children.back(),attributes.back(),0})));
            bones.push_back(add(named_object));text(bones.back(),2,i==0?"RArm04":"LArm04");
            sequences.push_back(add(named_object));text(sequences.back(),2,i==0?"SQ_WAIT":"SQ_ATCK1");
            labels.push_back(add(named_object));if(i==0)text(labels.back(),2,"");
        }
        ram_list=list(object_list,rams);node_list=list(object_list,nodes);bone_list=list(named_list,bones);
        sequence_list=list(object_list,sequences);
        info=add(scene_info);text(info,2,"weapon");scalar(info,4,1);scalar(info,7,node_list);
        other_info=add(scene_info);text(other_info,2,"other");scalar(other_info,4,128);scalar(other_info,7,node_list);
        info_storage=memory(words({ram_list,bone_list,sequence_list}));
        exported_info_list=list(info_list,{info});
    }
    IgbDocument document() const {
        auto doc=ScriptFixture::document();
        const auto storage=[&](std::uint32_t ref){return doc.memories.at(doc.references.at(ref).value_index).offset;};
        const auto type=[&](const char* name,std::int32_t parent,std::uint32_t slots) {
            IgbObjectType t;t.name=name;t.parent_index=parent;t.slots=slots;
            const auto result=static_cast<std::uint32_t>(doc.metadata.object_types.size());
            doc.metadata.object_types.push_back(std::move(t));return result;
        };
        const auto node_list_type=type("igNodeList",object_list,5),attr_list_type=type("igAttrList",object_list,5);
        const auto group_type=type("igGroup",node,8),attr_type=type("igAttrSet",static_cast<std::int32_t>(group_type),10);
        const auto ram_type=type("beWeaponInfoRam",object,11);
        const auto base_info_type=type("beBaseInfo",info_base,7);
        const auto info_type=type("beWeaponInfo",static_cast<std::int32_t>(base_info_type),10);
        const auto sequence_type=type("beWeaponSeqData",named_object,4);
        obj(doc,ram_list).type_index=type("beBaseInfoRamList",object_list,5);
        obj(doc,node_list).type_index=node_list_type;
        obj(doc,sequence_list).type_index=type("beWeaponSeqDataList",object_list,5);
        for(std::size_t i=0;i<rams.size();++i) {
            auto& ram=obj(doc,rams[i]);ram.type_index=ram_type;const auto at=storage(ram_storage[i]);
            ram.fields={{2,1,at,4},{5,4,at+4,1},{6,1,at+8,4},{7,1,at+12,4},
                {8,1,at+16,4},{9,1,at+20,4},{10,2,at+24,4}};
            auto& n=obj(doc,nodes[i]);n.type_index=attr_type;const auto p=storage(node_storage[i]);
            n.fields.insert(n.fields.end(),{{3,2,p,4},{5,1,p+4,4},{7,2,p+8,4},{8,2,p+12,4},{9,4,p+16,1}});
            obj(doc,children[i]).type_index=node_list_type;obj(doc,attributes[i]).type_index=attr_list_type;
            auto& sequence=obj(doc,sequences[i]);sequence.type_index=sequence_type;
            auto label=*doc.field(doc.object(labels[i]),2);label.slot=3;sequence.fields.push_back(label);
        }
        for(const auto ref:{info,other_info}) {
            auto& value=obj(doc,ref);value.type_index=info_type;const auto at=storage(info_storage);
            value.fields.insert(value.fields.end(),{{6,2,at,4},{8,2,at+4,4},{9,2,at+8,4}});
        }
        return doc;
    }
};
} // namespace weapon_fixture
