# Explicit player visual binding

The portable player-visual reader binds one explicitly selected player model,
instance and appearance branch to its original mesh and skinning data. It
provides the source for a placed animated-actor inspection. Player generation,
automatic LOD, controller updates, movement, collision and combat remain
unimplemented.

## Verified ownership chain

Only exact `beModelCtrlInfo` records on the document's explicit `igInfoList`
whose slot 7 is exactly `KIND_PLAYER` are player candidates. Their slot-6
`beBaseInfoRamList` supplies the instance ordinals. An arbitrary model-node
object found elsewhere is not a substitute for this exported ownership chain.

The selected instance follows:

1. Exact `beBaseInfoRam`, slot 10, to its `beBaseInfoDataList`.
2. Exactly one `beModelCtrlInfoRamData`, named exactly `ONE`, whose slot 4
   references an exact `beModelCtrlNode2`. Additional RAM visual data is outside
   this first profile.
3. The model node's single child, an exact `igLod`.
4. The caller's explicit LOD child index, selecting an exact `igActor`.
5. Actor slot 11 to exact `igAppearance`, then appearance slot 3 to exact
   `igSkin`.
6. Skin slot 3 to exact `igBlendMatrixSelect`, then its single `igGeometry`
   child, containing exactly one `igGeometryAttr1_5`.

The executable metadata traces include `beModelCtrlInfoRamData::_node`
registration at `0x802C8FC0`, actor registration at `0x801CE294`, and appearance
registration at `0x801C9208`. The local USA executable SHA-1 is
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Serialized field indices are used
to read an owned IGB document; guest addresses never become host pointers.

For `_common/p_agumon.igb`, `ndmw.afs` entry 850, the verified chain for request
`{1386, 0, 1}` is:

| Relationship | Reference |
| --- | ---: |
| Exported `beModelCtrlInfo`, `p_agumon`, `KIND_PLAYER` | 1386 |
| Four-instance RAM list | 5565 |
| Instance 0 RAM record | 1725 |
| Instance data list | 6435 |
| `ONE` visual record | 1724 |
| `beModelCtrlNode2` | 6390 |
| `igLod` | 6393 |
| Explicit branch 1 `igActor` | 6443 |
| `igAppearance` | 6444 |
| `igSkin`, `HighAppearance` | 404 |
| `igBlendMatrixSelect` | 5481 |
| `igGeometry` | 409 |
| Mesh geometry attribute | 5665 |

The LOD has three original branches. Branch 0 reaches shadow geometry 413;
branch 1 reaches high geometry 5665; branch 2 reaches low geometry 262. The
reader uses these ordinals only when explicitly selected. It does not infer
distance thresholds, assume that the first branch is the main character, or
select a branch by its diagnostic name. Instance 3 has model node 6590 and
can resolve the same high geometry through its own actor instance.

## Transform and state boundary

The authored model-node matrix is checked for finite affine values. It is not
applied to the returned source mesh. The original player placement operation
replaces that outer matrix with the selected `PLAYERPOINT` state, as traced in
[PLAYER_PLACEMENT_RESEARCH.md](PLAYER_PLACEMENT_RESEARCH.md). For example, the
sample instance's authored x translation is about `-26.5022`; adding this to a
player point would produce an extra translation that the original replacement
operation does not retain.

Transforms below that replacement boundary cannot be discarded. This first
profile consequently requires the actor's slot-14 matrix and both selector
matrices to be exactly identity, accepting signed zero. Additional transform
targets, model attributes, actor animation state, child attachments and
animation modifiers are rejected. Appearance attachment lists in slots 4–7
must be present, correctly typed and empty. A different graph or additional
skin does not silently fall back to its first supported component.

Node flags outside the existing lower five-bit profile are rejected. The
geometry trigger must be explicitly true; invisible or dynamic geometry does
not become visible merely because its mesh can be decoded. The selector and
geometry attributes are restricted to the verified position-preserving types
already used by scene inspection, plus vertex-blend state. Extra geometry on
the selector scope is rejected. Texture, lighting and blending fidelity remain
separate renderer concerns: successful shadow-branch binding, for example,
does not imply that its material supports Textured mode.

After validating the forward ownership chain, the reader calls the existing
`decode_igb_skinning` profile. That independently requires unique geometry,
selector, skin, database and skeleton relationships and validates weights,
palettes, inverse-bind matrices and bone hierarchy. The resulting geometry,
skin and actor provenance must agree with the selected player chain. No new
skin-weight algorithm or guessed skeleton mapping is introduced.

## API and bounds

`list_igb_player_models` follows explicit exports and reports model names,
instance counts and unsupported-instance diagnostics. Missing exports produce
an empty candidate list. Malformed export storage or repeated export references
are errors; hidden player-shaped objects are never scanned into the result.

`inspect_igb_player_visual` returns the recovered references and a diagnostic
when the requested profile is unsupported. `decode_igb_player_visual` returns
owned mesh and skinning data, or throws `PlayerVisualError`. Neither call picks
an instance or LOD branch automatically. Mesh positions remain below the
replaced model-node transform. UVs are an explicit subsequent decode, and a
caller applies player placement once after evaluating the selected pose.

Every traversed list has a maximum count and capacity of 4,096, validated
against its exact memory size. Enumeration shares a 4,194,304-link budget,
including repeated references to the same RAM lists. Names are limited to
4,096 decoded bytes, at most 4,104 serialized bytes including length and
padding, and a shared 4 MiB serialized-name budget. Serialized bytes are
charged before trimming NUL padding. Existing mesh, skinning and IGB input
limits also apply.

The asset-free tests generate the complete player/skin chain and malformed
variants. They exercise explicit branch selection, hidden exports, wrong
types, duplicate `ONE` records and geometry, attachment/modifier rejection,
nonidentity/nonfinite matrices, disabled geometry, padded names, list limits,
repeated-list work bounds and existing skinning failures. Optional local-data
checks resolve all three Agumon branches and the fourth player's high branch.
Original game bytes remain outside tracked fixtures.
