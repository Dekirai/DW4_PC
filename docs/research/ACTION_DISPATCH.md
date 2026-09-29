# ACTION messenger execution

`make_action_dispatch_manager` connects a source-qualified, actor-aware
`ActionRuntime` to actual native messenger delivery. It retains the binding,
runtime, animation source, ACTION services and optional scene-query owner.
The manager backlink is weak, so storing the adapter in that manager does not
create a manager-to-adapter-to-manager ownership cycle.

The existing ACTION query provider still implements ISCOMEND and the active
GETAPPEARANCELIST query. Query calls do not execute actions, sample clocks or
deliver pending events. Synchronous read-only queries remain valid during an
ACTION broadcast. Other original native queries keep their explicit service
boundaries and take precedence over same-named authored action definitions.

Named execution accepts actual definitions in the retained ACTION program.
NDSP and ONFREEZE/OFFREEZE use their existing supported runtime operations.
Native freeze handles its operation before ordinary pending-event draining.
Unknown names do not become successful playback. Unsupported original command
types, missing random services and incomplete broadcasts retain explicit stops.

One delivery retains its dispatch ID, registration row, recipient, captured
command, source/service owners and runtime state token. The adapter finishes
an earlier pending broadcast before accepting a new action. After acceptance,
it calls the runtime's resume endpoint without restarting completed commands.
An end or real action wait completes the messenger callback. A suspended host
service does not. Duplicate registration rows retain their original order;
the action runtime's physical selected-definition identity prevents a repeated
row from restarting the same action.

Ordinary execution reads the current manager context timestamp when accepting
the dispatch and retains it across host retries. NDSP and inactive freeze do
not need that clock. A missing manager/context suspends before ordinary action
acceptance. This bridges the existing explicit timestamp-taking ACTION profile;
it does not reproduce every original per-helper ACTION clock read, construct
the original ACTION work graph, or implement the outer context scheduler.
MODELCTRL source command sends remain responsible for their own captured
parent-name and live RAM-number read order.

Exceptions after partial execution fault the endpoint. A caught recursive
execution attempt still prevents outer completion. Reassignment or external
mutation of a suspended runtime cannot silently redirect its retained delivery.
Completed activation, commands and event prefixes remain observable after a
later failure.

`action_dispatch_tests` exercises actual messenger rows, query priority,
delayed context preparation, WAIT/random/unsupported stops, earlier and current
broadcast retries, context replacement, read-only nested queries, caught
recursive failure, state mutation and source/service lifetime. The adapter
reuses the established original ACTION handler evidence at 802F1794 and command
loop at 802F1510; no original bytes or disassembly are distributed.
