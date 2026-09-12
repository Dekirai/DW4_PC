# Prepared original player status

`model_status` implements the original player's `SETMETAL` message and its
`DOWNDATA` binding helper on explicitly prepared, shared model work. It also
preserves the status handler's command classification and initialization
prefix. The later status update loop, combat effects, full work construction
and remaining status commands are still required.

## Original dispatch order

The supplied USA GameCube executable defines the player vtable at `804E7DAC`.
Its ordinary message entry at `+8C` is `8030D910`; its status override at
`+C4` is `803AEBB4`. The ordinary entry finds `ONE`, calls the status override
before its generic command switch, and returns immediately when the override
returns low byte one (`8030D950..D994`). This status call has no active-RAM
byte gate. The player type's vtable is also selected by its type probe at
`80338C4C..C68`.

The status override captures the actual base work from `ONE+14`, then uses
the sixteen-command dictionary at `804E28E0` / `804E2920`. An unknown command
returns zero **before** status-list initialization (`803AECAC..ECD8`). Thus
ordinary commands such as `GETHP` do not allocate status lists. `CHECKSTATUS`
and `CLEARSTATUS` also skip the common initialization prefix. Other recognized
commands enter it before their individual command handlers.

`ModelStatusInvocation` retains one such invocation. Its owner holds the
actual shared `ModelScriptWork`, explicit `_isPlayer` byte, live RAM-number
field and nullable status lists. The event dispatcher must retain one
invocation per message delivery; a separate nested delivery receives its own
invocation even when it targets the same work. Shared field changes made by
nested callbacks remain visible.

`ActionEventRuntime::prepare_model_status` attaches these exact fields to the
same validated script-work owner and live query fields. The model stores a weak
link; its external owner retains the status fields. The message resolver creates
a separate status continuation for each recipient invocation and caches its
completed return value while releasing the finished work capture. Runtime
teardown clears model-owned query, broadcast and HITBOX continuations before
releasing model owners. This breaks a paused self-recipient's cycle through its
status work and live matrix without weakening captures needed during execution.
Synthetic integration covers nested same-model SETMETAL and abandonment of
both declined queries and genuinely paused status broadcasts.

Unknown dictionary names return handled zero. Recognized commands other than
`SETMETAL` preserve their applicable initialization prefix and report a missing
native service. They are not silently treated as successful or passed through
as unknown commands.

## Shared work and initialization

The native fields follow `beNDMWMdlPEBaseInfoWork` metadata at `804E2998` /
`804E29E8`:

| Guest offset | Native field | Original name |
| --- | --- | --- |
| `EC` | `is_player` | `_isPlayer` |
| `F0` | `display_ram_numbers` | `_dispRamNoList` |
| `F4` | `display_positions` | `_dispPosNoList` |
| `F8` | `timers` | `_timerList` |
| `FC` | `first_flags` | `_isFirstList` |
| `108` | `down_data` | `_downDataList` |
| `10C` | `up_data` | `_upDataList` |

Each list retains its shared identity. A null field and an existing empty list
are different states. The original initialization gate tests only whether
`display_ram_numbers` is null. When it is null, the invocation publishes new
lists in original order: twelve display RAM numbers filled with `-1`, twelve
display positions filled with `-1`, twelve timers filled with zero, and eleven
first flags filled with zero (`803AECE4..AF3C8`). Other preexisting list owners
are replaced independently. Existing aliases continue to own their old lists.
An existing empty display-RAM list suppresses this initialization.

The explicit `_isPlayer` value must come from actual prepared work. The earlier
player startup writes one at `8034D780`; a model name is not evidence for this
field. Status preparation does not run or claim the complete original work
constructor.

## SETMETAL and DOWNDATA

`SETMETAL` is dictionary index ten and reaches `803AFBD0..AFC44`. A null actual
base work returns handled one. Otherwise, the handler writes first-flag slot
seven to byte one when that list has more than seven entries, then timer slot
seven to integer one under the same independent length condition. It does
not grow short lists, clear other slots, clamp a value or consume arguments.
A null list at these original dereferences is an explicit checked failure;
the earlier flag write remains committed if the later timer read fails.

An existing non-null `down_data` skips the helper. When it is null, helper
`803AE5F8` uses the then-current `_isPlayer` byte:

- Zero allocates a new list of six zero integers and publishes that owner.
- Nonzero pushes the current messenger packet, selects category `GAMERAM`
  with null name and index `-1`, appends the then-live RAM number, and sends
  `GETPLAYER`. Once that actual delivery finishes, it reads the current
  object zero as a data row, finds the first exact `DOWNDATA` wrapper, assigns
  its nullable integer-list payload, and pops the child packet.

The returned row is retained separately from the row captured earlier by
player startup. A nested game-data reset does not retarget either capture.
The helper permits a null `DOWNDATA` payload. It overwrites a value installed
by a nested callback with the actual returned payload, including null.
Missing or wrongly typed row/wrapper results remain explicit checked failures.

The same child dispatch and packet remain owned across a missing-service
interruption. Resuming does not repeat the status stores, re-read the original
query RAM, change the selected player/non-player helper branch, or overwrite
the parent packet. A missing GAMERAM registration is an explicit preparation
boundary after the status stores and before pushing the query frame.
Exceptions fault the invocation and preserve its committed prefix; they do
not automatically pop the child frame or replay uncertain callbacks.

These effects prepare metal status. They do not establish the later combat
behavior, appearance changes or timed status processing.

## Validation and limits

The asset-free suite contains 77 checks covering command gates, original list
sizes and defaults, null and short lists, shared aliases, old-row retention,
nullable payloads, interrupted nested requests, a separate nested invocation
on the same work, packet restoration and committed-prefix failures.

A local independent comparison executes the supplied original instructions
from `803AECAC`, the allocation loops, `SETMETAL`, and helper `803AE5F8`.
594 cases execute 90,899 original instructions and 312 prepared GETPLAYER
requests with zero differences. The same 594 cases also pass when native
recipients pause after their first effects and later resume. Original
synchronous instructions execute once per case; this does not claim an
original asynchronous scheduler.

Allocation, reference ownership, context lookup, typed row lookup and actual
recipient delivery are explicit services in that comparison. Unsupported
recognized status commands stop at their original branch entry. Original
status-update execution and physical-console comparison are outside its scope.
The supplied executable is identified by SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`. Original bytes and disassembly are
kept outside tracked source.
