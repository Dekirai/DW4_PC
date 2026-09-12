# Native play preview

The play preview lets you control an animated Agumon in an original map through
native C++ code. It loads the map, original collision floor, player, textures
and original ACTION animation from your own extracted USA GameCube files. Version
0.34 also generates and renders the original common05 weapon on the same actor.
No PowerPC code
or emulated GPU runs here.

This is an early interactive port milestone. Movement, the follow camera and
floor following are provisional host implementations. The original game loop,
collision controller, enemy behavior, combat, progression, audio and saves are
still being recovered. The preview does not complete the original startup
sequence or claim that its movement matches the GameCube game.

## Start playing

Build the Windows package using `scripts/build.ps1 -Package`, then launch
`Start-DW4-PC.cmd`. Connect your extracted game directory and use the play button
to open the preview. `Play-DW4-PC.cmd` opens the preview directly using the same
remembered directory. You can also start it directly:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --play
```

The preview uses one verified map/player profile. The `--play` option is exclusive
with workbench page, asset, scene and boot selectors. It accepts the usual
`--data`, `--config`, `--smoke-test` and `--screenshot` options.

## Controls

| Action | Keyboard / mouse | SDL-mapped gamepad |
| --- | --- | --- |
| Move relative to the camera | W / A / S / D or arrow keys | Left stick |
| Orbit the camera | Hold the right mouse button and drag, or Q / E | — |
| Zoom the camera | Mouse wheel | — |
| Pause / resume | Escape | Start |
| Return to the entry point | R | Restart in the pause menu |

The pause menu provides Resume, Restart, Workbench and Exit. Restart restores
the preview's entry state. Menu navigation supports the directional pad or left
stick and the south face button (Xbox A / PlayStation Cross). Losing window
focus pauses the preview and clears held keyboard movement. This is one-player
control; detecting several
gamepads does not create additional players. SDL mappings cover supported
devices, but individual Xbox, PlayStation and GameCube adapters still require
hardware verification.

## Implemented profile

| Asset / state | Selection |
| --- | --- |
| Original release | USA `GDJEB2`, revision 0 |
| Map package | `dng101b1.igb`, member 1 |
| Scene | `BG02`, info reference 3372, root 2877 |
| Root activation | Verified `ALLON` effect of switch controller 3471 |
| Collision floor | Exported `beHitLandModelInfo` 1768, category 1 |
| Collision walls and doors | Category-2 infos 1953 and 192, stored inner masks |
| Player resource | `_common/p_agumon.igb` |
| Player visual | Model 1386, appearance branch 1, instance 0 |
| Entry point | Info 115, group 0, player 0 |
| Prepared ACTION | Info 546, RAM 0, actor ordinal 1 |
| Weapon | `_common/w_common05.igb`, original `w_common05` GENERATE |
| Stand/run actions | Original `WAITS1` and `RUNNS1`, selected by host movement |
| Animation clips | Original `waitS` and `runS`, selected by those actions |

The map and player remain separately owned source documents. The renderer
shares one camera, depth and MSAA target, preserves the static map batches and
updates the player's posed vertex range. Body and weapon have their own captured
model-view matrices. The same prepared ACTION runtime answers the weapon queries
and provides animation caches. The original entry matrix supplies the initial
placement. The play preview then owns a mutable position and facing direction.

The weapon's recovered sequence labels select the slash stand/run actions for
this profile. The host still decides when movement starts or stops; the original
input and locomotion state machines are not yet running. Missing ACTION or
graphics services stop with an error instead of silently skipping a command.
The renderer uses an explicit reset graphics baseline and a partial native
material shader. See [WEAPON_PREVIEW.md](WEAPON_PREVIEW.md) for those boundaries.

When map geometry hides Agumon, the preview draws the hidden part of his shape
in gold so you can keep track of his position. Visible surfaces retain their
normal texture. This is a native preview visibility aid, not an original game
effect; ordinary scene inspection keeps its usual depth rendering.

The provisional movement simulation advances at a fixed 120 Hz with a speed
of 80 source units per second. Presentation frequency is separate. These are
preview choices, not measured original game timing or movement constants.

Floor following uses the original category-1 collision geometry exported by
`beHitLandModelInfo` 1768. The bounded decoder validates its static outer/inner
switch layout and direct geometry children. Like the original land query, it
uses the inner switch's stored selection independently of the outer mask. The
accepted profile has no transforms, so stored vertices are world coordinates.

The provisional controller snaps the player down to the floor beneath the
original entry point and requires supporting triangles along each movement
segment. It rejects steep surfaces and unsupported gaps. This uses original
collision assets, while the floor queries and movement rules are host
implementations. Static wall/door triangles obstruct a swept body circle, with
a provisional radius of 1.5 source units and height band from 1 to 6 units above
the floor. A blocked diagonal can slide along a free world axis. Door switches
remain in their stored state; there are no door interactions. The original
collision manager, dynamic obstacles, jumping, gravity and falling remain
unimplemented; this does not establish full original traversal behavior.

## Verification

Asset-free command-line checks run in CI:

```powershell
python tests/play_cli_tests.py build/Release/dw4_pc.exe
```

Local rendering and movement checks require your original game files and a
working graphics driver:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' `
  --play --play-smoke --smoke-test 120 --screenshot work/play-preview.bmp
```

`--play-smoke` runs a deterministic input replay and requires `--play` plus at
least 120 smoke frames. It is separate from ordinary finite rendering without
replayed movement. A passing parser check does not prove real input or graphics
behavior. Local verification must also inspect an app-rendered frame and the
replay's movement, pause and restart results. Finite smoke runs do not establish
a sustained frame-rate benchmark or physical controller compatibility.

Original assets and locally generated captures remain outside the published
source files. For the retained startup research and its current stopping point,
see [STARTUP_LOADING.md](STARTUP_LOADING.md). The full original gameplay and PC
feature goals remain in [ROADMAP.md](ROADMAP.md).
