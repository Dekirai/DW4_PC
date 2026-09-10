# Original ACTION event consumers

This records original-code research used by the version 0.24 implementation.
Prepared native routing now executes Se/ActionStarter table consumers and appends
owned sound/effect work. Actual audio playback and effect generation remain
external required services. See [ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md).

The reference is USA revision-0 `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Addresses below identify inspected
instructions and metadata, not an independent executed instruction comparison.
Original bytes and generated disassembly remain local.

## Registration and delivery

ACTION per-RAM initialization at `802EFDC8` registers the recipient through
`80305308` as category `ACTION`, parent info name and assigned RAM ordinal.
Generic registration at `803050A8` appends ordered rows without deduplication.
Names/categories compare exactly; no source-file qualification is an original
messenger matching rule. Native source identity must identify owned objects
without silently filtering valid cross-file recipients.

Ordinary ACTION dispatch at `802F21B4..802F21D8` sends `ACTION_BROADCAST`
after its first command run, including a WAIT yield. Same-action suppression,
queries, freeze and NDSP bypass this broadcast. `80305ED0` selects through
`80305344` and immediately sends through `80305A28`; this path is synchronous.

Recipient selection preserves registration order and duplicates. Either a null
requested name or null registered name matches any name; an empty string is a
real exact key. Requested or registered index `-1` is a wildcard; otherwise the
indices must match. Index `-2` instead chooses the first inactive RAM. An enabled-only
flag equal to one skips inactive ordinary recipients. The structured manager
hook at virtual `+0x90` runs first; a false low byte invokes the command-string
fallback at `+0x8C`.

The messenger owns a 64-entry move-to-front selection cache keyed by category
and nullable requested name, excluding index. A nonempty hit reuses captured
recipient rows. Registration does not invalidate that cache in `803050A8`.
The native messenger now preserves reset/invalidation and interleaved registration;
recollecting fresh recipients on every send would change this observed behavior.
Nested sends also use the shared argument/result buffer and can change its
enabled-only flag for later recipients of the outer send.

## Real consumers

| Consumer | Registration / structured hook | Effect |
| --- | --- | --- |
| `beActionStarter` | `802F2244` / `802F228C` | Registers `ACTION_BROADCAST / null / -1`; translates matching table entries to `MODELCTRL SETEFFECT` |
| `beSeCtl` | `8031308C` / `803130D4` | Registers `ACTION_BROADCAST / null / -1`; translates matching table entries to `MODELCTRL SETSOUND` |

ActionStarter selects the first matching model-table row, then emits all matching
action rows in order. Se selects the last matching model-table row, then emits
all matching action rows. Their data uses an empty fallback for null model/action
names. Consumer source identity is independent of the emitting ACTION source
and the destination MODELCTRL source.

MODELCTRL `SETEFFECT` at `8030DD34..8030DE18` and `SETSOUND` at
`8030DE1C..8030DE64` append owned subcommands through `80310B50` and
`80310FAC`. The recipient must be running and have model work; otherwise these
handlers return without appending. Effect work retains delay, initial handle
`-1`, effect-info name, message, offset, rotation and lifetime/position-lock
flags. Sound work retains delay and sound name. Later subcommand processing
performs creation/playback through additional managers.

The prepared runtime now decodes loaded consumer exports, registers their RAM
occurrences, synchronously routes translated messages and appends typed work to
the selected model queue. It also owns delayed start and sequence interruption
with explicit sound/effect manager boundaries. A completed append remains
distinct from an executed sound/effect. Full startup registration, player
construction and actual audio/effect services remain required. See
[ACTION_EVENT_RUNTIME.md](ACTION_EVENT_RUNTIME.md), [ACTIONS.md](ACTIONS.md) and
[CONTROLLERS.md](CONTROLLERS.md).
