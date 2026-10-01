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
| 1 | Complete original native game and source | Recovered game and engine source drives original startup, menus, gameplay, combat, progression, transitions, audio and saves; independent local 1–4-player controls; behavior compared against the original | The engine boots through logos, title, character select and `gameStart.lua` into the lobby areas with story scenes, dialogs, NPC talk, jumping and gate stage changes, then the Death Valley practice tutorial with enemy waves, weapon attacks, FIGHTCALC damage and lesson demos, and Death Valley combat with the original AIM enemy AI, enemy damage, defeat, EXP/levels and generator item drops with pickup, quick panels with MP techniques, the status window (equipment, folder and skill pages), and the lobby programs (weapon, armor, chip, disk and junk shops, bank, X data keeper, appraiser, Digi-Lab with digivolution into the evolution scene, card keeper with booster packs and album), status effects (poison, sleep, stun, bind, attack/power down with their icons), the support techniques with raise and the bit capsules of defeated players, skill growth, the opening movie, the loading screens, the ADX music and ambience, the sound effects, projected shadows, stage lights, particle effects, rippling water, homing bullets, memory card saves in the save room and calling saved characters at the character select. The original collision queries and responses are reconstructed, with rides on boats, trains and moving platforms and the conveyor, quicksand and slippery floors; all six boss stages are completed in automated runs (defeat, death demo, reward boxes, boss gates, staff roll after the final boss); the later dungeons still need comparing against the original. |
| 2 | Performance and PC graphics | At least 60 FPS on documented hardware; options up to 300 FPS without faster simulation/audio; validated frame times, 16:9 camera/culling/HUD, 4K, display modes, VSync and MSAA | Original assets, skinned actors and text render through the OpenGL runtime at a fixed 60 Hz simulation (600-1000 frames per second in hidden test runs). The F1 menu sets borderless fullscreen, widescreen, VSync, a present limit, MSAA and window sizes up to 2560x1440. Presents above about 75 per second blend model, skinning, camera and shadow matrices between the last two 60 Hz steps (one step of latency; warps and new items are drawn as they are). 4K and frame-time validation remain. |
| 3 | ImGui settings and QoL | Working persistent graphics/input controls, freely editable keyboard bindings, optional cheats, save/load states, supported game languages and original/modernized options; applied settings and unsupported combinations visible | The F1 menu edits persistent settings while playing (settings.ini: borderless fullscreen, VSync, frame limit, MSAA, window size, master volume, freely editable keyboard bindings, pause while open) next to the debug tab, plus optional session cheats (god mode, infinite MP, one-hit defeats, refills, levels, bits). Save states work (snapshot at stage load plus replayed input, checked frame for frame, four slots saved as files). Supported-language selection (the USA disc has English only) and original/modernized options remain. See [LOCALIZATION.md](LOCALIZATION.md). |
| 4 | Content modding | HD texture replacements, versioned loader/manifests, custom maps, Digimon and weapons, reproducible randomizers, optional rebalancing and bug fixes | Texture dumps and HD texture replacement by pixel hash (any size, PNG, reloadable while playing) work. Versioned manifests, custom maps, Digimon, weapons, randomizers and rebalancing remain pending. |
| 5 | Online four-player co-op | Multi-machine sessions with consistent state, input ownership, latency/disconnect handling, matching mods and clear save ownership | Lockstep sessions over TCP for up to four machines (`--host` / `--join`): delay-based input exchange relayed by the host, game data checked by hash, state fingerprints for desync detection, released slots for leaving guests, shared card copies written back to their owners. Verified with two processes from boot through the character select into the lobby (identical fingerprints). Hosting and joining also work from the F1 Online tab (everyone restarts at the title together). NAT traversal remains. |
| 6 | Simple EXE release | User selects their ROM; supported revision is validated and required assets prepared automatically, without a compiler or manual extraction; repeat launches reuse validated local data; portable saves beside one EXE entry point | The runtime reads the game straight from an uncompressed GDJEB2 disc image or a Dolphin extraction; the first start asks for one (file or folder dialog), checks the game id and revision and saves the path in settings.ini. Saves default to saves/GC/USA next to the program; the EXE links the C++ runtime statically (only system DLLs), so no redistributable is needed. RVZ images are converted once into a plain image next to the program (a GameCube RVZ converted this way matches its Redump SHA-1). Clean-machine release acceptance remains pending. |

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
| Original / Modernized | Stored preference only | Original behavior baseline plus explicit optional changes |

## Requested extensions

- **HD texture packs:** done: dumps and replacements by pixel hash (`mods/textures`).
- **Mod loader:** versioned manifests, load order, stable IDs and validation.
- **Custom maps:** scene, collision, navigation and spawn schemas plus import tools.
- **Custom Digimon:** actor definitions, meshes, rigging, animations and abilities.
- **Custom weapons:** weapon data, models, animation hooks and damage behavior.
- **Randomizer:** identified data tables and reproducible seeds, with save metadata.
- **Difficulty rebalancing:** optional, versioned changes after original combat tests.
- **Bug fixes:** reproductions and regression checks against recovered behavior.
- **Online four-player co-op:** follows the simulation/local multiplayer gates above.

Keep each increment buildable and publish source progress to this repository.
Do not distribute the user's ISO, DOL, extracted assets or generated asset packs.
