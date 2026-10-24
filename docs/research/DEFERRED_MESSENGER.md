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

## Script sends, demo records and pauses (engine `Messenger`)

The Lua method `sd` has four overloads (method table `0x80424428..0x8042447C`,
cases 29..32 of the dispatcher `802EA1FC`). The overload resolver `8026B2E8`
matches by argument count and type masks (`802699FC`): a string slot takes a
string or nil, a number slot only a number.

| Case | Form | Handler | Effect |
| --- | --- | --- | --- |
| 29 | `sd(cat, name, idx, cmd, args)` | `802ECD20` | `80305344` + `80305A28`: sent at once |
| 30 | `sd(cat, name, idx, cmd)` | `802ECDA0` | `80305ED0`: sent at once |
| 31 | `sd(cat, name, idx, cmd, args, delay)` | `802ECDE0` | `80305F9C` + `802ECA74` + `803061BC`: posted |
| 32 | `sd(cat, name, idx, cmd, delay)` | `802ECE80` | `8030650C` (`80305F9C` + `803061BC`): posted |

`803061BC` files the message for `clock + delay * 16666666` without a case for
0, so `be:sd(..., 0)` arrives at the next delivery pass `80304C58`, which runs
in the following frame before the demo manager and the managers, never inside
the script that sent it (hit_n_agumon.lua's player `SQ_WAIT`, the recorders'
`SQ_WAIT`, bck_e_skulgrey.lua's `SQ_ATMAP`, the dng412b3 track mats'
`SQ_R*MOVE*`). None of the four returns a value (`0x802EA7F8..0x802EA860`).
About 220 decompiled scripts use the delay-0 form. A demo record (`DMsetSD`
with a delay, `802F7580`) is different: `802F7EC0` posts it only for a delay
above 0 (signed 64-bit test at `0x802F7F54..0x802F7F78`) and sends it at once
otherwise.

A delivery pass delivers the due head again and again, at most as many times
as the pool had records when the pass began (32 from `80303C0C`, one more per
preparation while every record is in use), so a delay-0 message that a
delivery posts usually still arrives in the same pass.

`80304FA8` pauses the queue (+0x28 = the world clock; 0 = running). While
paused, each pass moves every posted time on by the clock's advance and
delivers nothing. Callers: SUBWND GENERATE (`0x8034FC30`, between the
GENERATER and SOUND `ONFREEZE` broadcasts), the program's end (`0x8034F950`,
after the `OFFREEZE` broadcasts), SUBWND REMOVE before the program window
exists (`0x8034FC88`: unpaused, the REMOVE is posted again with delay 1), the
soft reset (`0x80347EB8`, after the MODELCTRL / GENERATER `ONFREEZE`) and the
disc error freeze (`80316974`, not modelled). The STAGECTL `NEXTSTAGE` handler
(`80347F2C`) hands the package `stagXXXX.igb` to the application loop
(`80021018`), whose module reload (`80021944` -> `8040F570` -> `8028A87C`)
calls every module's reset hook (vtable +0x70); the messenger's (`80304C38` ->
`803040C0`) drops the queue, the record pool and the pause, as
`Messenger::clear` at the stage teardown does.

Dolphin (round 33b/34): dngq5b1e talk, dng204b2 SkullGreymon attack cycles,
stag0301 recorder talk and the dng412b3 Trailmon ride match frame for frame
with these rules; the immediate delay-0 send was one frame early.

## Message objects and the unchecked bool (engine `MessageObjects`)

Every module reaches the messenger through its +0x14 (one beMessenger,
808413E0 in Dolphin). Its message objects hold the typed lists a send fills
and a dispatch answers into: the current one at +0x10 and one in each
deferred record at +0x28. `80303C0C` makes them at the first select or post of
a stage (made flag +0x30): the current object and 32 records with their own.
A select (`80305344`) and a post's preparation (`80305F9C`) reset the current
object's counts through `80304618`, the sender's arguments go in (`80306964`
for a bool), and the dispatch (`80305A28`) appends the answers; SEARCH answers
its found bool itself (`0x80305AE0..0x80305C40`). A reset only zeroes the
counts and the shrink to four entries keeps the data (`8004155C`), so a list's
first entry stays what the last bool written at index 0 left.

The objects change identity:

- A post's commit (`803061BC`, `0x803062A0..0x8030632C`) swaps the current
  object with the record's: the record takes the filled object, the record's
  old object becomes current. A new record (`802CF068`) brings a new object.
- A delivery (`80304C58`) selects (`0x80304D78`), swaps the record's object in
  for the dispatch (`0x80304DCC..0x80304E58`) and never swaps back: the record
  (`80304A04` resets its counts) goes to the end of the record list with the
  previous current object, and the next post takes the last record.
- Every pass of a made messenger ends with `80304618` on the current object
  (`0x80304F8C`), a paused pass too.
- The stage change's `803040C0` releases them all.

`80306A1C` reads a bool without the count check `8030690C` where a caller skips
it. The soft reset does (`80347BB4`: SUBWND ISRUN, then `0x80347DA4` /
`0x80347E54`): in a stage without a status window nothing answers, and the
read gets the first entry of whatever object is current - often a bool another
message left. `Messenger::stale_bool()` models exactly that: `MessageObjects`
keeps the identities, the swaps, the bool counts and each object's first
entry (0 for an object before its first bool, as every fresh object in the
Dolphin logs below holds). The port's own sends - the campaign bot's SUBWND ISRUN and
PLAYERPOINT queries and the input scripts' SEND lines - run inside a
`Messenger::PortOnly` scope: they touch no object, and their posts hold no
record (neither counted when a post decides to grow the pool nor spending a
pass's deliveries). The stage loader sends SOUND SEARCH / MAKEBINLIST /
ISDONETRANS (states 100 / 101, `0x802F9518..0x802F957C`) and DMgetCount falls
back to SUBWND ISRUN when no demo work runs (`802ED4B0`, -1 for a running
status window), both through the messenger like the original.

Dolphin (round 42, hidden soft resets; hooks M `0x80303C18`, S `0x803062E4`,
D `0x80304E10`, Z `0x80304F8C`, I / J `0x80347DA8` / `0x80347E58`): in stag0001
(read 0: back to the title), stag0101 (0), stag0203 (1: a model's ACTION
ISCOMEND, the reset hides everything and waits for good), dng101b1 (0) and
the lobby stagML11 (0) every swap of the run - boot, title, character
select, stage - comes in the same order on the same objects, and the reset
reads the same value. The objects' first entries at the pass ends agree
except where a single send comes in another frame (the title flow, the stage
start's scripts, the stage load's length): the model, not those timings, is
what this checks.
