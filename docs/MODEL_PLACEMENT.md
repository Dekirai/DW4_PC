# Original model placement

`ModelPlacementInvocation` implements the placement branch of USA routine
`8030D610`, beginning at `8030D6F4` after its RAM data and named ONE lookup.
It publishes through the actual node's concrete matrix setter. The caller must
supply a resolver for that captured ONE's live node; this component does not
create ONE, work, an identity transform, or a scene node.

Player generation calls this helper at `8034D95C`. The preceding PLAYERPOINT
exchange and subsequent GAMERAM GETPLAYER call remain responsibilities of
the player construction owner. Completing placement alone does not complete
GENERATION or establish an original gameplay loop.

## Argument and owner order

| Current packet | Original placement operation |
| --- | --- |
| One vector | Copy the live node matrix, then replace translation XYZ with vector0. |
| Exactly two vectors | Perform the one-vector operation, then replace the nine basis entries with vector1's Euler rotation in degrees. |
| More than two vectors | Perform the one-vector operation; vector1 does not rotate the model. |
| No vectors, at least one matrix | Copy matrix0 directly; do not read or multiply by the previous matrix. |
| Neither | Return without reading the node or calling its setter. |

Vectors take precedence over matrices. The original checks vector count before
copying the source matrix, reads vector0 after that copy, and checks vector count
again before reading vector1. After calculating its local matrix, it reloads
the captured ONE's node at `8030D778` or `8030D7C0`. The source node and setter
node can therefore differ; they are not collapsed into one snapshot.

`ModelPlacementNode` retains the actual shared matrix storage and an explicit
virtual `+94` operation. `make_model_ctrl_node2_placement` provides only the
verified `beModelCtrlNode2` operation: its vtable entry is `801166B8`, which
copies the supplied matrix into node+20 through `80128CF4`. Other node classes
need their own concrete setter. An absent setter stops with a required-service
result instead of silently selecting this known class's behavior.

Publication retains the local candidate, target owner and started setter across
suspension. Replacing the messenger packet, ONE's current node or the node's
callback after that point does not restart computation or retarget the started
operation. The concrete callback owns its own partial continuation. Exceptions
fault the placement invocation and forbid automatic replay; already published
external prefix effects are not rolled back. Completion releases temporary
node/callback captures. The returned `MessengerCallResult` value1 is native
completion bookkeeping; the original helper has no meaningful handled result
and ignores its setter's return value.

An active resolver or setter cannot recursively resume the same placement
invocation. Such an attempt faults that invocation, including if the callback
catches the recursive error. Distinct nested invocation objects are permitted.

## Matrix and Euler arithmetic

Initial matrix copies and the known node setter use original GQR0 paired-single
copy semantics: subnormal components become signed zero, including matrix
entries that rotation will subsequently overwrite. Vector0 is copied with
scalar loads/stores and retains its subnormal words in the local candidate.
The final node2 setter then applies its matrix-copy flushing to that candidate.
Matrix0 follows the original argument-copy and local-copy path, with no parent
multiplication, decomposition or normalization.

Only the exactly-two-vector path multiplies degree components by original
word `3C8EFA35` at `80426804`. Each multiplication rounds to binary32 under the
startup NI=1, nearest-even profile, flushing tiny unrounded results to signed
zero. A degree value that underflows here follows the Euler helper's exact-zero
branch.

`801292D4` calls double sine and cosine for X, then Y, then Z, narrowing each
result to binary32. Exact positive or negative zero skips the corresponding
calls and uses positive zero sine and cosine1. With the usual column-vector
convention, its matrix is `Rz * Ry * Rx`. The actual scalar order matters:

```text
sxsy = sx * sy       (positive zero when Y is exactly zero)
cxsz = cx * sz       (positive zero when Z is exactly zero)
cxcz = cx * cz
sxsz = sx * sz
sxcz = sx * cz

m0  = cy * cz
m1  = cy * sz
m2  = -sy
m4  = fma(sxsy, cz, -cxsz)
m5  = fma(sxsy, sz,  cxcz)
m6  = sx * cy
m8  = fma(cxcz, sy,  sxsz)
m9  = fma(cxsz, sy, -sxcz)
m10 = cx * cy
```

Each separate product and fused operation has its original binary32 rounding
and NI behavior. The helper writes only those nine entries. In particular,
indices3,7,11,15 keep the copied bottom row, and translation remains vector0.
The implementation reuses the verified Euler arithmetic in `force_model_turn`
through a detached temporary pose, copying only these nine results back. The
motion helper's surrounding bottom-row reset is not a placement effect.

All copied matrix components and consumed vector components must be finite.
Non-finite source matrix values are rejected at the initial copy even when
translation or rotation would later overwrite them. Unused source matrices on
matrix-only/no-argument paths are not validated. The original may execute
unsafe/non-finite cases; this implementation reports checked boundaries instead.
Double sine/cosine use host libm, consistent with the existing motion math;
there is no claim of original MSL argument-reduction or hardware exception-flag
parity for every input.

## Validation and provenance

Source evidence is the owner's USA revision-0 DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Relevant routines are placement
`8030D610`, vector count/read `80306C74`/`80306D28`, matrix count/read
`80306D58`/`80306DE8`, matrix copy `80128CF4`, translation store `80128D90`,
Euler basis `801292D4`, and the concrete node setter `801166B8`.

Asset-free tests cover argument precedence, exact count behavior, nonidentity
matrices, bottom-row preservation, original combined-angle output words,
signed-zero/tiny copies, distinct source/target owners, retained setter identity,
suspension, owner release, missing/null services and partial failures.

An ignored independent instruction oracle executes the original placement
branch, argument readers, Euler body, paired-single copies and concrete setter
against explicit prepared memory. Its scalar arithmetic uses integer/rational
NI1/RN00 rounding; host double sine/cosine are explicit shared service boundaries.
The comparison covers 3,638 cases per run: 3,494 accepted cases and 144 checked
non-finite initial-copy rejections, with 518,913 original instructions and 8,668
trig service calls. Normal and paused native publication both matched with
zero differences. Alias fixtures explicitly change ONE.node between its two
original reads; this tests reload semantics, not a claim of original concurrent
execution. The oracle does not execute the earlier RAM/ONE lookup, original
allocator, complete player constructor, original MSL or a console emulator.

Raw game bytes, generated instruction traces and the local oracle remain in
ignored `work/`. Public code, documentation and tests contain synthetic data.
