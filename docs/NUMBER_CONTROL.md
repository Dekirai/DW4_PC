# Original NUMBERCTRL data and geometry

The native core now owns the original `NUMBERCTRL` recipients, number work,
digit mappings and mutable texture coordinates. `SETVALUE` completes the actual
number-geometry update before publishing its current and target values. It does
not merely remember an integer or manufacture a HUD widget. Drawing the HUD,
attaching these owners to a scene renderer, and scheduling manager updates remain
separate integration work.

All source was independently recovered from the owner's USA executable and
local serialized objects. The inspected `main.dol` SHA-1 is
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. No original bytes, disassembly,
textures or generated resource exports are distributed.

## Recovered ownership and layout

The exact exported type is `beNumberCtrlInfo`. It adds no fields to
`beBaseInfo`: nullable name, raw active byte, nullable data list, and the ordered
RAM list retain their serialized identities. Generic initialization writes each
RAM occurrence's parent and ordinal and resets its four base words. Repeated
physical RAM references share one owner; the final parent/ordinal writes win.
Each registration occurrence remains a separate messenger row. A null info name
registers as the original empty-string literal.

`beNumberCtrlInfoRam` has a native size of `0x44`; its additional fields are:

| Serialized slot | Native offset | Field |
| --- | --- | --- |
| 11 | `+2C` | `_geometry`, nullable `igGeometry` |
| 12 | `+30` | `_offsetNext`, float atlas-cell width |
| 13 | `+34` | `_maxValue`, signed maximum |
| 14 | `+38` | `_mode`, signed leading-digit mode |
| 15 | `+3C` | `_numList`, nullable `beNumVerDataList` |
| 16 | `+40` | `_work`, nullable `beNumberCtrlInfoWork` |

Each `beNumVerData` owns a signed `_disp` word and its
`beNumVerInsideDataList`. Each mapping owns signed `_index` and `_kind` words.
The helper consumes exactly six mappings per digit in original order. Additional
authored mapping entries are preserved but are not consumed. Repeated digit
references retain shared display words and original list order.

The number work has three signed fields at `+08/+0C/+10`: `_dispNum` (current),
`_realNum` (target), and `_changeNum` (step). Authored work aliases remain shared;
a null work reference receives its own zero-initialized native allocation when
the original command/update branch requires one. This is an independent number
work object, not a `GameDataInt` wrapper. MODELCTRL player startup sends scalar
integers read from its captured player-data row.

The writable geometry owner is resolved through the geometry's first attribute,
its `igVertexArray1_1`, and the verified little-endian float2 UV stream 11. Native
storage is shared by the actual serialized memory identity, including different
vertex-array owners that reference the same memory. Separate loaded source
occurrences have separate mutable owners. The immutable decoded program remains
unchanged. `NumberRamState::uv` exposes the authoritative current coordinates
and publication revision for subsequent rendering integration.

## Original operations

`8031B2CC` registers the current RAM as `NUMBERCTRL/name/index`.
The actual structured hook at vtable `+90` is `8031B314`; fallback `+8C` is the
original declining base hook `802F24A0`. The four-entry command dictionary is
initialized by `802C3E9C` from the table at `804D0450`.

- `GETVALUE` appends the target word when work exists. Null work appends nothing.
  Existing argument/result lists remain intact.
- `SETVALUE` allocates missing work before checking the integer count. An empty
  integer list then returns successfully without changing UVs. Otherwise integer
  zero is first clamped upward to zero, then downward to the signed maximum.
  These are sequential checks: a negative maximum can produce a negative result.
  The digit/UV helper runs before current and target are assigned and step is
  reset to zero.
- `ADDVALUE` also allocates before checking arguments. It adds integer zero to
  the target with original 32-bit wrapping, applies the same sequential clamp,
  and recomputes the step from wrapped `target - current`. It does not change
  displayed geometry immediately.
- `SETVALUEDIRECT` is a dictionary entry which the original handler declines.
  Unknown commands are also declined; neither is an alias for `SETVALUE`.

These command paths have no RAM activity check. The existing messenger's exact
enabled-only gate still uses the live raw active byte when filtering recipients.

The display helper `8031AF04` writes least-significant digits first. Zero still
displays one zero. Remaining leading digits become zero in mode zero, or `-1`
in any other mode. A negative value fills every digit using that same mode rule.
All display-word assignments finish before the helper reads those words to write
UVs; this order matters when a digit is aliased within a list.

For a displayed digit `d` and cell width `w`, corner kinds `0/1/2/3` write
`(d*w,0)`, `(fma(d,w,w),0)`, `(d*w,1)`, and `(fma(d,w,w),1)` respectively.
The right edge uses the original single-rounded fused multiply-add. Another
kind preserves the existing coordinate. A negative displayed digit writes
`(0,0)` regardless of corner kind. Mapping aliases are processed in order.
The UV revision advances at the writable-stream release boundary, including an
empty but non-null digit list.

`update(id)` performs one explicit invocation of `8031AD7C`. State words two and
three skip processing; state one processes work directly; other state words
allocate missing work, increment the state and clear the other three words.
When current differs from target, wrapped `current + step` advances the display;
overshoot is clamped according to step sign, and a zero step selects the target.
The UV helper completes before current is published. No timer, frame rate,
activation policy or original outer manager schedule is inferred.

Step helper `8031B234` divides signed delta by 128 toward zero and adds one in
the delta's direction. Magnitudes are capped at 99. A nonzero multiple of ten
is moved one farther from zero. Zero delta gives zero step.

## Bounds and verification

The decoder checks exact field types/widths, duplicate slots, list storage,
counts/capacities, all consumed mapping indices, finite atlas results and the
existing verified mesh/UV profile. Per source the limits are 4,096 infos and RAM
occurrences, 65,536 exports, 262,144 list links, 256 digits per RAM, 4,096 mappings
per digit, 64 MiB decoded UV storage and 8 MiB name storage. Names/source labels
are at most 4,096 bytes. Big-endian geometry is an explicit unsupported profile.
Required missing display owners cause a checked host failure; a scalar-only
setter never reports successful completion. These host rejections are not
claims about original crash behavior or allocation failure.

`number_control_tests` is asset-free by default. It covers argument allocation
order, append semantics, signed wrapping/clamps, animation steps, actual corner
writes, leading digits, unknown commands/corners, raw active bytes, alias and
source ownership, facade lifetime, nullable fields, malformed graphs and atomic
registration rejection. An optional extraction-root argument checks the local
original `dng101b1.igb` package member `[0.179]` in `ndmw.afs`. This packaged member
is distinct from the catalog resource with the `usa_pack.igb` logical name.
Its seven number infos contain 43 RAM occurrences, including four slots each
for `hpnm`, `lvnm` and `mpnm`.

An ignored independent instruction oracle executes `8031B314`, `8031AF04`,
`8031B234` and `8031AD7C` from the supplied executable against synthetic guest
memory. It compares production work state, all digit display words, every UV
word, integer-list output and writable-stream publication. The current run has
1,457 cases and 266,733 executed instructions with no mismatches. Cases include
missing/present work, empty arguments, signed extrema, negative maxima, wrapping,
mode zero/nonzero, aliased digits and mapped vertex indices, unknown corners, nonbinary cell widths and
fused rounding. Allocation, dictionary lookup, typed integer-list access and
vertex lock/release are explicit prepared services. This is an instruction-level
comparison, not an independent renderer, physical-console or full-HUD test.
