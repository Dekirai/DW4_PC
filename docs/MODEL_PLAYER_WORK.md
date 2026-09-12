# Canonical player work

`ModelPlayerWork` is one retained, noncopyable and nonmovable allocation for the
68 reflected fields of `beNDMWMdlPlayerInfoWork`. Existing consumers bind to its
actual field groups. Work owns values and referenced objects; it never owns a
facade that retains itself. A pending invocation can therefore retain its exact
captured generation without creating a reference cycle.

## Recovered default profile

The supplied USA DOL has SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
The built-in factory at `80338F60` calls the allocation path `8006546C` and field
initializer `800667F4`. Reference clearing, auto-owned fields and defaults use
`80066F40`, `80066FF8` and `8006712C`. Player metadata describes size `0x130`.
The native function implements that normal field/default result; it does not
emulate the original allocator, runtime type registry or object headers.

| Field group | Guest offsets | Reflected fields |
| --- | --- | ---: |
| Script | `08`, `0C`, `10`, `125` | 4 |
| Sequence/wait | `14`–`2C`, `44`, `4C`, `128`, `12C` | 11 |
| Control | `30`–`40`, `48`, `7C`, `C8`, `D8`–`DA` | 11 |
| Motion | `78`, `80`, `8C`, `98`, `A8` | 5 |
| Status | `EC`–`10C` | 9 |
| Remaining player/base fields | Individually annotated in the public header | 28 |

Fresh work creates distinct zero HP and MHP wrappers, an empty pad float list,
an empty movement list, and a subcommand wrapper containing its own empty list.
These six guest object allocations are distinct across factory calls. Host
storage for field groups is not an additional guest object. Label lists, AI,
scene references, status lists and the turn-attack timer list start null.
Record and saved LOD range start at -1. Control, wall, floor and fall bytes
start at 1; all other scalar/vector fields start at zero, including main-lobby,
isPlayer and turn rates. Original floating zero defaults preserve positive zero.

The later player callback sets isPlayer, turn rates `(0,7,0)` and its timer list.
The outer GENERATE HP/MHP fallback to 100 is also later. Neither effect belongs
to the factory result and neither is silently applied here.

## Bound consumers and ownership

`bind_model_player_control`, `bind_model_player_sequence` and
`bind_model_player_script` create retaining views. Script work also captures
the corresponding status field group, so its ordinary status facade resolves
the same generation. Const facade accessors do not expose mutable wrapper/list
objects. Motion and PDMOVE operations resolve actual list slots; changing a
reference field does not redirect an explicitly retained previous list owner.
Sequence name, command-data owner, signed cursor/count, labels and wait state
have canonical storage. Command data retains and validates its exact source
program; a numeric sequence index is not its authority.

A field generation has one sequence executor. A non-owning host lease prevents
a second executor from bypassing a retained command prefix. Moving the executor
keeps its field and wait addresses; detached snapshots use separate fields.
This lease is host execution metadata, not an additional reflected guest field.
Subcommand facades resolve the live wrapper for new operations and retain the
captured wrapper for a pending pass. Replacing the live slot never redirects
that pending continuation or silently cleans up its external handles.
Sequence entry checks that cleanup can begin before publishing its selection
fields, so a rejected overlap cannot later resume an old update as cleanup.
Effect attachment reads the captured work's live parent fields after FIND_SPACE
and retains the resulting GENERATE payload across retries. Its resolver uses
a weak work reference to avoid a work/wrapper/context ownership cycle. Original
fresh ONE lookup at that phase still belongs to the remaining mutable graph.

Node matrices and the RAM's named KEIKATIMER are separate owners. Fresh work
may bind to the same node and timer while older invocations retain previous
work fields. Prepared snapshots detach numeric/list state; publishing one
updates the destination's existing owners. Publication is not work replacement.

The checked `ActionEventRuntime::register_model` overload accepts actual work,
node and timer owners. Its sequence, control, script, status and subcommand
consumers use that work. The legacy overload preserves explicit prepared
defaults (unknown lobby byte and control-disabled wait) and supplied matrix/
list/timer owners. Prepared script/status service lifetime remains explicit.

## Remaining lifecycle

The mutable source/info/RAM/data-list/ONE graph, original allocation service
effects and full GENERATE continuation are not implemented by this module.
No public runtime replacement method pretends to cover the original pre-C4,
player +98 or outer GENERATE captures. Full integration must replace ONE.work
while retaining each invocation's previously captured work and facade. It
must also preserve later registration, HP fallback, box handling, AI, sequence
and player-entry effects in their original order. Non-null unsupported scene
objects remain explicit implementation boundaries, not fabricated successes.
