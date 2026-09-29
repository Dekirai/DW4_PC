# Original scene activation research

The stored `BG02` switch in `dng101b1` is disabled. The original game has an
explicit command that enables it: the member's `first.lua` submits `ALLON` to
the `SWITCHCTRL` category with no name filter and instance selector `-1`.
The USA executable dispatches this command to each registered matching switch
controller and sets its target switch's mask words to `0xffffffff`.

This establishes a specific startup command's effect. It does not justify
enabling every serialized switch, executing arbitrary scripts, or claiming that
a static inspection result represents a complete running level. The existing
[scene traversal profile](SCENE_RESEARCH.md) and its rendering limitations still
apply after activation.

The primary executable is the local extracted `sys/main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877` (`GDJEB2`). Addresses below are guest
virtual addresses. Original resource bytes, script disassembly, and executable
disassembly remain in ignored `work/`. The analysis reads original data; it does
not execute the supplied bytecode or link original executable code.

## Resource and controller identity

The resource catalog resolves `dng101b1` to `ndmw.afs` entry 80. Its
`beFileListInfo` object 5 has an ordered slot-6 member list:

| Member index | Named-object reference | Stored member name |
| ---: | ---: | --- |
| 0 | 4 | `[0.178]/s` |
| 1 | 0 | `[0.179]` |

The ordering matters. The activation evidence below is inside entry 179;
entry 178 also contains static and sound-related exports. The original `/s`
suffix places a member in the preliminary loading queue. It does not change
the addressed AFS payload. The verified bracket grammar uses one decimal
archive digit and a nonnegative entry index that fits the original signed
16-bit field. [Package research](../PACKAGES.md) records the resolver and queue
addresses, exact supported grammar, and bounded inspection behavior. The host
retains the stage marker without executing the game's loading stages.

The following references are local to entry 179:

| Reference | Exact type | Relevant stored relationship |
| ---: | --- | --- |
| 3372 | `igSceneInfo` | Slot 5 points to root 2877; raw scene-list index 6 |
| 2877 | `igSwitch` | Name `BG02`; slot 7 child list 3417; slot 8 mask 2280 |
| 3342 | `igAttrSet` | The only child of 2877, also named `BG02` |
| 2280 | `igBitMask` | One word, capacity one, bit count one; memory 3107 contains zero |
| 3471 | `beSwitchCtrlInfo` | Slot 7 points directly to switch 2877 |
| 3475 | `beBaseInfoRamList` | Controller slot 6; contains runtime record 3443 |
| 1715 | `beLuaData` | `first.lua`; points to data object 1716 |
| 1716 | `beLuaDataObject` | Slot 2 length 1006; slot 3 memory 2431 |

The controller is an explicit export: `igInfoList` reference 1236 contains
controller 3471 at zero-based index 9. It is not merely an unreferenced object
found by scanning the document.

Scene indices depend on the inspection view. BG02 is raw scene-list index 6
when scanning all scene descriptors in archive entry 179. It is scene index 1
when selecting the explicit scene exports of package member 1. Both refer to
the same `igSceneInfo` 3372 and switch 2877; neither index is a serialized
object reference or the package-member index itself.

The script is a Lua 4.0 binary chunk. The read-only decoder follows the official
[Lua 4.0 chunk loader](https://www.lua.org/source/4.0/lundump.c.html) and
[instruction definitions](https://www.lua.org/source/4.0/lopcodes.h.html),
including byte order, declared sizes, function nesting, and operand layout.
Independent bounded read-only decoding
finds the following instruction sequence at zero-based PCs 121 through 127:
load global `be`, select method `sd`, push `SWITCHCTRL`, push nil, push integer
`-1`, push `ALLON`, and call with no results. This is the four-argument method
call `be:sd("SWITCHCTRL", nil, -1, "ALLON")`. It is followed by a call to
`funcFirstLayerSet`. Decoding a function body proves its contents; startup
scheduling and registration are separate evidence requirements.

## The Lua method is connected to the original command dispatcher

The binding registration at `0x802eb788` passes the name table at `0x804dce50`
and packed method descriptors at `0x804242ac` to `0x8026c068`. That registration
routine extracts the argument count, name index, and dispatch index from each
descriptor. The descriptor at `0x80424444`, `0x0000241e`, selects name index 1
(`sd`), four arguments, and dispatch index 30. Its argument descriptors are
string, string, integer, and string. There are separate five- and six-argument
overloads; the four-argument call does not use their additional payload paths.

The dispatch table entry at `0x804dd07c` selects `0x802ea800`. It forwards the
four converted arguments to `0x802ecda0`, then to `0x80305ed0`. The latter calls
`0x80305344` with category, optional name, and instance selector before passing
the final command string to `0x80305a28`.

The category selector looks up the registered category using the first string.
At `0x80305984` through `0x803059a8`, a null name bypasses the per-name string
comparison. It collects the category's recipients and their instance IDs. The
sender at `0x80305d80` through `0x80305d98` treats selector `-1` as matching all
collected instance IDs; it does not mean child index zero or a single scene.

The sender also has a recipient enabled-state filter, but applies it only when
the command-buffer byte at offset 8 equals one. For the normal initialized
four-argument `sd` path, selection first calls `0x80304618`, which explicitly
clears that byte at `0x80304624` through `0x80304634`. Earlier startup commands
initialize the interface. Consequently this direct `sd` broadcast is not
restricted to recipients already marked enabled. Other invocation paths and
the `-2` selector have different behavior and are outside this profile.

## Switch controller registration and mask mutation

`beSwitchCtrlInfo` registration at `0x802b7bd8` gives it runtime size `0x20`.
The metadata at `0x804cf430` names its extra field `_switchNode` and records
runtime offset `0x1c`, matching serialized slot 7. The controller's metadata
getter at `0x80315d30` returns this info type. Its registration routine
`0x80315d40` passes the literal `SWITCHCTRL` to `0x80305308`.

`0x80305308` registers the bound info's name and the runtime instance ID through
`0x803050a8`. Thus the dispatcher selection refers to the loaded controller
records, not scene names guessed from mesh contents. Entry 179's controller
3471 explicitly supplies the `BG02` target through its slot-7 reference.

The sender first tries the controller's virtual command hook at offset `0x90`.
If it returns false, it calls the string-command handler at offset `0x8c`.
The switch controller vtable contains the false-returning hook `0x802f2238`
at table address `0x804dfa88` and the fallback handler `0x80315d70` at
`0x804dfa84`. In that handler the bound info comes from command-recipient offset
`0x0c`; the target switch comes from info offset `0x1c`.

The enum-name table at `0x804cf448` and value table at `0x804cf450` map:

| Command | Value | Handler effect |
| --- | ---: | --- |
| `ALLON` | 0 | `0x80315dc0` calls `0x802066f0` |
| `ALLOF` | 1 | `0x80315dd0` calls `0x80206760` |

`ALLOF` is the exact original spelling. The set-all wrapper reads the switch's
mask at runtime offset `0x20` and calls `0x801d6834`. That routine writes
`0xffffffff` to every stored mask word, using a small-count unrolled path or
a byte-fill path. The clear-all wrapper calls `0x801d6908`, which writes zero
to every word. Neither operation changes the child list or creates geometry.

For `BG02`, the valid child-bit range contains only bit zero. Replaying this
proven `ALLON` effect therefore enables child 3342. Extra high bits written by
the original set-all operation do not create additional children: the original
switch traversal is bounded by the child-list length. A host implementation
can retain a bounded activation decision for the existing child instead of
modifying the serialized IGB bytes.

## Separate outer layer organization

`beLayerInfo` registration at `0x802d3434` and metadata at `0x804d1904` identify
`_groups` at runtime offset `0x1c`, serialized slot 7. In entry 179, info 26023
points to list 417 containing 19 named `beLayerGroup` objects. These include
`BG01`, `BG02`, `GM01`, `GM02`, `EM01`, `NP01`, `PC01`, `EF01` through `EF03`,
`BG03`, `GM03`, `WN00` through `WN04`, `FONT`, and `LOADING`. `BG02` is group
26019. These records inherit `igNamedObject`, not scene `igGroup`; their names
are instructions for runtime layer organization, not serialized scene edges.

The `LAYERCTL` command handler at `0x80301368` has a separate `ORDER` branch.
It creates an `igSwitch` named `LayerSwt`, attaches it under `LAYERPARENT` when
found, and creates ordinary named groups corresponding to the layer records.
It also creates a `NONE` group. The branch then reparents suitable original
root children into groups with matching names, or into `NONE`. This is runtime
graph construction; traversing the `beLayerGroup` records as stored scene nodes
would be incorrect.

The `SWITCH` branch starts at `0x8030183c`. It interprets `CLR`/`DISPOFF` as zero,
`SET`/`DISPON` as one, and `FLIP` as two, using enum tables at `0x804d1924` and
`0x804d1938`. For a named layer, it compares names of the newly constructed
`LayerSwt` children, then clears, sets, or flips that child's bit. Literal `ALL`
selects clear-all or set-all. This switch is an outer visibility layer and
does not replace the individual exported `BG02` switch controlled by
`beSwitchCtrlInfo`.

The `funcFirstLayerSet` helper in `func.lua` is child function 16, with stored
source line 625. Its PCs 0 through 7 submit `LAYERCTL`, nil, `-1`, `REMOVE`,
`LOADING`; PCs 8 through 15 submit `LAYERCTL`, nil, `-1`, `SWITCH`, `ALL/DISPON`.
The additional argument is the command's slash-separated payload, not another
scene reference. It removes the `LOADING` layer and enables the outer layers.
Separately, the native state machine at
`0x8037e9f8` submits `LAYERCTL BG02 DISPOFF SWITCH` at `0x8037eb74` through
`0x8037eb9c` and a corresponding `DISPON` at `0x8037f364` through `0x8037f38c`.
These later hide/show operations demonstrate why a single permanently enabled
flag cannot model the game's complete level lifecycle.

## Conservative host profile and remaining boundary

The current host API supports explicit controller-command replay. Only an exact
`beSwitchCtrlInfo` on the explicit `igInfoList` is eligible. The caller chooses
that controller and `ALLON` or `ALLOF`; the decoder validates its exact
`igSwitch` target, child list, and bounded mask storage. Scene integration
requires that target to be the selected scene's root. A result is a bounded
child-selection vector, not a mutation of original mask bytes. This mirrors
the command's visible selection effect, including a partial final mask word,
without exposing irrelevant high tail bits as children.

This explicit replay does not require or execute Lua and does not claim that
startup reached the command. Automatically deriving a startup state is a later
profile: it would also need the named package/member chain, the actual script
command, loaded controller registration, and startup scheduling evidence.
Neither profile may infer activation from a scene name, a zero mask, the
existence of meshes, or an unrelated controller somewhere in the document.

The original stored-mask view remains useful and distinct from this derived
state. Unrelated switches preserve their masks. Unknown commands, unsupported
control flow, or a target outside the validated graph require a diagnostic,
not an all-children fallback. Resource ordering and sound/static member load
behavior are separate package concerns.

This research does not yet establish the complete boot scheduler, every
controller's lifecycle, later visibility changes, actor placement, gameplay,
or a frame matching the original renderer. The next trace targets are the
caller that schedules `first.lua`, the generic controller creation path that
uses metadata getter `0x80315d30` and registration callback `0x80315d40`, and
subsequent stage commands. A full game-state runtime must implement those
relationships rather than generalizing this bounded command replay into a
Lua interpreter or unconditional level activation.
