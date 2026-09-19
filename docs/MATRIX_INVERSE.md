# Original native matrix inverse

`original_matrix_inverse` reconstructs USA `8012A92C`, its adjugate helper
`8012AA04` and determinant helper `8012AD50` in authored C++20. It consumes and
produces the native `AnimationMatrix`; it does not execute guest code or link
the set-aside recompilation runtime. The shared implementation is now used by
the native HITLAND geometry producer when no inverse override is installed.
The player-input camera inverse is another consumer still to connect.

## Behavior

The source-dependent cofactors and determinant are computed before destination
publication, so source and destination may be the same matrix. The original
three-term cofactor order uses a rounded product, fused subtraction and fused
addition. Final determinant negation occurs after rounding, preserving signed
zero. A general projective matrix follows the same path as an affine matrix.

The singular threshold loaded from `8055DE28` is **`0x00800000`**, the smallest
positive normal binary32 value. It is not a conventional approximate-inverse
tolerance. An absolute determinant below that threshold returns false and
leaves all destination words unchanged. Equality is accepted. Successful output
is scaled and stored in flat index order. A checked scaling overflow leaves
earlier stores intact; no transactional rollback is introduced.

The arithmetic currently covers startup NI=1, round-to-nearest-even finite
values, including subnormal inputs and tininess before rounding. Nonfinite
source or intermediate arithmetic raises `AnimationMathError`. Full nonfinite
arithmetic and original FPSCR exception flags remain outside this checked
profile. Build this source with strict floating-point options; generic LU,
double-precision inversion or reassociation would change the original results.

## Collision integration

For canonical `igAABox` bounds and a matrix-stack depth greater than one,
`HitLandGeometryInvocation` copies the local bound into the visitor's retained
AABox scratch, transforms it using the current physical top matrix, and then
computes the inverse for the segment loop. The registration at `80122AFC`
installs thunk `80123840` through `80127FFC`; dispatch `801283DC` resolves it
through `80127DC0`. The thunk calls the already recovered `80122D04` box
transform. Other bound classes still need their own native representations.

Scratch identity persists across visits; each visit copies the source again,
so transformations do not accumulate. Invalid matrix storage faults after the
completed bound copy. Original segment owners remain unchanged; a distinct
scratch ray receives the transformed segment. Existing service overrides may
still suspend and resume without replaying a completed bound operation.

The geometry caller ignores the original inverse status. If a singular matrix
leaves its local inverse unwritten, the native caller faults only when a segment
actually consumes that matrix, after copying the scratch ray. An empty segment
loop completes without inventing a usable inverse. Identity is never substituted.
Candidate normal publication keeps the original direct matrix multiplication;
it does not normalize or apply inverse transpose.

## Evidence

Validated locally on 2026-09-19 against the supplied USA DOL with SHA256
`EC61C010477C844781763A3041922E38719B3CED3B74BE5CE71D778CE458E471`.
Authored source is in `src/matrix_inverse.cpp`; the permanent synthetic tests
contain mathematical fixtures and original-instruction comparison results,
without original instruction words or game assets.

- The independent ignored Python oracle executes actual instruction words from
  the three original routines, using integer/rational rounding instead of the
  C++ cofactor algorithm. Its strict-FP native driver agrees on 7,798 cases and
  3,080,642 instruction steps, including 3,899 exact aliases, with zero result,
  status or destination-prefix differences.
- Cases classify 6,846 successes, 308 singular failures and 644 explicit
  finite-profile boundaries; 54 boundaries preserve a nonempty output prefix.
  All 328 instruction addresses in the entry ranges were visited. ABI PS1 save
  companions and fixed ABI helpers are excluded from semantic comparison;
  physical Gekko exception flags and full nonfinite execution are not covered.
- Permanent CTest coverage includes 90 inverse checks and 49 synthetic geometry
  checks. The geometry suite tests rotation, unequal scale, translation, shared
  scratch identity, source preservation, singular unused inverses, invalid
  storage, retained overrides and typed candidate dispatch.
- Original-data geometry integration passes 76 total checks: twelve existing
  floor queries yield ten candidates, and an additional prepared translated
  query reaches actual stage-179 vertices through the native bound/inverse path.
  The transform is explicit test setup; this does not establish original dynamic
  node startup or a complete gameplay frame.

Local evidence is `work/native-inverse-oracle-result.json`,
`work/native-inverse-oracle-report.md`, `work/native-source-resume-final-build.log`
and `work/native-source-geometry-original.log`. The full Release core build
passes 116 CTest suites. No game was launched and no original data was changed.
CHECKLAND response handling is subsequently implemented and tested separately
in [HITLAND_FLOOR.md](HITLAND_FLOOR.md). Wall responses, transform updates,
missing-bound construction and original startup/frame scheduling remain required.
