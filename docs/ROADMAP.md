# Roadmap and acceptance criteria

The selected implementation is a complete native source port: reconstruct the
original game and engine behavior in authored C++20 (`dw4_core`, `dw4_engine`,
`dw4_game`). The earlier DolRecomp/ModernGekko experiment was removed from the
source tree and does not establish features in the native engine.
A successful build, decoded record or settings control alone does not establish
playable behavior. The full original game, PC features, modding, online co-op and
simple ROM-driven release experience remain required.

| Gate | Deliverable | Acceptance | Current evidence |
| --- | --- | --- | --- |
| 1 | Complete original native game and source | Recovered game and engine source drives original startup, menus, gameplay, combat, progression, transitions, audio and saves; independent local 1–4-player controls; behavior compared against the original | The engine boots through logos, title, character select and `gameStart.lua` into the lobby areas with story scenes, dialogs, NPC talk, jumping and gate stage changes, then the Death Valley practice tutorial with enemy waves, weapon attacks, FIGHTCALC damage and lesson demos, and Death Valley combat with the original AIM enemy AI, enemy damage, defeat, EXP/levels and generator item drops with pickup, quick panels with MP techniques, the status window (equipment, folder and skill pages), and the lobby programs (weapon, armor, chip, disk and junk shops, bank, X data keeper, appraiser, Digi-Lab with digivolution into the evolution scene, card keeper with booster packs and album), status effects (poison, sleep, stun, bind, attack/power down with their icons), the support techniques with raise and the bit capsules of defeated players, skill growth, the opening movie, the loading screens, the ADX music and ambience, the sound effects, projected shadows, stage lights, particle effects, rippling water, homing bullets, memory card saves in the save room and calling saved characters at the character select. The original collision queries and responses are reconstructed, with rides on boats, trains and moving platforms and the conveyor, quicksand and slippery floors; all six boss stages are completed in automated runs (defeat, death demo, reward boxes, boss gates, staff roll after the final boss). Every stage exit is checked by an automated survey with an exploring test bot that plays a dungeon like a player (enemy waves started from floor mats, floor and wall switches, breakable walls, towers, keys, ledge drops, jumps over pits and long run-up jumps across pillars, quicksand rooms, radar-started waves and paired wall switches); objects run their AI maps as in the original, which made the floor switches of many dungeons work, and enemies a generator puts into the RAM of an erased intro enemy act again as in the original. All 216 stage exits found in the mat scripts are reached in one survey run (the bot rides moving platforms, crosses turret fire lanes and fights hovering enemies from posts), and an automated story run plays the whole game from a new game to the staff roll and the cleared game without flag presets (with the test cheats for invulnerability, one-hit defeats and MP; a run without them is not established yet). Scenes are compared frame by frame against the original running hidden in Dolphin (boot, title, character select, lobby, dungeons, quests, local two-player, boss fights up to the final boss, the ending and staff roll, the Digi-Lab, the transporter, a water field, shadows on slopes); the differences found were fixed in the animation, camera, rendering, timer, input, text, sound streaming (a model of the disc reads), manager scheduling and script command code. |
| 2 | Performance and PC graphics | At least 60 FPS on documented hardware; options up to 300 FPS without faster simulation/audio; validated frame times, 16:9 camera/culling/HUD, 4K, display modes, VSync and MSAA | Original assets, skinned actors and text render through the OpenGL runtime at a fixed 60 Hz simulation (600-1000 frames per second in hidden test runs). The F1 menu sets borderless fullscreen, widescreen, VSync, a present limit, MSAA and window sizes up to 2560x1440. Presents above about 75 per second blend model, skinning, camera and shadow matrices between the last two 60 Hz steps (one step of latency; warps and new items are drawn as they are). 3840x2160 renders with a readable HUD (about 400 frames per second in a hidden test window); the F1 menu plots the presented frame times (average and worst of the last 240). |
| 3 | ImGui settings and QoL | Working persistent graphics/input controls, freely editable keyboard bindings, optional cheats, save/load states, supported game languages and original/modernized options; applied settings and unsupported combinations visible | The F1 menu edits persistent settings while playing (settings.ini: borderless fullscreen, widescreen, VSync, frame limit, smooth motion, MSAA, window size, master volume, freely editable keyboard and controller bindings, pause while open, skip intro, memory card folder, texture packs and mods) next to the debug tab, plus optional session cheats (god mode, infinite MP, one-hit defeats, refills, levels, bits). Save states work (snapshot at stage load plus replayed input, checked frame for frame, four slots saved as files). An Original / Modernized preset sets the presentation, loading and balance options at once; *Loading: Fast* (a deliberate deviation from the original timing, carried by save states and online sessions) takes the simulated disc and loading-thread time out of stage changes (about 16 frames instead of 400-900). Supported-language selection applies only to other discs (the USA disc has English only). See [LOCALIZATION.md](LOCALIZATION.md). |
| 4 | Content modding | HD texture replacements, versioned loader/manifests, custom maps, Digimon and weapons, reproducible randomizers, optional rebalancing and bug fixes | Texture dumps and HD texture replacement by pixel hash (any size, PNG, reloadable while playing) work, as do optional rebalancing (EXP rate, damage dealt / taken) and a reproducible item drop randomizer (seeded permutation within each item table, key items kept), carried by save states and online sessions. Content mods load from mods/<id> with versioned manifests (name, version, author, priority, target disc) and replace or add files by logical path, so custom maps, Digimon and weapons ship as replaced packages; online sessions compare the active mods. Authoring tools for new packages remain. |
| 5 | Online four-player co-op | Multi-machine sessions with consistent state, input ownership, latency/disconnect handling, matching mods and clear save ownership | Lockstep sessions over TCP for up to four machines (`--host` / `--join`): delay-based input exchange relayed by the host, game data checked by hash, state fingerprints for desync detection, released slots for leaving guests, shared card copies written back to their owners. Verified with two processes from boot through the character select into the lobby (identical fingerprints). Hosting and joining also work from the F1 Online tab (everyone restarts at the title together). NAT traversal remains. |
| 6 | Simple EXE release | User selects their ROM; supported revision is validated and required assets prepared automatically, without a compiler or manual extraction; repeat launches reuse validated local data; portable saves beside one EXE entry point | The runtime reads the game straight from an uncompressed GDJEB2 disc image or a Dolphin extraction; the first start asks for one (file or folder dialog), checks the game id and revision and saves the path in settings.ini. Saves default to saves/GC/USA next to the program; the EXE links the C++ runtime statically (only system DLLs), so no redistributable is needed. RVZ images are converted once into a plain image next to the program (a GameCube RVZ converted this way matches its Redump SHA-1). `cmake --install` (or `scripts\build.ps1 -Package`) lays out the release around a single DW4_PC.exe; copied to a fresh folder it runs from the disc image with no other files. A test on a fresh Windows installation remains. |

Gate order sets the immediate queue, not an excuse to design out later features.
The final native application must provide one `DW4_PC.exe` entry point with
user-provided assets and portable save data beside it. The in-game ImGui menu
must open with **F1**, receive the current File/View commands and apply supported
settings while playing. These remain native integration requirements.
Preserve every existing save and use explicit migration where formats differ.

Keep rendering enhancements separate from simulation speed; a speed multiplier,
duplicated presents or an FPS slider alone cannot satisfy the 300 FPS target.
Higher internal render resolution and higher-resolution texture content also
have separate acceptance checks. Report hardware, scene, settings and measured
frame times with each performance claim; 300 FPS is a target, not a guarantee
for every device or scene.

## Requested PC options

| Option | Current scope | Full-port target |
| --- | --- | --- |
| 3840×2160 | Host window preference | Verified 4K scene render target and readable UI |
| Borderless | F1 menu / F11 borderless desktop fullscreen, saved | Game renderer resizes correctly across monitors |
| 16:9 | Widescreen option: 3D at the original vertical field of view across the window, 2D layers kept 4:3, full-screen fades stretched | Camera, culling, HUD and menus adapted |
| 60–300 FPS | Provisional walking-preview frame pacing only; original game scheduling remains pending | At least 60 FPS on documented hardware and presentation options up to 300 FPS, decoupled from measured original simulation |
| VSync Off | F1 menu swap interval, saved; frame limit 0-300 | Validated frame pacing in gameplay |
| MSAA 8x | Host context and mesh/static-scene viewport requests; actual samples reported and offscreen resolve tested | Gameplay render passes and material coverage validation |
| Keyboard / Mouse | Full original controls on rebindable keys (three per button), saved | Original gameplay, freely editable saved bindings and appropriate mouse camera support |
| Xbox / PlayStation | Full original controls on four independent slots with hotplug; rebindable buttons and triggers (F1, saved) | Tested original gameplay mappings, independent slots, hotplug and prompts |
| GameCube | SDL-mapped devices when available | Original gameplay mapping, dead zones and adapter support |
| Original / Modernized | F1 preset: Original (4:3, 60 fps motion, no MSAA, intro, original balance) or Modernized (widescreen, smooth motion, 4x MSAA, skip intro); options stay individually adjustable | Original behavior baseline plus explicit optional changes |

## Requested extensions

- **HD texture packs:** done: dumps and replacements by pixel hash (`mods/textures`).
- **Mod loader:** done: `mods/<id>/mod.ini` (version, priority, target disc), files by logical path, F1 toggles.
- **Custom maps:** replaced stage packages load through the mod loader; import tools remain.
- **Custom Digimon:** actor definitions, meshes, rigging, animations and abilities.
- **Custom weapons:** weapon data, models, animation hooks and damage behavior.
- **Randomizer:** item drops done (seeded, reproducible); more tables possible.
- **Difficulty rebalancing:** EXP rate and damage dealt / taken options done.
- **Bug fixes:** reproductions and regression checks against recovered behavior.
- **Online four-player co-op:** follows the simulation/local multiplayer gates above.

Keep each increment buildable and publish source progress to this repository.
Do not distribute the user's ISO, DOL, extracted assets or generated asset packs.
