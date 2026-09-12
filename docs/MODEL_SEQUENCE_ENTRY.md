# Actual MODELCTRL sequence entry and player callback

Version 0.41 connects ordinary MODELCTRL delivery to the canonical loaded SEQ
graph, a retained executor for each work generation and the player `+B8`
callback. `ModelBaseInvocation` calls `ModelSequenceEntry`, waits for a real
command-loop return, then calls `ModelPlayerSequenceInvocation`. Host service
suspension retains those invocations and does not count as the original return.
The original outer frame scheduler remains required.

## Entry and execution ownership

The base call captures RAM, parent and work before player `+C4`. Ordinary entry
uses that captured parent's current data, first named SEQ and requested sequence.
It publishes the nullable name and selected command-data owner before label
resolution, preserving those stores if a later consumed field is invalid.
The inactive ordinary-command gate returns before selection or `+B8`.

`ActionEventRuntime` retains the current execution bundle in its existing model
registry. An invocation retains its exact work, executor and frame through work
replacement. The bundle binds work fields without eagerly reading ONE.node;
motion commands resolve an actual node only at their own consuming sites.
RAM timers use the RAM's current manager and current context timestamp on each
original helper read. The executor does not store a frame timestamp in work.

Entry captures the actual work+E4 subcommand wrapper for cleanup. Replacing E4
during a paused cleanup does not redirect the retained pass. On cleanup return,
the original check/hit/invulnerability/movement resets complete before C3AC.
The runner retains command data and a local cursor while reading the selected
data's live command list. [MODEL_SEQUENCE.md](MODEL_SEQUENCE.md) describes
publication, physical backing, raw command fields and nested JUMP semantics.

ACTION sends use the captured parent's current name, current RAM number and
reached command name, with the original null-to-empty fallbacks. They use the
native messenger without a packet push. The actor-aware adapter executes actual
authored definitions and resumes pending broadcasts; see
[ACTION_DISPATCH.md](ACTION_DISPATCH.md). A missing provider is a service stop.
JUMP calls the real base controller with the same RAM and packet. Completed
JUMP retains the child's stores and suppresses the old runner's cursor writeback.

## Player callback

After every real ordinary command-loop return, including LABEL, WCHECK, end and
JUMP, the base caller invokes player `+B8` at `8034E1B0`. This callback freshly
captures parent, ONE and work; it may see a different generation from the earlier
base call. SQ_WAIT sends real SETEFFECT to that player with model `i_pcnumic`,
sequence `SQ_OPENP` plus the live RAM number, delay 480, offset `(0,10,0)` and
both flags enabled. These are original cached-frame units.

The panel gate uses the original seven-name dictionary's match status. It clears
the panel flag before sending REMOVE to NDMWPANEL. Selection fields and completed
send prefixes remain retained through missing services.

The reconciliation helper at `803AE1A4` independently resolves ONE/work again.
A null display list skips the helper. Its represented death path processes the
twelve status-icon slots in order, reading the current list at each iteration,
then separately clears current timer and display lists. SLEEP/STUN handling
removes the first named RAM record without reading a clock. SQ_WAIT with active
STUN performs a retained real direct SQ_STUN call. Checked vector-backed status
storage remains distinct from arbitrary guest list backing/capacity behavior.

## Evidence and limits

Source evidence is the supplied USA DOL, SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`: ordinary entry
`8030E944..8030EAA0`, command loop `8030C3AC`, ACTION `8030943C`, JUMP
`8030BCB8`, player callback `8034E1B0` and reconciliation `803AE1A4`.
Original disassembly and data remain outside the tracked repository.

Synthetic tests cover owner replacement, paused ACTION and cleanup, both player
callbacks after same-work JUMP, exact player-number effect payloads, dictionary
matches, status removal and committed checked-failure prefixes. Original local
integration is reported separately in [VALIDATION.md](VALIDATION.md).

The checked execution lease currently rejects sharing one work allocation across
different RAM executors and ACTION/Lua-triggered same-work entry. The supported
nested JUMP path uses distinct invocation frames. Manager attachment and actual
time access do not implement original scheduling. The ACTION adapter retains
its accepted timestamp rather than reproducing every original ACTION helper's
clock read. Active erase commands, remaining native services, complete original
input, camera, collision, combat and local multiplayer still require integration.
The visible walking preview continues to use its provisional host controller.
