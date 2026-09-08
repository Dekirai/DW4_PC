# Native stored base-image materials

The `materials` module resolves an original geometry's explicit texture
binding. It supports a single local base image on texture unit zero with the
verified linear/repeat sampler profile. The native viewer combines this result
with the mesh's original UVs and the native texture decoder to preview textured
stored geometry, including the high and low Agumon appearances.

This is a bounded asset inspection feature. It does not execute the original
material pipeline, lighting, GX texture environment, skinning, scene transforms,
or animation. Its output establishes a stored image association and sampler;
matching the final in-game rendering remains separate work.

## API and reference ownership

```cpp
auto material = dw4::resolve_igb_material(document, geometry_reference);
if (material.supported) {
    auto mesh = dw4::decode_igb_mesh(document, geometry_reference);
    mesh.texcoords = dw4::decode_igb_texcoords(document, geometry_reference);
    auto image = dw4::decode_igb_texture(document, *material.image_reference);
    // Upload mesh and image with the reported sampler in the rendering layer.
}
```

`MaterialInfo` reports support status, a diagnostic when unsupported, the
selected geometry reference, owner, binding and enable-state scope references,
the owner-to-scope path, attribute-list/bind/texture/image references, original names, and
the raw and interpreted sampler values. All reference numbers belong to the
same owning `IgbDocument`. Archive name and entry index remain the caller's
source identity; a reference from a different entry must never be reused.

The resolver does not choose the first image, match filenames, or substitute
a nearby material. Unsupported binding state returns `supported == false`.
UV and pixel decoding remain explicit operations which may also reject their
inputs. The viewer can keep solid geometry available with the reason visible;
an explicitly requested textured mode requires the complete supported chain.

## Stored-geometry lookup

The selected object must be an `igGeometryAttr1_5`. Exactly one `igGeometry`
node must explicitly contain it in its slot-8 `igAttrList`. The resolver checks
that node's attributes, then follows a unique parent path through `igGroup`
slot-7 `igNodeList` references until it reaches the nearest complete texture
scope. Cycles, multiple geometry owners, or multiple parents below that scope
are unsupported. Ancestors above a complete binding scope are outside this
narrow stored-binding result.

Attribute lists must contain `igAttr` objects, and child lists must contain
`igNode` objects. A complete supported scope has exactly one enabled
`igTextureStateAttr` and one `igTextureBindAttr`, both selecting unit zero.
Partial state is not combined across different scopes. Additional `igTexture*`,
`igMultiTexture*`, and `igTexGen*` state is rejected, including texture matrices
and alternate coordinate sources. The `igAttrSet::_trigger` boolean is not
interpreted as an inheritance override. Original code pushes attribute state
before testing this flag and traverses children in either case.

## Explicit scene-instance lookup

Static scene instances carry a checked root-to-owner node path. Resolve their
material in that context instead of searching globally for a unique parent:

```cpp
auto material = dw4::resolve_igb_material_on_path(
    document, instance.geometry_reference, instance.path);
```

Every supplied edge must connect through a valid `igNodeList`, and the final
`igGeometry` must uniquely contain the selected geometry attribute. A shared
node may have other parents outside this path; those instances do not supply
state to the selected instance. State above the supplied root is not inferred.
Scene traversal separately determines whether the path is active and supported;
material resolution does not select switch children or execute node handlers.

On an explicit path, the nearest unit-zero `igTextureBindAttr` and the nearest
unit-zero `igTextureStateAttr` are selected independently. A binding on a child
can inherit its enabled state from an ancestor, or a child's state can enable
an inherited binding. A nearest disabled state rejects textured rendering; an
older enabled state cannot bypass it. Both attributes must be present somewhere
on the supplied path.

Every scope on the full path is inspected, including ancestors above a local
binding. Texture matrices, generated/alternate coordinates, additional texture
state and other texture units are unsupported anywhere on that path. A local
binding cannot clear those inherited effects. Duplicate bindings or enable
states within one scope are rejected as ambiguous. Valid unit-zero states that
are shadowed by a nearer state are permitted, including a disabled ancestor
overridden by an enabled child. Invalid attribute lists, boolean values and
binding reference types still reject even when shadowed.

`scope_node_reference` identifies the binding scope;
`state_scope_node_reference` identifies the selected enable-state scope.
`attribute_list_reference` belongs to the binding scope. `inheritance_path`
runs from the owner through the farther of the two selected scopes, and
`inherited` is true if either state is inherited. For the stored-geometry
resolver both scopes remain identical, preserving its conservative same-scope
behavior.

The original USA `igAttrSet` handler at guest address `0x801DD7C0` supplies the
execution evidence. Instructions from `0x801DD7FC` through `0x801DD888` push
attributes to independent stacks selected by attribute type and unit/index.
The trigger test at `0x801DD898` occurs afterward. A false trigger skips the
dispatch block, but both branches traverse children at `0x801DD8CC` and restore
the pushed stacks afterward. These relationships justify independently
inherited binding and enable state without implementing the full material
pipeline. Original instruction bytes and generated disassembly remain local.

For the original BG02 sample in `ndmw.afs` entry 179, scene descriptor 3372,
explicit inspection of root child 0 produces 235 static geometry instances.
All 235 resolve with this path-aware profile and have matching finite UV arrays.
Their five unique base images decode to 528,384 RGBA bytes in total. This checks
the selected inspection subtree; it does not establish that the original actor
state activates that root child or reproduce original lighting and transparency.

## Texture object profile

The binding's slot 4 must reference an `igTextureAttr` with this measured profile:

| Slot | Recovered field | Required value |
| ---: | --- | --- |
| 4 | `_bColor` | 0; broader semantics remain unverified |
| 5 / 6 | `_magFilter` / `_minFilter` | 1 / 1, `LINEAR` |
| 7 / 8 | `_wrapS` / `_wrapT` | 1 / 1, `REPEAT` |
| 10 | `_mipmapMode` | 0 |
| 11 | `_source` | 0, `SOURCE_IMAGE` |
| 12 | `_image` | Explicit local `igImage` reference |
| 13 | `_paging` | False |
| 14 | `_tu` | Null |
| 15 | `_imageCount` | 1 |
| 16 | `_imageMipMaps` | Explicit empty `igImageMipMapList` |

The original USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, supplies primary field-name and
enum evidence. The texture field-name table starts at file offset `0x4755e4`
(guest address `0x804785e4`); the enum pairs for `LINEAR`, `REPEAT`, and
`SOURCE_IMAGE` occur at file offsets `0x48c300`, `0x48c330`, and `0x48c348`.
Detailed read-only findings are in [MATERIAL_RESEARCH.md](MATERIAL_RESEARCH.md).
No original tables, texture pixels, or UV buffers are included in source control.

The native preview samples the decoded base level with linear filtering and
repeat wrapping, preserving the decoded UVs. It does not claim mipmap execution,
material tint, lighting, alpha-test/blend state, or final texture-environment
colors. The public sampler enums reserve other modes for future verified
profiles; this resolver currently accepts only the table above.

## Original Agumon proof

The resource catalog maps `_common/p_agumon.igb` to `ndmw.afs`, entry 850.
The high appearance is mesh-list index 2, geometry reference 5665. Its chain is:

```text
geometry attribute 5665
  <- geometry node 409, attribute list 411
  <- parent blend node 5481, child list 5482
  -> inherited attribute list 408
  -> texture bind 414 -> texture attribute 405 -> image 5609
```

Image 5609 is `P_agumon01.dds`, a 256 by 256 BC1 texture. Vertex array 5621
references stream table 5500; element 11 references memory 6434, which contains
10,968 bytes for 1,371 float pairs. U ranges approximately from `0.00390625` to
`0.998081`; V ranges from `-0.000822544` to `0.992188`. The small negative V is
preserved and uses repeat wrapping.

An independent local Pillow DDS decode of the same BC1 payload matched every
native RGBA byte. The image has yellow/orange artwork and fully opaque alpha.
This checks the native pixel decoder; it does not by itself validate the
original scene's complete material execution or final displayed colors.

The low appearance, mesh-list index 0 / geometry 262, resolves to texture
5491 and image 5597, `P_agumon02.dds` at 64 by 64 pixels, with 570 UV pairs.
The shadow, mesh-list index 1 / geometry 413, is deliberately unsupported by
this material profile because its texture binding/state is incomplete or
ambiguous within one scope. Its solid stored-geometry preview remains available.

## Corpus checks and limits

On 2026-09-08, an independent native read-only census checked every geometry
in both original USA archives with the stored-geometry material and UV APIs.
These counts describe the original same-scope lookup, not the later explicit
scene-path resolver.

| Archive | IGB files | Geometries checked | Supported bindings | Geometries with valid UVs | Bound images decoded |
| --- | ---: | ---: | ---: | ---: | ---: |
| `area.afs` | 1,379 | 152,896 | 2,687 | 8,024 | 694 |
| `ndmw.afs` | 894 | 45,172 | 521 | 38,964 | 480 |
| Total | 2,273 | 198,068 | 3,208 | 46,988 | 1,174 |

Every supported binding had valid, correctly sized finite UVs. All associated
images decoded without error. Images are counted once per document/reference,
so repeated content in separate IGBs is counted again. The 151,080 UV failures
were absent stream 11; no present stream failed size or finiteness checks.

Unsupported materials were reported explicitly: 161,361 incomplete or
ambiguous scope states, 22,653 ambiguous parent paths, 6,143 texture-matrix
states, 4,592 missing complete ancestor bindings, and 111 alternate texture-
coordinate-source states. These counts describe the current conservative
profile, not corrupt original assets.

Traversal is capped at 64 nodes along an inheritance path and 4,194,304
inspected list elements per resolution. Document reference/field/input limits
also apply. The mesh reader separately bounds UV allocation to 1,048,576
float pairs and rejects non-finite components. The texture reader keeps its
64 MiB decoded-pixel limit. No external texture file is opened by the resolver.

## Reproduce validation

Run the 96 synthetic graph checks, which require no game files:

```powershell
.\build\Release\dw4_materials_tests.exe
```

Generated IGB fixtures exercise explicit ownership, direct and inherited
bindings, exact provenance, a deliberately unrelated first image, ambiguous
owners and parents, cycles and the depth cap, invalid list member types,
disabled/incomplete/duplicate texture state, extra texture transforms, sampler
guards, and null/wrong-type image/mipmap references. The separate mesh suite
contains UV byte-size and finite-value regressions. All checks remain active
in Release builds.

Explicit-path fixtures additionally cover shared instances with different
materials, independently inherited binding/state, distinct scope provenance,
disabled-state overrides, whole-path texture-modifier rejection and malformed
or duplicate attributes above a complete local binding. The stored-geometry
lookup retains its existing tests and behavior.

Inspect every geometry in the original Agumon sample; supported bindings also
decode UVs and the explicitly referenced image, while unsupported ones print
their diagnostic:

```powershell
.\build\Release\dw4_materials_tests.exe 'E:\path\files\ndmw.afs' 850
```

Require the high appearance to resolve and decode successfully:

```powershell
.\build\Release\dw4_materials_tests.exe 'E:\path\files\ndmw.afs' 850 5665
```

The native viewer's mesh page can select archive `ndmw.afs`, entry 850,
mesh index 2, then Textured mode. It shows source identity and material
provenance alongside the stored-geometry preview. Local corpus logs and
screenshots remain ignored; original files are optional integration inputs,
never CI fixtures or redistribution content.
