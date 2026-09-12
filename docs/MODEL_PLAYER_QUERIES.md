# Prepared MODELCTRL player queries

`ActionEventRuntime` implements the original base MODELCTRL `GETINFONAME`,
`GETINFONAME_RUN`, `GETHP` and `SETLABEL` bodies on explicitly prepared player field
owners. A separate `register_player` call adds the actual PLAYER category row.
These endpoints allow existing messenger consumers to obtain a live player
name and change the real sequence label cache. They do not execute original
player construction. Explicitly attaching `ModelStatusWork` also routes the
original PC virtual+C4 status prelude before the generic switch; see
[MODEL_STATUS.md](MODEL_STATUS.md). Other subclass virtuals remain unsupported.

## Shared HP results

`GETHP` (`8030E49C..8030E4FC`) appends integer zero only when the actual active
byte is zero or the explicitly prepared base-work pointer is null. Active work
appends current signed HP, the same HP wrapper and then the same MHP wrapper.
It does not clear existing arguments or change enabled-only filtering. A null
MHP becomes a nullable object result; a null HP reaches a checked error before
its unsafe scalar load. Missing or expired native work is an explicit service
boundary, not an invented original null pointer. `METERCTRL SETMODELHP` retains
the returned shared wrappers, so later HP writes reach its update directly.

## Field ownership and registration

`prepare_model_queries(model, ModelQueryOwner)` attaches these fields once to
an existing `PlayerRuntimeBinding`:

- The exact bound parent-info reference, source occurrence and source path.
- One shared nullable parent-name field. RAMs belonging to the same physical
  bound info share this field owner.
- Separate live RAM-number, raw active-byte and work-presence field owners for
  each actual RAM. The initial RAM number must match the binding's selected
  ordinal, in zero through three.

The binding decoder already requires an exported `beModelCtrlInfo` whose kind
is exactly `KIND_PLAYER`; this API does not infer player status from a model
name. Parent info is checked against the binding's exact selected reference,
without accepting an unprovided reference-alias mapping. Conflicting name-field
aliases, shared per-RAM fields, missing owners, invalid initial ordinals and
invalid source identity reject before attachment. Existing attached fields
cannot be replaced. Initial attachment may resolve an already suspended query,
like supplying a missing manager provider; it does not restart that delivery.

The same raw active-byte cell is used by messenger activity filtering, the
MODELCTRL endpoint gates and `set_active`. Calling `set_active` writes zero or
one into this cell. Direct prepared field updates can retain any byte value;
all nonzero values satisfy the endpoint's original active test.

`register_player(model)` is explicit. It captures the current name, converting
null to a non-null empty registration name, and the current checked RAM ordinal.
Every call appends one registration occurrence. It neither replaces prior rows
nor updates already cached recipient snapshots. Later field changes affect
queries but do not rewrite earlier registration keys. There is no source-file
filter or deduplication of query results.

## Endpoint behavior

| Command | Original gate | Effect |
| --- | --- | --- |
| GETINFONAME | No activity or command-work gate | Append the prepared parent-info name to the current string list; null becomes non-null empty. |
| GETINFONAME_RUN | Live RAM active byte is nonzero | Append the same live name; inactive RAM appends nothing. |
| SETLABEL | Live RAM active byte is nonzero and prepared command work exists | Read nullable string[0] and string[1], then update the actual sequence label cache. |

Inactive GETINFONAME_RUN and inactive or explicitly absent-work SETLABEL handle
the command without reading omitted payload slots. Active queries with missing
field preparation stop at an explicit service boundary. Missing fields are not
silently replaced by a name copied from immutable binding metadata or assumed
absent work. An active, present-work SETLABEL requires two actual string slots;
a missing second slot is different from a present null string.

Each successful body returns handled value one. Queries append to the existing
shared buffer, preserving other arguments/results. SETLABEL neither clears nor
appends a result and ignores additional payload slots. A later recipient can
append another name; consumers such as the weapon generator must apply their
own original final-count condition after the whole send completes.

The parent-name field is read when that recipient runs, after any earlier
recipient effects. A suspended send retains completed recipients and their
already appended string values. Resuming does not re-read an earlier recipient's
name or replay its label mutation. Existing messenger fault and ownership rules
apply, including rejection of unrelated top-level packet access during a pause.

## Nullable labels and sequence guards

`ModelSequenceLabel::label` and both public `set_label` APIs use
`optional<string>`. The original setter preserves null and empty separately for
both the sequence key and label. It replaces the first matching sequence key
or appends a new entry, preserving order and setting the cached offset to -1.
This mutates the existing `ModelSequenceRuntime`; a separate weapon-label map
would not affect actual sequence selection.

An uncached null label is stored successfully. A later selection that would
look up that label reaches an explicit checked unsafe-string boundary: original
code passes the null pointer directly to `strcmp`. Native selection rejects
without altering its current sequence, cursor or motion state. A non-null empty
label can match an actual empty LABEL command. The existing serialized
LABELCTRL path continues to normalize its own null command name to non-null
empty before calling the setter.

Pending sequence selection, Lua or subcommand work prevents messenger SETLABEL
from mutating that work. A conflicting delivery faults explicitly; it does not
claim a missing service that cannot resolve while that send owns the runtime.
Valid nested delivery to available target work remains supported.
Direct setters retain their existing execution and
ownership guards. Invalid payloads, unsafe label lookup and unsupported outer
commands are checked boundaries, not invented successful controller effects.

## Original evidence and remaining prelude

The reference is the supplied USA GameCube executable, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.

- Base enum `802CCD64` uses names `804D1084`, values `804D1108`: GETINFONAME=3,
  GETINFONAME_RUN=4 and SETLABEL=7.
- `8030DEFC..DF1C` appends captured info+08; `8030DF20..DF4C` first tests live
  RAM+14. Both normalize a null name to the original non-null empty literal.
- `8030E444..E498` tests RAM+14 and captured work before reading the two string
  slots through `8030709C`; it calls `8030B014` and handles with one.
- `8030B420..428` and `8030B5CC..5D4` preserve null label strings in replacement
  and append paths. `8030D084` resolves cached offsets; `8030D1F8..1FC` passes
  the stored label pointer directly to `strcmp`.
- PLAYER registration hook `8034C39C` calls generic `80305308` at `8034C3B4`,
  using the actual incoming RAM and original parent-name/RAM-number fields.

Original base handler `8030D910` captures ONE/work and invokes virtual+C4 before
the base command dispatch. PC implementation `803AEBB4` lazily prepares status
and work before consulting its sixteen-name dictionary. All three endpoints
above are absent from that dictionary, but its earlier preparation still has
effects. This adapter represents an explicitly prepared, declined base path;
it does not fabricate completion of that omitted PC prelude or status methods
such as SETMETAL. Full player readiness remains separate.

## Validation

The query suite passes 54 synthetic checks and 70 with the supplied original
data, including all four Agumon RAMs. The separate sequence suite passes 1,122
original-inclusive checks with nullable label storage and lookup enabled.

Synthetic tests exercise explicit preparation, inactive and absent-work gates,
live names/activity, exact null/empty distinctions, extra typed arguments,
duplicate registration and retained routing keys, shared physical field owners,
actual label-driven sequence selection, malformed inputs and missing-owner
continuation. A real earlier recipient changes the name; a later missing
provider then pauses the send, verifying that completed name results are not
replayed on resume. Separate sequence tests cover nullable cache replacement,
copy independence and the atomic unsafe-lookup boundary.

The optional original-data mode prepares all four actual Agumon MODELCTRL RAMs,
registers their PLAYER rows, obtains one exact active name per ordinal, and
delivers MODELCTRL SETLABEL to select the authored slash branch at SQ_WAIT
command offset four. These checks do not claim full PC construction or weapon
generation. Original binary/disassembly material remains ignored.
