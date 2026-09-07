#include "dw4/materials.hpp"
#include "dw4/assets.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/meshes.hpp"
#include "dw4/textures.hpp"

#include <algorithm>
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
    node_list, mip_list, node, group, attr_set, geometry_node, texture_state,
    texture_bind, texture_attr, image_type, texgen_attr, extra_texture_attr
};
struct Schema {
    std::string name;
    std::uint32_t parent, slots;
    std::vector<IgbObjectField> fields;
};
const std::vector<Schema> schemas{
    {"igObject",null_ref,2,{}},
    {"igObjectDirEntry",object,13,{{0,2,4},{1,11,4},{1,12,4}}},
    {"igMemoryDirEntry",object,14,{{0,2,4},{1,7,4},{1,10,4},{4,11,1},{1,12,4},{1,13,4}}},
    {"igAttr",object,2,{}},
    {"igGeometryAttr1_5",attr,14,{}},
    {"igObjectList",object,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igAttrList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igNodeList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igImageMipMapList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"igNode",object,3,{{0,2,4}}},
    {"igGroup",node,8,{{0,2,4},{2,7,4}}},
    {"igAttrSet",group,10,{{0,2,4},{2,7,4},{2,8,4},{4,9,1}}},
    {"igGeometry",attr_set,10,{{0,2,4},{2,7,4},{2,8,4},{4,9,1}}},
    {"igTextureStateAttr",attr,6,{{4,4,1},{1,5,4}}},
    {"igTextureBindAttr",attr,6,{{2,4,4},{1,5,4}}},
    {"igTextureAttr",attr,17,{{5,4,4},{6,5,4},{6,6,4},{6,7,4},{6,8,4},{6,10,4},{6,11,4},{2,12,4},{4,13,1},{2,14,4},{1,15,4},{2,16,4}}},
    {"igImage",object,23,{{0,22,4}}},
    {"igTexGenMatrixAttr",attr,2,{}},
    {"igTextureMatrixAttr",attr,2,{}}
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
            "igMemoryRefMetaField","igBoolMetaField","igUnsignedIntMetaField","igEnumMetaField"};
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
            static_cast<std::uint32_t>(field_names.size()),0xfada,0x20000006});
        append(bytes,fields); push32(bytes,4); append(bytes,types); append(bytes,entries);
        append(bytes,words({8 + 2 * count,count}));
        for (std::uint32_t i = 0; i < count; ++i) push16(bytes,static_cast<std::uint16_t>(i));
        append(bytes,objects); append(bytes,memories); return read_igb_document(std::move(bytes));
    }
};

struct Fixture : Graph {
    std::uint32_t geom, unrelated_image, image, texture, state, bind, owner, scope, owner_attrs, scope_attrs, scope_children, mipmaps;
    explicit Fixture(bool direct = false) {
        geom = add(geometry_attr);
        unrelated_image = add(image_type); text(unrelated_image,22,"unrelated-first.dds");
        image = add(image_type); text(image,22,"explicit-target.dds");
        texture = add(texture_attr); scalar(texture,5,1); scalar(texture,6,1); scalar(texture,7,1); scalar(texture,8,1);
        scalar(texture,12,image); scalar(texture,15,1); mipmaps = list(mip_list,{}); scalar(texture,16,mipmaps);
        state = add(texture_state); scalar(state,4,1);
        bind = add(texture_bind); scalar(bind,4,texture);
        owner = add(geometry_node); text(owner,2,"Synthetic geometry"); scalar(owner,9,1);
        owner_attrs = list(attr_list,direct ? std::vector<std::uint32_t>{geom,state,bind} : std::vector<std::uint32_t>{geom});
        scalar(owner,8,owner_attrs);
        scope = add(attr_set); text(scope,2,"Synthetic inherited scope");
        scope_children = list(node_list,{owner}); scalar(scope,7,scope_children);
        scope_attrs = list(attr_list,{state,bind}); scalar(scope,8,scope_attrs);
    }
    MaterialInfo resolve() const { return resolve_igb_material(document(),geom); }
};

void rejected(const Fixture& fixture, const std::string& reason) {
    const auto result = fixture.resolve();
    check(!result.supported && !result.diagnostic.empty(),reason);
}

void synthetic() {
    Fixture f;
    auto result = f.resolve();
    check(result.supported,"Resolve inherited single-stage material: " + result.diagnostic);
    check(result.geometry_reference == f.geom && result.geometry_node_reference == f.owner,"Preserve geometry ownership");
    check(result.scope_node_reference == f.scope && result.inherited,"Record inherited scope");
    check(result.inheritance_path == std::vector<std::uint32_t>{f.owner,f.scope},"Record unique parent path");
    check(result.attribute_list_reference == f.scope_attrs && result.texture_bind_reference == f.bind,"Record attribute and bind references");
    check(result.texture_reference == f.texture && result.image_reference == f.image,"Use explicit image reference, not first image");
    check(result.image_name == "explicit-target.dds" && result.geometry_node_name == "Synthetic geometry","Read target names");
    check(result.wrap_s == MaterialWrap::repeat && result.wrap_t == MaterialWrap::repeat &&
        result.min_filter == MaterialFilter::linear && result.mag_filter == MaterialFilter::linear,"Map verified sampler enums");
    check(!result.provenance.empty() && result.diagnostic.empty(),"Supported result has provenance");
    Fixture direct(true); result = direct.resolve();
    check(result.supported && !result.inherited && result.scope_node_reference == direct.owner,"Resolve direct binding before ancestors");
    check(result.inheritance_path == std::vector<std::uint32_t>{direct.owner},"Stop at complete local scope");
    auto parent = direct.add(group); direct.scalar(parent,7,direct.scope_children);
    check(direct.resolve().supported,"Ancestors above complete local binding do not change this narrow result");
    auto changed_trigger = f; changed_trigger.scalar(changed_trigger.owner,9,0); changed_trigger.scalar(changed_trigger.scope,9,1);
    check(changed_trigger.resolve().supported,"Do not reinterpret _trigger as a texture override flag");

    for (auto slot : {5u,6u,7u,8u}) {
        auto bad = f; bad.scalar(bad.texture,static_cast<std::uint16_t>(slot),0); rejected(bad,"Reject unverified sampler enum");
    }
    for (auto slot : {4u,10u,11u}) {
        auto bad = f; bad.scalar(bad.texture,static_cast<std::uint16_t>(slot),1); rejected(bad,"Reject unverified color/mipmap/source state");
    }
    for (auto value : {0u,2u,null_ref}) {
        auto bad = f; bad.scalar(bad.texture,15,value); rejected(bad,"Reject non-single image count");
    }
    for (auto value : {0u,2u}) {
        auto bad = f; bad.scalar(bad.state,4,value); rejected(bad,"Reject disabled or invalid texture-state boolean");
    }
    for (auto target : {f.state,f.bind}) {
        auto bad = f; bad.scalar(target,5,1); rejected(bad,"Reject nonzero texture units");
    }
    auto bad = f; bad.scalar(bad.texture,13,1); rejected(bad,"Reject paged images");
    bad = f; bad.scalar(bad.texture,14,bad.image); rejected(bad,"Reject texture-unit object state");
    bad = f; bad.contents(bad.mipmaps,{bad.image}); rejected(bad,"Reject explicit mipmap chains");
    bad = f; bad.scalar(bad.texture,16,null_ref); rejected(bad,"Require explicit empty mipmap list");
    bad = f; bad.scalar(bad.texture,16,bad.scope_attrs); rejected(bad,"Reject wrong mipmap list type");
    bad = f; bad.scalar(bad.bind,4,null_ref); rejected(bad,"Reject missing texture reference");
    bad = f; bad.scalar(bad.bind,4,bad.image); rejected(bad,"Reject wrong bound texture type");
    bad = f; bad.scalar(bad.texture,12,null_ref); rejected(bad,"Reject null local image");
    bad = f; bad.scalar(bad.texture,12,bad.bind); rejected(bad,"Reject wrong image object type");
    bad = f; bad.contents(bad.scope_attrs,{bad.state,bad.bind,bad.bind}); rejected(bad,"Reject duplicate bind state");
    bad = f; bad.contents(bad.scope_attrs,{bad.state,bad.state,bad.bind}); rejected(bad,"Reject duplicate texture enable state");
    bad = f; bad.contents(bad.scope_attrs,{bad.bind}); rejected(bad,"Reject incomplete bind without enable state");
    bad = f; bad.contents(bad.scope_attrs,{bad.state}); rejected(bad,"Reject incomplete enable state without bind");
    bad = f; bad.contents(bad.owner_attrs,{bad.geom,bad.state}); rejected(bad,"Do not combine partial state across scopes");
    bad = f; bad.contents(bad.scope_attrs,{bad.state,bad.bind,bad.image}); rejected(bad,"Reject non-attribute list member");
    for (auto type : {texgen_attr,extra_texture_attr}) {
        bad = f; const auto extra = bad.add(type); bad.contents(bad.scope_attrs,{bad.state,bad.bind,extra});
        rejected(bad,"Reject additional texture transform state");
    }
    bad = f; bad.contents(bad.owner_attrs,{}); rejected(bad,"Reject unowned geometry");
    bad = f; const auto extra_owner = bad.add(geometry_node); bad.scalar(extra_owner,8,bad.owner_attrs);
    rejected(bad,"Reject multiple geometry owners");
    bad = f; const auto extra_parent = bad.add(group); bad.scalar(extra_parent,7,bad.scope_children);
    rejected(bad,"Reject multiple parent paths below binding");
    bad = f; bad.scalar(bad.scope,7,null_ref); rejected(bad,"Reject missing ancestor binding path");
    bad = f; bad.contents(bad.scope_children,{bad.owner,bad.image}); rejected(bad,"Reject non-node child list member");
    bad = f; bad.scalar(bad.scope,7,bad.owner_attrs); rejected(bad,"Reject wrong node list type");
    bad = f; bad.scalar(bad.owner,8,bad.scope_children); rejected(bad,"Reject wrong attribute list type");
    bad = f; bad.contents(bad.scope_attrs,{}); const auto children = bad.list(node_list,{bad.scope});
    bad.scalar(bad.owner,7,children); rejected(bad,"Reject parent cycle");
    bad = f; bad.contents(bad.scope_attrs,{}); auto child = bad.scope;
    for (int i = 0; i < 64; ++i) {
        const auto ancestor = bad.add(group); const auto child_list = bad.list(node_list,{child});
        bad.scalar(ancestor,7,child_list); child = ancestor;
    }
    rejected(bad,"Bound inheritance traversal depth");
    check(!resolve_igb_material(f.document(),f.image).supported,"Reject selected non-geometry object");
    check(!resolve_igb_material(f.document(),99999).supported,"Reject invalid selected reference");
}

void inspect_original(const IgbDocument& document, std::uint32_t geometry, bool require_supported) {
    const auto material = resolve_igb_material(document,geometry);
    std::cout << "geometry=" << geometry << " supported=" << material.supported << " " << material.diagnostic << '\n';
    if (!material.supported) {
        check(!require_supported,"Requested original sample material must resolve"); return;
    }
    auto mesh = decode_igb_mesh(document,geometry); mesh.texcoords = decode_igb_texcoords(document,geometry);
    check(mesh.texcoords.size() == mesh.positions.size(),"Original UV count matches stored positions");
    const auto pixels = decode_igb_texture(document,*material.image_reference);
    check(pixels.rgba.size() == std::size_t(pixels.width) * pixels.height * 4,"Original bound image decodes");
    float min_u = mesh.texcoords[0].u, max_u = min_u, min_v = mesh.texcoords[0].v, max_v = min_v;
    for (const auto uv : mesh.texcoords) {
        min_u = std::min(min_u,uv.u); max_u = std::max(max_u,uv.u); min_v = std::min(min_v,uv.v); max_v = std::max(max_v,uv.v);
    }
    std::cout << material.provenance << "\nimage=" << material.image_name << " dimensions=" << pixels.width << 'x' << pixels.height
        << " vertices=" << mesh.positions.size() << " UV_u=[" << min_u << ',' << max_u << "] UV_v=[" << min_v << ',' << max_v << "]\n";
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        synthetic();
        if (args.size() == 3 || args.size() == 4) {
            const auto archive = assets::AfsArchive::open(args[1]);
            const auto document = read_igb_document(archive.read(std::stoul(args[2].string())));
            if (args.size() == 4) inspect_original(document,static_cast<std::uint32_t>(std::stoul(args[3].string())),true);
            else for (const auto& mesh : list_igb_meshes(document)) inspect_original(document,mesh.reference_index,false);
        } else if (args.size() != 1) throw std::runtime_error("Usage: dw4_materials_tests [archive.afs entry [geometry-reference]]");
        std::cout << checks << " material checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "Material test failure: " << error.what() << '\n'; return 1; }
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    std::vector<std::filesystem::path> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    return run(args);
}
