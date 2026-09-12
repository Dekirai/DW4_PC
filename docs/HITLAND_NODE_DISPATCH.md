# Original HITLAND node dispatch

`HitLandNodeDispatchInvocation` implements the original USA visitor dispatch
`8020B06C`, generic child traversal `8020B274`, and the empty, group, transform
and geometry handlers at `801F6DD8`, `801F6DE0`, `801F6ED4` and `801F707C`.
The default `HitLandIntersectionInvocation` now enters this retained native
dispatcher. An explicit dispatch override remains available for callers that
provide it before entry.

## Runtime types and filters

`register_hitland_node_type` explicitly registers a canonical node's runtime
class. The host registration assigns independent class indices, shared across
visitors, and selects geometry, transform, group or empty behavior in the
original inheritance-test order. An IGB metadata type index is never used as a
runtime dispatch index. Registration is a preparation API; it does not claim
to reproduce the game's complete startup registration order or absolute
runtime indices. A node without registration remains pending at the reached
dispatch-index lookup.

The function table retains separate signed count, capacity and physical
backing. A reached dispatch reads the physical function slot independently of
logical count and capacity. Unknown words and null targets fail at that read
or call. A known target outside the four recovered handlers requires its
actual provider.

The outer filter and the repeated transform/geometry filter have different
return rules. The outer filter continues on zero; two traverses children for
derived nodes but continues normal dispatch for exact `igNode`; every other
value skips the node. The inner filter skips on one, handles two in the same
way, and otherwise continues its concrete handler. An enabled null callback
fails. A pending callback retains the selected function even if the visitor's
filter field changes before resumption.

## Node and child captures

A group with a null child list or exactly zero children returns before touching
the path. Otherwise it appends the node to the current path and directly enters
the concrete [geometry producer](HITLAND_GEOMETRY.md). A zero producer return
enters generic child traversal; another return skips children. The handler
then reloads the visitor's current path, removes its last entry, and propagates
only return value two.

Generic traversal captures the child count and physical backing owner once.
It loads each reached physical child from that allocation. Replacing the
node's list or its allocation during a suspended child does not retarget the
existing traversal. Child return one ends the loop with zero; return two ends
the loop with two. Null children are retained until dispatch reaches a field
that consumes them. Negative or wrapped guest pointer ranges remain an
explicit unsupported address boundary.

A geometry handler captures its nullable node bound after its repeated filter.
An existing bound proceeds to path append, concrete production and a fresh
path pop. Missing bounds reach a retained provider for the complete original
attribute-derived bound construction at `801F7148..801F73C4`. That provider
must perform the original eligible-attribute scans and publish any new
canonical bound before returning its captured owner. A completed null result
means the provider established that no eligible attribute supplied a bound;
the dispatcher does not manufacture that outcome.

## Transform time and physical stacks

A transform reads the visitor time through the represented `801D138C` rules.
A nonzero bound-time byte selects the stored high/low words. Otherwise it
reaches the actual global clock virtual `+68` service. No host clock or stored
fallback timestamp substitutes for that operation. The resulting time is
captured before the node's original virtual `+9C` update. A pending clock or
update retains that phase and its captures.

After the update, the transform appends its path entry and calls the concrete
producer. Only a zero result pushes the current node matrix and traverses
children. The handler then reloads and pops the current matrix stack, reloads
and pops the current path, and propagates only two. These fresh pop reads are
deliberate: callbacks may replace the visitor's path or stack after the push.

Path and matrix lists preserve signed logical counts, separate capacity and
shared physical backing. Growth preserves old allocations held by aliases and
publishes the new allocation before the old-capacity copy. Path append
publishes count before its destination store. Path pop reads and releases the
last pointer before removing its slot. Canonical node destruction does not
execute arbitrary guest callbacks; this is not an emulation of arbitrary
guest destructor mutation. Uninitialized pointer slots and raw overwrites of
unreleased spare nodes remain checked boundaries.

Matrix push `8021B044` first appends a paired-single copy. For a nonempty stack,
`80128774` then composes the prior matrix with the node matrix. Its initial
rounded product uses Y, followed by fused X, Z and translation terms. It
captures the node operands, computes a local result and publishes through
`80128CF4`. A later arithmetic failure leaves the appended copy and count in
place. Matrix pop decrements count before copying the removed physical entry;
the physical matrix remains available to existing aliases.

The arithmetic uses the verified startup NI=1, nearest-even profile. Paired
copies flush subnormal words to signed zero. Nonfinite consumed arithmetic,
invalid physical addresses and allocations beyond 65,536 entries are explicit
boundaries. The implementation does not treat arbitrary guest address wrapping
as an ordinary host index.

## Validation and remaining work

Focused tests cover retained filters and children, repeated filter return
rules, registration pauses, path replacement across a producer pause, captured
transform time, missing clock/update/bound providers, matrix publication and
caught recursive entry. The local original-instruction comparison executes
the `80128774` composition body and `80128CF4` paired copy against the native
stack helper: 7,743 cases, including 200 direct copies and 128 aliases,
1,461,363 instructions, 1,093 explicit numeric boundaries and zero mismatches.
This comparison verifies those numeric kernels; allocation and continuation
prefixes have separate focused tests.

The installed static geometry path with a present bound can enter the concrete
candidate producer directly. Dynamic time/update services, missing geometry
bounds, transformed bounds and the remaining query reduction/response paths
still require their actual operations. This dispatcher does not establish a
complete original gameplay frame. See [HITLAND_PRIMITIVES.md](HITLAND_PRIMITIVES.md)
and [VALIDATION.md](VALIDATION.md) for adjacent implementation and validation.
