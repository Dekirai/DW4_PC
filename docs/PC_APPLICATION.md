# PC application packaging and controls

The PC application starts through `DW4_PC.exe`. Its game module is packaged as
`data/game.dll`; the application validates and imports the owner's supported ROM
into adjacent `savedata`. Old previews are separate snapshots and are not
modified by packaging a new build.

## Built-in resources

The `MODERNGEKKO_EMBED_SYSTEM_RESOURCES` build option replaces wholesale copying
of the upstream `Sys` tree. A fixed allowlist compiles the free GameCube font
substitutes, DSP tables, settings-menu font, DW4 game defaults and optional cheat
handler into the application. Other-game settings, Wii files, Qt themes,
branding images and title databases are not shipped. These service resources
are separate from the copyrighted game data imported from the owner's ROM.

The generator records each resource's size and SHA-256. Packaging requires the
manifest from the matching completed build. Existing user overrides retain
priority; the built-in bytes supply the system defaults. The original INI
parser and merge order are shared by file and memory inputs. The menu font
uses immutable process-lifetime storage with ImGui font ownership disabled.

Component copyright notices, font licenses, source revisions and local patches
remain in `licenses/`. This changes the product's packaging and interface;
the application still uses a Dolphin-derived runtime for graphics, audio and
console services. The external game module and Visual C++ libraries also remain,
so this is not yet a single-file distribution or an emulation-free engine.
The [built-in module design](RECOMP_BUILTIN_MODULE.md) describes the next step
for linking the generated game into the EXE while preserving dynamic mods.
It is design work only; the current package still requires `data/game.dll`.

## Portable settings

Runtime settings are written to `savedata/Config/Runtime.ini`, with the engine
log named `engine.log`. When an earlier `Config/Dolphin.ini` exists and the new
file does not, the frontend copies its bytes into the new file. The original
and all saves remain untouched. An existing new configuration always wins.
Migration is bounded and refuses irregular files; Windows publication uses an
exclusive temporary file and a no-replace rename of its open handle.

## Keyboard input

The keyboard editor belongs to the original game's native input configuration.
Open **F1 → Controls**, select Player 1–4, and choose keys or mouse buttons for
the 24 pad inputs. Movement, face buttons, Start, both sticks and their modifiers,
the D-pad, and separate digital/analog triggers are editable. **Keyboard defaults**
and **Clear keyboard** change the draft; **Apply and save** validates and saves
it before updating live input. **Reload bindings** refreshes the applied values.
Reloading or switching players discards unsaved edits.

Default movement is WASD; A/B/X/Y/Z are Space/Left Ctrl/E/R/Q; Start is Enter.
C-stick directions use I/K/J/L, L uses Left Shift, R uses F, and the D-pad uses
the arrow keys. Left Alt is the main-stick modifier; the C-stick modifier starts
unbound. Trigger keys apply to both digital and analog inputs and can be edited
separately. **F1** and **Escape** are reserved for the menu and cannot be assigned.
The integrated session disables the old Space fast-forward and F11/Alt+Enter
fullscreen shortcuts, so those bindings do not invoke legacy runtime actions.

Play does not require a connected gamepad. A fresh Player 1 profile receives
keyboard defaults after native input initialization. Existing Player 1 bindings
stay intact; defaults can be added through F1. Other players receive keys only
when assigned, and explicitly cleared saved keys remain cleared after restart.
Each player has independent assignments; shared-keyboard players should select
different keys. These assignments use the actual Windows DInput device names,
with friendly labels in the menu.

The checked atomic save writes `savedata/Config/GCPadNew.ini`. Keyboard expressions
are added alongside existing controller expressions; `Keyboard1`–`Keyboard4`
metadata identifies the editor's contribution so subsequent remaps or clears
preserve the underlying controller and other players. Saved keyboard assignments
also activate their standard controller ports. Their original port types and
ownership are persisted in the same file, then applied through runtime settings
and normal serial-interface device-change requests. Base `Runtime.ini` port
settings remain intact. Clearing a keyboard restores its original port type when
the editor still owns the current assignment, preserving an externally changed
active port. The launcher's controller selection merges only the requested
slots, preserving custom and explicitly unbound controls. Its saved gamepad
intent keeps a newly selected controller active after keyboard bindings are
cleared; startup ignores that intent if its device qualifier no longer matches.

The launcher's **Use selected controller** action also merges into this profile,
preserving keyboard assignments, custom or explicitly unbound inputs and other
players. Missing gamepad inputs receive defaults. A checked atomic save records
the selected device's port assignment, which survives clearing its keyboard and
restarting. Startup ignores that assignment if the configured device no longer
matches the saved qualifier.

Named game-specific profiles selected through `[Controls] PadProfileN` remain
authoritative. Startup respects the game's identity and revision when checking
them. The F1 editor reports an error for an overridden player before changing
or saving controls; editing those named profiles is not implemented here.

Opening F1 blocks game input, including background input, while the game keeps
running unless paused explicitly. Headless and hidden sessions do not create
keyboard defaults automatically; the controls diagnostic explicitly exercises
them in its isolated profile. The packaged hidden keyboard diagnostic passes;
physical keyboard gameplay remains pending. See [RECOMP_INGAME_MENU.md](RECOMP_INGAME_MENU.md)
for defaults, ownership details, source checks and separately dated earlier
menu evidence. A settings-file round trip does not establish gameplay or
independent multiplayer.

## Validation

The resource checker passes eight generator tests, 82 compiled embedded-resource
and INI-parser checks, and 49 compiled checks with embedding disabled. It compares
all seven compiled payloads with their source bytes: 330,796 bytes in total.
A production CMake object build starting without the generated include also
passes: the explicit `common` dependency generates both resource outputs before
compilation. Evidence is in `build/recomp-system-resource-check/`.

The actual portable migration helper passes 45 optimized MSVC checks, with no
failures and one skipped symlink fixture because Windows denied the required
privilege. This includes a concurrent destination created during copying;
the existing winner and original saves remain intact. Evidence is in
`build/recomp-portable-profile-check/`.

The complete local build and loaded CPU/module ABI check pass. As created, package
`dist/DW4_Recomp/PC-Preview-20260913-114826/` contains one EXE, `data/game.dll`,
the matching resource manifest, CRT libraries and licenses, with no `Sys` or
original game assets. Its EXE SHA256 is
`74F43D84E055D08653AFC1797245CAAE94D90C8E27F709CD01B62DC8D94A5A33`;
its module SHA256 is
`0F7C3DA7896E1A076E6672D664023DF92DDE131533C3A4096C490D63D94804E1`.
The owner subsequently opened this snapshot manually and imported game data;
that adjacent `savedata` is preserved separately from the isolated test profiles.

The package passes the hidden native-window diagnostic in 51.97 seconds overall,
including initialization and shutdown. All keyboard checks pass, including
actual Reload/Defaults/Apply buttons, persistence, input blocking, independent
slots, port restoration and preservation of a subsequently selected gamepad.
The exact test profile is restored. The 944 x 721 composed capture was visually
checked and displays the applied keys over the original game's image. Actual
NullSound reports no audio device or callbacks. Evidence is under
`work/recomp-ingame-menu/20260913-114845-c37d41d5/`.

A separate 90-second 1x Vulkan test starts the same package without an explicit
module argument. It loads the adjacent `data/game.dll`, reaches the original
Game Start / Option menu, and exits normally. Cubeb is verified at device volume
zero before startup. There are no failed SMC chunks or reported interpreter
fallback steps in that bounded run. Its final five-second window still includes
a slowdown (37.95 frame-counter advances/s and 34 audio-source replay events),
so this is not a performance or audible-quality acceptance result. Manual
preview activity also overlaps this validation period; the timing is not an
isolated performance comparison. Evidence is
in `work/recomp-startup/20260913-115019-39ebc746/`.

The asset-free GC and generic frontend configuration tests also pass, including
existing keyboard/controller merges, deliberate unbound keys, fresh player
assignments, untouched unrelated sections and failed atomic publication.
Logs are in `work/recomp-controller-merge-check-e6427a3c/`.
These checks establish the tested configuration and menu behavior; they do not
establish physical-key delivery, complete gameplay or independent multiplayer.
Builds and tests use no GitHub Actions.

### Subsequent cache-policy build

`dist/DW4_Recomp/PC-Preview-20260913-122547/` retains the same integrated keyboard
editor, embedded resources and external `data/game.dll`, with no `Sys` tree or
original assets included. Its updated runtime passes the expanded cache-policy
state/timing checks and bounded original movie/main-menu startup. The final
short 1x menu window runs near 60 frame-counter advances/s at original speed
without audio-source replay, while the movie remains slow. See
[RECOMP_PERFORMANCE.md](RECOMP_PERFORMANCE.md#rebuilt-policy-measurement) for
the exact hashes, measurements and limitations. This update does not establish
physical keyboard gameplay or remove the remaining runtime dependencies.
