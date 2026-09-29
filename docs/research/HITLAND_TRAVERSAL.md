# Original HITLANDMODEL common traversal

`HitLandTraversalInvocation` implements the represented `803009D0` common
query traversal from the supplied USA GameCube DOL. Wall and floor handlers
call this operation with a retained query. Its native query, scratch, visitor
and geometry-info owners remain distinct. Replacing the manager's current
query does not redirect an already entered call.

The operation now selects actual scene nodes, including `beCopyTransform`
children, using current inner-switch masks and original broad-phase gates.
The typed intersection dispatcher still requires its concrete node handlers,
attribute streams and triangle cursors. The separate recovered triangle leaf
does not by itself supply those handlers. `CHECKWALL`'s multiple passes and
`CHECKLAND`'s candidate reduction also remain to be connected to these owners.
No walking-preview solver supplies query results.

## Preparation and retained allocations

The passed query's signed segment count is captured first. Nonpositive count
returns without reading the manager, scratch, visitor or segment list. With
positive count, the current scratch object list is cleared before the integer
list. Each query segment is then read from current physical backing and
appended to the current scratch owner, followed by an integer zero on that
same captured scratch. Query count and its reusable list count are independent.

Object-list operations are shared with [HITLAND_QUERY.md](HITLAND_QUERY.md).
The integer list has separate signed count, capacity and shared physical
storage. Clear writes count zero when capacity is nonnegative; negative
capacity reaches the original growth helper. Append publishes its count before
the final zero store. Growth starts at a capacity of at least four, doubles
below 1,024, then adds 1,024 until sufficient. The new allocation is published
before copying the lesser old/new capacity, and capacity/count are stored
after the copy. Retained old allocations are not overwritten. Unwritten spare
words stay unspecified and produce an explicit checked boundary if read.

The captured visitor reset performs the actual matrix-stack identity and
node-path clear. Subsequent flags, query, scratch and callback assignments
independently reload the manager's current visitor. The installed callback is
the known `802FE1A4` query-candidate adapter and flag four is enabled. Query
bounds, the manager's initial info count and the low nine query-kind bits are
then captured for the loops. Original allocator internals and arbitrary guest
destructor callbacks are outside the represented native ownership profile;
the implementation does not claim raw-pointer destruction emulation.

## Original selection order

Each info occurrence reloads the manager's current list/backing. Its name is
copied into the query before filtering; a null name becomes an empty string.
The raw info kind must intersect the captured low-bit query mask. Info/query
bit `0x800` together suppress the info, and current query bit `0x1000` requires
that bit on the info. Those upper query bits are read live. Info activity and
the outer switch mask are not added as extra gates.

An accepted info captures its inner check switch and nullable child-list
count. Each child reads its current physical mask word before publishing the
query child index or reading the child. Mask logical size and bit count do not
substitute for the original direct physical-word read. The initial child count
is retained while later child/backing/mask changes remain visible.

For a static child, a null bound still reaches the visitor. A present bound
uses the four ordered, inclusive X/Z comparisons at `80300974`; Y is ignored.
After a successful bounded test the child is freshly read from the current
switch list, matching the original call site.

For kind bit `0x400`, the selected child reads its actual `+6C` source transform.
Its matrix is copied into the destination before any bound lookup or rejection.
`80128CF4` uses paired stores: subnormal words become signed zero, unlike the
scalar translation copies used by HITLANDDELIVER. The destination translation
is captured, then physical nested child zero supplies the bound, independently
of its list's logical count. Six original single additions translate the box
endpoints; there is no general rotated-box replacement. Missing later owners
retain the already published matrix. A rejected broad phase also retains it.

## Continuation and evidence

Each reached visitor operation retains the exact visitor and selected node.
A pause resumes that operation, preserving earlier scratch, name, index and
matrix effects. Subsequent children read the manager's then-current visitor.
Missing dispatch services stay pending; a completed traversal does not replay
effects. Exceptions and caught recursive resumes permanently fault the call.

Synthetic checks exercise physical/count separation, allocation aliases,
partial failures, low-bit capture versus live upper flags, mutable info and
child lists, delayed providers, visitor replacement, inactive mask slots,
dynamic matrix publication and static nullable bounds. Optional original-data
integration selects the twelve floor and fifteen wall node occurrences in
stage 179 using the canonical graph and an explicit prepared info order. Its
test observer verifies node ownership and selection only: it does not produce
triangle candidates or prove a complete collision response. See
[VALIDATION.md](VALIDATION.md) for the final integrated build evidence.
