# Native IGB stream and header prefix

`IgbOpenInvocation`, `IgbBufferInvocation`, and `IgbHeaderInvocation` reproduce
the represented caller operations at USA `8004F870`, `80053118`, and
`8004F974`. The original executable SHA-1 used for recovery is
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

This is an executable loading prefix. It is not a completed original file
manager, reflective allocator, or object decoder. The native byte-stream
adapter binds one exact filename to actual supplied bytes. It neither guesses
path dispatch nor creates a successful response for an unavailable resource.

## Opening and directory naming

An existing stream is retained without reopening or changing the auto-opened
word. With neither stream nor filename, opening returns the reached original
failure word. Otherwise, the selected opening operation represents
`8004D6DC`; its result is compared with the live failure word before the caller
publishes auto-opened state. A nonfailure result with a null stream therefore
still publishes `owns_stream = 1` before returning failure.

A failed opening reloads and releases the stream before clearing its field.
An unrepresented final original `igFile` destructor is an explicit fault with
that field preserved. Nonfinal shared ownership is supported; native shared
pointer counts are not a model of every original tagged reference-count bit.

The filename derivation in `8004D94C` runs only for an empty or null directory
name. Its backwards separator scan is retained, including trailing separators:
`a/b/` produces `b/`. An empty stream filename reaches an invalid byte-before-
start read and is rejected. String-pool acquisition and interning require an
explicit retained operation. The original mode setter `8004CED8` publishes
mode `4` after naming completes.

## Scratch storage

Scratch capacity and backing ownership remain separate. An existing buffer
with sufficient signed capacity returns the live success word without reading
the file, stream, or pool. Growth follows the original ordering:

1. Capture the old buffer and the live pool, then perform its scratch free.
2. Leave the released buffer published until a replacement result is stored.
3. Publish the file size hint, optionally replace a negative hint with the
   stream size, apply the signed minimum, and round the word up to four bytes.
4. Reload the pool and stream, capture the low 16 alignment bits and capacity,
   allocate, then publish the result, including null on allocation failure.
5. Read the reached success or failure word.

`igb_bind_native_scratch_pool(state, pool)` explicitly binds a native byte
allocator to one canonical pool identity. There is no default allocator for an
unrepresented original pool. A null pool faults at its reached dereference;
an unbound nonnull pool remains pending. Each pending free/allocation retains
its selected identity and arguments, while allocation after a completed free
reloads the live pool. This native profile owns bounded byte storage and does
not claim recovery of the original pool virtual bodies or guest addresses.

The native allocation and transfer limit is 64 MiB. Fresh scratch bytes are
unknown, represented by a separate initialized-byte mask. Reads initialize
only bytes actually supplied. Released aliases remain invalid rather than
silently becoming a second usable allocation.

## Header and endian handling

The header requests at least `0xC0` scratch bytes, then reads twelve four-byte
elements. The original ignores the stream read return value. Consequently, a
short read into reused, known storage can consume its unchanged tail. A short
read into fresh storage stops at the first reached unknown byte; zero-filled
allocator memory is never treated as original file data.

The header installs int and short metadata-field owners before validating the
cookie. Their represented native behavior is the recovered width-based byte
swap at `80064B70`; this does not claim complete reflective singleton creation.
Each incoming field is retained before releasing the old field, with int
publication preceding short acquisition. A reached unrepresented final old-field
destructor stops before that publication; acquired incoming ownership and all
earlier stores survive the fault, including destruction of the invocation.
The int field swaps all twelve words if the initial cookie does not match.
Failed post-swap validation preserves the prior endian flag. Valid data stores
the low 16 version bits and accepts versions zero through seven. An unsupported
version returns after publishing that version and the endian flag, before
publishing flags or section counts.

The top four flag bits populate their original byte fields. The legacy byte
and mode test whether the entire header word equals `1`, not whether its low
16 bits equal `1`. Ten section size/count fields are published in their original
order. The expected cookie is supplied by the file state rather than hardcoded
inside the header operation.

Success and failure globals are shared optional words. Missing words pause only
at their reached reads. A pending return never repeats a completed allocation,
stream read, or field publication. Overlap, recursive use, or abandonment of a
partly published operation is explicitly rejected.

## Validation

The permanent suite passes 258 synthetic checks covering endian variants, all represented flag meanings,
invalid cookies and versions, exact status comparisons, partial reads, retained
pending operations, scratch-pool identity changes, failure prefixes, naming,
and ownership boundaries. Its optional `--data <extracted-root>` mode feeds
actual `ndmw.igb` and three catalog IGB resources through byte opening and the
header, comparing the published fields with the existing independent metadata
reader. Original assets are not committed.

Independent original-instruction comparison evidence is kept under ignored
`work/native-igb-stream-*`. It distinguishes actual caller instruction bodies
from prepared stream, pool, field-registry, and opening endpoints. Prepared
endpoints establish same-input caller parity, not completed original services.

The final comparison passes 5,120 cases with zero differences: 4,096 header
cases and 1,024 opening/naming cases, executing 1,539,152 instructions across
370 original addresses. This corpus uses full header reads, valid bounded
allocations, and nonfinal original object references. Short-read rejection,
pending unbound services, and final-owner fault prefixes are covered by the
permanent native tests rather than claimed as general guest-memory execution.
