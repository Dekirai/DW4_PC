# HITLAND intersection primitives

`hitland_intersection.hpp` represents the shared matrix stack, node path and
query/scratch references used by `beHitLandModelTraversal`. It provides the
original reset, a retained invocation of the concrete `+64` wrapper, and the
complete bounded segment/triangle arithmetic leaf. Full node/attribute traversal
still requires the declared actual dispatch provider. The preview walking mesh
decoder is not used to manufacture candidates.

## Visitor ownership and reset

The supplied USA DOL constructs the concrete visitor through `0x802D4FF4`.
Its vtable at `0x804D7134` resolves `+64` to `0x8020B13C`. That wrapper calls
`+5C` (`0x801F6010`), middle dispatch `0x8020B06C`, then `+60` (`0x801F6030`).
The inherited entry/exit bodies reach the verified no-ops `0x8020B134` and
`0x8020B138`. The middle dispatch owns the callback and runtime type-table gates.

`HitLandIntersectionInvocation` retains the exact visitor and nullable node.
A missing provider returns incomplete without inventing geometry results. An
empty service placeholder does not become a permanent binding; once an actual
`node_dispatch` starts, its service owner remains fixed across retries. The
provider must retain its internal original operation. Caught recursive execution
permanently faults the wrapper. Completing or retrying this wrapper does not
reset visitor state or repeat common traversal's earlier publications.

`reset_hitland_intersection` follows `0x801F5F0C`: clear the matrix stack count,
append the original identity matrix, then clear the captured node path with its
count written last. Existing matrix allocations and aliases are reused when
capacity permits. Growth publishes replacement backing before copying old
capacity elements; a bad old copy preserves that replacement and the earlier
zero count. Null old backing skips the copy. Existing shared matrix pointers
retain their original allocation after a replacement. Unwritten spare matrices
and path slots remain unspecified; a reached read rejects them explicitly.

The node release profile is restricted to canonical native node owners. It does
not emulate arbitrary guest destructor callbacks or expose transient dangling
pointer bytes between the original release and zero loops. Matrix identity,
allocation and count prefixes survive a later null/invalid path read. The stack
and path operations have a 65,536-element host bound, checked when reached.

## Original segment/triangle leaf

`original_hitland_triangle` implements `0x801238E8` and cross-product helper
`0x8012CB74`. It takes the actual segment's origin, direction and length, three
reached vertex vectors, mutable position/normal outputs and the original side
selector. It preserves the ordered scalar NI=1/RN=00 arithmetic and output
aliases; it does not normalize the input segment again.

The raw cross-product normal is published first, then normalized using separately
rounded squared terms, sums, length, reciprocal and products. Face rejection
uses the sign bit, including signed zero. Side 0 rejects nonnegative facing;
side 1 rejects negative facing; side 2 bypasses facing and epsilon rejection.
Other side values retain the original epsilon check. The exact epsilon word is
`0x350637BD`. The plane distance uses the original Y product followed by fused
X and Z operations. Negative-sign distances or distances greater than segment
length reject before position is written. An equal endpoint distance is allowed.

The plane intersection position is published before the triangle-inside tests.
Those tests compare the sign bits of three cross-product projections, including
signed zero. A rejected point outside the triangle therefore remains observable.
Return values are the segment distance on success, `-2` for facing/parallel
rejection, `-3` for segment-range rejection, or `-4` for an outside point.

Finite reached arithmetic is the supported numeric profile. Degenerate or
nonfinite normal length raises `HitLandQueryError` after the raw normal prefix;
later nonfinite facing, distance or inside-test arithmetic raises at its reached
boundary. In particular, side 2 can reach a divide-by-zero boundary that other
sides reject normally. These checks do not claim complete original NaN payload
or FPSCR exception behavior.

## Validation and remaining integration

The focused suite checks reset allocation/count prefixes, old allocation aliases,
unwritten physical slots, retained dispatch and recursion, culling, segment
endpoints, outside-point publication, degenerate normal publication and aliased
outputs. An ignored raw-instruction driver executes the complete original
triangle leaf and cross helper with independently rounded integer scalar and
binary64 Newton arithmetic. It compares 2,902 cases and 605,246 instructions with
zero mismatches, including 240 output-alias cases and 89 explicit numeric-boundary
prefix cases. This is an instruction-model test, not physical Gekko execution.

The node dispatch still needs its canonical runtime type table, inherited filter
and time fields, geometry attribute owners, vertex/index map operations, bound
cache operations, transformed segment scratch and primitive group cursors.
The arithmetic leaf can consume those reached vertices once that actual producer
is implemented. Until then, a pending dispatch remains an explicit service
boundary; successful isolated triangle tests do not establish a complete
`CHECKWALL` or `CHECKLAND` response.
