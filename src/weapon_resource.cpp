#include "dw4/weapon_resource.hpp"
#include "dw4/actor_scene.hpp"

#include <limits>
#include <set>
#include <string_view>

namespace dw4 {
namespace {
constexpr std::uint32_t max_infos=4096,max_rams=4096,max_exports=65536,max_rows=4096;
constexpr std::size_t max_links=131072,max_text=16u*1024u*1024u;
[[noreturn]] void fail(const char* reason) {throw WeaponResourceError(reason);}
void checked_text(std::string_view value) {
    if(value.size()>4096 || value.find('\0')!=std::string_view::npos)
        fail("Weapon source or name exceeds its bounded byte profile.");
}
struct Reader {
    const IgbDocument& doc;
    const ActorSceneResource& resource;
    std::size_t links{},text_bytes{};
    const IgbObject& object(std::uint32_t reference,std::string_view type) const {
        const auto& value=doc.object(reference);
        if(doc.metadata.object_types.at(value.type_index).name!=type)
            fail("Unexpected weapon object type.");
        std::set<std::uint16_t> slots;
        for(const auto& field:value.fields)
            if(!slots.insert(field.slot).second) fail("Weapon object repeats a field slot.");
        return value;
    }
    const IgbFieldValue& field(const IgbObject& value,std::uint16_t slot,
        std::string_view type,std::optional<std::size_t> width=4) const {
        const auto* part=doc.field(value,slot);
        if(!part || doc.metadata.field_types.at(part->type_index).name!=type ||
           (width && part->size!=*width)) fail("Unexpected weapon field type or width.");
        doc.field_bytes(*part);return *part;
    }
    std::uint32_t canonical(std::uint32_t reference) const {
        return resource.canonical_reference(reference);
    }
    std::optional<std::uint32_t> ref(const IgbObject& value,std::uint16_t slot) const {
        field(value,slot,"igObjectRefMetaField");
        const auto result=doc.field_reference(value,slot);
        if(result) doc.object(*result);
        return result;
    }
    std::uint32_t required_ref(const IgbObject& value,std::uint16_t slot) const {
        const auto result=ref(value,slot);
        if(!result) fail("Weapon definition requires its actual authored list.");
        return *result;
    }
    std::int32_t integer(const IgbObject& value,std::uint16_t slot) const {
        field(value,slot,"igIntMetaField");return doc.field_i32(value,slot);
    }
    std::uint8_t byte(const IgbObject& value,std::uint16_t slot) const {
        return doc.field_bytes(field(value,slot,"igBoolMetaField",1))[0];
    }
    std::optional<std::string> name(const IgbObject& value,std::uint16_t slot=2) {
        const auto& part=field(value,slot,"igStringMetaField",{});
        if(part.size>4104 || part.size>max_text-text_bytes)
            fail("Weapon aggregate name budget exceeded.");
        text_bytes+=part.size;
        const auto text=doc.field_string(value,slot);checked_text(text);
        return part.size==4 ? std::optional<std::string>{} : std::string(text);
    }
    std::pair<WeaponListIdentity,std::vector<std::uint32_t>> list(std::uint32_t reference,
        std::string_view type,std::uint32_t limit) {
        const auto& value=object(reference,type);
        field(value,2,"igIntMetaField");field(value,3,"igIntMetaField");
        field(value,4,"igMemoryRefMetaField");
        const auto count=doc.field_u32(value,2),capacity=doc.field_u32(value,3);
        if(count>capacity || capacity>limit || count>max_links-links)
            fail("Weapon object list exceeds its bounded capacity.");
        links+=count;
        const auto memory=doc.field_reference(value,4);
        if((!memory && capacity) || (memory && doc.memory(*memory).size()!=std::size_t(capacity)*4))
            fail("Weapon object list storage differs from declared capacity.");
        std::optional<std::uint32_t> memory_owner;
        if(memory) memory_owner=doc.memories.at(doc.references.at(*memory).value_index).reference_index;
        WeaponListIdentity identity{reference,canonical(reference),memory,memory_owner,count,capacity};
        std::vector<std::uint32_t> result;result.reserve(count);
        for(const auto item:doc.object_list(reference)) {
            if(!item) fail("Weapon object list contains a null row.");
            result.push_back(*item);
        }
        return {identity,std::move(result)};
    }
    std::optional<std::uint32_t> data(const IgbObject& value,std::uint16_t slot) {
        const auto result=ref(value,slot);
        if(result) list(*result,"beBaseInfoDataList",max_rows);
        return result;
    }
};
}
struct WeaponProgram::Data {
    std::shared_ptr<ActorSceneResource> resource;
    std::vector<WeaponInfoDefinition> infos;
};
const std::shared_ptr<ActorSceneResource>& WeaponProgram::resource() const noexcept {return data_->resource;}
const std::string& WeaponProgram::source() const noexcept {return data_->resource->source();}
std::uint32_t WeaponProgram::source_occurrence() const noexcept {return data_->resource->source_occurrence();}
const std::vector<WeaponInfoDefinition>& WeaponProgram::infos() const noexcept {return data_->infos;}

WeaponProgram decode_weapons(std::shared_ptr<ActorSceneResource> resource) {
    try {
        if(!resource) fail("Weapon decoding requires a retained scene resource.");
        checked_text(resource->source());
        if(resource->source().empty() || resource->source_occurrence()==std::numeric_limits<std::uint32_t>::max())
            fail("Weapon decoding requires an explicit source and valid loaded occurrence.");
        const auto& doc=resource->document();
        if(doc.metadata.header.byte_order!=IgbByteOrder::little)
            fail("Big-endian weapon serialization is unverified.");
        if(!doc.info_list_reference) fail("Weapon decoding requires an exported info list.");
        Reader read{doc,*resource};
        auto data=std::make_shared<WeaponProgram::Data>();data->resource=std::move(resource);
        std::size_t ram_count{};
        for(const auto reference:read.list(*doc.info_list_reference,"igInfoList",max_exports).second) {
            const auto& exported=doc.object(reference);
            if(!doc.is_a(exported,"igInfo")) fail("Weapon export list contains a non-info object.");
            // Original manager selection compares the exact virtual info type.
            if(doc.metadata.object_types.at(exported.type_index).name!="beWeaponInfo") continue;
            if(data->infos.size()>=max_infos) fail("Weapon info count exceeds its budget.");
            const auto& info=read.object(reference,"beWeaponInfo");
            WeaponInfoDefinition decoded;
            decoded.reference=reference;decoded.owner_reference=read.canonical(reference);
            decoded.name=read.name(info);decoded.active=read.byte(info,4);decoded.data_reference=read.data(info,5);
            auto [ram_list,rams]=read.list(read.required_ref(info,6),"beBaseInfoRamList",max_rams);
            if(rams.size()>max_rams-ram_count) fail("Weapon aggregate RAM occurrence budget exceeded.");
            ram_count+=rams.size();decoded.ram_list=ram_list;
            for(const auto ram_reference:rams) {
                const auto& ram=read.object(ram_reference,"beWeaponInfoRam");
                if(doc.field(ram,3) || doc.field(ram,4))
                    fail("Serialized weapon RAM parent/controller restoration is unsupported.");
                WeaponRamDefinition row;
                row.reference=ram_reference;row.owner_reference=read.canonical(ram_reference);
                row.authored_index=read.integer(ram,2);row.active=read.byte(ram,5);
                for(std::uint16_t slot=6;slot<=9;++slot) row.authored_words[slot-6]=read.integer(ram,slot);
                row.data_reference=read.data(ram,10);decoded.rams.push_back(std::move(row));
            }
            auto [node_list,nodes]=read.list(read.required_ref(info,7),"igNodeList",max_rows);
            auto [bone_list,bones]=read.list(read.required_ref(info,8),"igNamedObjectList",max_rows);
            auto [sequence_list,sequences]=read.list(read.required_ref(info,9),"beWeaponSeqDataList",max_rows);
            if(nodes.size()!=bones.size()) fail("Weapon node and bone-name lists have different lengths.");
            decoded.node_list=node_list;decoded.bone_name_list=bone_list;decoded.sequence_list=sequence_list;
            for(const auto node:nodes)
                decoded.nodes.push_back({node,read.canonical(node),data->resource->node(node)});
            for(const auto bone:bones) {
                const auto& object=read.object(bone,"igNamedObject");
                decoded.bone_names.push_back({bone,read.canonical(bone),read.name(object)});
            }
            for(const auto sequence:sequences) {
                const auto& object=read.object(sequence,"beWeaponSeqData");
                decoded.sequences.push_back({sequence,read.canonical(sequence),read.name(object),read.name(object,3)});
            }
            data->infos.push_back(std::move(decoded));
        }
        return WeaponProgram(std::move(data));
    } catch(const WeaponResourceError&) {throw;}
      catch(const std::exception& error) {throw WeaponResourceError(error.what());}
}
} // namespace dw4
