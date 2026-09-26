# Adopting the original current thread

`IgbGamecubeThreadAdoptInvocation` reconstructs USA `80077EFC`, concrete thread
vtable `80470110` slot `+DC`. The original startup caller reaches this operation
after reflected thread construction and byte `+2C` publication. See
[IGB_GAMECUBE_THREAD_INIT.md](IGB_GAMECUBE_THREAD_INIT.md).

## Current-thread state and copy order

Original `8026111C` reads absolute `800000E4`. The invocation reads this cell
twice: the first result is written at thread `+350`, and the second selects the
source of the inline OS-context copy. `IgbOsThreadState` represents the canonical
cell as an optional address. Unknown state suspends; represented zero remains
null and faults if subsequently dereferenced. Source mappings retain original
arithmetic addresses and physical byte owners independently of host pointers.
An unavailable mapping suspends without rereading the captured second address.

The copy writes `0x318` bytes at thread `+38` in 105 original access groups:
89 pairs of word reads followed by paired stores, then the original halfword,
single-word and paired-word tail. Each pair reads both words before its first
store. The two halfword accesses at source `+2C8/+2CA` remain separate. This
ordering preserves overlapping source/destination behavior and the exact
completed prefix if a source read or destination write fails. A whole-buffer
copy would not preserve that contract.

The target has a canonical reflected thread identity; the original source is
raw OS storage and does not need an invented reflected object. The first target
access occurs at the reached `+350` store, after the first canonical load.
Captured physical owners and addresses are checked when accessed. Unknown bytes
are not silently replaced with zeros.

## Stack release

After the copy, actual `80068390` reads the fresh stack pointer at thread `+20`.
A null pointer skips tag and pool access. A nonnull pointer is retained while
the fresh single tag byte `+04` selects `8005929C`; the resulting pool receives
its virtual `+108` free call. Only after that call returns does the invocation
write zero at thread `+20`. The pointer originally returned by allocation is
not used to guess the pool, and pending callbacks do not replay the copy.

Tagged lookup and pool release remain explicit lower services. The optional
checked startup continuation lends the caller's fault guard through adoption
and release, without retaining it. A caught recursive startup failure therefore
prevents later native stores. Existing zero-argument callbacks retain their
selection and behavior. Incidental register leftovers from the original void
function are not public results.

## OS initialization remains a dependency

IDA analysis identifies `OSInit8025B8CC -> 80260FB4` as the initial OS-thread
construction path. It prepares the original record at `805151D0`, performs
context/stack/queue operations, invokes the selected switch callback and only
then publishes the current thread. Adoption does not implement that initializer.
It must not supply a host thread identifier or a blank record in its place.

The cold integration test uses an explicitly prepared raw context and pool
services to verify the composed continuation. It preserves the unknown-state
boundary first, then proves copy, pending free and the next manager insertion
call. Manager insertion, semaphore wait/signal, original OS initialization,
pool internals and scheduling remain required for complete startup.

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original artifacts and the IDA analysis
database remain local. Measured comparison scope belongs in
[VALIDATION.md](VALIDATION.md); prepared integration is not playable gameplay.
