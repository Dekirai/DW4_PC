#include "dw4/hitland_primitives.hpp"
#include "dw4/actor_geometry.hpp"
#include "dw4/hitland_query.hpp"
#include "dw4/assets.hpp"
#include "dw4/igb_document.hpp"

#include <bit>
#include <cmath>
#include <iostream>

namespace {
using namespace dw4;
using Vector=std::array<float,3>;
unsigned checks{};
void check(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& call) {
    ++checks;try {call();}catch(const std::exception&) {return;}
    throw std::runtime_error("Invalid HITLAND primitive operation was accepted.");
}
std::shared_ptr<ActorGeometryBuffer> floats(std::initializer_list<float> values) {
    auto result=std::make_shared<ActorGeometryBuffer>();
    for(auto value:values) {const auto word=std::bit_cast<std::uint32_t>(value);for(unsigned i=0;i<4;++i)result->bytes.push_back(static_cast<std::uint8_t>(word>>(8*i)));}
    return result;
}
struct Fixture {
    std::shared_ptr<ActorGeometryAttribute> attribute=std::make_shared<ActorGeometryAttribute>();
    std::shared_ptr<ActorGeometryBuffer> vertices=floats({-1,0,-1,0,0,1,1,0,-1,-1,-1,-1,0,-1,1,1,-1,-1});
    HitLandSegment segment;
    Vector position{77,88,99},normal{11,22,33};
    std::int32_t group=-1,stream=-1;
    std::vector<std::string> events;
    Fixture() {
        segment.origin={0,1,0};segment.direction={0,-1,0};segment.length=3;
        attribute->primitive_mode=3;attribute->primitive_count=2;
        attribute->vertices=std::make_shared<ActorVertexArray>();
        attribute->vertices->map_service=[this](auto&,auto& mapping,auto mode) {
            check(mode==0,"Vertex map mode changed.");events.push_back("VM");mapping.position={vertices,0};mapping.stride=12;
        };
        attribute->vertices->unmap_service=[this](auto&,auto&,auto mode) {check(mode==1,"Vertex unmap mode changed.");events.push_back("VU");};
    }
    bool run(std::uint8_t continuation=0,std::int32_t side=0) {
        return original_hitland_primitive(attribute,segment,position,normal,group,stream,side,continuation);
    }
    void indices() {
        attribute->index=std::make_shared<ActorIndexArray>();
        attribute->index->backing=std::make_shared<ActorGeometryBuffer>();attribute->index->backing->bytes={0,0,1,0,2,0,3,0,4,0,5,0};
        attribute->index->map_service=[this](auto& array,auto mode,auto first,auto length) {
            check(mode==0 && first==0 && length==-1,"Index map arguments changed.");events.push_back("IM");return ActorGeometryPointer{array.backing,0};
        };
        attribute->index->unmap_service=[this](auto&,const auto&,auto mode,auto first,auto length) {
            check(mode==1 && first==0 && length==-1,"Index unmap arguments changed.");events.push_back("IU");
        };
    }
};
void cursors_and_prefixes() {
    Fixture f;
    check(f.run() && f.group==0 && f.stream==0 && f.position==Vector{0,0,0},"First original primitive was not selected.");
    check(f.run(1) && f.group==1 && f.stream==3 && f.position==Vector{0,-1,0},"Continuation changed stream offset into triangle ordinal.");
    check(!f.run(1) && f.group==1 && f.stream==3 && f.position==Vector{0,-1,0},"Exhausted scan changed retained cursor or skipped output publications.");
    f.group=0;f.stream=-1;check(f.run(255) && f.group==0 && f.stream==0,"One minus-one cursor did not reset both words.");
    f.group=-2;f.stream=99;check(f.run(1) && f.group==0 && f.stream==0,"Cursor comparison was not signed and lexicographic.");
    f.attribute->vertices->format->word=0x10;f.group=55;f.stream=66;const auto previous=f.events;
    check(!f.run() && f.group==55 && f.stream==66 && f.events==previous,"Format rejection consumed maps or reset cursors.");
    f.attribute->vertices->format->word=0;f.attribute->primitive_mode=99;
    check(!f.run() && f.group==-1 && f.stream==-1 && f.events.back()=="VU","Unsupported mode skipped mapped cursor/unmap prefix.");
    f.attribute.reset();f.group=5;f.stream=6;
    check(!f.run() && f.group==5 && f.stream==6,"Null attribute changed caller cursor state.");
}
void physical_groups_and_winding() {
    Fixture f;f.attribute->primitive_count=1;
    f.attribute->primitive_lengths=std::make_shared<ActorPrimitiveLengths>();
    f.attribute->primitive_lengths->element_service=[](auto&,auto) {return 0u;};
    check(f.run() && f.group==0 && f.stream==0,"Zero group length incorrectly skipped its original do-while triangle.");
    f.attribute->primitive_lengths->element_service=[](auto&,auto) {return 0x80000000u;};
    check(f.run(),"Negative signed group length was validated before its first successful triangle.");
    f.attribute->primitive_mode=4;f.attribute->primitive_count=2;
    f.attribute->primitive_lengths->element_service=[](auto&,auto) {return 3u;};
    f.group=0;f.stream=0;
    check(!f.run(1) && f.normal==Vector{0,-1,0},"Odd absolute strip offset did not reverse winding after the group gap.");
    f.group=0;f.stream=0;check(f.run(1,2) && f.group==1 && f.stream==3 && f.normal==Vector{0,1,0},
        "Side two did not bypass the strip winding reversal.");
    Fixture indexed;indexed.indices();check(indexed.run() && indexed.events==std::vector<std::string>{"VM","IM","VU","IU"},
        "Indexed scan did not preserve map/unmap order.");
}
void live_map_owners() {
    Fixture f;f.indices();const auto old_vertices=f.attribute->vertices;const auto old_index=f.attribute->index;
    auto new_vertices=std::make_shared<ActorVertexArray>();auto new_index=std::make_shared<ActorIndexArray>();
    new_vertices->unmap_service=[&](auto&,auto& mapping,auto) {
        check(mapping.position.buffer==f.vertices,"Current unmap lost the original mapped pointer.");f.events.push_back("VU2");
    };
    new_index->unmap_service=[&](auto&,const auto& pointer,auto,auto,auto) {
        check(pointer.buffer==old_index->backing,"Post-map retained index owner received a replaced pointer.");f.events.push_back("IU2");
    };
    old_vertices->map_service=[&](auto&,auto& mapping,auto) {
        f.events.push_back("VM");mapping.position={f.vertices,0};mapping.stride=12;
        f.attribute->vertices=new_vertices;f.attribute->primitive_mode=99;f.attribute->primitive_count=0;f.attribute->vertex_base=100;
    };
    old_index->map_service=[&](auto& array,auto,auto,auto) {
        f.events.push_back("IM");f.attribute->index=new_index;return ActorGeometryPointer{array.backing,0};
    };
    check(f.run() && f.group==0 && f.stream==0,"Mapping callback replaced already-captured primitive fields.");
    check(f.events==std::vector<std::string>{"VM","IM","VU2","IU2"},"Unmap did not use current vertices and post-map captured index owner.");
    Fixture retained;const auto mapped=retained.vertices;
    retained.attribute->groups_service=[&](auto&) {
        retained.vertices=floats({0,0,0});return std::shared_ptr<ActorPrimitiveLengths>{};
    };
    check(retained.run() && retained.position==Vector{0,0,0} && retained.vertices!=mapped,"Group accessor retargeted a captured vertex mapping.");
}
void failures_retain_effects() {
    Fixture f;f.vertices=floats({0,0,0,0,0,0,0,0,0});f.group=7;f.stream=8;
    rejects([&]{f.run();});check(f.normal==Vector{0,0,0} && f.position==Vector{77,88,99} && f.group==-1 && f.stream==-1 &&
        f.events==std::vector<std::string>{"VM"},"Numeric failure rolled back normal/reset or ran unreached unmap.");
    Fixture late;late.attribute->vertices->unmap_service=[](auto&,auto&,auto) {throw std::runtime_error("Controlled unmap failure.");};
    rejects([&]{late.run();});check(late.group==0 && late.stream==0 && late.position==Vector{0,0,0},"Unmap failure rolled back selected hit/cursor.");
    Fixture group_failure;group_failure.group=7;group_failure.stream=8;
    group_failure.attribute->groups_service=[](auto&)->std::shared_ptr<ActorPrimitiveLengths> {throw std::runtime_error("Controlled group getter failure.");};
    rejects([&]{group_failure.run();});check(group_failure.group==7 && group_failure.stream==8 &&
        group_failure.events==std::vector<std::string>{"VM"},"Failed group lookup reached cursor reset or cleanup.");
    Fixture null_index;null_index.indices();const auto index=null_index.attribute->index;
    index->map_service=[&](auto& owner,auto,auto,auto) {null_index.attribute->index.reset();return ActorGeometryPointer{owner.backing,0};};
    rejects([&]{null_index.run();});check(null_index.group==0 && null_index.stream==0 && null_index.events.back()=="VU",
        "Null post-map index owner failed before selected hit and vertex unmap.");
    Fixture null_mapping;null_mapping.indices();
    null_mapping.attribute->index->map_service=[](auto&,auto,auto,auto) {return ActorGeometryPointer{};};
    check(null_mapping.run() && null_mapping.events.back()=="VU","Null mapped index pointer did not use unindexed scan and skip index unmap.");
}
void nullable_segment_read() {
    Fixture f;std::shared_ptr<ModelSceneObject> segment;
    f.attribute->primitive_count=0;
    check(!original_hitland_primitive(f.attribute,segment,f.position,f.normal,f.group,f.stream,0,std::uint8_t{0}),
        "Empty primitive count consumed a null segment.");
    f.attribute->primitive_count=1;f.events.clear();
    rejects([&]{original_hitland_primitive(f.attribute,segment,f.position,f.normal,f.group,f.stream,0,std::uint8_t{0});});
    check(f.normal==Vector{0,1,0} && f.position==Vector{77,88,99} && f.events==std::vector<std::string>{"VM"},
        "Null segment failed before its leaf normal publication or ran unmap.");
    segment=std::make_shared<ModelSceneList>();f.normal={11,22,33};
    rejects([&]{original_hitland_primitive(f.attribute,segment,f.position,f.normal,f.group,f.stream,0,std::uint8_t{0});});
    check(f.normal==Vector{0,1,0},"Wrong segment type lost its reached normalized-normal prefix.");
}
void alternate_and_unknown_attributes() {
    Fixture f;f.attribute=std::make_shared<ActorGeometryAttribute>("UnrelatedAttribute");f.group=7;f.stream=8;
    check(!f.run() && f.group==7 && f.stream==8,"Unrelated attribute reached geometry fields or cursor resets.");
    f.attribute=std::make_shared<ActorGeometryAttribute>("igGeometryAttr2");
    rejects([&]{f.run();});check(f.group==7 && f.stream==8,"Missing alternate preparation changed unreached cursor fields.");
    unsigned prefixes{};
    f.attribute->alternate_prefix_service=[&](auto&) {++prefixes;return true;};
    check(!f.run() && prefixes==1 && f.group==7 && f.stream==8,"Alternate semantic rejection fell through to primary fields.");
    f.attribute->alternate_prefix_service=[&](auto& attribute) {++prefixes;attribute.state=123;return false;};
    rejects([&]{f.run();});check(prefixes==2 && f.attribute->state==123 && f.group==7 && f.stream==8,
        "Original alternate null virtual access was silently repaired or lost its provider prefix.");
}
void service_return_checkpoints() {
    Fixture f;f.group=7;f.stream=8;bool poisoned{};
    f.attribute->vertices->map_service=[&](auto&,auto&,auto) {f.events.push_back("VM");poisoned=true;};
    const auto checkpoint=[&] {if(poisoned)throw std::runtime_error("Controlled owning invocation poison.");};
    rejects([&]{original_hitland_primitive(f.attribute,f.segment,f.position,f.normal,f.group,f.stream,0,std::uint8_t{0},checkpoint);});
    check(f.group==7 && f.stream==8 && f.events==std::vector<std::string>{"VM"} && f.normal==Vector{11,22,33},
        "Post-map checkpoint ran after cursor reset, leaf, or unmap effects.");
    Fixture late;late.indices();poisoned=false;
    late.attribute->vertices->unmap_service=[&](auto&,auto&,auto) {late.events.push_back("VU");poisoned=true;};
    rejects([&]{original_hitland_primitive(late.attribute,late.segment,late.position,late.normal,late.group,late.stream,0,std::uint8_t{0},checkpoint);});
    check(late.group==0 && late.stream==0 && late.events==std::vector<std::string>{"VM","IM","VU"},
        "Post-unmap checkpoint lost selected cursor or allowed unreached index unmap.");
}
void captured_arguments() {
    Fixture f;const auto selected=f.attribute;
    std::shared_ptr<ModelSceneObject> supplied_segment=std::make_shared<HitLandSegment>();
    const auto selected_segment=std::static_pointer_cast<HitLandSegment>(supplied_segment);
    selected_segment->origin=f.segment.origin;selected_segment->direction=f.segment.direction;selected_segment->length=f.segment.length;
    selected->vertices->map_service=[&](auto&,auto& mapping,auto) {
        mapping.position={f.vertices,0};mapping.stride=12;f.attribute.reset();supplied_segment.reset();
    };
    check(original_hitland_primitive(f.attribute,supplied_segment,f.position,f.normal,f.group,f.stream,0,std::uint8_t{0}) &&
        !f.attribute && !supplied_segment && f.position==Vector{0,0,0} && f.events==std::vector<std::string>{"VU"},
        "A map callback retargeted the captured attribute or segment argument owner.");
}
void original(const std::filesystem::path& root) {
    ActorSceneResource resource(read_igb_document(assets::AfsArchive::open(root/"files/ndmw.afs").read(179)),"ndmw.afs[179]",179);
    unsigned triangles{};
    for(const auto reference:{1819u,1822u,1813u,1821u,1728u,1802u,1750u,1749u,1747u,1817u,1741u,1820u}) {
        const auto attribute=resource.models().geometry_attribute(reference);
        check(attribute==resource.models().geometry_attribute(reference) && attribute->primitive_mode==3 && !attribute->index &&
            !attribute->primitive_lengths,"Original floor primitive did not use its canonical unindexed attribute owner.");
        ActorVertexMapping mapping;attribute->vertices->map(mapping);
        for(std::uint32_t primitive=0;primitive<attribute->primitive_count;++primitive) {
            const auto a=mapping.position.read_vec3((attribute->vertex_base+primitive*3u)*mapping.stride);
            const auto b=mapping.position.read_vec3((attribute->vertex_base+primitive*3u+1u)*mapping.stride);
            const auto c=mapping.position.read_vec3((attribute->vertex_base+primitive*3u+2u)*mapping.stride);
            Vector center{};for(unsigned i=0;i<3;++i)center[i]=(a[i]+b[i]+c[i])/3.f;
            HitLandSegment ray;ray.origin={center[0],center[1]+100,center[2]};ray.direction={0,-1,0};ray.length=200;
            Vector point{77,88,99},normal{};auto group=static_cast<std::int32_t>(primitive)-1;std::int32_t stream=-2;
            const auto found=original_hitland_primitive(attribute,ray,point,normal,group,stream,2,std::uint8_t{1});
            check(found && group==static_cast<std::int32_t>(primitive) && stream==static_cast<std::int32_t>(primitive*3u) &&
                std::abs(point[1]-center[1])<0.001f,"Original floor triangle centroid did not return its actual group/stream offset and height.");
            ++triangles;
        }
        attribute->vertices->unmap(mapping);
    }
    check(triangles==102,"Original floor primitive coverage differs from its 102 authored triangles.");
    std::cout<<"Original stage179: all 102 floor triangles reached through canonical geometry and native mapping.\n";
}
}
int main(int argc,char** argv) {
    try {cursors_and_prefixes();physical_groups_and_winding();live_map_owners();failures_retain_effects();nullable_segment_read();alternate_and_unknown_attributes();service_return_checkpoints();captured_arguments();
        if(argc==2)original(argv[1]);else if(argc!=1)return 2;
        std::cout<<checks<<" HITLAND primitive checks passed.\n";return 0;
    }catch(const std::exception& error) {std::cerr<<"HITLAND primitives after "<<checks<<" checks: "<<error.what()<<'\n';return 1;}
}
