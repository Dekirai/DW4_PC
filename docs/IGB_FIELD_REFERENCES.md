# Physical packed descriptor references

`IgbFieldReferenceAccess` shares reached reference reads and writes across the
native field batch, finalized append, metadata inheritance and base-object
defaults. Original descriptor `+04` is a packed 32-bit word: arithmetic updates
the entire word, while selected destruction branches test only its low 23 bits.
Allocator bits must survive ordinary increments/decrements and participate in
full-width wrapping arithmetic.

Earlier implementations updated only `IgbDecodeRuntimeField::reference_word`.
The optional physical header could then disagree during a pending append or
after inheritance. Later object/probe operations already read that physical
word, so matching only the canonical cache was insufficient.

## Captured access and publication

Construction captures the descriptor and its optional physical allocation,
offset, original address and byte owner without dereferencing memory. Reached
reads use the physical word when that binding exists. They do not overwrite
physical memory or refresh the canonical cache. Reached writes validate the
complete storage and knownness extents, then publish the word through the
existing bootstrap writer, which synchronizes its bound canonical owner.
An unavailable or partial word cannot change the cache first.

Unknown input bytes remain unknown and fail when read. A fully represented
unknown destination word can be written. A released, expired, rebound or
conflicting physical identity never falls back to the cached word. Once an
access faults, restoring the former binding does not restart it. Descriptors
that had no physical binding retain the explicit canonical-only profile.

The access retains no callback or service owner. Unfinished caller ledgers keep
the captured descriptor/storage/bytes alive without a host ownership cycle or
an invented original release.

## Original caller ordering

| Caller | Recovered reference order |
| --- | --- |
| Batch `80065924` | Capture metadata field registry; skip retain for null, otherwise actual `80053E6C` full-word retain; append; fresh read/decrement/store; fresh low23 read; conditional `80066E1C` |
| Finalized append `80065844` | Actual `80063F14` first; capture metadata registry; full-word retain before raw append |
| Parent attachment `80065640` | Freshly select each inherited field; capture child registry; retain before appending; preserve that retain after completion |
| Base defaults `80066B7C` | Preserve construction/name/finalization; after append, read/decrement/store and freshly read low23 before the reached destructor |

The batch's null constructor result still skips the original retain and faults
only at the later null creator release after a completed append. Retained append
and destructor continuations never replay a completed increment or decrement.
Callbacks that modify the actual header are observed by the later fresh read.

## Finalization prerequisite

The independent reference comparison also exposed two earlier finalization
gaps. Actual `80063F14` reads the descriptor's physical vtable before each of
its type, size and default virtual calls (`80063F28 / 58 / 74`). It then reads
the physical global-cell pointer at `+1C` (`80063F84`). A canonical-only read
could call the type virtual before an unavailable first word, or append/retain
after an unavailable last word.

The native finalizer now captures optional physical ownership without an early
read and uses those fresh physical vtable loads. Ordinal/size stores and the
later pointer read validate the same identity after the selected callback
finishes; a pending callback does not trigger an invented earlier receiver read.
Physical global pointer zero skips publication even when its canonical slot is
stale. A nonzero pointer is captured once and waits for the existing matching
numeric address and weak cell owner. Supplying that mapping does not replay the
pointer load or completed callbacks. No new address registry is manufactured.

Base defaults also publish their original `+1C` producer stores at `80066BB0 /
80066C90` before interning names. The exact `805622A8 / 805622AC` addresses reach
physical storage before the canonical mapping, so the finalizer consumes the
same pointer. A failed physical store leaves the later cache, names and append
untouched. This required producer correction does not extend the other adjacent
base-default stores into a complete raw-memory profile.

Canonical-only finalization keeps its existing profile. Returned type ordinals
and native global-cell storage remain explicit typed mappings; this correction
does not claim arbitrary raw pointer dereferencing. Buffer and name operations
are unchanged.

## Limits and provenance

This change covers the four listed callers and the reached finalizer prerequisite.
It does not synchronize descriptor
names, metadata reference headers, physical string headers or every copy,
prototype, list-specialization and adjacent descriptor store. Existing canonical
comparison projections remain historical evidence; this change does not
retroactively turn them into full raw-memory comparisons.

Primary evidence is the supplied USA DOL, SHA1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Relevant original instructions are
`80053E80 / 80053E88`, `80065978..80065994`, `800656BC..800656E8`,
`80066C4C..80066C68` and `80066D2C..80066D48`. IDA was used to confirm the
base-default release sequences. Disassembly and comparison corpora stay local.
See [VALIDATION.md](VALIDATION.md) for measured validation and remaining limits.
