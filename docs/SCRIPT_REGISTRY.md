# Owned script registration and selection

`ScriptRegistry` owns explicitly selected exported `beLuaInfo` occurrences and
resolves an arbitrary filename in their supplied registration order. It selects
the first matching name within each info, then retains the next registration
as a separate entry. It does not execute chunks, infer registration order,
register every export, or load a filesystem fallback.

## API and identity

Construction accepts `ScriptRegistration` rows containing a borrowed document,
source label, source occurrence, exact info-export ordinal, and exact info
reference. The reference must equal the selected export-list entry and its
type must be exactly `beLuaInfo`. An unexported object, a different info type,
or a reference paired with the wrong ordinal is rejected. The same source
occurrence must identify the same document and label throughout construction.
Different source occurrences may refer to the same document, representing
explicit repeated loads.

Repeated registrations are retained even when every input field is identical.
Repeated references within the original export list retain their distinct
selected export ordinals. Registry order is the caller's registration order,
which may differ from export order. `registrations()` exposes the owned
provenance and each sequential `registration_index`.

`lookup(filename)` returns an owned filename and one `ScriptLookupEntry` for
every registration. Each entry retains its qualified provenance and one state:

| State | Meaning |
| --- | --- |
| `no_match` | The whole info was inspected without finding this exact name. An exported empty info also produces this state. |
| `match` | The first matching row has supported owned storage. This does not mean its bytes are executable Lua. |
| `name_barrier` | An earlier row's name cannot be determined safely; a later apparent match cannot replace it. |
| `unsupported_match` | The first matching name is known, but its storage is unsupported. A later same-name row cannot replace it. |

Matches and barriers retain a `shared_ptr<const EmbeddedScript>` containing
the exact row metadata. Supported rows have owned bytes; barrier rows have no
usable byte buffer and retain the reader's diagnostic. Unknown names after an
established first match do not invalidate it. A malformed payload with a known
different name does not shadow the requested filename. Names compare exactly,
including case and the empty string; lookup names containing an embedded NUL
or exceeding 4,096 bytes are rejected.

The registry reuses `list_igb_scripts` to validate each distinct document once,
including its complete exported ownership lists. Thus malformed unrelated
ownership lists can reject construction under that reader's checked profile;
individual malformed script rows remain explicit barriers. Validated payloads
are copied directly from their checked memory references. Calling
`read_igb_script` separately for every row would repeatedly scan the complete
catalog and would report the first export occurrence for repeated info refs.

No document pointer survives construction. Both registry copies and retained
lookup plans keep their immutable selected bytes alive independently of the
source document. Lua programs are parsed only by the eventual executor using
the existing `read_lua4_chunk`; arbitrary or empty bytes can be valid storage
while remaining unsupported bytecode. An invalid selected chunk must stop its
execution path rather than cause lookup to try a later duplicate.

## Original selection and return convention

Evidence is from the supplied USA GameCube GDJEB2 revision 0 DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Registration through
`802F9F74..802F9FA8` reaches the append handler `80302378`. The selector
`80301FA4` reads the manager's ordered info registry at +14, each info's data
list at +1C, and compares names through `strcmp` at `8030214C`.

At a first matching name, `80302158..80302178` calls the script owner's
virtual +64 with the selected data object's buffer, byte length, and requested
filename. The branch at `80302180` ends only that info's inner search; the
outer registry loop continues at `80302194..803021A0`.

The original result is **the last matched dispatch status**:

1. `80301FC8` initializes r30 to -1.
2. Every matched dispatch overwrites r30 at `8030217C`. A later unmatched
   info leaves the earlier result intact.
3. Only final r30 == -1 (`803021A4/21A8`) invokes virtual +60 as a filesystem
   fallback at `803021DC`. Its result replaces r30 at `803021E0`.
4. `8030220C` returns this final value unchanged.

Fallback is therefore neither an any-success decision nor merely a count of
embedded matches. A later match returning -1 can require fallback after an
earlier result of 0; a later positive error status is also retained and does
not itself trigger the -1 branch. Lookup cannot determine this result before
execution. The plan deliberately has no execution-success or fallback-complete
boolean. A checked executor must also distinguish an unavailable host service
from an original numeric error result.

The actual script-owner vtable at `804DE7E4` has +64 = `80302AE4` and +60 =
`803025FC`. The embedded path delegates through `80272768` to `80278810`.
It returns a nonzero load status immediately; after a zero load status it
returns the protected call status from `80278648`. The protected runner
`802789E4` initializes its status to 0 at `80278A20` and returns that status
at `80278A74` on ordinary completion. Thus a fully completed supported chunk
corresponds to original status 0, independently of the chunk's Lua return
values. This does not assign an original numeric error code to an unimplemented
native effect or a checked host exception.

The selector prepares its fallback directory before embedded lookup. A null
directory argument formats the manager configuration's string as `%s/lua`
using literal `804263B8`; a supplied directory is retained directly. Fallback
formats `%s/%s` using literal `804263C0`. The registry does not perform these
native string-pool or filesystem effects and does not infer an archive path.

## Bounds and validation

The registry applies the existing script reader's per-document profile and
adds these checked aggregate bounds:

| Resource | Limit |
| --- | ---: |
| Explicit registration occurrences | 4,096 |
| Distinct source occurrences | 64 |
| Validated script-catalog rows across distinct documents | 65,536 |
| Registered script rows, counting repeated registrations | 65,536 |
| Catalog and owned metadata string bytes | 16 MiB |
| Owned payload bytes, counting every repeated occurrence | 64 MiB |
| Source label and requested filename | 4,096 bytes each |

The source reader additionally bounds each info-export list to 4,096 rows,
each script to 8 MiB, and its own cumulative names and expanded payloads. A
registry construction failure publishes no partially constructed registry.
Programs and VM execution use their own separate bounds.

`tests/script_registry_tests.cpp` covers arbitrary exact filenames, empty
names and buffers, hidden/wrong exports, explicit order, repeated source loads,
repeated registrations and repeated same-reference exports, first-match and
unknown-name barriers, independent later registrations, source and registry
destruction, and aggregate occurrence/byte limits. The optional archive check
selects `hit_p_agumon.lua` from original ndmw.afs entry 179, info 2783/export
237/data index 26; it verifies data 1700/object 2418/memory 2675, 3,230 owned
bytes, and lazy decoding as one Lua prototype with 534 instructions. Original
bytes and generated probes remain outside tracked files.

The isolated Windows `/W4 /permissive-` build of this new source and suite,
linked against the existing Release core library, passes 44 synthetic checks
and 51 with the optional original archive. This is separate from the final
integrated core build and does not claim original selector machine execution.
