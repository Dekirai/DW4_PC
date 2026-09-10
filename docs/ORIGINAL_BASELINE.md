# Original game baseline

The owner requests the complete USA GameCube game as a native PC version, with
local multiplayer using separately connected controllers. Original behavior
comes first; quality-of-life changes and optional enhancements follow it.

The version 0.12 walking preview is a development aid. Its 120 Hz movement,
speed, body dimensions, follow camera and visibility silhouette are provisional
host behavior. They are not evidence of original gameplay compatibility.

## Completion requirements

| System | Evidence required for the baseline | Current gap |
| --- | --- | --- |
| Startup and menus | Original title/start flow, player selection, loading states, transitions and error paths execute through native state | Preliminary loading, Static/Switch callbacks and func/boot inspection exist; full manager preparation, sound gates, start/first and menus remain |
| Player lifecycle | Original character creation, initial state, equipment, spawn, death, revival and map transitions | Checked MODELCTRL/ACTION/node ownership, prepared sequence/event queues, actor lifetime/traversal and cached draws exist; full player construction, appearance traversal and state machine remain |
| Input and local multiplayer | Independent original button/stick semantics for 1–4 joined slots, device assignment, unplug/reconnect and pause/menu ownership | Portable original four-port pad state exists; device assignment and original multiplayer/player ownership remain |
| Timing | Measured original update/timer/input semantics and presentation independent of simulation | Original capped per-RAM timers and uncapped system frame deltas exist; scheduler and lifecycle integration remain; preview timing is provisional |
| Animation | State-driven original clips, blending, events, root motion, weapons, effects, damage/death and all playable character profiles | Original ACTION2 execution, cached skin deformation and prepared draws now connect to loaded Se/Starter consumers and owned sound/effect queues; full appearance/attachment integration, effect actors, player scheduling and additional profiles remain |
| Movement and collision | Recovered movement constants, slopes, walls, body/weapon/enemy interactions, transitions and original collision responses | Native motion, supported sequence/control effects and original floor/wall assets exist; complete actor/sequence callbacks and collision response remain; preview solver is provisional |
| Camera and rendering | Original camera states, shared multiplayer framing, culling, materials, transparency, shadows, effects and HUD | Static/skin rendering, captured actor root followed by separate projection, and host camera exist; original camera and complete GX/material/frame parity remain |
| Combat and enemies | Original targeting, attacks, timing windows, damage, knockback, enemy AI, bosses, rewards and difficulty behavior | Prepared attack wait/latch and live ACTION completion routing exist; hit detection, damage, full player entry, enemies and combat gameplay remain |
| Progression and inventory | Original levels, experience, stats, items, weapons, skills, shops, quests, unlocks and endgame | Not implemented |
| World and scripts | Original maps, triggers, doors, object controllers, NPCs, cutscenes and complete transitions | Named packages and selected scripts decode; runtime controller effects are incomplete |
| Audio | Original music, sound banks, playback, priorities, volume, streaming and scene changes | Original event keys, delayed sound ownership and volume-down cleanup requests exist; real sound manager, bank transfer, playback and streaming remain |
| Persistence | Correct save/load, slot ownership, inventory/progression round trips, multiplayer saves and interrupted-write behavior | Not implemented |
| Complete playthrough | Reproducible original-reference runs through the game with 1–4 players and no skipped required native effects | Not established |

## Evidence discipline

Use the supplied USA revision-0 extraction and matching original DOL checksum.
Record source addresses and serialized fields for reconstructed behavior.
Original frames, input traces, save states and generated disassembly remain in
ignored local research directories. Only independent source and synthetic
fixtures are published.

Keep observed original behavior, inferred hypotheses and provisional host
behavior separate. A parser test establishes only its checked format; a
successful scene render does not establish combat, collision or progression.
Unknown effects must stop the corresponding original path rather than become
successful no-ops. Reuse native state implementations in the eventual game
runtime instead of treating diagnostic reports as initialized engine objects.

## Current critical paths

1. Reconstruct manager preparation, per-info/per-RAM initialization and messenger
   state so deferred original resources acquire their actual native effects.
2. Recover player input, movement, timing and camera logic while exercising the
   original game as a behavioral reference.
3. Resolve audio and layer/text prerequisites, then execute original start/first
   phases and follow their player/menu/controller transitions.
4. Extend that runtime through combat, world progression, persistence and local
   multiplayer; compare representative original scenarios continuously.

The complete baseline remains open until every requirement above has direct
evidence. Individual milestones do not redefine completion.
