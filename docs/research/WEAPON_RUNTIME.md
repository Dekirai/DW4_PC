# Prepared weapon controller

`WeaponRuntime` registers loaded `WeaponProgram` owners on the existing
`ActionEventRuntime` messenger. It implements the represented `beWeapon`
`GENERATE`, `REMOVE` and `GETINFONAME` fallback commands. The caller supplies
actual weapon resources and prepared PLAYER, ACTION and MODELCTRL providers.
This component does not construct a complete player or execute its renderer.

The supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`, contains the handler at `80318474`.
The enum at `802B3370` maps GENERATE, REMOVE and GETINFONAME to 0, 1 and 2.
The manager vtable entries at `804DFDCC` and `804DFDD0` select that fallback
and the unconditional-zero structured hook `802F2238` respectively.

## Registration and lifetime

The runtime allocates a separate native manager identity. Each registered RAM
uses its actual loaded source occurrence and canonical physical reference;
raw aliases resolve to that same live RAM. Generic initialization assigns the
registration ordinal and parent info and resets the four generic RAM words.
The authored active byte remains unchanged. Every ordered registration row
survives, including duplicate infos/RAMs. Later writes to a shared RAM establish
its final parent/index while earlier messenger rows retain their original
category, name and index keys.

Registration uses PLAYERARMS and normalizes a null info name to the original
empty string. Batches validate before registering; duplicate source occurrences,
conflicting owners or unsupported ordinals cannot leave a partial registration.
The prepared profile supports at most four RAM occurrences per info, matching
the manager's four tracking slots. `set_active` and `set_index` expose explicit
prepared live fields; they do not infer activation or player construction.

Original registration `80318298` allocates the manager's four tracking slots
once. All infos registered on that manager share them. Each slot retains an
ordered selector/appearance-list pair for every generated attachment. A
different weapon info on the same manager and ordinal therefore removes earlier
attachments. Separate managers and ordinals keep separate tracking lists.
The manager and loaded definitions remain alive through registered providers
even if the public inspection facade is destroyed.

## Command order and continuation

GENERATE captures its RAM's current parent definition, then performs these
original phases:

1. Remove existing attachments in the RAM's current tracking slot.
2. Query PLAYER/null/live ordinal/GETINFONAME_RUN. A completed result whose
   final string count is not one returns handled immediately after removal.
3. Query ACTION/returned name/live ordinal/GETAPPEARANCELIST. Capture actual
   object zero as an appearance list and object one as a skeleton.
4. Send every ordered sequence override to MODELCTRL/returned name/live ordinal
   using SETLABEL. A null row name becomes empty; a null label stays null.
5. Resolve each ordered bone name on the captured skeleton. A null name becomes
   empty; a negative lookup skips that node. For a match, allocate one selector
   with the ordinary bone index and the actual weapon-node child. Append that
   same selector to every appearance occurrence, then retain the selector/list
   pair in the RAM's current tracking slot.

The parent definition, returned name and returned object owners survive later
nested sends and shared-buffer reuse. The ordinal is read at the actual later
send/tracking phases. Original common weapons use helper bones that can have
`joint_index == -1`; selection uses bone matrices, never a skinning palette.
The appearance and skeleton must be non-null typed results from the same
actual loaded scene source. Matching path and occurrence labels alone cannot
join objects from separately constructed resource owners.

Each resolved messenger fallback retains its own invocation state and nested
`MessengerDispatch`. An unavailable service preserves completed removal,
queries and sequence rows, then resumes the same child send. Distinct nested
deliveries, including generation on the same RAM, retain separate invocation
states while sharing the manager's tracking lists. Completed messages are not
replayed. A thrown callback or checked failure after entering the effectful
prefix faults the manager; later mutation cannot guess whether to repeat it.
No rollback reinstalls already removed attachments.

REMOVE consumes the tracked appearance-list owners, not the current ACTION
query result. For each selector it removes every pointer-equal occurrence from
each appearance's bone-select list, preserving unrelated order, then clears
the manager slot. This follows `801D38D8`/`800693C4`. It does not clear all
appearance nodes or remove another manager's selectors.

GETINFONAME appends the current parent-info name, normalizing null to empty,
without an active-byte gate. Unknown command bytes return the original zero
fallback result. GENERATE/REMOVE do not add an invented active gate; ordinary
messenger enabled-only filtering remains separate.

## Bounds and validation

The resource decoder supplies bounded, typed immutable metadata. Registration
allows at most 4,096 loaded weapon sources and the messenger's existing
aggregate owner/row/text limits. Each of the four tracking slots permits at
most 4,096 attachment pairs. Scene owners independently bound nodes, selectors
and appearance list sizes. Invalid result types, mixed loaded owners and live
ordinals outside 0–3 are explicit checked failures. These failures do not claim
to emulate an original invalid pointer dereference or allocation failure.

The synthetic runtime suite exercises canonical aliases and final parent
writes, retained facade lifetime, ordered nullable labels, helper bones,
shared selectors across appearances, different-weapon removal, duplicate
pointer removal, old list retention, wrong PLAYER cardinality, interrupted
nested sends, live ordinal changes, nested same-RAM generation and uncertain
callback faults. The current Release core passes 105 synthetic runtime checks,
including independent managers sharing appearances and absent-bone skips.
Original resource validation is documented in
[WEAPON_RESOURCE.md](WEAPON_RESOURCE.md). Integrated deferred delivery through
four original player owners is a separate acceptance check.

The represented graph is ready for explicit selector traversal/capture by its
owning actor. Complete materials/rendering, the omitted player status prelude,
remaining player-creation UI/map calls and the original outer scheduler remain
separate implementation boundaries.
