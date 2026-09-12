# Prepared player spawn integration

The `player_spawn_tests` integration connects the real
[PLAYERPOINT provider](PLAYER_POINT.md) to `ModelPlacementInvocation` and the
same mutable node matrix shared by retained and newly prepared model work.
The test starts with the node's actual serialized matrix, queries the original
stage point through the messenger, and publishes the returned matrix through
the verified `beModelCtrlNode2` setter. It does not replace the starting matrix
with an invented identity or multiply the spawn point by the editor layout.

This is an explicit prepared integration. It does not implement the original
player-work factory, install fresh work into ONE, run the outer MODELCTRL
registration/GENERATE lifecycle, or establish native gameplay. Selecting LOD
branch 1 for graph verification is an inspection choice, not an original LOD
decision. Existing work and node storage are supplied to the integration at a
documented boundary.

## Actual ownership and message path

`PlayerRuntimeBinding` verifies the loaded MODELCTRL/ACTION agreement for the
selected player RAM, its ONE record, concrete model node and actor graph. The
integration retains that exact `ActorSceneNode` from the shared source resource.
Its original slot-8 `igMatrix44fMetaField` initializes the prepared mutable
node+`0x20` storage, preserving every finite binary32 word.

Two independently allocated `ModelMotionPose` owners represent retained old
work and newly prepared work. Both receive the same `shared_ptr` to that node
matrix. Their turn state and position-lock fields remain separate; applying
the original player turn rates `(0,7,0)` to the fresh owner cannot reset the
retained old owner. Matrix publication is therefore visible to both work
owners without conflating the rest of their state.

The test's query helper follows the original player callback's argument-buffer
exchange at `8034D8DC..8034D950`:

1. Retain the caller's actual current buffer and push a child buffer.
2. Select `PLAYERPOINT`, null name, index `-1`.
3. Append integer 0, then the selected live player RAM ordinal.
4. Send `GETMATRIX` to the registered original point manager and copy matrix 0
   from its actual response buffer.
5. Pop back to the same caller buffer and append that matrix. The caller's
   retained sequence string remains intact.
6. Resume `ModelPlacementInvocation` on that packet. The matrix-only branch
   resolves the actual node once and publishes through
   `make_model_ctrl_node2_placement`, representing virtual `+0x94` /
   `801166B8` with the original paired-single copy behavior.

The helper explicitly requires a response from a real registered recipient.
It is an integration assertion, not an implementation of every construction
branch. The original caller's separate no-response identity case, supplied
vector/matrix bypass, asynchronous service lifecycle and surrounding GENERATE
effects are not inferred from this helper.

The immutable loaded IGB document remains unchanged. The prepared mutable
node-matrix owner is the represented runtime state. A copied motion candidate
is detached until assignment publishes its values into that existing owner;
assignment must not exchange the node identity. A separate player's node and
work remain unaffected.

## Original-resource acceptance

Local integration reads the supplied USA resources directly from `ndmw.afs`:

| Resource | Archive entry | Verified graph |
| --- | ---: | --- |
| dng101b1 package member `[0.179]` | 179 | `bePoint01Info` 115, two groups, four points each |
| `_common/p_agumon.igb` | 850 | MODELCTRL info 1386, ACTION info 546, four player RAMs |

The model RAMs are `1725`, `6417`, `6420`, `6622`; their concrete model nodes are
`6390`, `6457`, `6524`, `6590`. Each selected ONE and node must retain distinct
ownership. Initial matrices come from those actual nodes; each is checked to
differ from the requested point matrix before placement.

Both saved groups are exercised for all four RAMs. The result is compared word
for word with each independently decoded original point transform, including
its full orientation and translation. An excessively large positive saved
group repeats the group-0 placement for all four players while preserving the
original saved manager word. No map normalization, host movement or provisional
camera coordinate conversion participates in the comparison.

See [original placement research](PLAYER_PLACEMENT_RESEARCH.md) for the eight
authored transforms and source provenance. Original bytes and generated
disassembly remain outside the tracked repository.

## Asset-free checks and limits

Synthetic tests use a generated combined MODELCTRL/ACTION/point graph with two
independent RAM/ONE/node chains. They check group selection, shared node
publication, retained caller buffers, old/new work separation, local positive
fallback, unchanged source documents and independent players.

A missing concrete setter must pause before modifying the node. After that
pause, changing the point manager's matrix and replacing the current message
packet cannot change the captured response or target of the suspended
placement invocation. Supplying the actual setter completes that same
invocation; a later query sees the point owner's newly published matrix.
Invalid negative-group access faults its query and leaves the player matrix
unchanged. A completed invocation cannot publish again.

The integrated Release build passes 233 synthetic checks and 672 checks when
the supplied original resources are included. The full integrated suite passes
85 CTests; the original-resource checks remain local and separate from CI.

These results verify the represented owner/message/placement connection.
They do not claim completed player construction, game progression, collision,
animation scheduling, controller ownership, rendering parity or an original
game frame. Those remain separate integration requirements.
