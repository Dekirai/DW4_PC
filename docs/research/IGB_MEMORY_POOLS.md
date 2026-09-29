# Native memory-pool name lookup

`IgbMemoryPoolLookupInvocation` implements the reached lookup chain
`800608F4 -> 800607D4 -> 800605E8` and the concrete manager virtual `+74`,
`8005B410`. Its result is a numeric handle from zero through forty-five, or
`0xFFFFFFFF` for a null, empty or unmatched name. It does not return a pool
pointer, acquire an object reference, allocate a pool, or finish IGB loading.

## Selection and concrete binding

The selector at `805621DC` is a signed word. Values one through three select
`804F2294[selector - 1]`; zero selects the fallback owner at `805621C8`.
Other values return null, including four. The outer wrapper then dereferences
the selected manager before testing the name, so an invalid selector or a
represented null manager faults even when the requested name is null.

The getter does not initialize a singleton. Missing native representations of
the selected globals suspend at their individual read sites. The selector is
captured before a missing table/fallback read. The manager is captured before
its virtual binding; changing global selection while that binding is pending
does not replace the retained manager. An unrepresented concrete virtual stays
pending rather than reporting a successful lookup.

`initBootstrap` at `8003D2F8` allocates the fallback manager when absent and
calls `8005AF08`. That constructor installs vtable `80471BDC`, whose `+74`
entry is `8005B410` and `+B4` initializer is `8005D02C`. The latter copies
forty-six names from the original pointer table at `80469C40`, using a
127-byte bounded copy followed by an explicit terminator. Names occupy inline
storage at `manager + 0x18 + index * 0x90`; lookup scans all forty-six slots
without consulting a count or requiring a pool pointer in the same record.

`make_original_igb_memory_pool_registry()` reproduces this proven name-storage
initialization and concrete lookup binding. It does not install selector
globals or claim the constructor's other allocator/bootstrap work is complete.
The original initialized inline range is preserved separately from unknown
object-header bytes. This native registry can be explicitly selected by the
current decoder integration while original global startup remains unfinished.

The handle used for padding by the separate IGB pool-table caller comes from
live global `8055D89C`. Its initial word in the supplied USA executable is
`0xFFFFFFFF`. That global is not substituted for the result of a named lookup.

## Name reads and bounded memory

The requested `IgbRuntimeNameSource` retains its scratch owner and offset
without copying or reading the name. The concrete method first tests pointer
nullness and the first byte, then compares each physical inline name in order.
The first duplicate match wins. Comparison is case sensitive and ends at a
C-string terminator rather than the length of a host string.

The equality path follows original `strcmp` at `8009D5D8`. Native scratch and
inline owner bases are represented as four-byte aligned; the request offset
selects its alignment. Different alignment uses byte reads. Equal alignment
uses the original left-word then right-word reads and the original zero/high
byte test before its byte tail. This can read initialized bytes after a
terminating NUL. A terminator alone therefore does not authorize inaccessible
or uninitialized word padding. First-byte mismatches do not read unused suffix
bytes. The host-string convenience constructor supplies an explicit padded
scratch representation; it does not change reads of caller-supplied buffers.

Released, missing or uninitialized reached bytes fault. A malformed comparison
that exceeds the represented 4096-byte bound also faults. Inline storage remains
physical, so lookup does not infer nonexistent rows or convert a malformed
unterminated row into a valid host string. Host owner retention protects pending
work; it is not an additional original object-reference operation. These are
bounded native memory rules, not emulation of arbitrary invalid guest memory.

## Validation

Permanent tests cover original numeric handles at all four request alignments,
case sensitivity, embedded terminators, first duplicate matches, selector
boundaries, missing versus represented-null globals, retained selector/manager
captures, lazy name reads, unknown virtual bindings, aligned word padding,
early mismatch, and released/uninitialized/missing physical bytes. Independent
raw-instruction comparison is recorded in ignored
`work/native-igb-memory-pools-*` research artifacts.

The permanent suite passes 123 checks. A separate driver linked only against
the final production core matches all 3,307 original-instruction cases with
zero differences, including 1,340 reached null or physical-read failures.
The original wrapper, selection, concrete lookup and `strcmp` execute 1,209,746
instructions across 137 addresses. A separate execution of `8005D02C` and
original `strncpy` executes 19,464 instructions across 61 addresses; all bytes
in the initialized inline range match the native constructor-name helper.
The corpus covers the original names, all four input alignments, invalid
selectors, duplicate and custom binary names, word padding and incomplete
physical storage. Prepared manager storage and global pointers do not qualify
the original allocator or complete bootstrap activation.

Source evidence comes from the supplied USA executable with SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. No original instruction words or
game assets are included in the public module.
