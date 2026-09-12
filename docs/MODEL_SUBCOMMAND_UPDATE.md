# Actual model subcommand frame update

`ModelSubcommandUpdateInvocation` connects one actual MODELCTRL frame's captured
work `+E4` wrapper to its existing ordered subcommand queue. It implements the
original `803107E0` / `80310754` update glue against the canonical RAM graph.
The prepared `ModelSubcommandQueue::update(float, context)` profile remains
available separately. This adapter does not derive an original frame delta
from a host clock, a rendering interval or the named `KEIKATIMER` cache.

An empty wrapper completes without a system, parent, ONE, work or external
effect provider. A delayed entry advances using the actual system owner. A
reached SOUND, EFFECT or HITBOX operation requires its corresponding real
provider; retaining an entry or reaching a messenger boundary does not produce
audio, create an effect actor or complete collision delivery.

## Original reads and captures

The supplied USA DOL has SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. The following addresses identify
recovered behavior; no original executable bytes are shipped by this module.

| Original address | Consumed owner or field |
| --- | --- |
| `8030D000` / `8030D008` | Outer frame captures its work's current `+E4` wrapper, then calls the update with RAM |
| `803107FC..80310800` | Wrapper update reads the list and captures its initial count |
| `80310764` | Each reached entry reads the RAM's current manager `+10` |
| `80310768` | Read this entry's current elapsed frames `+0C` |
| `8031076C..80310770` | Read captured manager's system `+10`, then system frame delta `+28` |
| `80310774..80310778` | Add in binary32 and publish elapsed before reading started or delay |
| `8031077C..803107AC` | An unstarted entry meeting its delay publishes started, then calls virtual `+5C` |
| `803107B8..803107C4` | An already started entry calls virtual `+60` |
| `80310808..80310888` | Visit original count in order; remove completed entries without skipping the next |

The canonical `ModelSceneManager::system` retains `ModelSceneSystem`, whose
`frame_time_f` is the represented system `+28` field. System preparation and
the enclosing scheduler remain explicit. The system's original conversion
from world-time difference is documented in [MODEL_SUBCOMMANDS.md](MODEL_SUBCOMMANDS.md).
Its delta is uncapped and independent of the named per-RAM timer.

The updater does not capture a frame-wide delta. A callback that changes the
RAM's manager or the system delta changes the next reached entry's advance.
A suspended entry retains its elapsed/start prefix and does not read the delta
again when its effect provider resumes. Callback appends wait for the next
wrapper pass. Replacing the outer work's `+E4` field leaves this invocation on
the original wrapper supplied at construction.

## Independent virtual helper scopes

`ModelSubcommandUpdateScope` contains the actual RAM and only the additional
owners consumed by the reached virtual helper. These are independent of the
outer frame's captured work.

SOUND start `803105DC` captures the current manager at `803105FC`. It consumes
no parent, ONE or work. The provider receives the nullable sound name and must
perform the actual `SOUND PLAY` operation. The existing queue preserves the
original returned-handle/type rules, including retention only for a valid
type-4 handle. Retained sound/effect update virtuals return true in the
original and therefore need no invented playback-completion polling.

EFFECT start `80310340` captures manager and parent at `80310368/6C`, before
`FIND_SPACE`. A completed no-space result removes the entry without consuming
the parent name or preparing RAM data. A successful result publishes the
selected effect RAM. At `803103BC/C4`, the helper reads the live source RAM
number and captured parent's current nullable name, applying the original
null-to-empty fallback. It then prepares the current RAM data and resolves its
first named, correctly typed ONE, followed by the current work at `803104A8`.

The attachment read uses that fresh work's `+DC/+E0`, after space selection.
A non-null empty attachment name remains empty. A null name uses a fresh read
of the captured parent's name and the live source RAM number. The complete
generation request is retained across unavailable generation services. A retry
does not repeat `FIND_SPACE`, select another ONE/work, or rebuild that request
from later attachment changes. Each subsequent effect captures its own scope.

The adapter stops at the actual-effect provider boundary. It does not expose
the original messenger's intermediate argument-list appends before a complete
generation request. Full native SOUND/EFFECT messenger transport, including
that intermediate buffer visibility, is a separate integration step.

HITBOX check `8030FEEC` captures parent at `8030FF10`, before data preparation
and ONE lookup. After lookup it reads current manager and the ONE's work at
`8030FFD0/D4`. The work may be null at this point: the later original debug
gate handles that distinction. Its provider owns the remaining geometry,
debug display, `CHKBULLET` and source `BHit` operations. The provider receives
these actual owners; the request does not supply a prepared source or
attachment snapshot in place of the original later reads. An expired HITBOX
entry is removed by its existing update virtual before another check or ONE
lookup.

## Continuations and checked boundaries

The invocation retains the first non-null `ModelSubcommandUpdateServices`
owner supplied to `resume`. Missing functions may be installed on that owner
before retry. Replacing it during an invocation rejects. Providers must retain
their own partial native operation when returning unavailable, and resume
that same operation without replaying completed effects.

The wrapper retains its actual access binding weakly. The invocation retains
RAM/parent/ONE/work, and the scope's manager reference is weak because the
manager can own the enclosing frame continuation. This avoids a
work-to-wrapper-to-work or manager-to-frame-to-manager ownership cycle.
Destruction performs no external cleanup. Abandoning a suspended actual
invocation makes its remaining queue access unusable; another facade cannot
pretend that access is still a prepared update.

Exceptions fault the invocation and queue. Recursive entry, including a caught
recursive call through another facade, poisons the active actual pass before
it can claim success. Explicit sequence cleanup can interrupt a paused update,
but resuming the superseded update rejects rather than treating completion of
cleanup as completion of the old update.

The actual profile checks finite elapsed/delta values and arithmetic overflow
only at the reached entry, preserving earlier committed entries and removals.
It uses the existing startup NI=1 binary32 addition, including tiny-result
flush to signed zero. Non-finite original state and alternate FPSCR modes are
outside this represented arithmetic profile. The prepared profile retains
its existing eager validation contract.

The native list exposes ordered entry owners and callback append/removal; it
does not expose arbitrary replacement of wrapper `+08`, nullable physical
entry slots, or independently malformed count/capacity/backing. Original
`803107E0` reloads that list reference at every iteration and removal. Those
additional mutable-list states remain outside this adapter's supported public
profile and are not claimed by its original-count behavior.

## Validation

Focused tests exercise empty/delayed gates, per-entry manager/delta changes,
count snapshots and callback appends, fresh effect ONE/work after successful
space lookup, nullable and empty attachments, retained packets and replaced
outer wrappers, reached invalid-owner and numeric prefixes, HITBOX scope and
expiry, service-owner retention, caught recursion, explicit cleanup
interruption, and cycle-free abandonment. The existing prepared subcommand
and HITBOX suites remain separate regression checks.

These tests validate the native access/continuation contract. They do not
claim physical-console execution, audio output, full effect actor creation,
HITBOX target response, or completion of the original frame scheduler.
