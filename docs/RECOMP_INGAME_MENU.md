# Integrated game menu

The Windows game session now draws its settings menu directly through the
game renderer's ImGui context. **F1** opens or closes it; **Escape** closes an
open menu. Repeated F1 key-down messages do not toggle it repeatedly. The
native File/View menu bar has been removed.

Opening the menu keeps the game running. Keyboard/mouse input and emulated
controller polling are blocked from the game while it is open, including when
background input is enabled. Pause/Resume is explicit. The renderer continues
to draw the menu while the game is paused without advancing guest frames.

## Controls

- Pause/Resume, Mute/Unmute, fullscreen/windowed, close menu and exit game.
- Save a new timestamped state, refresh the newest 128 states and load a
  selected state using the existing save-state format.
- Internal rendering scale, aspect ratio, supported MSAA levels, VSync and
  loading installed HD texture packs.

Graphics changes currently apply to the running session. The launcher still
provides the persistent startup resolution/fullscreen settings. The 16:9
choice is explicitly labelled **stretch**: it does not establish a corrected
widescreen camera or HUD. EFB dimensions describe the internal render target,
not a guaranteed monitor resolution. Higher simulation/presentation targets,
cheats, language selection and gameplay modifications remain separate work.

Pause, save/load and graphics changes are disabled during online sessions.
This restriction does not establish complete online compatibility. Ordinary
sessions started with `--mute` can be unmuted. Strict audio/UI diagnostics
cannot enable audible output.

## Ownership and input

The window thread publishes bounded, platform-independent input events. ImGui
consumes them on the video owner thread before `NewFrame`; the menu draws before
`Render`. Actions use session-guarded host jobs so pausing, state operations and
window changes do not block while holding the ImGui drawing mutex.

Paused redraw and resize requests pass through the CPU owner before entering
the GPU event queue. Session retirement rejects stale jobs before core shutdown;
video hooks are removed after the core has joined its video thread. Focus loss
and unexpected mouse-capture loss release held input. Ordinary mouse button-up
does not discard the click. Native Space fast-forward restores its previous
speed and cannot restart from an old held-key repeat after closing the menu.

## Local checks

No GitHub Actions or interactive game launch is required:

```powershell
./scripts/test-recomp-menu.ps1
./scripts/test-recomp-composed-capture.ps1
./scripts/test-recomp-ingame-menu.ps1 -ExtractedRoot '<verified extraction>'
```

The focused input checker passes 2,026 checks. The composed-capture channel and
SDR conversion checker passes 134 checks. A separate actual-source decoder
check covers ten UTF-16/scalar cases. These checks do not boot the game.

The finite native-window diagnostic creates a fresh isolated Unicode profile
under `work/recomp-ingame-menu`, with empty physical controller bindings. Its
real Win32 Vulkan surface is never shown or activated. It posts messages only
to its own HWND and checks that no native menu is attached. Actual NullSound
telemetry must show no device start or callbacks; requested mute alone is not
accepted as proof.

The test exercises F1/repeat/Escape, Space capture, real Pause/Resume button
clicks, paused redraw without guest-frame progress, resumed game-frame progress
and unchanged save-state inventory. A 60-second internal deadline and a
separate process-identity watchdog bound the run. It does not validate a full
save/load round trip, all graphics combinations, audible quality or gameplay.

The diagnostic PNG reads the composed Vulkan backbuffer after game and ImGui
drawing. Its swapchain requests supported transfer-source usage only for this
test; normal presentation keeps its existing usage. Readback follows the normal
submission and explicitly converts supported SDR RGBA/BGRA output. Ordinary
XFB screenshots do not include the menu and cannot replace this check.

The packaged build passed the tested input and pause/resume sequence in 39.11
seconds from diagnostic initialization, with 57 menu draws and 24,177
hidden-window checks. Actual NullSound stayed device-free. The 944 x 721
composed PNG was visually inspected: the original opening image and the usable
settings menu are both visible. During the pause the game-frame count remained
at its recorded baseline while the menu continued drawing; game frames resumed
after clicking Resume. The ordinary FPS title updates were also observed on
the hidden window. The complete process exited normally within 50.64 seconds,
including game inspection, initialization and shutdown. All title-update callers,
including core startup, now use coalesced lifetime-guarded host jobs; shutdown
never needs to join a helper waiting for a synchronous window title message.

Local evidence: `work/recomp-ingame-menu/20260913-083907-e92f7e57/`. The tested
package is `dist/DW4_Recomp/PC-Preview-20260913-083859/`, with EXE SHA256
`DC44D97D53119D67DF11601C8FED99EECCACCCA98BA7B989D842E4F367770F42`.
It contains one EXE and no test profile or original assets. The game DLL and
runtime resource/CRT sidecars are still required.

This is a functional menu milestone. It does not fix the reported opening-movie
slowdown or establish a stable 60 FPS gameplay/audio result.
