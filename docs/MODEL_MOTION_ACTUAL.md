# Actual model movement invocation

`ModelMotionInvocation` connects the actual `8030A448` movement body to
canonical RAM, work, movement entries and scene nodes. The caller supplies the
outer captured parent/work. The original position-lock gate belongs to that
caller. This invocation does not perform input, status, ride, wait or collision
processing on its behalf.

It reuses the [model motion arithmetic](MODEL_MOTION.md), including per-axis
turn interpolation, Euler basis and local translation. The older pose-based
APIs retain their prepared or explicitly bound-node contracts. Passing a pose
with one fixed node is not a replacement for the actual lookups below.

## Captures and publication order

1. Capture work+C8, clear work+78 (`translated`), then read the captured list's
   count. Zero returns without querying time or looking up a node. A null list
   fails after the byte clear.
2. Query the actual named `KEIKATIMER` on the retained RAM. The default endpoint
   reads the live RAM.manager/context clock. A supplied endpoint must retain
   this same RAM. Timer publication survives later checked motion failures.
3. Read captured-list entry 3's active field. If active, separately reload the
   work's current C8 list and advance that current entry 3. Publish desired Y
   and turn rate using the original degrees conversion and ordered sign test.
4. Execute `8030C85C`: first capture desired XYZ, then independently query time
   for each changed axis whose live rate is nonzero. Equality and zero-rate
   branches skip that axis's timer. After interpolation, compare against the
   live applied angles. Only a changed result performs a fresh RAM.data/first
   named ONE lookup and reads its current node matrix. Rebuild that matrix
   directly, preserve translation and publish applied XYZ. This path does not
   call the translation setter.
5. Revisit the original captured movement list and short-circuit active XYZ in
   order. If any is active, perform another fresh RAM/ONE lookup and retain
   that ONE. Copy its current node matrix before advancing XYZ. The copy uses
   the represented `80128CF4` paired-store subnormal behavior.
6. Advance each XYZ scalar using a separate reload of the captured work's
   current C8 field. Completed scalar updates survive a later missing entry,
   checked numeric result or setter failure. Transform the displacement into
   the copied matrix, then reread the retained translation ONE's live node for
   its concrete virtual `+94` operation. The verified `beModelCtrlNode2` setter
   uses `801166B8`; unsupported concrete node setters remain checked boundaries.
7. After setter completion write translated=1. Read the captured work's current
   base box. If nonnull, reread the retained translation ONE's current node,
   create a missing represented destination bound, copy the source box, then
   transform that destination by the current node matrix (`8030D7F4`). Existing
   box aliases remain aliases, including source=destination.

The local candidate, retained translation ONE and selected setter survive any
required setter pause. The invocation never replaces the old captured work
with a newly installed ONE.work. A completed invocation is idempotent;
exceptions and recursive entry prohibit replay while preserving completed
original prefixes. Parent is retained as an incoming owner but is not read by
these reached movement/turn paths.

## Scalar and numerical boundaries

`advance_motion_actual` consumes the `8030A2D8` active gate before reading scalar
payloads. It updates the actual entry directly and ignores unrelated authored
stop parameters until stopping reaches them. Unknown mode numbers follow the
original unchanged-speed branch. Aliased XYZ entries can therefore be advanced
multiple times in their original order. The existing checked prepared
`advance_motion` contract is unchanged.

Finite timer deltas are required by this native arithmetic profile. Reached
stop setup retains its existing finite parameter checks; an expiry-clear that
precedes a failed setup remains published. Derived IEEE values may survive
scalar arithmetic until a checked matrix consumer rejects them. The existing
turn wrapping budget, nonfinite turn checks, host double sin/cos boundary and
matrix arithmetic limits still apply. This does not claim full guest floating
point exception, NaN-payload or raw allocation fidelity.

`MotionChannelList` currently represents logical size and backing together.
Actual consumers avoid whole-list shape validation and allow repeated entries,
but separate negative counts or spare physical slots beyond size are not yet
represented. Inactive or unvisited entries do not validate numerical fields.

`model_motion_actual_tests` covers clock-driven RAM.data, ONE/work/node and C8
replacement; original versus fresh list reads; per-axis clock counts; direct
turning versus concrete translation setters; source-copy order; aliased scalar
and box owners; committed failure prefixes; default manager/context time; and
recursion. These source-reviewed capture regressions are separate from earlier
prepared numerical instruction-oracle runs and do not establish a complete
original frame or game loop.
