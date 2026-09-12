#include "dw4/actor_geometry.hpp"
#include "dw4/assets.hpp"
#include "player_runtime_binding_fixture.hpp"

#include <cmath>
#include <iostream>
#include <set>

namespace {
using namespace dw4;
namespace pf=player_runtime_fixture;
unsigned checks{};
void check(bool value,const char* reason) {++checks;if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F operation) {
    ++checks;try {operation();}catch(const std::exception&) {return;}
    throw std::runtime_error("Unsupported geometry operation completed.");
}
struct Fixture {
    pf::RuntimeFixture source;
    std::uint32_t second_vertices{},second_geometry{},index{},lengths{},storage{},storage_alias{},table{},table_alias{};
    Fixture() {
        storage=source.memory(pf::words({3,0,0,0x7fc00001u}));
        storage_alias=source.memory(pf::words({99,99,99,99}));
        pf::Bytes stream_table=pf::words({storage,pf::null_ref,storage_alias});
        table=source.memory(stream_table);table_alias=source.memory(stream_table);
        source.scalar(source.vertices,2,table);source.scalar(source.vertices,3,0);
        source.scalar(source.vertices,6,0);source.scalar(source.vertices,7,storage_alias);
        second_vertices=source.add(pf::vertex_array);source.records[second_vertices]=source.records[source.vertices];
        source.scalar(second_vertices,2,table_alias);
        index=source.add(pf::vertex_array);lengths=source.add(pf::vertex_array);
        source.scalar(index,2,storage_alias);source.scalar(lengths,2,storage);
        source.scalar(index,3,0);source.scalar(index,4,99);source.scalar(index,5,77);
        source.scalar(lengths,3,0);source.scalar(lengths,4,7);
        source.scalar(source.geom,5,index);source.scalar(source.geom,13,lengths);
        source.scalar(source.geom,7,0xffffffffu);source.scalar(source.geom,8,0xffffffffu);
        second_geometry=source.add(pf::geometry_attr);source.records[second_geometry]=source.records[source.geom];
        source.scalar(second_geometry,4,second_vertices);
        source.contents(source.owner_attrs,{source.geom,pf::null_ref,source.geom,second_geometry});
        source.scalar(source.owner_attrs,2,1);source.scalar(source.owner_attrs,3,0);
        source.scalar(source.owner,9,0x80);
    }
    IgbDocument document() const {
        auto document=source.document();
        document.references[storage_alias].value_index=document.references[storage].value_index;
        document.references[table_alias].value_index=document.references[table].value_index;
        for(const auto& entry:std::vector<std::pair<std::uint32_t,std::string>>{{index,"igIndexArray"},{lengths,"igPrimLengthArray1_1"}}) {
            auto type=document.metadata.object_types[pf::vertex_array];type.name=entry.second;type.parent_index=pf::object;
            const auto type_index=static_cast<std::uint32_t>(document.metadata.object_types.size());
            document.metadata.object_types.push_back(std::move(type));
            document.objects[document.references[entry.first].value_index].type_index=type_index;
            if(entry.first==index)for(auto& field:document.objects[document.references[index].value_index].fields)
                if(field.slot==4)field.type_index=6;
        }
        return document;
    }
};
void owners() {
    Fixture f;auto document=f.document();
    const auto alias=static_cast<std::uint32_t>(document.references.size());document.references.push_back(document.references[f.source.geom]);
    auto scene=std::make_shared<ActorSceneResource>(std::move(document),"synthetic/geometry",8);
    const auto node=scene->mutable_node(f.source.owner);
    check(node->is_a("igAttrSet") && node->is_a("igNode") && !node->is_a("igTransform"),"Node inheritance was confused with exact type identity.");
    check(!node->runtime_dispatch_index(),"Serialized metadata index must not fabricate native dispatch registration.");
    node->set_runtime_dispatch_index(42);check(node->runtime_dispatch_index()==42u,"Actual dispatch index is not mutable.");
    node->set_runtime_dispatch_index({});
    check(node->attributes_enabled()==0x80,"Attribute-enable byte was narrowed to bool.");
    const auto list=node->attribute_list_owner();
    check(list==scene->models().list(f.source.owner_attrs) && list->count==1 && list->capacity==0 && list->backing->size()==4,
        "Attribute list count/capacity/backing must remain independent and canonical.");
    check(!(*list->backing)[1] && (*list->backing)[0]==(*list->backing)[2],"Nullable/repeated physical attribute slots changed.");
    const auto geometry=scene->models().geometry_attribute(f.source.geom);
    check(geometry==scene->models().geometry_attribute(alias) && geometry==(*list->backing)[0],"Attribute source aliases created distinct owners.");
    check(geometry->is_primary() && !geometry->is_alternate() && geometry->is_a("igAttr") && geometry->is_a("igGeometryAttr"),
        "Geometry family classification lost actual inheritance.");
    check(geometry->primitive_count==0xffffffffu && geometry->vertex_base==0xffffffffu,"Raw unsigned geometry fields were normalized.");
    const auto other=scene->models().geometry_attribute(f.second_geometry);
    const auto vertices=geometry->vertices;
    check(vertices==scene->models().vertex_array(f.source.vertices) && vertices!=other->vertices && vertices->streams==other->vertices->streams,
        "Distinct vertex arrays must share their exact aliased stream table.");
    check(vertices->streams->slots.size()==3 && !vertices->streams->slots[1] && vertices->streams->slots[0]==vertices->streams->slots[2],
        "Physical stream slots must retain nullable memory aliases without a fabricated twenty-slot allocation.");
    const auto memory=vertices->streams->slots[0];
    check(memory==vertices->blend_weights && memory==geometry->index->backing && memory==geometry->primitive_lengths->backing,
        "Position/index/group/blend references must share the same actual byte allocation.");
    check(memory->reference()==f.storage && memory->document_owner()==scene->document_owner() && vertices->streams->reference==f.table,
        "Geometry backing source identity metadata is missing.");
    check(geometry->index==scene->models().index_array(f.index) && geometry->primitive_lengths==scene->models().primitive_lengths(f.lengths),
        "Typed array accessors must return the canonical object registry allocation.");
    check(geometry->primitive_lengths->element(0)==3 && geometry->primitive_lengths->element_bits==7,
        "Primitive element lookup must ignore count and element-width metadata.");
    memory->bytes[0]=9;
    check(geometry->index->map().read_u16()==9 && other->primitive_lengths->element(0)==9,"Live shared byte writes did not reach all aliases.");
    ActorVertexMapping mapping;vertices->map(mapping);
    check(mapping.length==0 && mapping.stride==12 && mapping.position.buffer==memory,"Position mapping incorrectly consumed logical count or raw offset fields.");
    auto replacement=std::make_shared<ActorGeometryBuffer>();replacement->bytes=pf::words({23,0,0});
    vertices->streams->slots[0]=replacement;
    check(mapping.position.read_u32()==9 && other->vertices->streams->slots[0]==replacement,
        "Replacing live stream backing must not retarget a captured physical mapped pointer.");
    const auto old_list=node->attribute_list_owner();node->set_attribute_list_owner({});node->set_attributes_enabled(0);
    check(!node->attribute_list_owner() && old_list->backing->size()==4 && !node->attributes_enabled(),"Live attribute fields lost captured old owner identity.");
    node->set_attribute_list_owner(old_list);
    rejects([&]{scene->mutable_node(f.source.nodes[0])->attribute_list_owner();});
    rejects([&]{scene->models().geometry_attribute(f.source.vertices);});
    std::weak_ptr<ActorSceneResource> weak_scene=scene;scene.reset();
    check(weak_scene.expired() && geometry->document_owner() && mapping.position.read_u32()==9,
        "Geometry graph must retain document and mapped bytes without keeping its resource facade alive.");
}
void mapping_order() {
    ActorVertexArray vertices;vertices.count=2;
    ActorVertexMapping mapping;mapping.first=3;mapping.stride=91;mapping.state=73;
    rejects([&]{vertices.map(mapping);});
    check(mapping.length==0xffffffffu && mapping.stride==91 && mapping.state==73,"Missing table must preserve the published wrapped length and untouched later mapping fields.");
    vertices.streams=std::make_shared<ActorVertexStreams>();vertices.streams->slots.push_back({});
    mapping={};vertices.map(mapping);
    check(mapping.length==2 && !mapping.position && mapping.stride==12 && mapping.state==0,"A null position slot is mapped before any later vertex dereference.");
    mapping.first=1;vertices.map(mapping);
    check(bool(mapping.position) && !mapping.position.buffer && mapping.position.byte_offset==12,"Null-base pointer arithmetic must retain the reached nonzero address.");
    rejects([&]{mapping.position.read_vec3();});
    auto memory=std::make_shared<ActorGeometryBuffer>();memory->bytes=pf::words({1,2,3,0x7fc00001u});
    vertices.streams->slots[0]=memory;vertices.flags=4;mapping={};mapping.state=77;
    rejects([&]{vertices.map(mapping);});
    check(mapping.position.buffer==memory && mapping.length==2 && mapping.stride==12 && mapping.state==0,
        "Missing synchronization must retain every preceding map publication.");
    unsigned synchronized{};vertices.synchronize_service=[&](ActorVertexArray& current) {++synchronized;check(&current==&vertices,"Wrong synchronization owner.");};
    vertices.map(mapping);check(synchronized==1,"Reached synchronization must invoke its actual provider.");
    vertices.unmap_state=9;vertices.unmap(mapping,123);
    check(vertices.unmap_state==1,"Concrete vertex unmap must set raw +30 even for a different mode.");
    mapping={};mapping.selector=4;mapping.stride=65;
    rejects([&]{vertices.map(mapping);});
    check(mapping.length==2 && mapping.stride==65,"Unsupported semantic map must retain its length prefix.");
    const ActorGeometryPointer pointer{memory,0xfffffffcu};
    check(pointer.read_u32(4)==1 && std::isnan(ActorGeometryPointer{memory,12}.read_float()),
        "Physical reads must preserve wrapped byte arithmetic and unconsumed nonfinite float payloads.");
    rejects([&]{ActorGeometryPointer{memory,15}.read_u16();});
    ActorIndexArray index;index.backing=memory;index.count=0;index.format=99;index.offset=73;
    const auto indexed=index.map(21,0x80000001u,0);
    check(indexed.byte_offset==2 && indexed.read_u16()==0,"Index concrete map must use wrapped first*2 and ignore format/count/length.");
    index.backing.reset();index.unmap(indexed,999,9,0);
    check(indexed.buffer==memory,"Concrete index unmap must neither validate nor retarget the saved pointer.");
    ActorPrimitiveLengths lengths;lengths.backing=memory;
    check(lengths.element(0x40000000u)==1,"Primitive element address must wrap uint32 multiplication.");
    lengths.element_service=[](ActorPrimitiveLengths&,std::uint32_t) {return 57u;};
    check(lengths.element(3)==57,"Virtual primitive element override did not run.");
    ActorGeometryAttribute alternate("igGeometryAttr2");check(alternate.is_alternate() && !alternate.is_primary(),"Synthetic alternate family differs.");
    rejects([&]{alternate.prepare_alternate();});
    alternate.alternate_prefix_service=[](ActorGeometryAttribute&) {return true;};
    check(alternate.prepare_alternate(),"Explicit alternate semantic rejection provider did not run.");
}
void malformed() {
    Fixture f;auto document=f.document();
    document.memories[document.references[f.table].value_index].size=5;
    ActorSceneResource partial(std::move(document),"synthetic/partial-table",0);
    rejects([&]{partial.models().geometry_attribute(f.source.geom);});
    document=f.document();
    document.references[f.storage_alias].value_index=document.references[f.table].value_index;
    ActorSceneResource cyclic(std::move(document),"synthetic/table-cycle",0);
    rejects([&]{cyclic.models().geometry_attribute(f.source.geom);});
    document=f.document();
    auto& vertex=document.objects[document.references[f.second_vertices].value_index];
    for(auto& field:vertex.fields)if(field.slot==6)field.type_index=8;
    ActorSceneResource rollback(std::move(document),"synthetic/rollback",0);
    auto retained=rollback.models().geometry_attribute(f.source.geom);
    rejects([&]{rollback.models().geometry_attribute(f.second_geometry);});
    check(rollback.models().geometry_attribute(f.source.geom)==retained && retained->vertices->streams->slots[0]==retained->index->backing,
        "Failed graph load must preserve previously exposed canonical allocation identities.");
}
void original(const std::filesystem::path& root) {
    auto archive=assets::AfsArchive::open(root/"files/ndmw.afs");
    auto scene=std::make_shared<ActorSceneResource>(read_igb_document(archive.read(179)),"ndmw.afs:179",179);
    std::set<std::uint32_t> arrays;
    unsigned geometry_count{},index_count{},group_count{};
    for(auto ref:{1768u,192u,1953u}) {
        const auto info=scene->models().hit_land_geometry_info(ref);const auto children=info->check_switch->child_list_owner();
        for(std::int32_t i=0;i<children->count();++i) {
            const auto node=children->backing_node(static_cast<std::size_t>(i));const auto list=node->attribute_list_owner();
            check(list && node->attributes_enabled()!=0 && !node->runtime_dispatch_index(),"Original geometry must expose authored attributes without fabricated native dispatch index.");
            bool found{};
            for(const auto& attr:list->values())if(const auto geometry=std::dynamic_pointer_cast<ActorGeometryAttribute>(attr)) {
                found=true;++geometry_count;const auto vertices=geometry->vertices;
                check(geometry==scene->models().geometry_attribute(*geometry->reference()) && geometry->is_primary() && vertices,
                    "Original selected geometry/vertices lack canonical typed identities.");
                check((vertices->get_format()->word&0xff0u)==0,"Original collision vertices unexpectedly require rejected blended formats.");
                ActorVertexMapping mapping;vertices->map(mapping);
                check(mapping.position && mapping.stride==12 && mapping.length==vertices->count,"Original position map differs from native stream0/count profile.");
                const auto position=mapping.position.read_vec3();
                check(std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2]),"Original reached position is not finite.");
                if(geometry->index) {++index_count;geometry->index->map().read_u16();}
                if(geometry->primitive_lengths) {++group_count;geometry->primitive_lengths->element(0);}
                vertices->unmap(mapping);check(vertices->unmap_state==1,"Original native vertex unmap publication is missing.");
                arrays.insert(*vertices->reference());
            }
            check(found,"Original selected collision node has no represented primary geometry attribute.");
        }
    }
    check(geometry_count==27 && arrays.size()==27,"Original floor/wall geometry selection count changed.");
    std::cout<<"Original stage179: "<<geometry_count<<" collision attributes, "<<arrays.size()<<" vertex arrays, "<<index_count<<" index arrays, "<<group_count<<" primitive-length arrays.\n";
    bool indexed_found{};
    for(const auto& object:scene->document().objects) {
        if(!scene->document().is_a(object,"igGeometryAttr1_5") || !scene->document().field_reference(object,5))continue;
        const auto geometry=scene->models().geometry_attribute(object.reference_index);
        check(geometry->index && geometry->primitive_lengths && geometry->primitive_mode==4,
            "Original indexed stage mesh is missing its actual strip/group owners.");
        const auto index=geometry->index;const auto lengths=geometry->primitive_lengths;
        check(index==scene->models().index_array(*index->reference()) && lengths==scene->models().primitive_lengths(*lengths->reference()),
            "Original indexed geometry must share the canonical index and primitive-length owners.");
        check(index->count==4 && lengths->count==1 && lengths->element_bits==32 && lengths->element(0)==4,
            "Original indexed stage geometry count or group bytes differ from the verified source.");
        auto mapped=index->map();const auto before=mapped.read_u16();index->unmap(mapped);
        check(mapped.read_u16()==before && before<geometry->vertices->count,"Original reached index map/unmap changed the captured bytes.");
        indexed_found=true;break;
    }
    check(indexed_found,"Original stage179 indexed mesh coverage is missing.");
}
}
int main(int argc,char** argv) {
    try {owners();mapping_order();malformed();const auto synthetic=checks;
        if(argc==2)original(argv[1]);else if(argc!=1)throw std::runtime_error("Usage: actor_geometry_tests [extracted-root]");
        std::cout<<checks<<" actor geometry checks passed ("<<synthetic<<" synthetic).\n";
    }catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
