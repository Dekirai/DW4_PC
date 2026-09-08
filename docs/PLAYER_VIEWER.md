# Placed player visual inspection

Version 0.9 joins a supported original player skin and original map geometry in
one native depth/MSAA target. This is a visual inspection milestone. Startup
scripts, the player-generation state machine, input, collision, combat, weapons,
shadow composition and multiplayer sessions are not running.

## Select a visual

First select a complete, nonempty map in **Scenes**. A named package preserves
its explicit member/export ordering; **State** can submit an original `ALLON`
command for a supported root. Opening an inactive map does not place a player
into an otherwise empty viewport.

In **Player**, expand **Placement selection**, enter a logical player resource
and select **Load resource**. Choose an exported player model and an exported
entry-point descriptor from the current map member. Supply the zero-based player
slot, entry group and appearance LOD branch, then select **Place visual**. These
are explicit inspection inputs. The slot selects both the model instance and
the matching point; only one visual is displayed at a time.

The visual reader follows the selected instance's unique `ONE` record into a
checked model node, one LOD node, the selected actor, appearance and skin. It
rejects unsupported attachments, modifiers, visibility flags and lower
transforms. The original model node's authored matrix is checked but replaced
by the entry-point matrix, matching the recovered original assignment. No
position, heading or scale is invented for the selected point.

## Original USA example

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'PATH\Extracted' `
  --package dng101b1.igb --package-member 1 --scene-root 1 `
  --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured `
  --scene-player-resource _common/p_agumon.igb --scene-player-model 1386 `
  --scene-player-slot 0 --scene-player-branch 1 `
  --scene-player-point 115 --scene-player-group 0 `
  --scene-player-animation waitS --scene-player-play
```

This selects map export `BG02`, player model `p_agumon`, instance 0 and its high
appearance branch. The entry-point transform is reference 98 at
`(-300, 40, 431)`. The high appearance resolves to actor 6443, skin 404 and
geometry 5665, containing 1,371 vertices and 1,966 triangles. The combined scene
has 235 static batches plus one player batch and six decoded original textures.
These reference numbers are observations for the verified USA data; the
implementation does not hardcode them.

Branch 2 selects the low appearance. Branch 0 selects the shadow skin, whose
geometry can be inspected in Solid mode; its material remains unsupported in
Textured mode. This branch selection does not reproduce the original LOD or
shadow-composition rules. Entry descriptor 115 contains two groups of four
points. Choosing group 0 is an inspection choice, not proof of the startup
state's initial group selection.

## Playback and camera

The **Player** tab lists clips from the selected skin's owning database. Choose
a clip, seek with the seconds slider or use **Play**, **Pause** and **Loop**.
**Stored skin vertices** returns to the source geometry. `--scene-player-time`
selects a paused time; `--scene-player-play` advances it using the host clock.
Out-of-range times and unsupported clips report an error. A failed replacement
keeps the previous clip and uploaded pose consistent.

Each update samples the skin-local pose and applies the selected entry matrix
once. Only the final GPU batch's position range changes. Static geometry, UVs,
textures, sampler state, indices and normalization remain fixed. The renderer
performs one shared depth/MSAA render; the visual is not drawn as an overlay.

Placement initializes an elevated host inspection angle. **Focus player**
centers the visual while preserving the current orbit angle; **Fit** returns to
the map view. Drag to orbit, right-drag to pan and use the wheel to zoom. The
camera has no collision or visibility solver. In the second entry group, nearby
map geometry can obscure the visual at the initial inspection angle. Original
camera logic, culling, lighting and multi-pass materials remain separate work.

Changing the map root/member or its switch selection removes the visual.
Loading another player resource also removes its old GPU batch. A failed
Textured conversion clears the render target state; selecting Solid rebuilds a
consistent scene. Player and map image references belong to separate documents
and cannot accidentally share a texture by numeric reference.

## Verification and boundaries

Synthetic tests check exported graph identity, exact matrix selection,
malformed lists/types/flags, finite affine values, overflow and aggregate input
budgets. Native GPU fixtures verify isolated dynamic batch updates, unchanged
static pixels, occlusion, UV/sampler retention, reset, MSAA and camera focus.
Original-data finite runs verify selected placements and original clips in the
combined scene. They do not establish parity with recorded gameplay frames.

See [PLAYER_VISUAL.md](PLAYER_VISUAL.md),
[PLAYER_PLACEMENT_RESEARCH.md](PLAYER_PLACEMENT_RESEARCH.md),
[STARTUP_RESEARCH.md](STARTUP_RESEARCH.md) and [VALIDATION.md](VALIDATION.md).
