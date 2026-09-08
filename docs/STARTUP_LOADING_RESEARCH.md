# Common-resource loading and deferred registration

This is a bounded trace of the USA file-list manager after the original
`func.lua` and `boot.lua` return. It establishes directory consumption and info
routing. Decoding those directories and constructing the corresponding host
lists does not execute every original manager callback, complete the loading
screen, initialize layers/text, or run `start.lua` and `first.lua`.

The source is the original `GDJEB2` DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, inspected as data. Address ranges
below refer to that executable. Independent local probes read original AFS/IGB
files without modifying them. The probes, payloads and disassembly are not
distributed. See [STARTUP_HOST_RESEARCH.md](STARTUP_HOST_RESEARCH.md) for the
queue-producing script execution and explicit input profiles.

## Manager state and one-at-a-time consumption

The update routine is `0x802F8A64`; its state dispatch is at
`0x802F8A90`–`0x802F8B24`. A positive system field at `+0x58` makes the update
return before dispatch. Relevant file-list-manager fields are:

| Runtime offset | Observed role |
| --- | --- |
| `+0x24` | Initial ordinary filename queue |
| `+0x28` | Active file-list descriptor followed by held ordinary scene infos |
| `+0x2C` | Pending loaded-directory reference list |
| `+0x30` | Deferred info-registration list |
| `+0x34` | Late scene-info list |
| `+0x38` | Current filename ordinal |
| `+0x3C` | Update state |

The common filename list belongs to the system object at its `+0x34`, not the
file-list manager's similarly numbered field. State 4 passes that list to
`0x802F9E98` at `0x802F97D8`. The same helper also consumes initial member
queues during earlier states.

The helper performs these steps:

1. If the current ordinal is at least the supplied list's count, return true
   (`0x802F9EB0`–`0x802F9EC4`).
2. If no directory is pending, read the string at that ordinal and request its
   directory through `0x8006FD34`. Append the result to the pending list and
   return false (`0x802FA484`–`0x802FA524`). The ordinal is not advanced here.
3. Otherwise, inspect the first pending directory. `0x80070130` returns true
   only when the directory byte at `+0x30` is zero. While it is nonzero, return
   false without consuming another filename (`0x802F9EC8`–`0x802F9EF8`).
4. When ready, visit the directory's explicit info list at `+0x1C`, in ascending
   stored index, using the routing rules below.
5. Release the directory's loader claim, clear the pending reference list,
   increment the filename ordinal once, and return false
   (`0x802FA114`–`0x802FA278`). Completion becomes visible on the next helper
   call, through step 1.

The recovered path submits one directory at a time. Parallel host reads could
be an implementation detail, but publication and routing must preserve this
original order. Do not sort by archive, object reference, type, or scene name.
The original code assumes valid loader results and exports; a bounded host must
report invalid/null data rather than reproduce unsafe dereferences.

## Exact exported-info routing

Only the loaded directory's explicit info-list occurrences participate. The
loader does not scan every object to discover possible controllers or scenes.
Its first comparison uses exact `beLuaInfo` type identity; its second uses exact
`igSceneInfo` identity. Derived types do not automatically take these routes.

| Incoming export | Route at directory completion |
| --- | --- |
| Exact `beLuaInfo` | Immediately call generic registration `0x8028A7DC` |
| Exact `igSceneInfo` with nonempty `_cameras` | Append to late scene list `+0x34` |
| Exact `igSceneInfo` with no cameras and root name `ROOTATTR` | Append to late scene list `+0x34` |
| Other exact `igSceneInfo` | Append to holding list `+0x28`; not generic registration at this phase |
| Every other export | Append to deferred list `+0x30` |

The immediate Lua branch is `0x802F9F74`–`0x802F9FA8`. The exact type handle
is at `0x80535100`, registered by `0x802D1ADC`. This route is why initial
embedded Lua scripts are available before deferred registration.

The scene branch is `0x802F9FAC`–`0x802FA068`. Handle `0x80564A14` belongs
to `igSceneInfo`: registration `0x801B41E8` supplies its type name at
`0x804AD360`. The register base established at `0x80005420`–`0x80005424` is
`r13 = 0x80564F00`, so that registration's `r13 - 0x4EC` addresses this handle.
The metadata names at `0x804AD3DC` and offsets at `0x804AD414` establish:

| Serialized slot | Field | Runtime offset |
| --- | --- | --- |
| 5 | `_sceneGraph` | `+0x14` |
| 7 | `_cameras` | `+0x1C` |

A null camera-list pointer is treated as no cameras. A present positive count
routes late without inspecting the root, even if its entries are null. The
loader does not test camera-list contents in this branch. With no cameras, it
dereferences the scene root and reads its inherited name. There is no original
null-root guard here; the host should reject that malformed case. A null name
uses an empty-string fallback. The comparison is against the interned literal
`ROOTATTR` at `0x804256B4`, with exact spelling and case.

The ordinary branch at `0x802FA06C`–`0x802FA100` contains a loop that can look
like type replacement. It does not read prior deferred entries: it repeatedly
compares the incoming type with the fixed `igSceneInfo` handle. That exact type
has already taken the preceding scene branch, so this path appends every
ordinary occurrence. There is no supported basis for deduplicating these
exports by reference, name, or type. Shared references in different documents
must also remain distinct qualified identities.

## Late-list order and state 5

State 4 waits for both the directory helper and the separate loading-node gate.
When both are ready, it resets the filename ordinal and enters state 20
(`0x802F97EC`–`0x802F9808`). It then repeatedly removes element zero from the
late scene list and appends it to the deferred list
(`0x802F9810`–`0x802F987C`). Consequently:

- All ordinary deferred exports from initial and common directories precede
  all late scenes.
- Ordinary exports retain directory order and explicit info-list order.
- Late scenes retain their own encounter order across those same directories.
- Held ordinary scenes remain in their separate list for later layer work.

The IGB loader's cleanup function `0x8006F8CC` is called at `0x802F988C`.
It conditionally clears a loader mode and releases its `+0x20`/`+0x24` objects;
this trace does not identify it as an audio-completion operation.
State 20 only advances to 21 (`0x802F9894`), and state 21 only advances to 5
(`0x802F98A0`). These intermediate updates are not evidence of further asset
reads or completed controller initialization.

State 5 calls `0x802FA53C`. It passes each non-null deferred entry to
`0x8028A7DC` in ascending list index, releases all deferred references, clears
the list and returns true (`0x802FA568`–`0x802FA6B4`). The scheduler then enters
state 6. Neither this helper nor its caller establishes success of each
individual native manager effect. A host that retains registration candidates
without implementing their handlers must expose them as pending callbacks,
not as initialized controllers.

## Directory and object lifetime

`0x8006FD34` first searches the loader's existing directories by requested name
through `0x8006F35C`. Reuse adds a directory loading claim via `0x80043D44`;
otherwise it creates a directory and starts a synchronous or asynchronous load
according to loader state. Directory name identity is separate from the later
physical catalog's ASCII name conversion described in the boot research.

After export routing, `0x802FA124` calls `0x80070140` with the directory name.
That function finds the directory and releases its loading claim through
`0x80043DE4`; a zero remaining claim can remove it from the loader's directory
list. The pending object-list references are released afterward. The info
objects survive because appending through `0x80069128` increments their object
reference counts, and registered managers retain their own references.

A native session should therefore own the parsed documents for as long as any
registered or queued record refers to them. A pair such as document identity
plus object reference is suitable; a numeric object reference alone is not.
The host need not reproduce guest pointers or GameCube allocation pools.
Releasing temporary read buffers before copying/retaining referenced object
data would violate the observed lifetime ordering.

## Loading-node gate is a separate prerequisite

Manager initialization `0x802F8328` stores an `igViewerSceneInfoManager` at
`+0x18`, using handle `0x8055C788`. Its registration is `0x80404660`, with
type name at `0x80461E58`. State 4 follows the raw pointer chain viewer `+0x14`,
then object `+0x0C`, then node `+0x1C` to inspect a child list. This trace does
not assign an unverified serialized type to the intermediate objects.

An empty/null child list or a first child outside the `beLoadingNode` ancestry
makes this gate ready. Only the first child is tested. `beLoadingNode` is
registered at `0x802C43B8`, with type handle `0x80534B94` and name
`0x8041E854`. A matching node calls `0x80311674`: it returns true when the
signed 64-bit `_playTime` at `+0x68` exceeds its internal pair at `+0x78`.
The `_playTime` offset is recorded in the field tables
`0x804D04B8`/`0x804D0508`.

Only after both this gate and queue completion succeed does `0x80311660` copy
the node's internal pair at `+0x70` into `_stopTime` at `+0x38`. No renderer
clock, loading-screen lifetime, or initial value for these internal pairs has
been recovered here. Successful host document decoding must not silently mark
this original scheduler condition satisfied. The correct report can separately
state that resource payloads and routing are complete while this native gate,
manager callbacks, layers and text initialization remain pending.

## Active manager callbacks remain separate

The system dispatcher `0x8028A7DC` visits active managers in order and calls
virtual offset `0x6C`, stopping on the first nonzero low-byte result. It then
appends the info to the system list at `+0x10` even when no manager accepted it.
This routine does not construct managers or deduplicate infos. Manager insertion
at `0x8028A268` is separate: it deduplicates manager pointers and invokes virtual
offset `0x5C`. The smart-list priority path includes `0x80041E40`; complete active
manager priority initialization remains unrecovered.

The common handler at `0x802F251C` demonstrates why routing is not initialization.
It checks the expected exact info type, appends the info to manager `+0x18`, calls
per-info virtual offset `0x78`, initializes each RAM-list record with manager/info
backreferences, an index and four runtime fields, then calls per-RAM virtual
offset `0x7C`. It schedules the manager through `0x8028A398` and sets manager
`+0x1C`. These are active runtime effects requiring their own implementations.
An empty host manager list or a retained info address cannot prove they ran.

## Original data checks

An independent routing probe loaded the two initial `dng101b1.igb` members in
preliminary-then-ordinary order, then the queues produced by the independently
verified zero-, one-, and three-player boot profiles. It applied the recovered
type/scene rules and retained every explicit export occurrence. Byte totals
below count initial and common member payloads, excluding the descriptor and
catalog files.

| Explicit profile | Initial + common documents | Bytes | Exports | Immediate Lua | Ordinary deferred | Late scenes | Held scenes | Final deferred |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Zero joined players | 96 | 14,535,397 | 688 | 1 | 580 | 3 | 104 | 583 |
| One joined player | 9 | 4,006,731 | 454 | 1 | 433 | 3 | 17 | 436 |
| Three joined players | 11 | 4,747,791 | 466 | 1 | 443 | 3 | 19 | 446 |

In all three cases the late scenes are, in order: initial member 1's export
414/reference 1825 (`ROOTATTR`), `aaaCamera.igb` export 0/reference 139 (one
camera), and `ACMR_pack.igb` export 0/reference 48 (one camera). Their final
deferred indices in the one-player profile are 433, 434 and 435. The immediate
Lua record is initial member 1 export 237/reference 2783.

The one-player common documents contain 27 external-directory metadata names:
2 in `aaaCamera`, 1 in `ACMR_pack`, 18 in `generatorN`, 3 in `p_agumon`, and
1 each in `w_sword01`, `w_gun01` and `USA_pack`. Every one is literally
`system`; duplicates are retained. Initial members contain none. All referenced
directory records in these parsed files use the already supported local object
or memory forms. These metadata names alone do not establish an additional
queue or authorize guessed recursive file loads. Preserve them as provenance;
do not claim that all original external-object linking is implemented.

These checks establish real file decoding and ordered routing, not execution
of the deferred managers or a playable start. The next effects remain the
registered managers' actual callbacks, the loading-node prerequisite, state 6
layer setup, and state 7 text initialization before the `start.lua` call.
