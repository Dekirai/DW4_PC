# Actual HITLAND primitive enumeration

`original_hitland_primitive` implements the reached primitive scan at
`801F64E4` in the supplied USA executable. It consumes canonical geometry,
vertex, index and primitive-length owners and calls the separately verified
`original_hitland_triangle` leaf. It does not consume the preview's copied
triangle mesh. One synchronous call returns the next accepted primitive.

The caller provides its local point, normal and signed group/stream cursors.
The stream cursor records an element offset, not an ordinal triangle number.
Each call starts scanning at group zero and stream zero. It calls the leaf
before comparing a successful result against the cursor, so rejected or
previously returned triangles can still overwrite the point and normal.

## Reached reads and publications

The primary attribute branch reads the current vertex format first. Either
nonzero nibble in bits 4 through 11 returns false before mapping or changing
cursors. Otherwise it captures vertex base, primitive count and primitive
mode, then maps the current vertex array. The returned position pointer and
stride retain the actual mapped allocation. An optional index array is mapped
next. Its owner is reloaded after that map and retained for the eventual unmap.
Primitive-length ownership is obtained after both maps.

Only then does a zero continuation byte, or either cursor equal to minus one,
reset both cursors to minus one. Unsupported primitive modes still reach this
reset and the normal unmap sequence. A null or unrelated attribute returns
before any cursor reset.

Mode 3 advances the stream by three; mode 4 advances it by one. Mode 4 swaps
vertices one and two at odd absolute stream offsets, except when side is two.
The outer group comparison is unsigned; cursor comparisons are signed. With
primitive lengths, each group starts with length minus two and uses a do-while
scan: lengths of two or less still reach one triangle. After an exhausted group,
the stream advances two extra elements. Without lengths, each group reaches one
triangle and does not add that extra two. The producer does not repair short
groups or reinterpret them using preview strip rules.

A nonnegative leaf result updates the group cursor first and then the stream
cursor only when it is lexicographically later than the caller's current pair.
The normal return path reloads the attribute's current vertex array for unmap,
then unmaps a nonnull index pointer using the retained post-map index owner.
A checked failure keeps earlier map effects, point/normal writes and cursor
stores. It does not execute cleanup instructions the original never reached.

## Explicit boundaries

The alternate `igGeometryAttr2` branch is an explicit reached preparation
provider; its different vertex/semantic owner graph is not yet native. Its format-semantic gates and
reached map/accessor prefix cannot be replaced by primary-branch field reads.
After that prefix, `801F69FC` through `801F6A04` access the primary attribute
register, which remains null on this branch. The native path reports this
reached invalid virtual access rather than silently repairing the executable.

Map and unmap operations are synchronous. Output vectors belong to the caller,
as in `801F6050`; they must not alias raw mapped vertex/index bytes. Checked
physical reads reject unavailable memory only when consumed. Triangle numeric
boundaries are those documented in `HITLAND_INTERSECTION.md`. Guest address
arithmetic retains 32-bit wrapping, followed by checked physical access.
Native operation bounds stop pathological enumeration without prevalidating
unreached groups or vertices. Arbitrary guest destructor callbacks and physical
Gekko/FPSCR behavior remain outside this profile.

An optional post-service checkpoint lets a retained owning invocation stop
immediately after a synchronous callback poisons that invocation. It runs
after every reached format/map/group/element/unmap or alternate preparation
call, before the next native read or write. It does not mutate guest state.
Nullable or wrong-type segment owners are checked at the first segment read in
the triangle leaf, after the normalized normal has already been written.

This component alone does not establish the complete attribute producer,
visitor dispatch, or CHECKWALL/CHECKLAND response path.
