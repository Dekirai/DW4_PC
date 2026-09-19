# Loader directory lookup and loading claims

This module reconstructs the USA loader operations `8006F35C` (lookup),
`80043D44` (acquire a loading claim), `80043DE4` (release a loading claim),
and `80070140` (release by filename). Claims are separate signed 32-bit
fields; they are not object reference counts. Arithmetic wraps at 32 bits.
The supplied executable has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

## Shared owners and lookup

`SystemStageLoader` holds its root directory at original `+0C` and an ordered
directory list at `+10`. List count and capacity are independent of physical
backing storage. `SystemStageDirectory` holds filename `+14`, a nonowning
loader pointer `+20`, claim count `+34`, and optional mutex `+44`. A weak
pointer with an expired owner is distinguished from an original null parent.
The fields are shared with the stage caller and directory request module.

Registration `80032B6C..78` supplies name table `804671D8`, descriptor-global
table `80467220`, and offset table `80467268`. They identify `_name` at `14`
with global descriptor `80561CB0`, `_loadedRefCount` at `34`, and
`_loadingSemaphore` at `44`. The native `LoaderDirectoryMutex` name describes
its acquisition/release boundary; it does not substitute host mutex semantics
for the original semaphore virtual operations.

Ordinary lookup uses the actual `800695CC` traversal: capture the list owner,
then the global filename descriptor, then the signed count once. Each reached
iteration reloads the list backing and descriptor offset. Offset `14` selects
the represented filename field. Null filenames are skipped; null rows are
invalid dereferences. The first case-sensitive C-string match wins, including
termination at an embedded zero byte. A represented null descriptor returns
no match without reading the list; missing native service bindings pause
before selecting the descriptor. Negative counts visit no rows. Unsupported
metadata offsets fail only when a row reaches that field access.

The exact special name `igImageDirectory` takes `8006F5E0`. That accessor can
create a directory and also reaches `80098E7C` when a root already exists.
It remains an explicit retained operation. Merely having a root pointer does
not satisfy the accessor or create a successful placeholder directory.

## Claim ordering

With no mutex, acquisition increments immediately and returns that value.
With a mutex, capture its virtual `+84` operation with argument true. After
completion, read the live success word from global `805622D0`, then the live
claim count. Only an equal result increments the count. A failed result
returns the captured count without unlocking. A successful result publishes
the increment, reloads the directory's mutex, and calls its virtual `+7C`.
Replacing that mutex with null therefore fails after the increment.

The initialized success word is zero: startup `800946D8/80094848` and the
first `igRefAlchemy` path `8003CFC8/8003D004` both publish zero to that global.
The service retains an explicit optional binding and reads it after the
acquisition callback, preserving both an unavailable native binding and live
replacement of the original word.

Release also calls optional virtual `+84(true)`, but ignores its result. It
always decrements the live count and retains that signed result independently
of subsequent callback mutations. It reloads and unlocks a present mutex.
If the retained result is less than one, it then reads the current parent
loader and captures that loader's list. A null parent returns normally. The
first identical directory pointer is removed in physical order, using the
captured search count and original `80041C10` erase behavior; the new trailing
slot is cleared. Duplicate occurrences are not all removed.

The original reaches a backing access at index minus one when the directory
is absent. Native code reports that invalid access after the decrement and
unlock, rather than silently accepting missing membership. Captured return
values survive callback changes to the claim count, parent, list or root.

## Filename release wrapper

`LoaderDirectoryReleaseInvocation` performs lookup and holds its returned
directory. No match completes without changing loader state. For a match,
capture whether it equals the current root before calling release. Fill the
directory's parent only if its original pointer is null. After release, if
the captured root comparison was true and the returned signed count is less
than one, release and clear the root field that is now live. A mutex callback
can therefore replace the root that will subsequently be cleared. Finally
release the held directory reference.

## Continuations and ownership limits

A pending mutex or root operation retains the selected owner and callback.
Later call sites reload fields at their original sites. Claim and wrapper
state reject overlapping updates, recursion and restart after an abandoned
or faulted published prefix. Completed operations are idempotent. These are
native continuation contracts rather than hardware exception semantics.

Unknown final directory destruction is a reached failure boundary. The
wrapper retains its outstanding temporary in loader state after failure or
abandonment; it does not lose that reference when the continuation is
destroyed. Native
shared owners represent valid nonfinal ownership, not packed guest reference
counts. Parent pointers remain nonowning to avoid an invented loader cycle.

For valid represented ranges, moving native list owners has the same final
ownership and ordering as the original release, raw memmove and trailing
zero. Native moved-from slots become null earlier within that uninterrupted
copy. No original callback occurs there in this supported profile. Malformed
partial memmove effects, allocator metadata and unknown final destructors
are not claimed equivalent. Reached backing accesses are bounded to 262,144
slots; counts are not clamped, sorted or deduplicated.

## Validation

The permanent synthetic suite passes 142 checks covering exact lookup and descriptor semantics,
noncanonical mutex results, live field replacement, signed wrap, duplicate
membership, missing/expired owners, root replacement, retained callbacks,
pending global bindings, overlap, abandonment and caught recursion.

The ignored independent oracle executes the actual lookup, named-list,
claim, erase and wrapper instruction bodies. Its prepared endpoints are
`strcmp`, `memmove`, mutex virtuals, and the special root accessor. Reference
counts exclude final destructors and physical ranges are valid. It compares
returned words, directory/loader/list state and callback traces. A driver
linked only against the final production core matched all 4,096 cases:
392,355 original instructions across 328 addresses, with zero mismatches.
The composed core build passed all 133 CTest tests. These cases do not
establish complete loading or startup. The original caller ordering and
abandoned-temporary preservation also received independent source review.
