# Embedded scripts and startup selection

The portable `scripts` module follows explicit exported Lua-info graphs and
returns owned script bytes. Its startup plan reproduces the verified order
`func.lua`, `boot.lua`, `start.lua`, `first.lua` and the original first-match
rule within each registered info. A successful plan means embedded lookup is
complete; it does not execute bytecode or complete the native loading stages.

## Source evidence

[STARTUP_RESEARCH.md](STARTUP_RESEARCH.md) records the original USA DOL hash,
guest addresses, registration path, scheduling states and selected chunk hashes.
The relevant serialized graph is:

| Object | Slot | Required field and meaning |
| --- | ---: | --- |
| `beLuaInfo` | 2 | Inherited inline string name, retained as metadata |
| `beLuaInfo` | 7 | Object reference to exact `beLuaDataList` |
| `beLuaDataList` | 2, 3, 4 | Integer count/capacity and local memory reference containing ordered object references |
| `beLuaData` | 2 | Exact inline script filename |
| `beLuaData` | 3 | Object reference to exact `beLuaDataObject` |
| `beLuaDataObject` | 2 | Integer declared byte size |
| `beLuaDataObject` | 3 | Local memory reference to the script buffer |

The reader accepts the verified little-endian storage profile. It requires exact
types for the ownership chain; derived types are not automatically assigned the
same runtime behavior. The declared byte size must equal the referenced memory
size. Bytes are opaque to this module: an owned empty buffer or arbitrary bytes
can be valid storage while remaining invalid for a Lua interpreter.

The original exact `beLuaInfo` registration path is `0x802F9F74` through
`0x802F9FA8`; the manager handler at `0x80302378` appends infos to its registry.
The selector at `0x80301FA4` compares names using `strcmp` at `0x8030214C`,
breaks the inner data-list search after its first match, then continues to the
next registered info. A later duplicate in one info must not replace an earlier
unsupported match. Matches in subsequent infos remain separate ordered calls.
Case is preserved and compared exactly. An embedded NUL in a decoded filename
is unsupported, because comparing the whole C++ string would disagree with
the original C-string lookup.

The original selector also has a file-loading fallback. This module has no
filesystem fallback, directory search or inferred archive mapping. No matching
embedded record is reported explicitly as incomplete lookup.

## API and ownership

`list_igb_scripts(document)` walks the document's explicit `igInfoList`, then
each exported Lua info's data list in stored order. It does not scan unrelated
objects for scripts. Repeated exports, repeated data rows and shared payload
objects are preserved. Every row records the info reference and export ordinal,
data-list index, data-object and memory references, labels, size and provenance.

Malformed ownership lists throw `ScriptError`. An individual malformed script
row remains in the result with `supported == false` and a diagnostic. If its
name cannot be read safely, `name_valid == false` prevents startup selection
from silently skipping it to a potentially shadowed later match. An invalid
row after an already established first match does not change that match.
The underlying IGB reader rejects external or wrong-kind scalar references
before constructing the document; external files are never opened.

`read_igb_script(document, info_reference, data_index)` requires an explicitly
exported info and an exact data-list ordinal. It rejects unsupported rows and
returns independent bytes, with no retained pointer into the input document.
If the same info is exported repeatedly, this exact reference/index pair has
the same payload; its returned provenance uses the first export occurrence.
The startup plan separately retains every export occurrence.

`build_igb_startup_plan(sources)` accepts documents in explicit registration
order. It validates them once under a common expansion budget, then resolves
each startup filename independently in every info. Calls store a source index
into the plan's owned source-label array. The plan retains neither document
pointers nor byte buffers and performs no registry discovery or package loading.

## Native prerequisites

The plan attaches the following barriers before the applicable script phase.
They describe the original state machine; they have no "completed" field that
lookup could accidentally satisfy.

| Before phase | Native prerequisites |
| --- | --- |
| `func.lua` | Package members loaded; exported Lua infos registered in the supplied order; Lua state reset |
| `boot.lua` | No additional native barrier between the ordered `func.lua` and `boot.lua` calls |
| `start.lua` | Boot-requested common resources and loading conditions completed; deferred infos registered; layer ordering and related setup performed; text initialized |
| `first.lua` | Original stage-enabled system state set |

The original script call sites are `0x802F971C`, `0x802F972C`, `0x802F9B50`
and `0x802F9BB4`. The original scheduler does not require each call to return
success before advancing. The native host must report its own execution and
barrier state separately from `lookup_complete` and `embedded_lookup_complete`.

## Bounds

Limits apply across expanded occurrences, including aliases. Startup sources
share the budgets below; listing or reading one document uses a fresh budget.

| Resource | Limit |
| --- | ---: |
| Explicit startup sources | 64 |
| Cumulative info-export rows | 4,096 |
| Cumulative script-list rows | 65,536 |
| Decoded name or source label | 4,096 bytes |
| Serialized inline name field | 4,104 bytes including length, terminator and padding |
| Cumulative name work and owned metadata copies | 4 MiB |
| One script buffer | 8 MiB |
| Cumulative supported script-buffer expansion | 64 MiB |

List counts and capacities are checked before expansion. Serialized string
sizes are checked and charged before scanning or trimming NUL padding, so a
large padded string cannot masquerade as a free empty name. Repeated script
names charge each scan; retained per-row info-name copies also consume the
metadata budget. Each shared payload occurrence charges its full byte size
even though listing does not copy its buffer. Rows exceeding a per-name or
payload budget are unsupported; ownership expansion beyond its budget throws.

## Verification

`tests/script_fixtures.hpp` writes small synthetic serialized IGB graphs from
caller-supplied names and bytes. Both script-reader and startup-executor tests
can use it without original game data or access to private document storage.

The script-reader suite covers hidden infos, exact field/types and local
references, ordered duplicate names within and across infos/sources, repeated
exports, shared payloads, unsupported first-match shadowing, uncertain earlier
names, missing names, byte-size mismatches, owned output, padded-name budgets,
shared payload expansion and cumulative list/source limits.

The optional `dw4_scripts_tests <ndmw.afs>` integration checks original entry
179, package `dng101b1` member index 1. It validates all 55 exported records
under info 2783/export ordinal 237, including shared bytecode objects. It
resolves exactly one call per phase at data indices 9, 6, 53 and 8, with
buffer sizes 16,849, 7,204, 2,099 and 1,006 bytes and original Lua 4 signatures.
The independently recorded object/memory references are checked as well.
Original buffers, archives and generated probes remain outside tracked files.
