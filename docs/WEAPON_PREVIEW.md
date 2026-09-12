# Original ACTION and weapon preview

Version 0.34 joins actual loaded player and weapon resources to a native shared
render pass. This is a prepared inspection boundary, also used by the walking
preview. Full original player construction, locomotion, combat and rendering
remain development work.

## Mesh inspection

Use the original data externally, with an explicit model, ACTION RAM, actor and
main-skin geometry. For the USA Agumon profile:

```powershell
dist/DW4_PC/dw4_pc.exe --data "PATH/TO/EXTRACTED_GAME" `
  --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured `
  --actor-model-info 1386 --actor-info 546 --actor-ram 0 --actor-index 1 `
  --actor-action WAITS1 --actor-weapon-resource _common/w_common05.igb `
  --actor-weapon-name w_common05 --animation-play
```

Select RAM 0..3 explicitly. `--actor-index` is the actual ACTION actor-list
ordinal; its matching LOD branch is resolved and its geometry must match the
explicit `--mesh`. All weapon options are required together. Ordinary clip
inspection remains available through `--animation`; it cannot be combined with
the original ACTION weapon mode. `--animation-time` provides an initial forward
inspection time up to 600 seconds. Play/pause controls the prepared clock.

For the two-hand resources, use `_common/w_common04.igb` / `w_common04`, or
`_common/w_common03.igb` / `w_common03`. The selected ACTION is explicit; the
mesh viewer does not claim to choose the original weapon-dependent sequence.
The walking preview uses common05 with its verified slash stand/run actions.

## Ownership and draw order

`WeaponPreview` owns one actual registered MODELCTRL/PLAYER/ACTION instance.
The same mutable ACTION runtime that answers the weapon manager's queries also
executes the chosen action and provides the body and weapon bone caches.
Original GENERATE updates the actual appearance lists and MODELCTRL labels.
The selected appearance must contain the resulting selectors.

Body CPU skin deformation is captured first. Each attached common selector then
uses the selected actor's current bone array before another actor can replace
the shared combiner root. `SceneTraversal` visits its real ordered child graph.
Shared left/right geometry remains separate draw occurrences, with exact stored
left transforms and source-qualified attribute identities. Texture bindings
resolve along that occurrence's path. Image owners can be shared across draws.

The renderer uploads raw positions, applies each captured model-view once, then
a separate shared projection. Map, body and weapon draws share depth and MSAA
in the walking preview. Updated body positions preserve the weapon geometry,
samplers and upload order.

## Explicit boundaries

The prepared context uses the original graphics reset's disabled vertex-blend
default and an empty matrix-list stack. A normally completed main-skin scope
restores this baseline before rigid weapon children. This is documented original
reset behavior; it does not establish arbitrary later gameplay frame state.
Previsit is explicitly disabled and the transform clock is supplied from the
prepared actor traversal. Unknown graphics effects and missing ACTION services
stop with diagnostics.

The native inspection shader is not the complete original GX material pipeline.
Compiled selectors, arbitrary inherited graphics contexts, render-list execution,
full appearance effects and recorded original-frame pixel comparison remain
required. Walking movement, action selection, placement and follow camera are
host preview behavior. Independent connected multiplayer, combat, progression,
audio and saves are not supplied by this preview.

See [SCENE_TRAVERSAL.md](SCENE_TRAVERSAL.md),
[PREPARED_RENDERING.md](PREPARED_RENDERING.md), [PLAY_PREVIEW.md](PLAY_PREVIEW.md)
and [VALIDATION.md](VALIDATION.md).
