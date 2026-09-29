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
| 1 | Complete original native game and source | Recovered game and engine source drives original startup, menus, gameplay, combat, progression, transitions, audio and saves; independent local 1–4-player controls; behavior compared against the original | The engine boots through logos, title, character select and `gameStart.lua` into the lobby areas with story scenes, dialogs, NPC talk, jumping and gate stage changes. Combat, dungeons, audio, saves and the original collision responses remain. |
| 2 | Performance and PC graphics | At least 60 FPS on documented hardware; options up to 300 FPS without faster simulation/audio; validated frame times, 16:9 camera/culling/HUD, 4K, display modes, VSync and MSAA | Original assets, skinned actors and text render through the OpenGL runtime at a fixed 60 Hz simulation. Presentation options, 16:9, 4K and frame-time validation remain. |
| 3 | ImGui settings and QoL | Working persistent graphics/input controls, freely editable keyboard bindings, optional cheats, save/load states, supported game languages and original/modernized options; applied settings and unsupported combinations visible | The runtime has an F1 debug menu (stage loader, state, notes). The gameplay settings menu, persistent bindings, cheats, states and supported-language selection remain. See [LOCALIZATION.md](LOCALIZATION.md). |
| 4 | Content modding | HD texture replacements, versioned loader/manifests, custom maps, Digimon and weapons, reproducible randomizers, optional rebalancing and bug fixes | Original asset identities and decoders are available. Native content contracts, loading and gameplay integration remain pending. |
| 5 | Online four-player co-op | Multi-machine sessions with consistent state, input ownership, latency/disconnect handling, matching mods and clear save ownership | Pending. The engine models four controller slots; local multiplayer must be verified before network integration. |
| 6 | Simple EXE release | User selects their ROM; supported revision is validated and required assets prepared automatically, without a compiler or manual extraction; repeat launches reuse validated local data; portable saves beside one EXE entry point | The runtime reads supplied extracted data (`--data` or `local/settings.ini`); `tools/verify_game.py` identifies the disc. Automatic ROM preparation, saves and clean-machine release acceptance remain pending. |

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
| Borderless | Host fullscreen desktop window | Game renderer resizes correctly across monitors |
| 16:9 | Stored preference only | Camera, culling, HUD and menus adapted |
| 60–300 FPS | Provisional walking-preview frame pacing only; original game scheduling remains pending | At least 60 FPS on documented hardware and presentation options up to 300 FPS, decoupled from measured original simulation |
| VSync Off | Host OpenGL swap-interval request | Validated frame pacing in gameplay |
| MSAA 8x | Host context and mesh/static-scene viewport requests; actual samples reported and offscreen resolve tested | Gameplay render passes and material coverage validation |
| Keyboard / Mouse | Tool UI and provisional keyboard walking controls | Original gameplay, freely editable saved bindings and appropriate mouse camera support |
| Xbox / PlayStation | SDL device inspection and mapped provisional walking input | Tested original gameplay mappings, independent slots, hotplug and prompts |
| GameCube | SDL-mapped devices when available | Original gameplay mapping, dead zones and adapter support |
| Original / Modernized | Stored preference only | Original behavior baseline plus explicit optional changes |

## Requested extensions

- **HD texture packs:** replacement lookup after reliable texture decoding.
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
