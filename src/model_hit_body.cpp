#include "dw4/model_hit_body.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <utility>

namespace dw4 {
namespace {
constexpr std::uint32_t max_boxes=4096,max_instances=1024,max_lists=4096;
constexpr std::size_t max_links=32768,max_text=1024u*1024u;
[[noreturn]] void fail(const char* reason) { throw ModelHitBodyError(reason); }
struct Reader {
    const IgbDocument& doc;
    std::size_t links{},text_bytes{};
    const IgbObject& exact(std::uint32_t reference,std::string_view type) const {
        const auto& value=doc.object(reference);
        if(doc.metadata.object_types.at(value.type_index).name!=type) fail("Unexpected model HIT object type.");
        return value;
    }
    const IgbFieldValue& field(const IgbObject& object,std::uint16_t slot,std::string_view type,
        std::optional<std::size_t> width=4) const {
        const auto* value=doc.field(object,slot);
        if(!value || doc.metadata.field_types.at(value->type_index).name!=type || (width && value->size!=*width))
            fail("Unexpected model HIT field type or width.");
        doc.field_bytes(*value);
        return *value;
    }
    std::uint32_t ref(const IgbObject& object,std::uint16_t slot) const {
        field(object,slot,"igObjectRefMetaField");
        const auto value=doc.field_reference(object,slot);
        if(!value) fail("Required model HIT graph reference is null.");
        return *value;
    }
    void boolean(const IgbObject& object,std::uint16_t slot) const {
        const auto bytes=doc.field_bytes(field(object,slot,"igBoolMetaField",1));
        if(bytes[0]>1) fail("Model HIT boolean exceeds the serialized profile.");
    }
    std::string text(const IgbObject& object,std::uint16_t slot) {
        const auto& value=field(object,slot,"igStringMetaField",{});
        if(value.size>4104 || value.size>max_text-text_bytes) fail("Model HIT name budget exceeded.");
        text_bytes+=value.size;
        auto result=std::string(doc.field_string(object,slot));
        if(result.size()>4096 || result.find('\0')!=std::string::npos) fail("Invalid model HIT name bytes.");
        return result;
    }
    std::vector<std::uint32_t> list(std::uint32_t reference,std::string_view type,std::uint32_t limit) {
        const auto& value=exact(reference,type);
        field(value,2,"igIntMetaField");field(value,3,"igIntMetaField");field(value,4,"igMemoryRefMetaField");
        const auto count=doc.field_u32(value,2),capacity=doc.field_u32(value,3);
        if(count>capacity || capacity>limit || count>max_links-links) fail("Model HIT list allocation budget exceeded.");
        links+=count;
        const auto memory=doc.field_reference(value,4);
        if((!memory && capacity) || (memory && doc.memory(*memory).size()!=std::size_t(capacity)*4))
            fail("Model HIT list storage differs from its declared capacity.");
        std::vector<std::uint32_t> result;result.reserve(count);
        for(const auto item:doc.object_list(reference)) {
            if(!item) fail("Model HIT graph list contains a null object.");
            result.push_back(*item);
        }
        return result;
    }
    std::array<float,3> vector(const IgbObject& object,std::uint16_t slot) const {
        const auto bytes=doc.field_bytes(field(object,slot,"igVec3fMetaField",12));
        std::array<float,3> result;
        for(std::size_t i=0;i<3;++i) {
            std::uint32_t bits{};
            for(std::size_t j=0;j<4;++j)bits|=std::uint32_t(bytes[i*4+j])<<(j*8);
            result[i]=std::bit_cast<float>(bits);
            if(!std::isfinite(result[i])) fail("Model HIT box contains a non-finite endpoint.");
        }
        return result;
    }
    void ram(std::uint32_t reference) const {
        const auto& value=exact(reference,"beBaseInfoRam");
        field(value,2,"igIntMetaField");boolean(value,5);
        for(std::uint16_t slot=6;slot<=9;++slot)field(value,slot,"igIntMetaField");
        field(value,10,"igObjectRefMetaField");
        if(const auto data=doc.field_reference(value,10))exact(*data,"beBaseInfoDataList");
    }
};
} // namespace

struct ModelHitBodyDefinition::Data {
    std::string source,name;
    std::size_t occurrence;
    std::uint32_t model,data_list,ram_list,hit,box_list;
    std::array<std::uint8_t,3> flags;
    std::vector<std::uint32_t> rams,ram_owners,box_refs;
    std::vector<CollisionBox> boxes;
};
const std::string& ModelHitBodyDefinition::source() const noexcept { return data_->source; }
std::size_t ModelHitBodyDefinition::source_occurrence() const noexcept { return data_->occurrence; }
std::uint32_t ModelHitBodyDefinition::model_reference() const noexcept { return data_->model; }
const std::string& ModelHitBodyDefinition::model_name() const noexcept { return data_->name; }
std::uint32_t ModelHitBodyDefinition::data_list_reference() const noexcept { return data_->data_list; }
std::uint32_t ModelHitBodyDefinition::ram_list_reference() const noexcept { return data_->ram_list; }
const std::vector<std::uint32_t>& ModelHitBodyDefinition::ram_references() const noexcept { return data_->rams; }
const std::vector<std::uint32_t>& ModelHitBodyDefinition::ram_owners() const noexcept { return data_->ram_owners; }
std::uint32_t ModelHitBodyDefinition::hit_reference() const noexcept { return data_->hit; }
std::uint8_t ModelHitBodyDefinition::hit_player_bullet() const noexcept { return data_->flags[0]; }
std::uint8_t ModelHitBodyDefinition::hit_enemy_bullet() const noexcept { return data_->flags[1]; }
std::uint8_t ModelHitBodyDefinition::hit_player_talk() const noexcept { return data_->flags[2]; }
std::uint32_t ModelHitBodyDefinition::box_list_reference() const noexcept { return data_->box_list; }
const std::vector<std::uint32_t>& ModelHitBodyDefinition::box_references() const noexcept { return data_->box_refs; }
const std::vector<CollisionBox>& ModelHitBodyDefinition::local_boxes() const noexcept { return data_->boxes; }

ModelHitBodyDefinition decode_model_hit_body(const IgbDocument& doc,std::uint32_t model_ref,
    std::string source,std::size_t occurrence) {
    try {
        if(source.empty() || source.size()>4096 || source.find('\0')!=std::string::npos ||
            occurrence==std::numeric_limits<std::size_t>::max()) fail("Model HIT requires a bounded explicit source and loaded occurrence.");
        if(doc.metadata.header.byte_order!=IgbByteOrder::little) fail("Big-endian model HIT graphs are unverified.");
        Reader read{doc};
        if(!doc.info_list_reference) fail("Model HIT requires an explicitly exported MODELCTRL info.");
        const auto exports=read.list(*doc.info_list_reference,"igInfoList",max_lists);
        for(const auto item:exports)if(!doc.is_a(doc.object(item),"igInfo")) fail("Info export list contains a non-info object.");
        if(std::find(exports.begin(),exports.end(),model_ref)==exports.end()) fail("Selected MODELCTRL is not an explicit export.");
        const auto& model=read.exact(model_ref,"beModelCtrlInfo");
        auto name=read.text(model,2);read.boolean(model,4);
        const auto data_list=read.ref(model,5),ram_list=read.ref(model,6);
        const auto rows=read.list(data_list,"beBaseInfoDataList",max_lists);
        std::optional<std::uint32_t> hit;
        for(const auto row:rows) {
            const auto& object=doc.object(row);
            if(!doc.is_a(object,"beBaseInfoData")) fail("MODELCTRL data list contains a non-data object.");
            const auto row_name=read.text(object,2);read.field(object,3,"igIntMetaField");
            if(!hit && row_name=="HIT") hit=row;
        }
        if(!hit) fail("MODELCTRL has no authored named HIT data.");
        const auto& hit_object=read.exact(*hit,"beModelCtrlInfoDataHit");
        const std::array<std::uint8_t,3> flags{
            doc.field_bytes(read.field(hit_object,4,"igBoolMetaField",1))[0],
            doc.field_bytes(read.field(hit_object,5,"igBoolMetaField",1))[0],
            doc.field_bytes(read.field(hit_object,7,"igBoolMetaField",1))[0]};
        const auto box_list=read.ref(hit_object,6);
        auto box_refs=read.list(box_list,"igAABoxList",max_boxes);
        std::vector<CollisionBox> boxes;boxes.reserve(box_refs.size());
        for(const auto reference:box_refs) {
            const auto& box=read.exact(reference,"igAABox");
            boxes.push_back({read.vector(box,2),read.vector(box,3)});
        }
        auto rams=read.list(ram_list,"beBaseInfoRamList",max_instances);
        std::vector<std::uint32_t> owners;owners.reserve(rams.size());
        for(const auto reference:rams) {read.ram(reference);owners.push_back(doc.object(reference).reference_index);}
        return ModelHitBodyDefinition(std::make_shared<const ModelHitBodyDefinition::Data>(ModelHitBodyDefinition::Data{
            std::move(source),std::move(name),occurrence,model_ref,data_list,ram_list,*hit,box_list,flags,
            std::move(rams),std::move(owners),std::move(box_refs),std::move(boxes)}));
    } catch(const ModelHitBodyError&) { throw; }
      catch(const std::exception& error) { throw ModelHitBodyError(error.what()); }
}

ModelHitBodyCache::ModelHitBodyCache(ModelHitBodyDefinition definition,std::uint32_t instance)
    :definition_(std::move(definition)),instance_index_(instance) {
    if(instance>=definition_.ram_references().size()) fail("Model HIT instance is absent from its actual RAM list.");
}
std::uint32_t ModelHitBodyCache::ram_reference() const noexcept { return definition_.ram_references()[instance_index_]; }
std::uint32_t ModelHitBodyCache::ram_owner_reference() const noexcept { return definition_.ram_owners()[instance_index_]; }
const std::vector<CollisionBox>& ModelHitBodyCache::update(const AnimationMatrix& matrix,std::uint64_t time) {
    try {
        for(const auto value:matrix)if(!std::isfinite(value))fail("Model HIT matrix contains a non-finite value.");
        if(initialized_ && time==calc_time_)return boxes_;
        const auto& local=definition_.local_boxes();
        std::vector<std::array<float,3>> minima,maxima;
        if(!initialized_) {
            minima.reserve(local.size());maxima.reserve(local.size());
            for(const auto& box:local) {
                const auto distances=original_box_distances(box);
                minima.push_back(distances.minimum);maxima.push_back(distances.maximum);
            }
        }
        const auto& minimum=initialized_ ? minimum_distances_ : minima;
        const auto& maximum=initialized_ ? maximum_distances_ : maxima;
        std::vector<CollisionBox> output;output.reserve(local.size());
        if(time==calc_time_) {
            constexpr auto bound=std::numeric_limits<float>::max();
            output.resize(local.size(),CollisionBox{{bound,bound,bound},{-bound,-bound,-bound}});
        } else {
            for(std::size_t i=0;i<local.size();++i)output.push_back(original_body_box(matrix,local[i],{minimum[i],maximum[i]}));
        }
        if(!initialized_) {minimum_distances_=std::move(minima);maximum_distances_=std::move(maxima);}
        boxes_=std::move(output);calc_time_=time;initialized_=true;
        return boxes_;
    } catch(const ModelHitBodyError&) { throw; }
      catch(const std::exception& error) { throw ModelHitBodyError(error.what()); }
}
} // namespace dw4
