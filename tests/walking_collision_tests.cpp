#include "dw4/walking_collision.hpp"
#include "dw4/assets.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/meshes.hpp"
#include "dw4/textures.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace dw4;
using Bytes = std::vector<std::uint8_t>;
constexpr std::uint32_t null_ref = 0xffffffffu;
int checks{};
void check(bool condition, const std::string& reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}
void word(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes.at(at + i) = static_cast<std::uint8_t>(value >> (i * 8));
}
void push32(Bytes& bytes, std::uint32_t value) {
    const auto at = bytes.size(); bytes.resize(at + 4); word(bytes, at, value);
}
void push16(Bytes& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value)); bytes.push_back(static_cast<std::uint8_t>(value >> 8));
}
void append(Bytes& bytes, const Bytes& source) { bytes.insert(bytes.end(), source.begin(), source.end()); }
Bytes words(std::initializer_list<std::uint32_t> values) {
    Bytes result; for (auto value : values) push32(result, value); return result;
}
void name(Bytes& bytes, const std::string& text) {
    bytes.insert(bytes.end(), text.begin(), text.end()); bytes.push_back(0);
}
void align(Bytes& bytes) { bytes.resize((bytes.size() + 3) & ~std::size_t(3)); }

enum Type : std::uint32_t {
    object, object_dir, memory_dir, attr, geometry_attr, object_list, attr_list,
    node_list, node, group, attr_set, geometry_node, transform_node, switch_node,
    bitmask, info_base, scene_info, vertex_array, keep_group, unknown_group,
    info_list, switch_control, derived_control, floor_info
};
struct Schema { std::string name; std::uint32_t parent, slots; std::vector<IgbObjectField> fields; };
const std::vector<Schema> schemas{
    {"igObject",null_ref,2,{}},
    {"igObjectDirEntry",object,13,{{0,2,4},{1,11,4},{1,12,4}}},
    {"igMemoryDirEntry",object,14,{{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
    {"igAttr",object,2,{}},
    {"igGeometryAttr1_5",attr,14,{{2,4,4},{2,5,4},{6,6,4},{5,7,4},{5,8,4},{1,10,4},{2,13,4}}},
    {"igObjectList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAttrList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igNodeList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igNode",object,6,{{0,2,4},{1,5,4}}},
    {"igGroup",node,8,{{0,2,4},{1,5,4},{2,7,4}}},
    {"igAttrSet",group,10,{{0,2,4},{1,5,4},{2,7,4},{2,8,4},{4,9,1}}},
    {"igGeometry",attr_set,10,{{0,2,4},{1,5,4},{2,7,4},{2,8,4},{4,9,1}}},
    {"igTransform",group,12,{{0,2,4},{1,5,4},{2,7,4},{7,8,64},{6,9,4},{2,11,4}}},
    {"igSwitch",group,9,{{0,2,4},{1,5,4},{2,7,4},{2,8,4}}},
    {"igBitMask",object_list,6,{{1,2,4},{1,3,4},{3,4,4},{5,5,4}}},
    {"igInfo",object,3,{{0,2,4}}},
    {"igSceneInfo",info_base,11,{{0,2,4},{2,5,4},{8,10,12}}},
    {"igVertexArray1_1",object,12,{{3,2,4},{5,3,4},{5,4,4},{3,7,4},{3,8,4},{2,10,4}}},
    {"beGroupKeep",group,8,{{0,2,4},{1,5,4},{2,7,4}}},
    {"UnknownGroup",group,8,{{0,2,4},{1,5,4},{2,7,4}}},
    {"igInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beSwitchCtrlInfo",info_base,8,{{0,2,4},{2,7,4}}},
    {"UnknownSwitchCtrlInfo",switch_control,8,{{0,2,4},{2,7,4}}},
    {"beHitLandModelInfo",info_base,11,{{0,2,4},{2,7,4},{2,8,4},{1,10,4}}}
};
// Generated IGB records and local reference graph; no original game bytes.
struct Graph {
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
};

Bytes floats(std::initializer_list<float> values) {
    Bytes result; for (const auto value : values) push32(result,std::bit_cast<std::uint32_t>(value)); return result;
}

struct Fixture : Graph {
    std::uint32_t info, outer, inner, outer_children, inner_children;
    std::uint32_t owner, geom, vertices, positions, attrs, inner_mask, outer_mask;
    Fixture() {
        positions = memory(floats({-10,7,-20, 10,7,-20, 0,7,20}));
        Bytes table(80,0xff); word(table,0,positions);
        const auto stream = memory(std::move(table));
        vertices = add(vertex_array); scalar(vertices,2,stream); scalar(vertices,3,3);
        geom = add(geometry_attr); scalar(geom,4,vertices); scalar(geom,6,3); scalar(geom,7,1);
        owner = add(geometry_node); scalar(owner,5,16); scalar(owner,9,1);
        scalar(owner,7,list(node_list,{}));
        attrs = list(attr_list,{geom}); scalar(owner,8,attrs);
        inner = add(switch_node); inner_children = list(node_list,{owner}); scalar(inner,7,inner_children);
        inner_mask = mask(1,{1}); scalar(inner,8,inner_mask);
        outer = add(switch_node); outer_children = list(node_list,{inner}); scalar(outer,7,outer_children);
        outer_mask = mask(1,{0}); scalar(outer,8,outer_mask);
        info = add(floor_info); scalar(info,7,outer); scalar(info,8,inner); scalar(info,10,1);
        exported_info_list = list(info_list,{info});
    }
    std::uint32_t mask(std::uint32_t bits, std::initializer_list<std::uint32_t> values) {
        const auto ref = add(bitmask); const auto count = static_cast<std::uint32_t>(values.size());
        scalar(ref,2,count); scalar(ref,3,count); scalar(ref,4,memory(words(values))); scalar(ref,5,bits); return ref;
    }
    std::vector<MeshData> decode() const { return decode_walking_collision(document(),info); }
};
void throws(const Fixture& fixture, const std::string& reason) {
    try { fixture.decode(); }
    catch (const WalkingCollisionError&) { check(true,reason); return; }
    check(false,reason);
}
void synthetic() {
    const Fixture f;
    const auto doc = f.document();
    const auto source_bytes = std::vector<std::uint8_t>(doc.bytes().begin(), doc.bytes().end());
    auto data = decode_walking_collision(doc,f.info);
    check(data.size() == 1 && data[0].positions.size() == 3 && data[0].indices.size() == 3,
        "Decode explicit original floor profile with a disabled outer wrapper");
    check(data[0].positions[0].x == -10 && data[0].positions[0].y == 7 && data[0].positions[0].z == -20,
        "Stored floor world coordinates remain unchanged");
    check(data[0].bounds.max.x == 10 && data[0].bounds.min.y == 7 && data[0].bounds.max.z == 20,
        "Floor mesh bounds retain original units");
    check(std::equal(source_bytes.begin(),source_bytes.end(),doc.bytes().begin()),
        "Floor decoding never modifies game data");
    auto selected = f;
    selected.contents(selected.inner_children,{selected.owner,selected.owner});
    selected.scalar(selected.inner,8,selected.mask(2,{2}));
    check(selected.decode().size() == 1,"Inner stored child bits select only enabled geometry");
    selected.scalar(selected.inner,8,selected.mask(2,{3}));
    check(selected.decode().size() == 2,"Repeated enabled geometry occurrences remain ordered");
    selected.scalar(selected.inner,8,selected.mask(2,{0}));
    throws(selected,"No selected floor geometry rejects");
    selected = f;
    selected.contents(selected.inner_children,std::vector<std::uint32_t>(36,selected.owner));
    selected.scalar(selected.inner,8,selected.mask(36,{0,8}));
    check(selected.decode().size() == 1,"Inner mask selection preserves little-endian bit order across words");
    auto wall = f;
    wall.scalar(wall.info,10,2);
    wall.records[wall.positions].data = floats({-10,7,-20, 10,7,-20, 0,107,-20});
    const auto wall_data = decode_walking_walls(wall.document(),wall.info);
    check(wall_data.size() == 1 && wall_data[0].positions[2].y == 107,
        "Explicit category-2 walls preserve their original vertical triangles");
    wall.scalar(wall.inner,8,wall.mask(1,{0}));
    check(decode_walking_walls(wall.document(),wall.info).empty(),
        "Disabled stored walls remain absent without disabling the floor");
    try { decode_walking_walls(doc,f.info); check(false,"Floor category rejects as walls"); }
    catch (const WalkingCollisionError&) { check(true,"Floor category rejects as walls"); }
    auto bad = f; bad.contents(bad.exported_info_list,{}); throws(bad,"Unexported info rejects");
    bad = f; bad.exported_info_list = null_ref; throws(bad,"Missing exports reject");
    bad = f; bad.scalar(bad.info,10,2); throws(bad,"Wall category is not a floor");
    bad = f; bad.scalar(bad.info,10,4); throws(bad,"World limit category is not a floor");
    bad = f; bad.scalar(bad.info,7,null_ref); throws(bad,"Missing outer wrapper rejects");
    bad = f; bad.scalar(bad.info,8,null_ref); throws(bad,"Missing inner switch rejects");
    bad = f; bad.contents(bad.outer_children,{bad.owner}); throws(bad,"Declared inner switch must match outer child");
    bad = f; bad.contents(bad.outer_children,{}); throws(bad,"Empty outer wrapper rejects");
    bad = f; bad.contents(bad.inner_children,{null_ref}); throws(bad,"Null selected geometry rejects");
    bad = f; bad.contents(bad.inner_children,{bad.inner}); throws(bad,"Nested switch or cycle rejects");
    bad = f; bad.contents(bad.inner_children,{bad.add(transform_node)}); throws(bad,"Unverified transformed floor rejects");
    bad = f; bad.scalar(bad.inner,5,2); throws(bad,"Unverified switch flags reject");
    bad = f; bad.scalar(bad.owner,5,28); throws(bad,"Render geometry flags do not establish a collision shape");
    bad = f; bad.scalar(bad.owner,9,0); throws(bad,"Disabled geometry trigger rejects");
    bad = f; bad.scalar(bad.owner,9,2); throws(bad,"Malformed geometry boolean rejects");
    bad = f; bad.scalar(bad.owner,7,bad.inner_children); throws(bad,"Nested geometry children reject");
    bad = f; bad.contents(bad.attrs,{bad.geom,bad.geom}); throws(bad,"Ambiguous geometry attribute list rejects");
    bad = f; bad.contents(bad.attrs,{}); throws(bad,"Empty geometry attribute list rejects");
    bad = f; bad.scalar(bad.inner_mask,5,0); throws(bad,"Short bit mask rejects");
    bad = f; bad.scalar(bad.inner_mask,5,0xffffffffu); throws(bad,"Oversized bit mask rejects before arithmetic overflow");
    bad = f; bad.scalar(bad.inner_mask,3,2); throws(bad,"Mask storage must agree with capacity");
    bad = f; bad.scalar(bad.inner_mask,4,null_ref); throws(bad,"Null mask storage rejects");
    bad = f; bad.scalar(bad.inner_children,3,257); throws(bad,"Shape capacity is bounded before allocation");
    bad = f; bad.scalar(bad.exported_info_list,3,4097); throws(bad,"Export capacity is bounded before allocation");
    bad = f; bad.records[bad.positions].data = floats({0,0,0}); throws(bad,"Truncated original position stream rejects");
    bad = f; word(bad.records[bad.positions].data,0,0x7fc00000); throws(bad,"Nonfinite floor coordinates reject");
    auto forged = f.document();
    forged.metadata.header.byte_order = IgbByteOrder::big;
    try { decode_walking_collision(forged,f.info); check(false,"Big-endian profile rejects"); }
    catch (const WalkingCollisionError&) { check(true,"Big-endian profile rejects"); }
    forged = f.document();
    for (auto& object_value : forged.objects) if (object_value.reference_index == f.info)
        for (auto& value : object_value.fields) if (value.slot == 10) value.type_index = 2;
    try { decode_walking_collision(forged,f.info); check(false,"Same-width wrong field type rejects"); }
    catch (const WalkingCollisionError&) { check(true,"Same-width wrong field type rejects"); }
    // Repeated geometry must consume the aggregate budget even with one source buffer.
    bad = f;
    Bytes large_positions;
    for (std::size_t i=0;i<1500;++i) append(large_positions,floats({-10,7,-20, 10,7,-20, 0,7,20}));
    bad.records[bad.positions].data = std::move(large_positions);
    bad.scalar(bad.vertices,3,4500); bad.scalar(bad.geom,7,1500);
    bad.contents(bad.inner_children,std::vector<std::uint32_t>(256,bad.owner));
    bad.scalar(bad.inner,8,bad.mask(256,{0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,
        0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu}));
    throws(bad,"Repeated floor occurrences cannot evade the combined vertex budget");
}
void original(const std::filesystem::path& root) {
    const auto doc = read_igb_document(assets::AfsArchive::open(root / "files/ndmw.afs").read(179));
    const auto meshes = decode_walking_collision(doc,1768);
    std::size_t vertices{},triangles{};
    for (const auto& mesh : meshes) { vertices += mesh.positions.size(); triangles += mesh.indices.size()/3; }
    check(meshes.size() == 12 && vertices == 306 && triangles == 102,
        "Original dng101b1 collision floor contains 12 shapes, 306 vertices and 102 triangles");
    check(meshes[6].bounds.min.x == -350 && meshes[6].bounds.max.y == 40 && meshes[6].bounds.max.z == 460,
        "Original entry-point floor retains the verified bounds");
    std::cout << "Original floor: " << meshes.size() << " shapes, " << vertices << " vertices, " << triangles << " triangles\n";
    const auto doors = decode_walking_walls(doc,192);
    const auto walls = decode_walking_walls(doc,1953);
    std::size_t door_vertices{}, door_triangles{}, wall_vertices{}, wall_triangles{};
    for (const auto& mesh : doors) { door_vertices += mesh.positions.size(); door_triangles += mesh.indices.size()/3; }
    for (const auto& mesh : walls) { wall_vertices += mesh.positions.size(); wall_triangles += mesh.indices.size()/3; }
    check(doors.size() == 3 && door_vertices == 72 && door_triangles == 24,
        "Original stored door-wall geometry contains 3 shapes, 72 vertices and 24 triangles");
    check(walls.size() == 12 && wall_vertices == 612 && wall_triangles == 204,
        "Original static wall geometry contains 12 shapes, 612 vertices and 204 triangles");
    check(walls[5].bounds.min.y == 14 && walls[5].bounds.max.y == 140,
        "Sloped entry corridor wall bounds retain original height range");
    std::cout << "Original walls: " << walls.size()+doors.size() << " shapes, "
        << wall_vertices+door_vertices << " vertices, " << wall_triangles+door_triangles << " triangles\n";
}
} // namespace
int main(int argc,char** argv) {
    try {
        synthetic();
        if (argc == 2) original(argv[1]);
        else if (argc != 1) throw std::runtime_error("Usage: walking_collision_tests [EXTRACTED_ROOT]");
        std::cout << "Walking collision checks passed: " << checks << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
