# Original camera view publication math

`original_camera_view_inverse` reconstructs the matrix operation at USA
`8012A860`, called by the concrete `beCameraMode` update at `802F4418`.
It publishes the transposed 3x3 rotation and negative rotated translation
into an existing sixteen-float matrix. It accepts fixed-extent spans;
`AnimationMatrix` arguments convert directly.

This helper implements only that numerical operation. Camera selection,
mode timing, graph evaluation, Euler decomposition/reconstruction, observers,
and viewer snapshot publication remain separate operations. The mode writes
the `igViewModel` matrix at owner `+0C`; this is distinct from the
`igViewerParameters::view_matrix` snapshot at `+6C` described in
[VIEWER.md](VIEWER.md).

## Specialized operation and store order

The original performs no determinant test, normalization, or generic inverse.
For disjoint matrices, it transposes the source's upper 3x3 and writes affine
constants into destination indices 11, 7, 3 and 15. Scale, shear or a zero
rotation block do not select a different path. Source indices 3, 7, 11 and
15 are not read in the disjoint case, even if they contain nonfinite words.

Loads and stores are interleaved. In particular, an in-place call overwrites
source entries before some later reads; it does not produce the result of a
safe transpose through a temporary matrix. Partially overlapping spans retain
the same ordered behavior. Translation Y, X and Z are captured between the
affine constant stores, before the three translation result stores. The
implementation deliberately preserves that ordering instead of snapshotting
the input.

For each destination translation component, the scalar sequence is:

1. Round translation Y multiplied by the destination Y coefficient.
2. Fuse translation X multiplied by the X coefficient with that result.
3. Fuse translation Z multiplied by the Z coefficient with that result,
   then negate the rounded result.
4. Publish the component before evaluating the next one.

The final negation belongs after rounding, including for exact zero. The
routine's last separate store writes a framework metadata word through its
`r3` return wrapper. This matrix-only C++ API does not expose or manufacture
that wrapper. Original callers' observer notifications likewise are outside
this helper.

## Arithmetic profile

The supported startup profile is Gekko NI=1 with nearest-even rounding.
Scalar arithmetic retains subnormal operands and flushes exact tiny results
to signed zero before rounding. Pure copies retain their source words.
Products use a binary64 exact-product check at the minimum-normal boundary;
fused operations retain the low part of the product-plus-addend sum with
TwoSum so a tiny exact result cannot become minimum-normal through double
rounding. The final fused operation uses an explicit post-round negation.

Reached arithmetic operands and results must be finite. Unsupported operands
or overflow throw `AnimationMathError`, preserving all completed destination
stores. Source fields are not prevalidated as a block, and a failure does not
roll back the previously published rotation/constants or earlier translation
components. This finite profile does not implement complete guest exception
flags, signaling-NaN behavior, or general nonfinite arithmetic. The host
requires nearest-even arithmetic with gradual operand handling and the
module's strict floating-point compiler settings.

## Validation

Permanent tests cover disjoint and in-place matrices, overlap in both
directions, signed zero, copied subnormals, fused cancellation, pre-round
minimum-normal thresholds, ignored homogeneous fields, singular/scaled
inputs and visible prefixes on later arithmetic failure. The expected overlap
and rounding fixtures were evaluated independently through the original
instructions; no original instruction bytes are included in this module.

The ignored research oracle decodes the supplied USA DOL with SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, using Python integer arithmetic
for independent single-precision rounding. It executes `8012A860` and compares
the complete surrounding buffer, including aliases and failure prefixes.
The return metadata store is checked separately in the raw fixture; it is
not claimed as part of the native matrix API. The corpus has 15,474 cases:
11,646 completed calculations and 3,828 cases that reach the explicitly
bounded finite-arithmetic limit. It executes 730,725 original instructions
across all 51 addresses, with zero native comparison mismatches. It includes
every whole-float overlap displacement from -16 through +16, exact rounding
fixtures, arbitrary finite exponent patterns and quiet nonfinite boundary
fixtures. This is a bounded corpus, not exhaustive binary32 validation.
The final comparison driver links the production `dw4_core.lib` without
replacement implementation objects. The permanent suite contains 20 checks;
the integrated local build passes 126/126 CTest suites. Load/store ordering,
alias behavior and arithmetic failure prefixes also received an independent
review against the original instructions.

Validation of this helper does not establish a complete active camera
producer or a rendered gameplay frame.
