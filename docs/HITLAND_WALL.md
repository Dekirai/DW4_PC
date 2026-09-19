# Original CHECKWALL wall response

`HitLandWallInvocation` reconstructs USA `802FE7F0..802FF744` in the native
core. It uses the original query segments, common traversal, geometry candidates
and three-pass correction order. `make_hitland_query_receiver` supplies retained
CHECKLAND and CHECKWALL deliveries plus the original disabled-wall gate.
CHECKWALLFULL is a separate original algorithm and remains unimplemented.

## Request and manager state

The manager owns the original wall-enabled byte, initially one, and direction
rotation matrix from reset `802FE1DC`. The matrix comes from Euler rotation
`(0, binary32 pi/2, 0)`, including cosine word `B33BBD2E` and original signed
zeros. An exact axis swap would change later arithmetic. The constructor only
provides these native field defaults; it does not implement manager startup.

The enclosing receiver checks wall-enabled state before the CHECKWALL or
CHECKWALLFULL leaf. Disabled walls clear booleans and append false while
preserving all other packet lists, even for a short request. CHECKLAND is
independent of this gate. Once a leaf has entered, its command and continuation
remain selected across suspension.

CHECKWALL returns without edits if the boolean list is nonempty or the vector,
float or integer list is empty. Otherwise it captures two vectors (position and
direction), three floats (width, an unused but required upper-range read, and
vertical offset) and two integers (query flags and preflight bypass). Missing
indexed arguments fault before integers are cleared. The body then clears only
integers; existing strings and the other typed lists remain.

Let H be separately rounded `1.42f * width`. The base Y is position Y plus the
vertical offset. The side vector is the original rotated and normalized
direction, with separately rounded squared terms and the recovered positive
square-root path. The original direction remains distinct and is not normalized
again here. Endpoints at base minus/plus H times that direction are retained
through the first two passes.

## Three ordered passes

Each reached pass resets the manager's current query, captures that query owner
and sets flags to the request flags with bit zero cleared. A later manager query
replacement does not retarget an already suspended traversal.

1. **Wide preflight.** Unless the second input integer is nonzero, append three
   segments: the retained center segment and copies displaced by plus/minus
   H times the side vector. No candidates means immediate unchanged-position,
   false-hit publication; the other two passes are skipped.
2. **Longitudinal correction.** Append the same center segment and offsets of
   half H. Capture doubled H before the first append. Select the first minimum
   candidate distance, retaining a scalar minimum and the original tie rule.
   Subtract the remaining gap along the original direction using fused
   operations, then append that hit's index and nullable name immediately.
   A miss still proceeds to the lateral pass.
3. **Lateral correction.** Use the corrected position and a forward displacement
   computed as separately rounded `3 * H`, then `* 0.25`. Append two segments
   from that center to directly fused plus/minus side endpoints. This minimum
   loop reloads the current winning candidate for each comparison. The chosen
   raw normal drives the correction without normalizing it, followed by another
   index/name append. Both metadata pairs remain if both correction passes hit.

X/Z extrema use the original scalar comparison order and strict mode values.
Equal values retain their existing signed zero; unordered comparisons do not
store. Bounds Y is the current position Y, not the offset ray height. The first
two passes use a rounded displacement followed by coordinate addition; lateral
endpoints use direct fused multiply-add. These expressions are not interchangeable.

Final publication clears booleans, clears vectors, appends the corrected vector
and then appends the hit byte. Earlier metadata and successful appends survive a
later failure. Pending calls retain their request, endpoints, query, traversal
and completed effects; completed calls do not replay them. Recursive entry,
changed messenger ownership and retries after failure fault explicitly.

## Original arithmetic and verification

The authored helpers in `hitland_wall_math.cpp` use strict floating-point
compilation and the established finite NI=1, RN=00 profile. They preserve
subnormal loads, signed zeros, fused versus separate operations, pre-round
tininess and pass C's explicit X/Z narrowing. Reached nonfinite arithmetic,
zero represented lengths and unsupported numeric inputs are explicit boundaries.

The lateral factor includes two independently reached square-root calculations
of the same normal's squared length, a division, ordered clamp, original acos,
binary32 narrowing, original cos and another narrowing. It is not replaced by
constant one or an assumed host-libm identity. An independent interpreter of
the original MSL routines verifies all 17 binary32 inputs `3F7FFFF0..3F800000`;
each returns its own bits after both narrowings. The native helper represents
that checked mapping, applies the original upper clamp, and rejects inputs
outside the verified domain.

A 33,554,432-case sweep covers all binary32 significands in both squared-length
exponent parities and the extremal exponents. It compares the original reciprocal
square-root estimate and three binary64 Newton steps with native sqrt, finding
zero rounded differences.
The ratio takes only words `3F7FFFFE`, `3F7FFFFF`, `3F800000`, `3F800001`;
the last is clamped to one. Power-of-four scaling preserves the normal finite
calculation; range endpoints are checked separately. An independent integer
instruction interpreter cross-checks 8,196 samples and 147,528 instruction steps.
The reciprocal-square-root seed model remains a research assumption; these are
not physical Gekko measurements. This supports the reached positive finite
profile, not arbitrary trig or full FPSCR exception behavior.

The separate helper corpus compares 5,089 cases with 272,900 actual original
instruction steps at 530 addresses: 4,604 exact outputs and 485 checked
arithmetic boundaries, with zero mismatches. Permanent authored fixtures retain
reset-matrix words, oblique side normalization, split/fused rounding differences,
a longitudinal correction, nonunit lateral normals and a lateral factor
measurably below one. Original
instruction words remain local and ignored.

## Integration boundary

The local Release build passes 118 CTest suites. The wall suite passes 131
synthetic and 163 original-inclusive checks. Synthetic tests distinguish scripted
traversal callbacks used for control-flow checks from real mapped geometry.
An authored vertical triangle produces actual
wall candidates and a correction that a registered HITWALL recipient applies to
its shared node. Supplied-data checks cover fifteen wall-node centers in
`ndmw.afs[179]`, producing fourteen corrections, then recompute and apply a
selected response through a registered HITWALL delivery to a prepared node.

The existing original player-construction test now registers all five collision
info occurrences from that stage's actual info list, including dynamic geometry.
After four prepared original GENERATE calls, its first neutral player frame
enters HITLANDDELIVER, native CHECKWALL and real stage traversal. It reaches the
required dynamic-transform global-clock virtual `+68`; retries retain the query
and frame prefix without repeating earlier work. No substitute clock, transform
update or collision result is installed to force frame completion.

Original dynamic clock/update services, missing geometry bounds, other bound
classes, CHECKWALLFULL, startup and the complete frame scheduler remain required.
The walking preview still uses its provisional movement and camera. These core
tests do not establish a complete gameplay session or connected multiplayer.

Build and run the supplied-data integrations locally:

```powershell
.\scripts\build.ps1 -Configuration Release -CoreOnly -Jobs 4
.\build\Release\dw4_hitland_wall_tests.exe 'PATH\Extracted'
.\build\Release\dw4_model_player_create_tests.exe 'PATH\Extracted'
```

See [VALIDATION.md](VALIDATION.md), [HITLAND_FLOOR.md](HITLAND_FLOOR.md),
[HITLAND_DELIVER.md](HITLAND_DELIVER.md) and
[HITLAND_NODE_DISPATCH.md](HITLAND_NODE_DISPATCH.md). Original DOL SHA256 remains
`EC61C010477C844781763A3041922E38719B3CED3B74BE5CE71D778CE458E471`.
