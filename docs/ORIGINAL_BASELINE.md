# Original game baseline

The owner requests the complete USA GameCube game as a native PC version, with
local multiplayer using separately connected controllers. Original behavior
comes first; quality-of-life changes and optional enhancements follow it.

## Systems and current state

| System | Required for the baseline | State in `dw4_engine` |
| --- | --- | --- |
| Startup and menus | Logos, title, Game Start / Option, new game, loading, transitions | Logos, title menu, option window (without text), character select panels and stage loader run; name entry, difficulty and memory card dialogs remain |
| Player lifecycle | Creation, initial state, equipment, spawn, death, revival, map transitions | Partner choice reaches `gameStart.lua` and spawn points; stats, equipment, death and revival remain |
| Input and local multiplayer | Original pad semantics for 1–4 slots, device assignment, pause/menu ownership | Original `bePadData` masks for four slots; only controller 1 is mapped by the host so far |
| Animation | Clips, blending, events, weapons, effects | Skinned actors with Enbaya clips and ACTION2 motions; effects and weapons remain |
| Movement and collision | Original movement, slopes, walls, jumps, platforms, collision responses | Recovered motion channels, walking and jumping; provisional floor/wall query on the original geometry |
| World and scripts | Maps, triggers, doors, NPCs, cutscenes | Stage scripts, demo manager, dialogs, NPC talk and floor gates run; demo cameras and moving platforms remain |
| Combat and enemies | Attacks, damage, AI, bosses, rewards | Talk hitboxes and hit scripts work; attacks, damage and enemies remain |
| Progression and inventory | Levels, items, weapons, skills, shops, quests | Game flags and counters; the rest remains |
| Audio | Music, sound effects, streaming | Requests are recorded, nothing is played yet |
| Persistence | Save/load and slot ownership | Not implemented |
| Complete playthrough | Reference runs through the whole game | Not established |

## Evidence discipline

Use the supplied USA revision-0 extraction and the matching `main.dol`
checksum. Record source addresses for reconstructed behavior. Original frames,
input traces and generated disassembly stay in ignored local directories; only
independent source is published. Keep observed original behavior and
provisional host behavior separate, and verify changes in the running game.
