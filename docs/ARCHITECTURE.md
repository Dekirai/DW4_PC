# Native port architecture

## Implemented boundary

`dw4_core` is a C++20 library with explicit byte-order decoding and no console or
graphics dependencies. `assets` reads boot, DOL, AFS and BNR structures. `igb`
reads version 6 metadata; `igb_document` loads owned object records, memory
payloads and validated references. `resources` resolves logical names through
the original AFS setup, and `textures` decodes verified indexed8/RGBA8 and BC1
base images. `settings` loads and validates host preferences. `dw4_inspect` and
`dw4_catalog` are read-only CLIs; `dw4_pc` is the native SDL3/OpenGL/ImGui viewer
and workbench.

AFS payloads are read on demand with limits. Guest addresses remain integers
inside diagnostics; they are never cast into host pointers. A validated DOL is
not executable native code. There is currently no PowerPC execution path,
recompiled game code, scene loader, or emulated GPU.

The original DOL BSS envelope overlaps some initialized data sections. A future
loader must zero the envelope before loading initialized sections or clear only
uncovered ranges; blindly zeroing it after section loading destroys valid data.

The application uses graphics dependencies pinned to exact upstream commits and
archive hashes. Core-only builds do not fetch dependencies. Parser tests use
small generated fixtures instead of original game bytes. `verify_game.py`
additionally checks the original DOL checksum and compares every extracted
filesystem file against its ISO extent.

## Engine reconstruction order

1. **Implemented:** resolve the AFS setup in `ndmw.igb`, including archive/entry
   identities and logical paths. See [RESOURCES.md](RESOURCES.md).
2. **Implemented:** decode IGB object instances, memory references and optional
   directory/pool names. External-object loading is still pending. On-disk schemas
   stay separate from host objects; inherited slots are format metadata, not a
   ready-to-use host ABI. See [IGB_OBJECTS.md](IGB_OBJECTS.md).
3. **In progress:** texture base images are decoded and displayed natively.
   Decode mesh/vertex/index layouts next; verify transforms, materials and
   appearance against the original. See [TEXTURES.md](TEXTURES.md).
   Initial vertex-stream and index observations are recorded in
   [MESH_RESEARCH.md](MESH_RESEARCH.md); topology and material interpretation
   still need verification before scene rendering.
4. Reconstruct scene traversal, animation and actor state. Replace OS/DVD/PAD/GX
   boundaries with native services using explicit interfaces.
5. Recover game loop, combat, inventory, scene transitions and persistence in
   narrow, behavior-tested increments. Retain a trace from each adapted function
   to reference addresses and evidence.

The renderer will eventually consume platform-independent materials and meshes.
OpenGL currently displays original textures, a banner and tools; choosing a final game rendering
backend is still open. Avoid making new gameplay objects depend on ImGui or SDL.

## Timing and PC settings

Host presentation frequency and game simulation frequency must be separate.
Measure original DW4 timing before choosing a fixed simulation step; do not
assume 30 or 60 Hz. Animation, physics, input sampling and timers must preserve
their original semantics at 144 Hz presentation. Interpolation and widescreen
camera/UI behavior require game-specific work. A borderless window uses the
desktop display size; selecting 3840×2160 does not change the desktop mode or
prove a 4K internal renderer exists.

SDL-mapped gamepads can be inspected now. Keyboard/mouse gameplay, Xbox,
PlayStation and GameCube gameplay mappings remain separate tasks. Native
GameCube adapters may require dedicated adapter handling and calibration.

## Modding and online co-op dependencies

Stable asset identity and a validated loader come before texture overrides or
mod manifests. Version the future mod API and distinguish declarative content
from executable code. Original versus modernized gameplay should share a tested
original baseline before opting into changes.

Four-player online co-op requires proven local multiplayer, identified player
and world state, authoritative simulation ownership, reproducible RNG handling,
save ownership, reconnect/error behavior and latency testing. The network model
(lockstep, rollback or host-authoritative snapshots) remains undecided until
simulation behavior is understood. No networking implementation is claimed by
this milestone.
