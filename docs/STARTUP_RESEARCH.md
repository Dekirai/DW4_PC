# Original startup scheduling and embedded scripts

The USA file-list manager explicitly calls `func.lua`, `boot.lua`, `start.lua`
and `first.lua` in that order, with native loading and registration stages
between the calls. `first.lua` is therefore a scheduled startup script, not
merely a suggestive filename. This identifies scheduling and script selection;
it does not establish that the native workbench can execute startup successfully.

The primary source is the user's local USA `sys/main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses below are guest virtual
addresses. Original binary chunks, executable disassembly and read-only probes
remain in ignored `work/`. No original program code is executed by this research.

## Exact script ownership in `dng101b1`

[Package resolution](PACKAGES.md) identifies descriptor `dng101b1.igb` at
catalog archive 0, entry 80. Original member index 1 is `[0.179]`. In that
member, `igInfoList` reference 1236 exports exact `beLuaInfo` reference 2783 at
zero-based export index 237. Its slot 7 points to `beLuaDataList` reference
5812. That list contains 55 script records with unique exact names. These are
script-list indices, not info-export indices or arbitrary object-scan indices.

| Script | Data-list index | `beLuaData` reference | `beLuaDataObject` reference | Memory reference | Bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| `func.lua` | 9 | 2391 | 2392 | 6578 | 16849 |
| `boot.lua` | 6 | 1701 | 1711 | 2652 | 7204 |
| `start.lua` | 53 | 2654 | 2655 | 396 | 2099 |
| `first.lua` | 8 | 1715 | 1716 | 2431 | 1006 |

The executable metadata confirms the reference chain:

- `beLuaInfo` registration `0x802D1ADC` uses table `0x804D1828`. `_dataList`
  has native offset `0x1C`, serialized slot 7.
- `beLuaData` registration `0x802D1F48` uses table `0x804D1848`.
  `_dataObject` has native offset `0x0C`, serialized slot 3. The script name is
  inherited `igNamedObject` slot 2.
- `beLuaDataObject` registration `0x802D2310` uses table `0x804D1860`.
  `_dataSize` and `_data` have offsets `0x08` and `0x0C`, serialized slots 2
  and 3. All 55 declared byte sizes equal the referenced memory sizes.

Shared data objects are legitimate: several differently named scripts in this
list refer to the same bytecode object. Script identity must retain both the
name record and data reference instead of deduplicating by memory reference.

For reproducible local verification, the extracted chunk SHA-256 values are:

| Script | SHA-256 |
| --- | --- |
| `func.lua` | `9dd555630a99bf1ccf192fc9fe6a6a9eede57f249479adb96301e1d92aa8d3d1` |
| `boot.lua` | `053412ab91095c3d0b81bd1b30ca2b0e3a9d62e2284c444c6df7d5deb84f21bc` |
| `start.lua` | `ad531be5ea5b2324d1685c86e725b88325c1540940747a2a04ae6a1857e317ef` |
| `first.lua` | `e33c3f9f68e162c64b1c3ef46122964b85f61e899a261346c953d582ba92ee32` |

## Native scheduling

The file-list manager update routine starts at `0x802F8A64`. Its `_proc` field
at offset `0x3C` selects a state. A positive system field at offset `0x58`
defers processing before the state dispatch. The routine advances through
individual update calls and can wait for asynchronous loading; state numbers
are not frame counts or durations.

| State | Relevant verified operation |
| ---: | --- |
| 0 | Takes the first queued descriptor's inherited name, interns it and stores it at system offset `0x38`; clears system byte `0x2C`. |
| 1, 100–102 | Builds the preliminary `/s` queue, loads it and handles sound transfer before queuing remaining descriptor members. |
| 2 | Loads the remaining filename queue through `0x802F9E98`. On completion, advances to state 3. |
| 3 | Finds the registered `beLua` manager, resets its Lua state, then calls `func.lua` followed by `boot.lua`. Advances to state 4. |
| 4, 20, 21 | Processes the additional common-file queue and other loading conditions, with intermediate update states. |
| 5 | Registers the deferred loaded info records through `0x802FA53C`; advances to state 6. |
| 6 | Sends the layer controller's `ORDER` command and related layer setup. |
| 7 | Sends `TEXTCTL INITIALIZE`, then calls `start.lua`. |
| 8 | Sets system byte `0x2C` to one and advances to state 9. |
| 9 | Calls `first.lua`, then sets `_proc` to `-1`. |

The exact script call sites are `0x802F971C` for `func.lua`, `0x802F972C` for
`boot.lua`, `0x802F9B50` for `start.lua`, and `0x802F9BB4` for `first.lua`.
All call `0x80301FA4`. The strings are at `0x80425608`, `0x80425614`,
`0x8042566C` and `0x80425678`, respectively.

The supplied directory argument is formatted as `%s/lua`, using the active
descriptor name stored at system offset `0x38`. State 0 copies that name from
the first file-list info's inherited name at offset `0x08`; it is not evidence
that `_section` should replace that name when the two strings differ. In this
sample both strings happen to be `dng101b1`.

Lua-manager lookup uses `0x8028A730` and the exact type handle stored at
`0x80535124`, registered as `beLua` at `0x802D26CC`. If the manager is absent,
the script call sites are skipped. The scheduling routine does not test each
script call's return value before advancing its state. Therefore reaching
state 9 alone is not proof that earlier scripts succeeded.

## Registration occurs before script lookup

The package-reading routine `0x802F9E98` walks the newly loaded directory's
explicit info list. At `0x802F9F74` through `0x802F9FA8`, exact `beLuaInfo`
objects are routed immediately through `0x8028A7DC`, before other info records
are collected for later processing. This allows the embedded scripts to be
available for state 3 even though many ordinary controller records are
registered later in state 5.

`0x8028A7DC` tries registered manager handlers through virtual offset `0x6C`.
The `beLua` vtable entry at `0x804DE778` selects handler `0x80302378`. It checks
the incoming object's `beLuaInfo` ancestry and appends accepted objects to
`beLua::_infoList` at offset `0x14`. Metadata table `0x804D1888` confirms that
field and `_luaState` at offset `0x18`.

This establishes the Lua-info routing path. It does not establish every
controller factory's construction, instance selection or registration order.
Those relationships still require their own traces.

## Exact selection and execution boundary

`0x80301FA4` first prepares the supplied directory string, or derives a default
directory from the active descriptor name when the argument is null. It then
iterates `beLua::_infoList` in registry order. For each info it iterates the
`_dataList` in stored order and compares the script name with `strcmp` at
`0x8030214C`.

On a match, the routine reads the data object's memory and declared size,
passes those plus the requested script name to Lua-state virtual offset
`0x64`, and breaks only the inner data-list loop. It continues to the next
registered info. Thus the original behavior is the first matching name in
each registered Lua-info list, potentially executing multiple chunks across
different lists. It is not a single global first match, nor a lookup scoped by
the supplied directory argument. The local sample has one exported Lua info
and a unique match for each startup name.

The virtual entry at `0x804DE848` selects `0x80302AE4`, which calls
`0x80272768`. That wrapper calls the Lua buffer load-and-call routine
`0x80278810`. The latter loads the buffer through `0x802787A4` and, on success,
calls it through `0x80278648`. This is execution of the chunk's top-level
function, rather than selecting a named Lua function inside it.

If the final result remains `-1`, `0x80301FA4` formats a fallback `%s/%s`
directory/name and calls the Lua state's file-loading virtual function at
offset `0x60`. The host must not invent a fallback archive mapping from this
string. The four startup scripts in member 179 resolve through their embedded
records, so that fallback is unnecessary for this sample.

## What the chunks establish

The read-only Lua 4 chunk probe described in [SCENE_ACTIVATION.md](SCENE_ACTIVATION.md)
decodes all four selected buffers without executing them. `func.lua` defines
helpers used by later scripts. `boot.lua` calls common-file-list methods,
including player-dependent resource selection. `start.lua` calls
`funcStartLayerSet`, then `funcPlayerSet`, and submits additional actor, light
and gameplay commands. `first.lua` contains the previously verified
`be:sd("SWITCHCTRL", nil, -1, "ALLON")` sequence and calls
`funcFirstLayerSet`.

These scripts depend on game state and native methods. Replaying only the
switch command does not reproduce the earlier resource loads, actor creation,
layer construction, flags, camera or player state.

## Narrow next implementation

A bounded startup-inspection API can now produce a provenance-bearing ordered
plan from explicitly selected loaded package members: retain their exported
Lua-info order, validate the `_dataList -> _dataObject -> memory` chains,
and resolve the four scheduled exact names with the original per-info matching
rule. It should report missing embedded matches and external fallback as
unsupported, preserve multiple matches across info lists, and limit list work,
script bytes and duplicate-reference expansion. Synthetic fixtures can exercise
hidden scripts, conflicting names, repeated data objects, size mismatches and
invalid references without any game data.

That plan would describe native scheduling and selected buffers; it would not
be a Lua interpreter or automatically mark startup complete. Executing a
bounded subset requires independently specified native-method effects and
their prerequisites. Actor visual binding and explicit placement can proceed
as separate inspected state while the full startup runtime remains pending.
