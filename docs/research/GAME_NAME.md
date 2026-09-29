# Original player-name conversion

`GameNameConverter` implements the bounded `beFont` conversion operation used by
GAMESTART. It requires both a mapping table and an explicitly supplied shared
`convertData` scratch owner. Loading the table does not construct a font, register
it in an info context, or make the original same-context font service available.
The GAMESTART owner must resolve that service after completing its common player
preparation and retain the resolved converter through the name operation.

`read_game_name_table(main_dol)` reads the 424 ordered pairs and final
`FFFF,FFFF` sentinel at guest address `80425770` from the supplied executable.
Each pair is two big-endian 16-bit words: destination code unit, encoded input.
The DOL section, file size, complete range, pair count and sentinel are checked.
Original table bytes are not compiled into the port. `GameNameTable` also accepts
at most 424 synthetic pairs for asset-free validation. Duplicate encoded values
retain their order, and lookup selects the first matching pair.

## Exact conversion and ownership

The operation follows `802FB59C`, including its two different line-feed tests:

1. The first pass counts mapped output units. CR is skipped. LF increases the
   allocation count only after that count is already greater than zero. Every
   other input unit replaces the contents of the same scratch owner, even if no
   mapping exists. A byte with its high bit set consumes two input bytes;
   other bytes consume one. A final high-bit byte consumes the C string's implicit
   zero terminator as its second byte.
2. A new `beUnicodeObj` receives a zeroed buffer of `count + 1` 16-bit units.
3. The second pass emits LF only when more than one output unit already exists.
   A mapped two-byte value emits the pair's destination word. A mapped
   single-byte value emits that input byte itself, irrespective of the pair's
   destination word. Unmapped values are dropped. The operation writes a zero
   terminator at the actual output count and retains any spare zero-filled units
   from the first pass's allocation.

For example, a prepared table mapping `A`, `B` and `C` converts `A\nB\nC` to
`A B LF C NUL NUL`: six allocated units, of which four precede the terminator.
An empty or CR/LF-only input leaves the previous scratch contents unchanged.
The second pass does not modify scratch. The scratch owner represents the
mutable string object at `beFont+38`; its allocator's raw character-address
identity is outside this prepared service.

`beUnicodeObj` inherits `igNamedObject`. Registration `802D9334` uses base
registration `80029D58`, whose type name is `igNamedObject` at `8046428C`.
The object has a nullable name at `+08` and buffer pointer at `+0C`, with total
size `0x10`. The native `GameDataUnicode` therefore retains a nullable `name`
and a separate nullable shared vector of 16-bit `buffer` units. Newly converted
objects leave the name null. A null buffer represents the unallocated state;
it is distinct from an allocated empty string containing a zero unit.

`make_player_name` follows `8036ED14..8036EE48`: allocate a distinct zeroed
nine-unit buffer, copy at most eight units from the converted source, and stop
at the first zero. The ninth destination unit stays zero. It copies neither the
source object's name nor its buffer owner. It does not interpret surrogate pairs,
normalize text, divide character counts by bytes, or substitute a host codec.

The parallel GAMESTART object route requires no conversion or Unicode cast.
It retains and appends the same represented object or null after the common
preparation. A displayed player name is not a saved player-data row.

## Checked service limits

Input is at most 65,535 bytes, excluding the implicit terminator; embedded NULs
are rejected by the checked C-string profile. Null input is a `GameDataError`
when conversion is invoked. GAMESTART must not perform that check before its
earlier common phases merely because its eventual display string is null.
A missing scratch owner is also an explicit error. No ASCII, UTF-8, Shift-JIS
library, default font, replacement character or empty-string fallback is supplied.

The conversion has no asynchronous callback and cannot recursively enter itself.
A host exception may follow committed scratch writes; the containing retained
GAMESTART invocation must fault rather than replay uncertain effects. The public
API does not model font rendering, font asset setup, text layout or the complete
native allocator lifecycle. `make_player_name` rejects absent/truncated source
buffers before an invalid original read; exactly eight available nonzero source
units are sufficient because the original copy reads no ninth source unit.

## Validation and provenance

The Release core-linked suite passes 35 synthetic checks and 461 checks with
the supplied original table. Synthetic checks cover paired endian decoding,
mapping order, single-byte output, both LF thresholds, unused zeroed capacity,
unknown values, scratch identity, trailing lead bytes, raw surrogate units,
separate nine-unit name ownership, maximum input and malformed table/buffer data.

The ignored instruction oracle, rerun against the Release core-linked test
executable, compares 516 cases with zero mismatches across
2,519,526 guest instructions, including 2,494,747 conversion instructions. It
executes actual `802FB59C`, allocation control `802FC878`, and the eight-unit copy
block `8036ED14..EE48`. Cases include each of the 424 original mapping inputs,
the four original default display-name byte strings read from the DOL, eight
focused synthetic cases and 80 mixed sequences. It compares the full allocated
Unicode buffer, final scratch bytes and final nine-unit name buffer against the
native implementation. Allocation, Unicode factory, scratch-string primitives,
allocator service and `strlen` are explicit prepared helper boundaries. This is
not a claim of full font construction or physical hardware validation.

Reference: supplied USA GDJEB2 revision 0 `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original data and disassembly remain
in ignored work files. The asset-free executable's `--oracle` mode accepts only
explicit mapping/input/scratch packets and emits numeric buffers for independent
comparison; ordinary tests need no original assets.
