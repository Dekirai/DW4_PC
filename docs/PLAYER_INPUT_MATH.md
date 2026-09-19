# Original player heading arithmetic

`original_player_input_heading()` implements the numerical path at
`8034C67C..8034C6B8`. Its input is the retained inverse camera matrix and the
two stick values already captured by the player input invocation. It returns
the desired Y heading without publishing work fields or changing owners.

The caller retains responsibility for the original camera matrix and inversion.
The original input callback initializes its local inverse destination to
identity, calls `8012A92C`, and ignores that operation's success flag. A singular
matrix therefore retains that initialized destination. This helper neither
invents a camera owner nor changes the inversion policy.

## Recovered operations

The vector operation `8012D434` transforms `(0,0,1)` through the inverse's
three rotation columns. Separate Y products precede the fused X and Z terms;
even zero products are retained. Homogeneous and translation fields are not
consumed. All three output components are calculated, although only X and Z
are passed to the heading calculation.

The callback then calls MSL `atan2` at `800A2C74`, whose actual implementation
is `8009F8B0`, for both `(forward.x, forward.z)` and `(left_x, -left_y)`.
Each result is independently narrowed to binary32 before their single-precision
sum. The result is not normalized into a particular angular interval.

The authored implementation reconstructs the finite paths of that MSL routine
and its `atan` helper at `800A21F8`. It retains the original signed-zero cases,
exponent-distance branches, range-reduction thresholds, polynomial coefficients,
and separate binary64 operations. Original fused operations use explicit
`std::fma`; host `atan` and `atan2` are not substituted. Strict floating-point
compilation prevents implicit contraction of the remaining operations.

NI=1 with round-to-nearest-even applies to the original single operations and
the two result narrowings. An exact result below the minimum normal binary32
magnitude becomes signed zero before rounding. For example, with a forward
camera, `left_y=-1` and `left_x` represented by `00000001`, the original narrowed
angle is zero. A plain host `atan2` followed by a float cast retains the smallest
subnormal instead. The minimum normal input `00800000` remains nonzero.

## Supported profile and validation

Reached matrix arithmetic and stick operands must be finite; unsupported
reached arithmetic throws `AnimationMathError`. Unused matrix fields are not
validated. This is a numerical reconstruction, not a claim about guest FPSCR
exception flags, trapping behavior, paired-register ABI state, or nonfinite MSL
branches. The finite binary32 inputs constrain every reached binary64 quotient
to the normal representable range.

The permanent tests contain numerical goldens independently obtained by
executing the original vector and MSL instructions. They cover all four `atan`
reduction regions, quadrants, oblique camera/stick combinations, signed zeros,
tiny results, the minimum normal boundary, and reached versus unused invalid
matrix fields. They contain no original instructions or game assets.

An ignored independent oracle decodes the supplied original instruction words
and rounds scalar arithmetic using Python integers. Its expanded corpus checks
every signed-byte stick pair after division by74, camera/stick boundary values,
arbitrary finite and nonfinite word profiles, and unused matrix fields. This
separately validates the authored formulas; it does not derive expected values
from the native helper or host trigonometric functions. Measured results are
also summarized in `VALIDATION.md`. The final driver linked only the completed
production core and passed 71,162 comparisons with zero mismatches: 70,934 exact
numerical results and 228 explicit finite-profile boundaries. The raw execution
covered 17,011,398 instructions at 283 distinct addresses. The 65,536 signed-byte
axis combinations are exhaustive for that discrete input grid; the remaining
finite matrix/stick space is sampled, not exhaustively enumerated.

This helper does not establish camera manager startup, device assignment,
pad polling, frame scheduling, movement/collision completeness, or gameplay.
