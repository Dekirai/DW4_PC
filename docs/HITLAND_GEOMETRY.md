# Original HITLAND geometry candidate production

`HitLandGeometryInvocation` translates the original USA `801F6050` operation.
It connects canonical scene geometry and the primitive enumerator to the
actual `HitLandQuery::append_candidate` callback. It does not use the walking
preview's decoded mesh copies or its host collision solver.

## Captures and publication

The initial nullable node bound is read before the matrix stack or flags. A
null bound returns zero. For the installed flag-4 path, eligible `igGeometry`
nodes capture the physical top matrix element, the attribute and segment list
owners, and their separate counts. The loop keeps these owners while reading
their current physical backing. A replacement node attribute list does not
retarget an already captured loop.

Each segment iteration reads the current scratch side word. Each eligible
attribute starts with group and stream cursors of minus one. The actual
primitive routine maps its arrays, computes the next hit, publishes its cursor
and unmaps before the candidate callback. A callback suspension retains that
finished operation, its local position and normal, and its captured segment
and attribute. Resuming the callback does not remap or recalculate that hit.

Every new candidate reloads the original segment from the captured segment
list's current backing. Its payload retains the current scratch and node path,
the captured physical matrix element, and the original segment owner. The
attribute index and primitive group/stream position identify the original
triangle. The last field is a stream element offset, not a triangle ordinal.

The local point is transformed by `8012D498`, including its homogeneous-W
division. Distance is measured from the original segment origin to that world
point, using the Y-square, fused X-square, fused Z-square order at `801F63BC`.
The installed callback `802FE1A4` reads the visitor's current query and calls
its candidate append. Append transforms the payload normal in place and copies
the current query name and child index. Matrix and path references remain
aliases; completing a node traversal can clear the path retained by results.

## Transform and numeric operations

`original_hitland_point` translates `8012D498`. It captures source components
before the three output stores and supports an in-place point. It preserves
the original Y-first multiply/fused X/Z order, separate translation addition,
and positive epsilon substitution only for a small absolute W.

`original_hitland_transform_segment` translates `801252D0`: construct the
second endpoint, transform both endpoints, subtract them, and normalize the
resulting displacement. This routine always divides by the resulting length;
it does not reuse segment creation's short-length threshold. A zero resulting
length reaches the checked finite-arithmetic boundary after earlier origin and
direction stores.

Arithmetic uses the startup NI=1, nearest-even profile. Nonfinite arithmetic
and uninitialized physical storage remain explicit checked boundaries. Local
original-instruction comparisons and integration results are recorded in
[VALIDATION.md](VALIDATION.md).

## Remaining original operations

The installed static flag-4 path with present bounds and a one-element matrix
stack requires no geometry service. The following reached operations still
require their real native providers:

- The bound scratch allocation/copy and typed transform dispatch through
  `801283DC` when the matrix stack count exceeds one.
- The original inverse `8012A92C` before transforming segments in that case.
- Flag-1 volume tests and flag-2 typed segment-bound tests and their callbacks.
- An explicitly selected candidate callback other than `802FE1A4`.

Providers retain unfinished calls and must perform the named operation before
reporting completion. Missing providers leave the invocation pending. Test
providers that supply known matrices verify continuation order; they do not
establish original dynamic collision behavior.

Typed node dispatch, path/stack handling and geometry-bound creation have their
own reached behavior in [HITLAND_NODE_DISPATCH.md](HITLAND_NODE_DISPATCH.md).
Primitive mapping is described in [HITLAND_PRIMITIVES.md](HITLAND_PRIMITIVES.md).
Complete `CHECKLAND` reduction, the multiple `CHECKWALL` passes, and delivery of
those responses to the original player frame remain required. Triangle
candidates alone do not complete an original gameplay frame.
