# Named package inspection

The **Scenes** page can resolve a named original package descriptor to its member
files. Package inspection keeps the descriptor, member and scene export separate:
a catalog name can refer to a small descriptor rather than the archive entry
containing the visible map geometry.

## Selection flow

Connect the extracted game directory under **Original data** and open **Scenes**.
Choose **Package** in the source selector, then choose the descriptor's exact
logical catalog path. The member selector lists original file records in their
source order, with their checked archive locations and unsupported-state
diagnostics.

Choose a supported member to inspect its actual exported scene roots. Loading a
member does not select its first root. Choose the desired root explicitly, then
use the existing Solid, Wireframe or Textured inspection controls. Export identity
comes from original info-list references; names are display labels and are not
used to guess associations.

Package loading does not activate the game's scene. Stored switch masks remain
the default. In the **State** tab, select an exported controller that targets the
exact selected root, then explicitly apply **ALLON** or **ALLOF**. These evaluate
the verified original switch commands for this static inspection. The visible
command label records the controller reference and states that game startup is
not running. **Stored selection** clears the command.

**Inspect root child** remains a separate, visibly labeled inspection override;
its runtime activation is unverified. Applying a command clears this override,
and applying an override clears the command. Changing the package, member or
root clears previous geometry and selection state.

Unsupported member forms remain visible with a reason. The viewer does not strip
an unknown suffix, select a similarly named file, or substitute the first archive
entry. Exact `/s` is recognized as the original preliminary-load marker and
retained in the source label. Its payload can be inspected explicitly, with a
diagnostic that staged initialization is not executed. Unknown suffixes remain
unsupported. Member reads use the bounded entry limit from the other scene
sources; descriptor reads additionally have an independent 4 MiB cap.

**Resource** mode retains the direct catalog-resource and raw AFS archive/entry
controls. Existing texture, mesh, rig and animation command-line options keep
their separate behavior.

## Command line

`--package NAME` loads the exact logical descriptor name and opens the Scenes
page. By itself it inspects only the descriptor and member list; it does not
choose a member, export or scene activation.

Rendering through a package requires both `--package-member N`, the original
zero-based member ordinal, and an explicit `--scene-root N` in that member's
scene-export list. Package scene exports retain their original info-list order.
The existing raw-resource scene index remains unchanged.

For the verified USA data, this explicitly selects the map member, `BG02` export
and its original controller:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --package dng101b1.igb --package-member 1 --scene-root 1 `
  --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured
```

The original descriptor resolves to `ndmw.afs` entry 80. Member 0 is
`[0.178]/s`, with four non-scene exports. Member 1 is `[0.179]`, with 419 exports;
its filtered scene index 1 is `igSceneInfo` 3372, root 2877 (`BG02`). This is scene
index 6 when inspecting raw entry 179, whose legacy list order is unchanged.
The controller is exported reference 3471 and explicitly targets root 2877.

`--scene-switch-control REF` and `--scene-switch-command all-on|all-off` are
required together and are exclusive with `--scene-inspect-child`. They do not
execute the original startup script. Without either activation option, the
original stored mask remains authoritative. `all-off` produces a valid empty
selection, so a smoke test requesting a rendered scene returns exit code 3.

`--package` is exclusive with `--resource` and `--archive`/`--entry`. Package
selection is also separate from the individual texture, mesh, rig and animation
options. Missing required selectors or malformed CLI values return exit code 1.
Unresolved descriptors, unsupported selected members and scene rendering failures
return exit code 3 during finite smoke tests. A descriptor-only smoke verifies
package parsing and explicitly reports that no scene was selected or rendered.

This is native original-data inspection. Runtime loading, activation, actors,
collision and playable map behavior remain separate work. Original game payloads
and locally rendered integration images are not included in the source package.

## Local native validation

The USA `dng101b1.igb` member 1 / scene 1 / controller 3471 ALLON path rendered
235 original instances, 31,886 placed vertices and 26,352 triangles, with all 235
batches textured from five verified images. The native 720p layout was inspected
with both the original command and the source package visible.

Thirty-six finite native UI/CLI checks covered descriptor-only inspection, the
empty stored and ALLOF states, preliminary member 0 with no scene exports,
missing/invalid descriptors, members, roots and controllers, paired-argument
guards, all eight workbench pages, legacy raw scene selection, texture decoding,
original clip sampling and manual rig inspection. Invalid rendered-scene requests
returned 3; malformed argument combinations returned 1. These checks establish
the bounded inspection path, not full runtime startup or original-frame parity.
