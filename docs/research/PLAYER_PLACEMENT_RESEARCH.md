# Original player placement

The original `dng101b1` resource exports two groups of four player placement
matrices. `PLAYERPOINT` returns one of these matrices, and the original player
generation path copies it over the generated model node's outer transform.
The authored editor-layout transform at that node is replaced; multiplying
the placement by that editor transform would introduce an incorrect offset.

The native API supports explicit group/player selection and affine placement
of actor-local vertices. It does not select the startup group, execute
`SETGENERATEPOS`, create players, or establish collision, movement, camera, or
visual parity with an original frame.

Evidence comes from the local USA `sys/main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877` (`GDJEB2`), and the user's extracted
resources. Addresses below are guest virtual addresses. Original bytes,
disassembly and probes remain in ignored `work/`; no original executable code
or game payload is included in the native implementation.

## Exported point graph

The named package `dng101b1.igb` resolves through descriptor `ndmw.afs` entry
80. Its member 1 is `[0.179]`; these references belong to member entry 179,
not the descriptor document. See [PACKAGES.md](../PACKAGES.md).

`igInfoList` reference 1236 contains exact `bePoint01Info` reference 115 at
zero-based export index 6. Its name is empty. The descriptor type registration
at `0x802c0ecc` gives runtime size `0x24`; member metadata at `0x804cffe4`
names the two extra fields:

| Serialized slot | Runtime offset | Field | Original reference |
| ---: | ---: | --- | ---: |
| 7 | `0x1c` | `_switch` | `igSwitch` 118 |
| 8 | `0x20` | `_dataList` | `beGroupKeepList` 114 |

The switch controls display of the point visualization. It does not gate
matrix lookup. Data list 114 contains `beGroupKeep` groups 39 and 41 in that
order. Group 39 has child list 113; group 41 has child list 122. Each contains
four exact `igTransform` nodes. The point matrices are in transform slot 8;
all observed targets are zero and transform-input references are null.

| Group index | Player index | Transform reference | Translation `(x, y, z)` |
| ---: | ---: | ---: | --- |
| 0 | 0 | 98 | `(-300, 40, 431)` |
| 0 | 1 | 100 | `(-300, 40, 438)` |
| 0 | 2 | 102 | `(-300, 40, 445)` |
| 0 | 3 | 139 | `(-300, 40, 452)` |
| 1 | 0 | 141 | `(324, 24, -440)` |
| 1 | 1 | 142 | `(331, 21, -440)` |
| 1 | 2 | 24 | `(338, 19, -440)` |
| 1 | 3 | 112 | `(345, 16, -440)` |

These are original game coordinate units. No conversion to meters or other
physical units is established or applied. The translation is already in the
same coordinate system consumed by the model placement path; the point group's
parent graph is not multiplied into it by `PLAYERPOINT`.

## Command and selection semantics

Registration at `0x803129b0` associates the point controller with category
`PLAYERPOINT`. The command enum-name table at `0x804d0010` and value table at
`0x804d002c` map:

| Value | Command | Effect in handler `0x80312a70` |
| ---: | --- | --- |
| 0 | `SHOWPOLYGON` | Set all bits of the visualization switch |
| 1 | `HIDEPOLYGON` | Clear all bits of the visualization switch |
| 2 | `GETMATRIX` | Select saved point-group index and second integer argument |
| 3 | `GETMATRIXDIRECT` | Select first integer argument as group and second as player |
| 4 | `GETPOINTNAME` | Return point-group names in data-list order |
| 5 | `SETGENERATEPOS` | Store first integer argument in controller `_pointNo` |
| 6 | `GETGENERATEPOS` | Return the saved `_pointNo` |

The `_pointNo` metadata at `0x804d004c` gives controller runtime offset `0x20`.
`SETGENERATEPOS` at `0x80312ba8` through `0x80312bb4` changes this integer; it
does not directly move an actor. The `GETMATRIX` branch at `0x80312ae8` uses
that saved integer, while `GETMATRIXDIRECT` at `0x80312b18` reads both indices
from arguments. Both call the same helper, `0x803129e0`.

That helper reads `_dataList[group]`, then the selected group's
`_childList[player]`, and passes the child's matrix at runtime offset `0x20`
to the command matrix-output routine `0x80306d68`. It performs no matrix
multiplication, scale normalization, heading extraction, coordinate conversion,
or switch-mask test. Its native out-of-range behavior is not a safe parser
contract: it changes an excessively large group index to zero and assumes
other list accesses are valid. The host instead rejects any invalid explicit
group/player index and all null required graph links.

The read-only Lua 4.0 analysis described in
[SCENE_ACTIVATION.md](SCENE_ACTIVATION.md) finds a transition helper in
`func.lua` that queues `PLAYERPOINT`, nil, `-1`, `SETGENERATEPOS` with an integer
payload constructed from the destination group. It then queues `NEXTSTAGE`.
In the local disassembly these are PCs 372 through 383 and 384 through 392 of
that helper. This connects point-group selection to transitions, but does not
establish which group is the initial startup state. `first.lua` reads
`PLAYERgetPos` before choosing an entry sequence; that read is not itself a
new placement command. It returns the group the last `SETGENERATEPOS` chose:
`802EB4C4` calls `802EE1F0`, which queues `PLAYERPOINT`, `GETGENERATEPOS`
and returns int 0 of the reply (so `stagML12` takes its transporter entrance,
group 3, after the evolution scene). The group survives stage loads, so save
state snapshots keep it.

## Orientation is retained as a matrix

The matrix convention is the same verified column-major, column-vector
convention as [static scenes](SCENE_RESEARCH.md). Translation occupies elements
12 through 14. Group 0's linear part is approximately `diag(-1, 1, -1)`;
its tiny off-diagonal values are retained exactly. Group 1 maps local positive
X to positive Z and local positive Z to negative X, with local positive Y
unchanged. The host preserves the full stored values, including signed zeros.

These observations describe basis mappings, not a guessed facing axis or a
gameplay heading field. A separate original helper at `0x802ee240` converts
matrix-derived data into a position/orientation string, but it is unnecessary
for the verified matrix-consumption path and is not ported by this API.

For an input point `(x,y,z)`, placement computes:

```text
x' = x*m[0] + y*m[4] + z*m[8]  + m[12]
y' = x*m[1] + y*m[5] + z*m[9]  + m[13]
z' = x*m[2] + y*m[6] + z*m[10] + m[14]
```

The original point routine at `0x8012d498` establishes this arrangement.
The host uses double intermediates and checks conversion back to finite float;
it does not claim instruction-identical PowerPC rounding.

## How player generation consumes the matrix

The original generation routine `0x8034d61c` has a branch for a request with
neither explicit vector nor matrix arguments. At `0x8034d8e4` through
`0x8034d910`, it selects `PLAYERPOINT`, pushes integer zero and the player
record's instance index at offset 8, and sends `GETMATRIX`. The first argument
does not override `_pointNo` on this command. If a returned matrix exists, the
routine copies it into temporary storage; it then makes that matrix available
to `0x8030d610` at `0x8034d954` through `0x8034d95c`.

`0x8030d610` obtains the bound runtime record's data dictionary at offset
`0x28`, looks up `ONE` using the literal at `0x804266c0`, and reads its node
pointer at offset `0x10`. Its matrix-only branch at `0x8030d794` through
`0x8030d7d4` copies the command matrix and invokes this node's virtual setter
at vtable offset `0x94`. This path does not multiply by the existing matrix.

The `beModelCtrlNode2` constructor at `0x802c5ee8` installs vtable
`0x804d9dcc`. Its slot `0x94`, at `0x804d9e60`, is `0x801166b8`, the same setter
used by `igTransform`. That setter adds `0x20` to the node address and calls
the 64-byte matrix-copy routine `0x80128cf4`. This proves replacement of the
node's serialized slot-8 transform, including editor-layout translations.

The replacement is specific to this outer node boundary. Descendant
transforms, an actor's own matrix, skinning output coordinates, and model
selection must be established by the actor reader. Applying placement directly
to a supported actor-local pose is valid only when that reader has accounted
for all transforms below the replaced node. This evidence does not authorize
discarding arbitrary descendant transforms or applying placement twice.

## Native API and validation

`list_igb_player_placements()` follows the explicit `igInfoList`; unexported
objects are ineligible. Exact `bePoint01Info`, `igSwitch`, `beGroupKeepList`,
`beGroupKeep`, `igNodeList`, and `igTransform` types are required in the
supported path. Unknown derived descriptors remain visible with diagnostics.
The initial profile requires four points in every nonempty group, finite
affine matrices, transform target zero, null transform input, and only verified
static node flag bits. The visualization switch mask is not consulted.

`decode_igb_player_placement(document, info, group, player)` returns the full
matrix and descriptor, group, transform, and ordinal provenance.
`apply_player_placement(placement, positions)` transforms an owned result from
the supplied actor-local points. It revalidates the matrix even when supplied
by a caller rather than the decoder, rejects non-finite inputs and float
overflow, and leaves source bytes and local positions unchanged.

Limits include 4,096 exports, 4,096 groups cumulatively across a listing,
16,384 point records, 4,096 decoded bytes per label, 4 MiB of cumulative stored
name-field bytes, and
4,194,304 vertices per placement application. List count/capacity and memory
ranges are checked before allocation. Repeated group-list references consume
the cumulative budget; they cannot multiply unbounded work across exports.
Name fields are bounded and charged before scanning or trimming NUL padding;
the per-field bound includes the length word, terminator and alignment padding.
Repeated references charge stored name bytes on every scan, even when the
decoded label is empty.

An isolated MSVC Release build with `/W4` passes 66 synthetic checks. They cover
full matrix preservation, noncommuting rotation/translation, nonuniform scale
and reflection, invalid/null/derived links, missing exports, malformed list
limits, unknown field types, non-finite and projective matrices, dynamic input,
aggregate shared-list work, oversized and repeatedly shared padded labels,
source immutability, and output overflow. The
optional original-data run reads all eight placements above and compares all
128 float components bit-for-bit with their serialized matrix bytes.
