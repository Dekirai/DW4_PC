#include "dw4/player_placement.hpp"
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
    object, object_dir, memory_dir, object_list, info_base, info_list,
    point_info, node, group, keep_group, node_list, keep_list, switch_node,
    transform_node, derived_info, derived_transform
};
struct Schema { std::string name; std::uint32_t parent, slots; std::vector<IgbObjectField> fields; };
const std::vector<Schema> schemas{
    {"igObject",null_ref,2,{}},
    {"igObjectDirEntry",object,13,{{0,2,4},{1,11,4},{1,12,4}}},
    {"igMemoryDirEntry",object,14,{{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
    {"igObjectList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igInfo",object,3,{{0,2,4}}},
    {"igInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"bePoint01Info",info_base,9,{{0,2,4},{2,7,4},{2,8,4}}},
    {"igNode",object,6,{{0,2,4},{1,5,4}}},
    {"igGroup",node,8,{{0,2,4},{1,5,4},{2,7,4}}},
    {"beGroupKeep",group,8,{{0,2,4},{1,5,4},{2,7,4}}},
    {"igNodeList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beGroupKeepList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igSwitch",group,9,{{0,2,4},{1,5,4},{2,7,4},{2,8,4}}},
    {"igTransform",group,12,{{0,2,4},{1,5,4},{2,7,4},{7,8,64},{1,9,4},{2,11,4}}},
    {"DerivedPointInfo",point_info,9,{{0,2,4},{2,7,4},{2,8,4}}},
    {"DerivedTransform",transform_node,12,{{0,2,4},{1,5,4},{2,7,4},{7,8,64},{1,9,4},{2,11,4}}}
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
    std::uint32_t info, polygon, groups, first_group, first_points, first_point;
    Fixture() {
        polygon = add(switch_node); // Display mask is unrelated to GETMATRIX.
        std::vector<std::uint32_t> group_refs;
        for (std::uint32_t g = 0; g < 2; ++g) {
            std::vector<std::uint32_t> point_refs;
            for (std::uint32_t p = 0; p < 4; ++p) {
                const auto point = add(transform_node);
                text(point,2,"Point " + std::to_string(g) + "/" + std::to_string(p));
                scalar(point,5,2);
                // A rotated, nonuniformly scaled and reflected affine matrix.
                records[point].values[8] = floats({0,2,0,0, -3,0,0,0, 0,0,-4,0,
                    float(10+100*g+p),20,30,1});
                point_refs.push_back(point);
            }
            const auto points = list(node_list,point_refs);
            const auto group_ref = add(keep_group); scalar(group_ref,7,points);
            group_refs.push_back(group_ref);
            if (!g) { first_group = group_ref; first_points = points; first_point = point_refs[0]; }
        }
        groups = list(keep_list,group_refs);
        info = add(point_info); text(info,2,"Synthetic placements"); scalar(info,7,polygon); scalar(info,8,groups);
        exported_info_list = list(info_list,{info});
    }
    PlayerPlacement decode(std::uint32_t g = 0, std::uint32_t p = 0) const {
        return decode_igb_player_placement(document(),info,g,p);
    }
};
template<class F> void throws(F&& operation, const std::string& reason) {
    try { operation(); } catch (const PlayerPlacementError&) { check(true,reason); return; }
    check(false,reason);
}
void synthetic() {
    Fixture f; auto doc = f.document();
    const auto bytes = doc.bytes(); const Bytes before(bytes.begin(),bytes.end());
    const auto listed = list_igb_player_placements(doc);
    check(listed.size() == 1 && listed[0].supported && listed[0].group_count == 2,"Validate two explicit placement groups");
    check(listed[0].reference_index == f.info && listed[0].polygon_switch_reference == f.polygon &&
        listed[0].name == "Synthetic placements","Retain descriptor and display-switch provenance");
    const auto placement = decode_igb_player_placement(doc,f.info,0,0);
    check(placement.info_reference == f.info && placement.group_reference == f.first_group &&
        placement.transform_reference == f.first_point && placement.group_index == 0 && placement.player_index == 0,
        "Retain exact selection provenance");
    check(placement.name == "Point 0/0" && placement.matrix[1] == 2 && placement.matrix[4] == -3 &&
        placement.matrix[10] == -4 && placement.matrix[12] == 10,"Preserve full orientation, scale, reflection and translation");
    check(f.decode(1,3).matrix[12] == 113,"Group/player ordering selects the corresponding original transform");
    const std::vector<Vec3> local{{1,2,3},{0,0,0}};
    const auto world = apply_player_placement(placement,local);
    check(world.size() == 2 && world[0].x == 4 && world[0].y == 22 && world[0].z == 18,
        "Apply column-major affine matrix to actor-local point");
    check(world[1].x == 10 && world[1].y == 20 && world[1].z == 30,"Place local origin at original translation");
    check(local[0].x == 1 && local[0].y == 2 && local[0].z == 3,"Placement does not modify sampled local vertices");
    check(std::equal(before.begin(),before.end(),doc.bytes().begin(),doc.bytes().end()),"Read and apply operations preserve original bytes");
    throws([&]{f.decode(2,0);},"Reject out-of-range group instead of original unsafe fallback");
    throws([&]{f.decode(0,4);},"Reject out-of-range player index");
    throws([&]{decode_igb_player_placement(doc,f.first_group,0,0);},"Reject unexported descriptor request");
    auto bad = f; bad.exported_info_list = null_ref;
    check(list_igb_player_placements(bad.document()).empty(),"Missing export list has no scan fallback");
    throws([&]{bad.decode();},"Unexported descriptor is ineligible");
    bad = f; bad.contents(bad.exported_info_list,{f.info,f.info});
    throws([&]{bad.decode();},"Reject duplicate placement export");
    bad = f; bad.contents(bad.exported_info_list,{f.first_point});
    throws([&]{bad.decode();},"Reject non-info export");
    bad = f; bad.contents(bad.exported_info_list,{null_ref});
    throws([&]{bad.decode();},"Reject null export");
    bad = f; bad.scalar(bad.exported_info_list,3,4097);
    throws([&]{bad.decode();},"Bound export capacity before allocation");
    for (int kind = 0; kind < 16; ++kind) {
        bad = f;
        if (kind == 0) bad.records[bad.info].type = derived_info;
        if (kind == 1) bad.scalar(bad.info,7,null_ref);
        if (kind == 2) bad.scalar(bad.info,7,bad.first_group);
        if (kind == 3) bad.scalar(bad.info,8,null_ref);
        if (kind == 4) bad.scalar(bad.info,8,bad.first_points);
        if (kind == 5) bad.contents(bad.groups,{});
        if (kind == 6) bad.contents(bad.groups,{null_ref});
        if (kind == 7) bad.contents(bad.groups,{bad.first_point});
        if (kind == 8) bad.scalar(bad.groups,3,4097);
        if (kind == 9) bad.scalar(bad.first_group,7,null_ref);
        if (kind == 10) bad.contents(bad.first_points,{bad.first_point});
        if (kind == 11) bad.contents(bad.first_points,{null_ref,bad.first_point,bad.first_point,bad.first_point});
        if (kind == 12) bad.records[bad.first_point].type = derived_transform;
        if (kind == 13) bad.scalar(bad.first_point,9,1);
        if (kind == 14) bad.scalar(bad.first_point,11,bad.first_group);
        if (kind == 15) bad.scalar(bad.first_point,5,0x40);
        const auto descriptors = list_igb_player_placements(bad.document());
        check(descriptors.size() == 1 && !descriptors[0].supported && !descriptors[0].diagnostic.empty(),
            "Malformed or unsupported descriptor remains visible with diagnostic");
        throws([&]{bad.decode();},"Reject malformed or dynamic placement");
    }
    for (const auto index : {0u,7u,15u}) {
        bad = f; word(bad.records[bad.first_point].values[8],index*4,0x7fc00000u);
        throws([&]{bad.decode();},"Reject every non-finite matrix component");
    }
    bad = f; word(bad.records[bad.first_point].values[8],12,std::bit_cast<std::uint32_t>(1.f));
    throws([&]{bad.decode();},"Reject projective placement matrix");
    doc = f.document(); doc.metadata.header.byte_order = IgbByteOrder::big;
    throws([&]{decode_igb_player_placement(doc,f.info,0,0);},"Reject unverified byte order before matrix decoding");
    doc = f.document(); doc.metadata.field_types[7].name = "igVec4fMetaField";
    check(!list_igb_player_placements(doc)[0].supported,"Check matrix field type instead of byte size alone");
    bad = f; bad.text(bad.first_point,2,std::string(4097,'p'));
    throws([&]{bad.decode();},"Bound point labels before copying");
    auto boundary_label = f; boundary_label.text(boundary_label.first_point,2,std::string(4096,'p'));
    check(boundary_label.decode().name.size() == 4096,"Allow the decoded label limit with its stored terminator");
    bad = f; bad.text(bad.first_point,2,std::string(1024u*1024u-1,'\0'));
    bad.contents(bad.first_points,std::vector<std::uint32_t>(4,bad.first_point));
    bad.contents(bad.groups,std::vector<std::uint32_t>(64,bad.first_group));
    const auto padded = list_igb_player_placements(bad.document());
    check(!padded[0].supported && padded[0].diagnostic.find("serialized name") != std::string::npos,
        "Reject oversized NUL-padded field before repeatedly scanning shared points");
    bad = f; bad.text(bad.first_point,2,std::string(4095,'\0'));
    bad.contents(bad.first_points,std::vector<std::uint32_t>(4,bad.first_point));
    bad.contents(bad.groups,std::vector<std::uint32_t>(260,bad.first_group));
    const auto repeated_padding = list_igb_player_placements(bad.document());
    check(!repeated_padding[0].supported && repeated_padding[0].diagnostic.find("serialized name") != std::string::npos,
        "Charge bounded NUL padding each time shared point names are scanned");
    // Shared group references must consume the aggregate budget on each scan.
    bad = f; bad.contents(bad.groups,std::vector<std::uint32_t>(2049,bad.first_group));
    const auto second = bad.add(point_info); bad.text(second,2,"Second");
    bad.scalar(second,7,bad.polygon); bad.scalar(second,8,bad.groups);
    bad.contents(bad.exported_info_list,{bad.info,second});
    const auto bounded = list_igb_player_placements(bad.document());
    check(bounded[0].supported && !bounded[1].supported,"Bound aggregate work across exported descriptors sharing a list");
    auto forged = placement; forged.matrix[15] = 0;
    throws([&]{apply_player_placement(forged,local);},"Revalidate caller-supplied affine matrix");
    forged = placement; forged.matrix[12] = std::numeric_limits<float>::infinity();
    throws([&]{apply_player_placement(forged,{});},"Even empty input rejects invalid matrix");
    const std::vector<Vec3> nan{{std::numeric_limits<float>::quiet_NaN(),0,0}};
    throws([&]{apply_player_placement(placement,nan);},"Reject non-finite local positions");
    const std::vector<Vec3> huge{{std::numeric_limits<float>::max(),0,0}};
    throws([&]{apply_player_placement(placement,huge);},"Reject output float overflow");
    check(apply_player_placement(placement,{}).empty(),"Valid empty vertex span stays empty");
}
void original(const std::filesystem::path& archive_path, std::size_t entry) {
    const auto doc = read_igb_document(assets::AfsArchive::open(archive_path).read(entry));
    for (const auto& info : list_igb_player_placements(doc)) {
        std::cout << "PLACEMENT info=" << info.reference_index << " groups=" << info.group_count << " supported=" << info.supported << '\n';
        if (!info.supported) { std::cout << info.diagnostic << '\n'; continue; }
        for (std::uint32_t g = 0; g < info.group_count; ++g) for (std::uint32_t p = 0; p < 4; ++p) {
            const auto placement = decode_igb_player_placement(doc,info.reference_index,g,p);
            const auto& source = doc.object(placement.transform_reference);
            const auto bytes = doc.field_bytes(*doc.field(source,8));
            for (std::size_t i = 0; i < 16; ++i) {
                const auto at = i*4;
                const auto expected = std::uint32_t(bytes[at]) | (std::uint32_t(bytes[at+1]) << 8) |
                    (std::uint32_t(bytes[at+2]) << 16) | (std::uint32_t(bytes[at+3]) << 24);
                check(std::bit_cast<std::uint32_t>(placement.matrix[i]) == expected,"Original full matrix bits preserved");
            }
            std::cout << " group=" << g << " player=" << p << " point=" << placement.transform_reference
                << " position=" << placement.matrix[12] << ',' << placement.matrix[13] << ',' << placement.matrix[14] << '\n';
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        synthetic(); std::cout << "Player placement tests passed (" << checks << " checks).\n";
        if (argc == 3) original(argv[1],std::stoul(argv[2]));
        else if (argc != 1) { std::cerr << "Usage: dw4_player_placement_tests [AFS ENTRY]\n"; return 2; }
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
