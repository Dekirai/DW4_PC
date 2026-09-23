# Native memory-reference member construction

`IgbMemoryFieldConstructInvocation` reconstructs `0x80062524`, memory-reference
table `0x80476088` virtual `+0x7C`. Its allocation helper reconstructs
`0x80062BB8`, including the reached metadata ancestry test `0x80068128` and
conditional zero fill `0x80003100` / `0x80003130`.

Evidence is the supplied USA `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`: instruction ranges
`80062524–800625D8`, `80062BB8–80062C8C`, `80068128–80068180`, and
`80003100–800031E4`. Original instructions remain outside the repository.

## Original behavior

The outer wrapper reads construct byte `+0x34`. Zero returns immediately.
A positive flag reloads element descriptor `+0x3C` and calls its size virtual
`+0x64`. The low 16 bits form a divisor. The wrapper then reads requested bytes
`+0x38` and pool-name pointer `+0x4C`, and computes the rounded element count
with the original wrapping 32-bit addition and unsigned division. A zero
divisor is an undefined original operation and fails explicitly.

A nonempty pool name calls `80060834`. Null, empty, or a failed named lookup
selects `80068430` / `8005929C` using the target's live reference high byte.
The resulting pool and count are captured by the helper. It independently
reloads the element for another `+0x64` call and then for alignment virtual
`+0x6C`. Allocation size is the wrapping product of the captured count and
the second size's low 16 bits; alignment is also truncated to 16 bits. The
selected pool receives the original aligned allocation virtual `+0xDC`.

A null allocation leaves the old member untouched and reads failure status
global `805622D4`. A nonnull allocation reloads the element once more, captures
reference-field metadata global `80561684`, and calls the element's fresh
metadata getter `+0x58`. Native `68128` walks that returned metadata's live
`+0x38` parent chain, comparing pointer identities with the captured reference
type. Unknown parents pause; a cyclic chain faults.

Matching reference ancestry zero-fills the allocation with the captured byte
count. Nonmatching ancestry preserves allocator bytes. The zero-fill writer
retains the original byte stores below 32 bytes and aligned word stores for
larger ranges, including the prefix of completed stores before a bounds fault.
There is no per-element constructor, old-buffer release, or list-count update.
After allocation and type callbacks, the helper reloads descriptor `+0x08`
and publishes the captured allocation pointer there. It then reads success
status `805622D0`. The outer wrapper ignores this status after the helper
returns, as in the original.

## Physical storage and native services

An allocation supplies `IgbPointerListBacking`: explicit arithmetic address,
scratch allocation, byte offset, and the appropriate field- or metadata-pointer
mapping. Missing physical or typed mappings remain pending; a host vector is
not evidence of initialized allocator bytes. Proven zero fill synchronizes the
represented pointer slots to null, retaining displaced host owners without
altering original reference counts.

Publication at an internal list's `+0x08` updates its physical pointer and
canonical backing buffer, byte offset, arithmetic address, and pointer mapping.
It preserves the list's existing count. Raw overwrites retain displaced backing
owners. Allocator results and late bindings cannot substitute another allocation
at an already captured address.

The small native element adapter recognizes object-reference table `804762FC`:
size `8006A538` returns four, alignment `8006CD08` returns four, and metadata
getter `80094090` reads canonical `805616E8`. Named/tagged pool lookup and pool
allocation remain explicit service endpoints. Unknown element virtuals are
also explicit; the module does not fabricate a successful operation.

Status cells remain explicit service inputs. Original surrounding startup
establishes success zero and failure one: `8003CFC8` / `8003D004` initialize
`805622D0`, and `8003D000` / `8003D00C` initialize `805622D4` before ArkCore
construction. Static initialization repeats these stores at `80094848` and
`8009484C`. These values are proven startup preconditions, not guesses from BSS.

## Internal-list integration and verification scope

The reached internal object-list `_data` descriptor requests `0x40` bytes and
uses the real object-reference prototype. Its two size calls return four and
alignment returns four, so allocation requests 64 bytes aligned to four.
Its reference-field ancestry requires zero fill of 16 pointer slots. The
separate `_count` int descriptor initializes count zero. Object-list table
`80475CB0` has native return-only tails at `+0x0C`, `+0x24`, `+0x2C`, and
`+0x34`; its `+0x58` getter reads canonical list metadata `805619CC`.

Synthetic tests cover separate live size/alignment/metadata reads, truncation
and wrapping arithmetic, pool selection, allocation failure, exact publication
and status order, late mappings, selected continuations, type-parent changes,
raw overwrite lifetime, reentrancy, and partial zero-fill failures. Prepared
allocator/field callbacks in these tests are explicit boundaries. This module
does not establish complete startup, gameplay, or all recursive field types.
