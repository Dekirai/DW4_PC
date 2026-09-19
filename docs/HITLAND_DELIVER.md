# Actual HITLANDDELIVER recipient

`ModelHitLandInvocation` implements one original `beHitLandDeliv` recipient
callback at `802FDA0C`. It consumes the canonical `ModelHitLandRam` and its
shared scene node. The retained invoking manager is supplied by the dispatcher
and held weakly across pauses; replacing RAM.manager does not redirect an
already entered invocation. The manager must remain alive, and every retry
uses the same native messenger and event runtime.

All five original commands are represented: `HITWALL`, `HITWALLFULL`,
`HITFLOORCHECK`, `HITFLOORADJUST` and `HITFLOOR`. An unknown nonnull command
returns unhandled before consuming node or collision fields. A null command
is a reached invalid dictionary string. Inactive floor adjustment reads no
node. Command selection is retained independently of later messenger cache
changes caused by nested queries.

## Wall operations

`HITWALL` captures the current node translation, then compares X and Z with
the recipient's current previous position in that order. Equal X/Z skips the
geometry query. That path still consumes a present vector result from the
existing packet to update previous position, and publishes three false bytes;
it does not reset the recipient's existing position field or move the node.

For changed X/Z, the callback selects `HITLANDMODEL`, appends the captured
position, computes the original horizontal direction from freshly read previous
X/Z, then appends live half-width, upper/lower ranges, check flags and the
original position-X flag. `HITWALLFULL` always queries and supplies the captured
position twice instead of normalizing a direction. The native commands are
`CHECKWALL` and `CHECKWALLFULL` respectively.

On return, only response boolean zero equal to **one** sets the recipient's
position to `(1,0,0)`. Other bytes, including 255, set `(0,0,0)`. If a vector
exists, exact-one also freshly resolves the recipient's current node and copies
the vector to its translation. Previous position then receives a separate read
of vector zero. The final three response bytes are `(moved,moved,0)`; a true
query byte without a vector does not claim movement.

The normalization helper in [hitland_math.hpp](../include/dw4/hitland_math.hpp)
preserves the original subtraction, fused squared length, rounded length,
reciprocal and product order. Its finite, positive-length arithmetic profile
explicitly rejects zero/underflowed and nonfinite used lengths. Such a failure
occurs after selection and the first vector append, preserving that prefix.
An equal-X/Z path does not consume unused Y or perform normalization.

## Floor operations

`HITFLOORCHECK` and `HITFLOOR` capture the current node translation and send a
real `HITLANDMODEL/CHECKLAND` query with that position, three live ranges and
check flags. At least three response booleans and one vector are needed for
the complete result. Otherwise the original local fallback is `(0,-1,0)` with
three zero bytes; this is the callback's recovered post-query behavior, not a
substitute for an unavailable query service.

Returned Y at least one stores the result in land position and previous
position, retaining the third response byte as land-position validity. A lower
Y reads the current node translation again after the query. If its current Y
is below zero, land and previous position receive the returned X/Z with Y zero,
validity becomes one and the response is `(1,1)`. Otherwise the stored positions
remain unchanged, validity becomes zero and the response is `(1,0)`. The native
ordered comparisons preserve the original branch for unordered current node Y.

The callback publishes the two computed response bytes before optional node
adjustment. `HITFLOOR` then follows the same final step as `HITFLOORADJUST`:
only nonzero current validity causes a fresh current-node lookup and scalar
copy of the current land position. A reached missing node can therefore fail
after land fields and response bytes have already been published. The scalar
matrix getter/setter paths `80128DAC` and `80128D90` preserve tiny values and
signed zeros without validating unrelated matrix components.

## Native messages and publication

Queries execute through retained `NativeMessenger` dispatches and
`ActionEventRuntime::resume`. Missing `HITLANDMODEL` registration pauses before
query selection; a pending actual recipient retains the already selected
packet, appended arguments and internal dispatch. Retry does not recapture
the initial node position or repeat completed argument appends. No query
success or collision boolean is manufactured for an unavailable backend.

Direct callback retries compare retained execution owners without reopening
the runtime's top-level mutable messenger accessor while their own send is
pending. The model execution lease still guards entry. Throwing or caught
recursive callbacks permanently fault the invocation before later response
publication; completed invocations do not replay their effects.

`MessengerBuffer::replace_booleans` represents the complete callback-free
`8030691C` resize followed by two or three known `803069EC` stores. All bytes
are computed by the reached branch before publication. Other seven typed
lists, their objects and enabled-only gate remain intact, and aggregate packet
limits remain enforced. This combined publication does not expose arbitrary
original uninitialized slots, independent logical count or spare capacity;
it is not a general original boolean-list resize API.

## Verification and scope

Evidence is the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`: recipient `802FDA0C`, dictionary
`804D1CB8`, original float constants, scalar matrix copies and messenger
boolean publication helpers. Ignored local disassembly and probes remain
outside tracked source.

Focused tests cover all five commands, exact-one response handling, missing
vectors/short responses, equal-X/Z packet behavior, shared versus replaced
nodes, late node failures, live ranges and previous fields, actual query
pause/retry, manager loss and RAM-manager replacement, numerical prefixes,
signed zeros, and caught recursion. Messenger tests verify combined boolean
publication, aliasing input, other-list/gate preservation and aggregate bounds.

The callback supplies actual wall/floor transport and response state updates.
The September 19 native [CHECKLAND implementation](HITLAND_FLOOR.md) now provides
the floor query and response through a retained per-delivery receiver. Prepared
integrations map actual triangle geometry and apply its result through HITFLOOR,
including supplied map data. Native [CHECKWALL](HITLAND_WALL.md) now supplies
three-pass wall responses through the shared query receiver, including actual
HITWALL node adjustment. Original manager startup, complete dynamic geometry
and the distinct CHECKWALLFULL algorithm remain required. Existing walking-preview
geometry and prepared projectile `CHKBULLET` caches do not replace those operations or prove completion of the original
frame or gameplay loop.
