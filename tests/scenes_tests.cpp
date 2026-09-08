#include "dw4/scenes.hpp"
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
    info_list, switch_control, derived_control
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
    {"igNode",object,6,{{0,2,4},{5,5,4}}},
    {"igGroup",node,8,{{0,2,4},{5,5,4},{2,7,4}}},
    {"igAttrSet",group,10,{{0,2,4},{5,5,4},{2,7,4},{2,8,4},{4,9,1}}},
    {"igGeometry",attr_set,10,{{0,2,4},{5,5,4},{2,7,4},{2,8,4},{4,9,1}}},
    {"igTransform",group,12,{{0,2,4},{5,5,4},{2,7,4},{7,8,64},{6,9,4},{2,11,4}}},
    {"igSwitch",group,9,{{0,2,4},{5,5,4},{2,7,4},{2,8,4}}},
    {"igBitMask",object_list,6,{{1,2,4},{1,3,4},{3,4,4},{5,5,4}}},
    {"igInfo",object,3,{{0,2,4}}},
    {"igSceneInfo",info_base,11,{{0,2,4},{2,5,4},{8,10,12}}},
    {"igVertexArray1_1",object,12,{{3,2,4},{5,3,4},{5,4,4},{3,7,4},{3,8,4},{2,10,4}}},
    {"beGroupKeep",group,8,{{0,2,4},{5,5,4},{2,7,4}}},
    {"UnknownGroup",group,8,{{0,2,4},{5,5,4},{2,7,4}}},
    {"igInfoList",object_list,5,{{1,2,4},{1,3,4},{3,4,4}}},
    {"beSwitchCtrlInfo",info_base,8,{{0,2,4},{2,7,4}}},
    {"UnknownSwitchCtrlInfo",switch_control,8,{{0,2,4},{2,7,4}}}
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
    std::uint32_t info, root, owner, geom, vertices, positions, stream, attrs, root_children;
    Fixture() {
        positions = memory(floats({1,0,0, 0,1,0, 0,0,1}));
        Bytes table(80,0xff); word(table,0,positions); stream = memory(std::move(table));
        vertices = add(vertex_array); scalar(vertices,2,stream); scalar(vertices,3,3);
        geom = add(geometry_attr); scalar(geom,4,vertices); scalar(geom,6,3); scalar(geom,7,1);
        owner = add(geometry_node); text(owner,2,"Synthetic geometry"); scalar(owner,9,1);
        attrs = list(attr_list,{geom}); scalar(owner,8,attrs);
        root = add(group); text(root,2,"Synthetic root");
        root_children = list(node_list,{owner}); scalar(root,7,root_children);
        info = add(scene_info); text(info,2,"Synthetic export"); scalar(info,5,root);
        records[info].values[10] = floats({0,1,0});
    }
    std::uint32_t transform(std::vector<std::uint32_t> children, Bytes matrix) {
        const auto ref = add(transform_node); scalar(ref,7,list(node_list,children));
        records[ref].values[8] = std::move(matrix); return ref;
    }
    std::uint32_t mask(std::uint32_t bits, std::initializer_list<std::uint32_t> values) {
        const auto ref = add(bitmask); const auto count = static_cast<std::uint32_t>(values.size());
        scalar(ref,2,count); scalar(ref,3,count); scalar(ref,4,memory(words(values))); scalar(ref,5,bits); return ref;
    }
    std::uint32_t switch_root(const std::vector<std::uint32_t>& children, std::uint32_t selection) {
        root = add(switch_node); scalar(root,7,list(node_list,children)); scalar(root,8,selection); scalar(info,5,root); return root;
    }
    SceneData decode(SceneOptions options = {}) const { return decode_igb_scene(document(),info,options); }
    std::uint32_t controller() {
        const auto ref = add(switch_control); text(ref,2,"Synthetic controller"); scalar(ref,7,root);
        exported_info_list = list(info_list,{info,ref}); return ref;
    }
};
void throws(const Fixture& fixture, const std::string& reason, SceneOptions options = {}) {
    try { fixture.decode(options); }
    catch (const SceneError&) { check(true,reason); return; }
    check(false,reason);
}
void incomplete(const Fixture& fixture, const std::string& reason) {
    const auto data = fixture.decode();
    check(!data.complete && !data.diagnostics.empty(),reason);
    check(!data.diagnostics[0].path.empty() && !data.diagnostics[0].reason.empty(),"Unsupported behavior has an exact path and reason");
}
void synthetic() {
    Fixture f; const auto doc = f.document();
    const auto listed = list_igb_scenes(doc);
    check(listed.size() == 1 && listed[0].supported,"List explicit scene exports");
    check(listed[0].info_reference == f.info && listed[0].root_reference == f.root,"Keep info and root references distinct");
    check(listed[0].name == "Synthetic export" && listed[0].root_type == "igGroup" && listed[0].up_direction.y == 1,"Preserve export metadata");
    auto data = f.decode();
    check(data.complete && data.instances.size() == 1 && data.diagnostics.empty(),"Decode static root");
    check(data.visited_nodes == 2 && data.vertex_count == 3 && data.triangle_count == 1 && data.transform_nodes == 0,"Count visits and geometry");
    check(data.instances[0].path == std::vector<std::uint32_t>{f.root,f.owner},"Preserve explicit root-to-owner material path");
    check(data.instances[0].mesh.positions[0].x == 1 && data.instances[0].mesh.texcoords.empty(),"Identity leaves positions unchanged and UVs explicit");
    check(data.bounds.min.x == 0 && data.bounds.max.z == 1,"Compute aggregate bounds");

    // Noncommuting rotation/translation distinguishes parent*local from local*parent.
    auto nested = f;
    const auto local = nested.transform({nested.owner},floats({1,0,0,0, 0,1,0,0, 0,0,1,0, 2,0,0,1}));
    const auto parent = nested.transform({local},floats({0,1,0,0, -1,0,0,0, 0,0,1,0, 10,20,30,1}));
    nested.contents(nested.root_children,{parent}); data = nested.decode();
    const auto point = data.instances[0].mesh.positions[0];
    check(point.x == 10 && point.y == 23 && point.z == 30,"Compose parent rotation and local translation in original order");
    check(data.transform_nodes == 2 && data.instances[0].world_transform[13] == 22,"Record composed matrix");
    check(data.bounds.min.x == 9 && data.bounds.max.y == 23 && data.bounds.max.z == 31,"World bounds follow every transformed vertex");

    auto dag = f;
    const auto left = dag.transform({dag.owner},floats({2,0,0,0, 0,3,0,0, 0,0,-1,0, -4,0,0,1}));
    const auto right = dag.transform({dag.owner},floats({1,0,0,0, 0,1,0,0, 0,0,1,0, 4,0,0,1}));
    dag.contents(dag.root_children,{left,right}); data = dag.decode();
    check(data.instances.size() == 2 && data.visited_nodes == 5,"Shared DAG node emits one instance per path");
    check(data.instances[0].geometry_reference == data.instances[1].geometry_reference && data.instances[0].path != data.instances[1].path,"Shared geometry retains distinct provenance");
    check(data.instances[0].mesh.positions[0].x == -2 && data.instances[1].mesh.positions[0].x == 5,"Instances have independent transformed positions");
    check(data.bounds.min.x == -4 && data.bounds.max.x == 5 && data.bounds.min.z == -1,"Nonuniform scale and reflection remain valid");
    check(data.vertex_count == 6 && data.triangle_count == 2,"Budgets count instances, not unique source meshes");
    dag.contents(dag.root_children,{dag.owner,dag.owner});
    check(dag.decode().instances.size() == 2,"Repeated ordered edges remain separate draw instances");

    auto skipped = f; skipped.scalar(skipped.owner,9,0); skipped.scalar(skipped.owner,7,skipped.root_children);
    check(skipped.decode().complete && skipped.decode().instances.empty(),"False geometry trigger skips entire branch, including cycle");
    skipped = f; skipped.scalar(skipped.owner,5,0x20);
    check(skipped.decode().instances.empty(),"IS_INVISIBLE suppresses geometry");
    skipped = f; skipped.scalar(skipped.owner,5,28);
    check(skipped.decode().instances.size() == 1,"Flags 4/8/16 are not visibility suppression");
    skipped.scalar(skipped.owner,7,skipped.root_children);
    check(skipped.decode().visited_nodes == 2,"Geometry handler does not traverse its child list");
    auto scoped = f; const auto scope = scoped.add(attr_set);
    scoped.scalar(scope,7,scoped.root_children); scoped.scalar(scoped.info,5,scope);
    check(scoped.decode().instances.size() == 1,"False AttrSet trigger does not suppress child traversal");
    auto kept = f; const auto keep = kept.add(keep_group); kept.scalar(keep,7,kept.root_children); kept.scalar(kept.info,5,keep);
    check(kept.decode().instances.size() == 1,"Verified beGroupKeep uses ordinary ordered child traversal");
    auto leaf = f; leaf.scalar(leaf.info,5,leaf.add(node));
    check(leaf.decode().complete && leaf.decode().visited_nodes == 1 && leaf.decode().instances.empty(),"Exact base igNode is a no-op");

    auto switched = f; const auto mask = switched.mask(3,{2});
    switched.switch_root({switched.owner,switched.owner,switched.owner},mask); data = switched.decode();
    check(data.instances.size() == 1 && data.visited_nodes == 2,"Switch follows only selected stored child bits");
    auto off = f; off.switch_root({off.owner},off.mask(1,{0}));
    check(off.decode().complete && off.decode().instances.empty(),"Disabled stored switch stays empty");
    data = off.decode({0});
    check(data.instances.size() == 1 && data.inspected_root_child == 0,"Explicit root-child inspection is recorded");
    check(off.decode().instances.empty(),"Inspection never mutates stored switch state");
    throws(off,"Out-of-range inspection child rejected",{1});
    throws(f,"Non-switch inspection root rejected",{0});
    auto inner = f; const auto inner_switch = inner.switch_root({inner.owner},inner.mask(1,{0}));
    inner.switch_root({inner_switch},inner.mask(1,{0}));
    check(inner.decode({0}).instances.empty(),"Root inspection preserves nested switch masks");
    auto bits = f; auto children = std::vector<std::uint32_t>(34,bits.owner);
    bits.switch_root(children,bits.mask(34,{0x80000000u,2}));
    check(bits.decode().instances.size() == 2,"Switch bits cross 32-bit boundaries in LSB-first order");
    auto null_child = f; null_child.switch_root({null_ref},null_child.mask(1,{1}));
    check(null_child.decode().instances.empty(),"Stored null child is skipped");
    throws(null_child,"Cannot inspect null root child",{0});

    auto bad = f; bad.scalar(bad.info,5,null_ref); throws(bad,"Reject null export root");
    check(!list_igb_scenes(bad.document())[0].supported,"Malformed export remains listed with diagnostic");
    bad = f; bad.scalar(bad.info,5,bad.geom); throws(bad,"Reject non-node root");
    bad = f; bad.records[bad.info].values[10] = floats({0,std::numeric_limits<float>::infinity(),0}); throws(bad,"Reject non-finite up vector");
    bad = f; bad.contents(bad.root_children,{bad.geom}); throws(bad,"Reject non-node child");
    bad = f; bad.scalar(bad.root,7,bad.attrs); throws(bad,"Reject wrong child list type");
    bad = f; bad.scalar(bad.owner,8,bad.root_children); throws(bad,"Reject wrong attribute list type");
    bad = f; bad.contents(bad.attrs,{bad.owner}); throws(bad,"Reject non-attribute list entry");
    bad = f; bad.contents(bad.root_children,{bad.root}); throws(bad,"Reject active cycle");
    bad = f; bad.scalar(bad.root_children,2,4194305); throws(bad,"Bound child list before allocation");
    bad = f; bad.scalar(bad.owner,9,2); throws(bad,"Reject malformed trigger boolean");
    bad = f; bad.scalar(bad.owner,5,0x40); incomplete(bad,"Dynamic geometry rejected");
    bad = f; bad.scalar(bad.root,5,0x20); incomplete(bad,"Invisible non-geometry propagation is not guessed");
    bad = f; bad.scalar(bad.root,5,0x100); incomplete(bad,"Unknown flags rejected");
    bad = f; const auto unknown = bad.add(unknown_group); bad.scalar(unknown,7,bad.root_children); bad.scalar(bad.info,5,unknown);
    incomplete(bad,"Unknown subclass behavior is not flattened");
    auto multiple = f; multiple.contents(multiple.attrs,{multiple.geom,multiple.geom});
    check(multiple.decode().instances.size() == 2,"Every geometry attribute is emitted in serialized order");
    bad = f; bad.contents(bad.attrs,{}); incomplete(bad,"Geometry owner without geometry remains unsupported");
    bad = f; bad.contents(bad.attrs,{bad.geom,bad.add(attr)}); incomplete(bad,"Unverified attribute behavior remains unsupported");
    bad = f; bad.scalar(bad.vertices,7,bad.positions); incomplete(bad,"Skin weights are not treated as static geometry");
    bad = f; bad.scalar(bad.vertices,8,bad.positions); incomplete(bad,"Skin indices are not treated as static geometry");
    bad = f; bad.scalar(bad.vertices,10,bad.owner); incomplete(bad,"Additional vertex state is not guessed");
    bad = nested; bad.scalar(local,9,1); incomplete(bad,"Nonzero matrix target rejected");
    bad = nested; bad.scalar(local,11,bad.owner); incomplete(bad,"Dynamic transform input rejected");
    bad = nested; word(bad.records[local].values[8],12,std::bit_cast<std::uint32_t>(1.f)); throws(bad,"Projective matrix rejected");
    bad = nested; word(bad.records[local].values[8],0,0x7fc00000u); throws(bad,"Non-finite matrix rejected");
    bad = nested; word(bad.records[local].values[8],0,std::bit_cast<std::uint32_t>(std::numeric_limits<float>::max()));
    word(bad.records[parent].values[8],4,std::bit_cast<std::uint32_t>(std::numeric_limits<float>::max()));
    throws(bad,"World float overflow rejected");

    for (const auto kind : {0,1,2,3,4,5}) {
        bad = f; const auto m = bad.mask(1,{1}); bad.switch_root({bad.owner},m);
        if (kind == 0) bad.scalar(bad.root,8,null_ref);
        if (kind == 1) bad.scalar(bad.root,8,bad.root_children);
        if (kind == 2) bad.scalar(m,5,0);
        if (kind == 3) bad.scalar(m,2,2);
        if (kind == 4) bad.scalar(m,3,0);
        if (kind == 5) bad.scalar(m,4,null_ref);
        throws(bad,"Malformed switch mask rejected");
    }
    bad = f; bad.contents(bad.root_children,std::vector<std::uint32_t>(4097,bad.owner));
    throws(bad,"Instance budget enforced even for shared geometry");
    auto deep = f;
    for (int i = 0; i < 63; ++i) {
        const auto group_ref = deep.add(group); deep.scalar(group_ref,7,deep.list(node_list,{deep.root}));
        deep.root = group_ref; deep.scalar(deep.info,5,group_ref);
    }
    throws(deep,"Depth budget enforced");
    auto partial = f; const auto dynamic = partial.add(unknown_group); partial.contents(partial.root_children,{partial.owner,dynamic});
    data = partial.decode();
    check(!data.complete && data.instances.size() == 1 && data.diagnostics[0].node_reference == dynamic,"Partial traversal is explicitly incomplete");
}

void controls() {
    Fixture f; const auto mask = f.mask(34,{0,0});
    f.switch_root(std::vector<std::uint32_t>(34,f.owner),mask);
    const auto ref = f.controller();
    const SwitchControlRequest on{ref,SwitchControlCommand::all_on}, off{ref,SwitchControlCommand::all_off};
    const SceneOptions activate{std::nullopt,on}, deactivate{std::nullopt,off};
    const auto doc = f.document(); const auto source = doc.bytes();
    const Bytes before(source.begin(),source.end());
    const auto listed = list_igb_switch_controls(doc);
    check(listed.size() == 1 && listed[0].supported,"Only explicit exported controller is eligible");
    check(listed[0].reference_index == ref && listed[0].target_reference == f.root &&
        listed[0].child_count == 34 && listed[0].mask_bit_count == 34,"Keep controller, target and mask provenance");
    const auto selected = evaluate_igb_switch_control(doc,on);
    check(selected.selected_children.size() == 34 && std::count(selected.selected_children.begin(),selected.selected_children.end(),true) == 34,
        "ALLON enables children across word boundary without adding tail bits as children");
    check(f.decode(activate).instances.size() == 34,"Original controller selects every root child");
    const auto result = decode_igb_scene(doc,f.info,activate);
    check(result.root_control && result.root_control->controller_reference == ref && !result.inspected_root_child,
        "Decoded scene retains controller command provenance");
    check(f.decode(deactivate).complete && f.decode(deactivate).instances.empty(),"ALLOF produces a complete empty scene");
    check(f.decode().instances.empty(),"Controller command does not persist into stored selection");
    check(std::equal(before.begin(),before.end(),doc.bytes().begin(),doc.bytes().end()),"Commands leave original source bytes unchanged");
    throws(f,"Reject command combined with manual inspection",{0,on});
    throws(f,"Reject an unexported controller reference",{std::nullopt,SwitchControlRequest{f.root,SwitchControlCommand::all_on}});
    throws(f,"Reject unknown command enum",{std::nullopt,SwitchControlRequest{ref,static_cast<SwitchControlCommand>(99)}});
    auto bad = f; bad.scalar(bad.info,5,bad.owner); throws(bad,"Reject controller for another selected root",activate);
    bad = f; bad.exported_info_list = null_ref; throws(bad,"No object scan fallback without explicit exports",activate);
    check(list_igb_switch_controls(bad.document()).empty(),"No info list yields no eligible controllers");
    bad = f; bad.contents(bad.exported_info_list,{bad.info}); throws(bad,"Unexported but present controller is ineligible",activate);
    bad = f; bad.contents(bad.exported_info_list,{ref,ref}); throws(bad,"Reject repeated controller export",activate);
    bad = f; bad.contents(bad.exported_info_list,{null_ref}); throws(bad,"Reject null info export",activate);
    bad = f; bad.contents(bad.exported_info_list,{bad.owner}); throws(bad,"Reject non-info export",activate);
    bad = f; bad.scalar(bad.exported_info_list,3,4097); throws(bad,"Bound info-list capacity before reading",activate);
    bad = f; bad.records[ref].type = derived_control;
    check(!list_igb_switch_controls(bad.document())[0].supported,"Unverified derived controller stays visible with diagnostic");
    throws(bad,"Reject derived controller execution",activate);
    for (int kind = 0; kind < 11; ++kind) {
        bad = f;
        if (kind == 0) bad.scalar(ref,7,null_ref);
        if (kind == 1) bad.scalar(ref,7,bad.owner);
        if (kind == 2) bad.scalar(bad.root,8,null_ref);
        if (kind == 3) bad.scalar(mask,5,33);
        if (kind == 4) bad.scalar(mask,2,1);
        if (kind == 5) bad.scalar(mask,3,1);
        if (kind == 6) bad.scalar(mask,4,null_ref);
        if (kind == 7) bad.scalar(mask,5,4194305);
        if (kind == 8) bad.scalar(mask,3,131073);
        if (kind == 9) bad.scalar(bad.root,7,bad.attrs);
        if (kind == 10) bad.scalar(mask,4,bad.positions);
        const auto values = list_igb_switch_controls(bad.document());
        check(values.size() == 1 && !values[0].supported && !values[0].diagnostic.empty(),"Malformed controller reports diagnostic");
        throws(bad,"Malformed controller rejected even when turning off",deactivate);
    }
    Fixture nested; const auto inner = nested.switch_root({nested.owner},nested.mask(1,{0}));
    nested.switch_root({inner},nested.mask(1,{0})); const auto nested_ref = nested.controller();
    check(nested.decode({std::nullopt,SwitchControlRequest{nested_ref,SwitchControlCommand::all_on}}).instances.empty(),
        "ALLON of root leaves nested stored switch disabled");
    Fixture sparse; sparse.switch_root({null_ref,sparse.owner},sparse.mask(2,{0})); const auto sparse_ref = sparse.controller();
    check(sparse.decode({std::nullopt,SwitchControlRequest{sparse_ref,SwitchControlCommand::all_on}}).instances.size() == 1,
        "Null child slots remain skipped after ALLON");
    Fixture empty; empty.switch_root({},empty.mask(0,{})); const auto empty_ref = empty.controller();
    check(empty.decode({std::nullopt,SwitchControlRequest{empty_ref,SwitchControlCommand::all_on}}).complete,
        "Zero-bit empty controller target is valid");
    Fixture budget;
    // Repeated references need little file space but still consume validation
    // work. Bound the sum across controllers, not just each individual target.
    budget.switch_root(std::vector<std::uint32_t>(1048576,budget.owner),null_ref);
    const auto large_mask = budget.add(bitmask);
    budget.scalar(large_mask,2,32768); budget.scalar(large_mask,3,32768); budget.scalar(large_mask,5,1048576);
    budget.scalar(large_mask,4,budget.memory(Bytes(32768*4))); budget.scalar(budget.root,8,large_mask);
    const auto a = budget.controller(), b = budget.controller(), c = budget.controller(), d = budget.controller(), e = budget.controller();
    budget.contents(budget.exported_info_list,{a,b,c,d,e});
    const auto bounded = list_igb_switch_controls(budget.document());
    check(bounded.size() == 5 && bounded[0].supported && bounded[3].supported && !bounded[4].supported &&
        bounded[4].diagnostic.find("aggregate") != std::string::npos,"Repeated large controller targets respect aggregate work budget");
}

void original(const std::filesystem::path& archive_path, std::size_t entry) {
    const auto doc = read_igb_document(assets::AfsArchive::open(archive_path).read(entry));
    const auto infos = list_igb_scenes(doc);
    for (const auto& controller : list_igb_switch_controls(doc)) {
        if (!controller.supported) continue;
        const auto scene = std::find_if(infos.begin(),infos.end(),[&](const auto& info) { return info.root_reference == controller.target_reference; });
        if (scene == infos.end()) continue;
        const auto on = decode_igb_scene(doc,scene->info_reference,{std::nullopt,SwitchControlRequest{controller.reference_index,SwitchControlCommand::all_on}});
        const auto off = decode_igb_scene(doc,scene->info_reference,{std::nullopt,SwitchControlRequest{controller.reference_index,SwitchControlCommand::all_off}});
        check(off.complete && off.instances.empty(),"Original controller ALLOF is complete and empty");
        std::cout << "CONTROL ref=" << controller.reference_index << " target=" << controller.target_reference
            << " ALLON complete=" << on.complete << " instances=" << on.instances.size() << " triangles=" << on.triangle_count << '\n';
        if (controller.child_count == 1 && on.complete) {
            const auto inspected = decode_igb_scene(doc,scene->info_reference,{0});
            check(on.instances.size() == inspected.instances.size(),"Original ALLON agrees with one-child independent inspection");
            for (std::size_t i = 0; i < on.instances.size(); ++i) {
                const auto& a = on.instances[i]; const auto& b = inspected.instances[i];
                check(a.path == b.path && a.geometry_reference == b.geometry_reference && a.world_transform == b.world_transform &&
                    a.mesh.indices == b.mesh.indices,"Original activation preserves instance identity and transforms");
            }
        }
    }
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const auto& info = infos[i];
        std::cout << "SCENE index=" << i << " info=" << info.info_reference << " root=" << info.root_reference << " name=" << info.name << '\n';
        if (!info.supported) { std::cout << " unsupported=" << info.diagnostic << '\n'; continue; }
        for (const bool inspect : {false,true}) {
            if (inspect && info.root_type != "igSwitch") continue;
            try {
                const auto data = decode_igb_scene(doc,info.info_reference,inspect ? SceneOptions{0} : SceneOptions{});
                std::cout << " inspection=" << inspect << " complete=" << data.complete << " visits=" << data.visited_nodes
                    << " transforms=" << data.transform_nodes << " instances=" << data.instances.size() << " vertices=" << data.vertex_count
                    << " triangles=" << data.triangle_count << " diagnostics=" << data.diagnostics.size() << '\n';
                for (std::size_t d = 0; d < std::min<std::size_t>(data.diagnostics.size(),8); ++d)
                    std::cout << " node=" << data.diagnostics[d].node_reference << " reason=" << data.diagnostics[d].reason << '\n';
            } catch (const SceneError& error) { std::cout << " error=" << error.what() << '\n'; }
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        synthetic(); controls(); std::cout << "Scene tests passed (" << checks << " checks).\n";
        if (argc == 3) original(argv[1],std::stoul(argv[2]));
        else if (argc != 1) { std::cerr << "Usage: dw4_scenes_tests [AFS ENTRY]\n"; return 2; }
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
