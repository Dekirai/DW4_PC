# Original deferred messenger

The native messenger implements the prepared deferred-message path used by the
MODELCTRL player-start caller. A queued PLAYERARMS `GENERATE` request is retained
until due; enqueuing it does not construct a weapon. The original outer clock,
loaded manager order and full gameplay loop remain required.

The reference is the supplied USA revision-0 `main.dol`, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original bytes, instruction probes
and generated disassembly remain outside tracked source.

## Preparation and packet ownership

Original initialization `80303C0C` prepares five stack-buffer owners and 32
deferred records. Each record owns a separate argument buffer. The queue count
includes inactive reusable records. Preparation `80305F9C` clears CURRENT and
writes category, nullable name and signed index into the inactive tail. If that
tail is active, it creates another record. Repeated preparation reuses the same
inactive tail, replacing its keys and clearing CURRENT again. Preparation does
not select an ordinary recipient-cache record, set a command or activate work.

`prepare_deferred` exposes those effects. Callers append the represented typed
arguments through `buffer()`. `schedule_deferred` then reloads the live tail and
current packet; it does not use a reserved token or a private argument snapshot.
An intervening ordinary selection or explicit push/pop therefore changes which
packet is committed. `defer_to` performs preparation and commit for an empty
argument request, as the original wrapper `8030650C` does.

Scheduling `803061BC` swaps CURRENT's buffer owner with the record's buffer
owner. It retains the command, writes active=1, removes the tail and inserts it
before the first inactive record or strictly later signed deadline. Equal-time
messages preserve enqueue order. Recipients are selected only at delivery;
ordinary registration and cache rules still apply then.

`buffer_owner()` and `deferred_snapshot()` provide read-only observations of the
actual shared owners. Snapshot records include inactive slots in physical queue
order. The arguments remain live objects, not copied lists. A raw buffer reference
is valid only while its owner survives. Buffer assignment and destructive move
are disabled; read-only value copies remain available.

## Integer time and pause

The exact scheduling arithmetic is:

```text
deadline = (context_time + delay * 16666666) modulo 2^64
```

The integer literal is `0x00FE502A`. It differs from the floating animation frame
denominator. Deadline comparisons interpret the resulting words as signed int64;
the arithmetic preserves unsigned wrapping. The API requires the original
context time explicitly and does not substitute wall time, SDL delta, a model
wait timer or an ACTION animation clock.

`set_deferred_pause(raw, time)` follows `80304FA8`: the low byte of `raw` selects
whether to copy time into the pause anchor or write zero. Zero is the only
unpaused sentinel. Requesting pause at time zero leaves normal delivery enabled.
Setting pause before lazy initialization retains the anchor.

A paused update adds `(now - anchor) modulo 2^64` to each active record up to
the first inactive slot, writes anchor=now, delivers nothing and clears CURRENT.
Backward times and wrapping shifts are preserved. Reaching time zero clears the
sentinel naturally. A pause request inside a delivery callback does not change
the branch already selected for that update.

## Update and service continuation

`begin_deferred_update(time)` captures one original `80304C58` manager invocation:
initial total pool count, readiness time and paused/ordinary branch. An
uninitialized context returns completed without allocating or clearing buffers.
`resume(update, services)` performs the retained invocation:

1. Read the live head; stop at an inactive record or a signed future deadline.
2. Select its category/name/index, clearing the then-current packet.
3. Swap CURRENT with the retained head's argument owner and send its command
   through the ordinary shared-cache delivery path.
4. After the send completes, reset the retained record, clear its own replacement
   packet, remove the live head and append that inactive record to the tail.
5. Continue up to the initially captured count, then clear the actual CURRENT
   buffer. Neither earlier selection nor earlier packet identity is restored.

Callbacks may enqueue work or explicitly push/pop buffers. A callback's zero-delay
enqueue can run later in the same update when the initial budget permits. Pool
growth does not enlarge that budget. Enqueue uses its supplied live context time;
the current update's readiness time does not change.

The original sender is synchronous. Native service suspension is an explicit
extension that retains the exact record, dispatch/cache phase, clock and budget.
It does not recycle the message or clear/restore buffers while a provider is
missing. The provider owns any partially committed internal effects. Completed
recipients and completed messages are never replayed; uncertain throws fault the
invocation. Abandoning its handle leaves that unfinished invocation retained by
the context until reset.

`ActionEventRuntime::begin_deferred_update` reserves the top-level invocation
immediately. Its `resume` overload uses the existing actual controller providers
and permits their nested ordinary sends. Unrelated top-level work rejects while
the update owns the runtime. Missing providers can still be registered, and
read-only packet inspection remains available. Packet mutation guards also
cover mutable references saved before suspension, including parked and queued
owners. Reset detaches old owners from future generations' guards.

## Checked limits and remaining boundaries

The native pool can grow to 65,536 records; exceeding that explicit host bound
rejects rather than dropping a message. Existing per-packet and string bounds
remain documented in [MESSENGER.md](MESSENGER.md).

The checked API requires a prepared inactive live tail at commit. Original
unchecked commits without preparation are not made into useful native behavior.
A drain between preparation and commit may change which record is the tail;
the native API does not reserve the earlier prepared record across that drain.

Recursive manager updates and callback insertion before the currently delivered
head reject. The original routines can corrupt their live-head removal order in
those cases; native code does not silently replace it with removal by identity.
Ordinary callback enqueue, equal-time ordering and arithmetic wrap outside that
unsafe insertion remain represented.

Full cleanup `803040C0` releases current, registration/cache, deferred-record and
stack owners, clears depth/pause/preparation and invalidates handles. It differs
from recycling `80304A04`, which clears a record's fields and own packet but
retains that packet for reuse. A separately retained packet survives full
cleanup with its contents. Cleanup inside a callback rejects.

The context clock writer `8028AF84` and manager batch `8028B7B0` are identified,
but their complete loaded ownership and invocation order are not established by
this module. A prepared queue test is not an original startup or gameplay frame.
The auxiliary original buffer string at +0C remains outside the represented
typed-packet profile. See [MODEL_PLAYER_START.md](MODEL_PLAYER_START.md) for the
bounded caller and [VALIDATION.md](VALIDATION.md) for measured checks.
