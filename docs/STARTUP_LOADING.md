# Owned startup resources and info routing

Version 0.11 extends native func/boot execution with real reads of the requested
common resources. A move-only `StartupSession` retains every loaded IGB document,
its explicit export list and the successful `BootRuntime`. The runtime owns the
actual Lua VM, globals and closures; moving the session does not leave callbacks
pointing at an old report or discard the function definitions.

This completes bounded payload decoding and the recovered info-routing rules.
It does not execute the original loading-screen state, manager initialization
callbacks, layer/text setup, `start.lua` or `first.lua`.

## Loading order and ownership

`load_startup_session` reads the exact package descriptor, validates its member
addresses, and loads preliminary `/s` members before ordinary members, preserving
relative order in both groups. It executes func/boot once using these initial
documents, resolves the generated queue with the original ASCII name rule, and
then reads and decodes every common resource in queue order.

The session keeps a separate document for each load occurrence. It does not
deduplicate repeated physical locations or merge equal object references from
different files. `StartupInfoAddress` contains the loaded file index, explicit
export ordinal and local object reference. Both file and export identity matter:
repeated exports of the same object remain separate occurrences.

Original directory metadata is retained, including repeated external-directory
names. The tested common resources contain names such as `system`; the host does
not turn these names into guessed filesystem dependencies. The parser continues
to reject unsupported external object references. Decoding these supported
documents does not establish general original external-object linking.

## Proven routing

The routing function follows only the explicit `igInfoList`, in file and export
order. It applies exact type comparisons recovered from the original loader:

| Export | Route |
| --- | --- |
| Exact `beLuaInfo` | Immediate Lua-info route |
| Exact `igSceneInfo` with a nonempty camera list | Late-scene route |
| Exact `igSceneInfo` without cameras, whose graph is named exactly `ROOTATTR` | Late-scene route |
| Other exact `igSceneInfo` | Separate held-scene list |
| Every other info type | Ordinary deferred queue |

After all common documents have loaded, late scenes are appended to the end of
the deferred queue in their original encounter order. No name/type/reference
deduplication takes place. Empty export names are valid. A null camera list is
empty; reference zero is a valid reference. The camera branch does not require
a graph. Without cameras, a null graph is rejected because the original code
dereferences it. Lowercase `rootattr` is not the special name.

These lists represent loader routing and retained identities. They do not claim
that a deferred manager accepted an info or initialized its runtime records.
Common-file Lua infos are retained for later lookup, but their func/boot chunks
are not retroactively run. The current executor exposes no later-phase execution
API. See the address-level evidence in
[STARTUP_LOADING_RESEARCH.md](STARTUP_LOADING_RESEARCH.md).

## Workbench and CLI

The existing commands now perform the additional loading phase automatically:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'PATH\Extracted' `
  --boot-package dng101b1.igb --boot-player-0 p_agumon `
  --boot-weapon-0-0 w_sword01 --boot-weapon-0-1 w_gun01

.\dist\DW4_PC\dw4_boot.exe 'PATH\Extracted' --package dng101b1.igb `
  --boot-player-0 p_agumon --boot-weapon-0-0 w_sword01 --boot-weapon-0-1 w_gun01
```

The Startup page shows the original resource queue, loaded files and routed
info lists. File rows report their source, serialized bytes and export counts.
Info rows retain file/export/reference identity and the final deferred order.
Changing any inspection input, remounting data or starting another run releases
the previous session and report. Failed loads replace previous success; they do
not leave an old live session visible as the new result. The scene renderer and
its explicitly placed animated visual remain separate.

The tool returns 0 for complete payload loading and routing, 3 for failed boot,
resolution, decoding or routing, and 1 for malformed arguments or mount failure.
Exit 0 is not a claim that original startup or gameplay completed. Detailed
output includes loaded files, qualified routed infos and the pending barrier.

## Limits and failure state

The descriptor has a separate 4 MiB limit. Initial members are capped at 32;
initial plus common files at 128. Reads allow at most 64 MiB per file and 256 MiB
of combined serialized payloads. The reader receives the remaining allowance
before reading, and oversized callback results are rejected before decoding.

Across loaded documents the session allows at most 32 MiB of serialized IGB
metadata, 1,048,576 declared references (objects plus memories), 1,048,576
directory entries, 1,048,576 objects and 4,194,304 decoded directory/object field
values. The parser's independent per-document limits still apply. Per-file
exports are capped at 4,096, with at most 65,536 exports across the session.
Loaded metadata text and routing text each have an 8 MiB budget, including
serialized export-name scans before NUL-padding is removed. A camera list has
at most 65,536 entries/capacity; repeated camera traversals are charged against
a cumulative 1,048,576-reference budget.

A failed session may retain its successfully decoded file prefix for diagnostics.
Its visible byte/object/field totals describe that prefix. `files_loaded` is set
only after all initial and requested payloads decode. `info_routing_complete`
is separate and is set only after the full route validates. Routing arrays are
published together on success; failed partial routing never appears complete.
Neither flag represents the original loading-screen gate or manager callbacks.

The successful boot VM remains available even if later resource resolution or
loading fails. A failed VM is discarded; its script results and partial queue
remain diagnostic. Each `BootRuntime` permits one execution attempt and cannot
be restarted after success or failure. Starting over requires a fresh runtime.

## Original profiles

Independent raw-export routing gives the following expected results, including
the two initial members. Byte counts exclude the descriptor and catalog.

| Explicit input | Files | Bytes | Exports | Lua | Held scenes | Final deferred |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| No joined players | 96 | 14,535,397 | 688 | 1 | 104 | 583 |
| Agumon with sword01/gun01/nil | 9 | 4,006,731 | 454 | 1 | 17 | 436 |
| Agumon/Vmon/Dorumon with shared weapons | 11 | 4,747,791 | 466 | 1 | 19 | 446 |

All three deferred queues end with the same three late-scene occurrences:
initial member 1 export 414/reference 1825, camera file export 0/reference 139,
then camera-pack export 0/reference 48. The preserved Lua info is initial member
1 export 237/reference 2783. Actual local validation is recorded in
[VALIDATION.md](VALIDATION.md).
