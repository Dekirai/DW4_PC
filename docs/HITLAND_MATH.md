# Original HITLAND arithmetic

`hitland_math.hpp` provides two bounded helpers used by the original collision
path. It does not supply the `HITLANDMODEL` triangle traversal or response backend.
The preview collision mesh reader and walking solver remain separate; see
[Walking collision research](WALKING_COLLISION_RESEARCH.md).

`original_hitland_wall_direction(current, previous)` follows the
`HITLANDDELIVER/HITWALL` arithmetic at `0x802FDAF0..0x802FDC0C`. It subtracts the
reached previous-position X/Z from the captured current X/Z, sets Y to the
original positive zero, accumulates squared length in the original Y/X/Z order,
then applies the rounded length, separate reciprocal and three products.
Scalar arithmetic follows startup NI=1/RN=00, including signed-zero preservation
and flushing exact tiny arithmetic results before rounding. Unused Y coordinates
are neither read nor validated.

The supported numeric profile requires finite used coordinates, finite positive
squared length and finite output. Zero or underflowed squared length, overflow
and nonfinite used coordinates raise `CollisionMathError`. These are explicit
checked boundaries; the original nonfinite fallback is not claimed. The caller
performs the original exact X/Z equality gate and keeps its earlier packet
effects if normalization reaches a boundary. This helper does not read RAM or
select a node, so the recipient retains responsibility for the original capture
and fresh-read sites.

The original positive-length branch uses `frsqrte` and three binary64 Newton
corrections before `frsp`. Production uses the matching rounded binary32 square
root. A separate ignored instruction oracle verifies that choice over its bounded
corpus. The constants read from the supplied USA DOL are float positive zero at
`0x804260C0`, double `0.5` at `0x804260C8`, double `3.0` at `0x804260D0`, double
zero at `0x804260D8`, and float `1.0` at `0x804260E0`.

`original_hitland_xz_overlap(model, query)` reproduces `0x80300974`:

1. Model maximum X is greater than or equal to query minimum X.
2. Model minimum X is less than or equal to query maximum X.
3. Model maximum Z is greater than or equal to query minimum Z.
4. Model minimum Z is less than or equal to query maximum Z.

Each failed comparison returns immediately. Touching bounds are included; Y is
ignored; inverted bounds retain these tests; unordered reached comparisons fail.
There is no added finite-box or sorted-bounds validation. This helper supplies
the original X/Z broad phase, not a segment/triangle intersection test.

Validation includes 44 focused checks for directions, arithmetic boundaries,
signed zero, ignored fields and ordered bounds. The local raw-instruction oracle
executes the supplied USA DOL normalization block and complete X/Z helper using
independently rounded integer single/double arithmetic and an explicit `frsqrte`
instruction model. Its 8,080 cases execute 201,953 instructions with zero native
comparison mismatches: 3,906 supported positive lengths, 3,166 checked numeric
boundaries and 1,008 X/Z cases. All 3,906 positive lengths also match the rounded
binary32 square root. This is bounded instruction-model validation, not physical
Gekko testing or a proof of the complete collision backend. Original bytes and
the research driver remain under ignored `work/`.
