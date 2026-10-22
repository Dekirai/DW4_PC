# Original game baseline

The owner requests the complete USA GameCube game as a native PC version, with
local multiplayer using separately connected controllers. Original behavior
comes first; quality-of-life changes and optional enhancements follow it.

## Systems and current state

| System | Required for the baseline | State in `dw4_engine` |
| --- | --- | --- |
| Startup and menus | Logos, title, Game Start / Option, new game, loading, transitions | Logos, opening movie, title, Game Start / Option, option window, name entry, difficulty dialog, partner choice, calling saved characters, loading screens, stage loader and soft reset run as in the original |
| Player lifecycle | Creation, initial state, equipment, spawn, death, revival, map transitions | New and saved characters, stats and equipment, spawn points, death with bit capsules, raise and revival, the game-over return to the lobby and gate stage changes work |
| Input and local multiplayer | Original pad semantics for 1–4 slots, device assignment, pause/menu ownership | Original `bePadData` masks for four slots; player 1 uses the keyboard and the first controller, players 2–4 controllers 2–4 (hot-unplug releases a slot); main stick, D-pad and C-stick are separate inputs as on the GameCube (the quick panels' cursor takes the D-pad / C-stick, walking the main stick); bindings are editable in the F1 menu |
| Animation | Clips, blending, events, weapons, effects | Skinned actors with Enbaya clips, ACTION2 motions and MOTIONSET states, weapons, particle effects and transform sequences (demo cameras, emblems), compared frame by frame against the original in Dolphin |
| Movement and collision | Original movement, slopes, walls, jumps, platforms, collision responses | Recovered collision queries and responses: walking, slopes, walls, jumps, ledge drops, moving platforms, boats and trains, conveyors, quicksand and slippery floors |
| World and scripts | Maps, triggers, doors, NPCs, cutscenes | Stage Lua runs in the own Lua 4 VM: triggers, switches, doors, NPC talk, dialogs, demos with their cameras, box cameras and camera shakes |
| Combat and enemies | Attacks, damage, AI, bosses, rewards | Weapon attack chains, FIGHTCALC damage, MP techniques, status effects, the original AIM enemy AI, enemy waves and generators, all six bosses with death demos, reward boxes and the staff roll |
| Progression and inventory | Levels, items, weapons, skills, shops, quests | EXP and levels, skill growth, item drops, quick panels and the status window, the lobby shops, bank, appraiser, X data keeper, Digi-Lab digivolution, card keeper and quest gates |
| Audio | Music, sound effects, streaming | ADX music and ambience streams and the sound effects play |
| Persistence | Save/load and slot ownership | Memory card saves in the save room as GCI files in a portable card folder (the layout of Dolphin's GCI folders), loaded at the character select; save states as a PC option |
| Complete playthrough | Reference runs through the whole game | Every stage exit is checked by an automated survey with an exploring bot (216 exits from the stages' mat scripts; all 216 reached in one run; the bot wins the stag0203 train ride in about seven of eight tries), all boss stages are completed in automated runs; one uninterrupted automated run from a new game to the ending is recorded (the campaign driver, `0 SEEK duration campaign`, with the test cheats `DW4_GOD`, `DW4_ONE_HIT` and `DW4_INFINITE_MP`): all six bosses, the staff roll from frame 1,687,374 and the cleared game (flag 86) back in the lobby at 1,696,547 (132 stage changes with the original's stage load times, about 7.9 hours of game time), every story flag, key item, area choice, door and boss gate set by the game's own scripts. Without the test cheats the whole story is played as well: the driver plays like a player (guard, HP disks, shopping, training below gate levels, restock trips, equipment and PWR medals, boss routines, the bank) and saves the game like a player at nine story points (the no-cheat checkpoint set nc1); started from those memory card saves its ten segments reach every next checkpoint and the last one the staff roll and the cleared game (flag 86, level 40): all six bosses including Daemon's three forms, 7,393,714 frames in all (about 34 hours of game time), no port bug found on the way |

## Evidence discipline

Use the supplied USA revision-0 extraction and the matching `main.dol`
checksum. Record source addresses for reconstructed behavior. Original frames,
input traces and generated disassembly stay in ignored local directories; only
independent source is published. Keep observed original behavior and
provisional host behavior separate, and verify changes in the running game.
