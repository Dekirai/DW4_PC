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
- A **Controls** panel with 24 keyboard assignments for each of Players 1–4,
  including stick modifiers and separate digital/analog trigger inputs.

Graphics changes currently apply to the running session. The launcher still
provides the persistent startup resolution/fullscreen settings. The 16:9
choice is explicitly labelled **stretch**: it does not establish a corrected
widescreen camera or HUD. EFB dimensions describe the internal render target,
not a guaranteed monitor resolution. Higher simulation/presentation targets,
cheats, language selection and gameplay modifications remain separate work.

Pause, save/load, graphics changes and keyboard editing are disabled during online sessions.
This restriction does not establish complete online compatibility. Ordinary
sessions started with `--mute` can be unmuted. Strict audio/UI diagnostics
cannot enable audible output.

## Keyboard bindings

The Controls panel edits the native GameCube pad configuration consumed by the
original game. Select a player, then choose an available keyboard key or mouse
button for each action. The list comes from the Windows DInput **Keyboard Mouse**
device; friendly labels such as **Enter**, **Left Ctrl** and **Mouse left** retain
the backend's actual input names in saved expressions. **F1** and **Escape** are
reserved and cannot be assigned. Each player has separate assignments; players
sharing one keyboard should choose separate key sets.

**Keyboard defaults** fills the selected player's draft with this layout:

| Pad input | Default keyboard input |
| --- | --- |
| Main stick up / down / left / right | W / S / A / D |
| Main stick modifier (move slowly) | Left Alt |
| A / B / X / Y / Z | Space / Left Ctrl / E / R / Q |
| Start | Enter |
| C-stick up / down / left / right | I / K / J / L |
| C-stick modifier | Unbound |
| L and L analog | Left Shift |
| R and R analog | F |
| D-pad up / down / left / right | Arrow keys |

**Clear keyboard** clears the draft. **Apply and save** validates the selected
keys, saves the profile, then updates the live pad and its device references.
**Reload bindings** refreshes the editor from the applied controls; reloading or
switching players discards unsaved edits. Defaults and clear require Apply and
save before they affect the game.

A fresh Player 1 profile receives these defaults when the native keyboard is
ready, so Play does not require a connected gamepad. Existing Player 1 bindings
are retained; use Keyboard defaults and Apply and save to add keyboard controls
to that profile. An explicitly cleared saved keyboard stays cleared on restart.
Other player slots receive no automatic keyboard defaults.

Bindings are stored in `savedata/Config/GCPadNew.ini`. The editor combines each
keyboard expression with its existing controller expression and records its
own contribution in `Keyboard1`–`Keyboard4` sections. Rebinding or clearing
removes only the contribution it still owns. Other players' settings are
preserved. The checked temporary-file-and-rename save completes before live
bindings change; a reported save failure leaves them unchanged.

Assigning a keyboard also activates that player's standard controller port.
The same checked profile stores the original port type and keyboard ownership;
startup reapplies the saved port assignment through the runtime configuration.
The base port settings in `Runtime.ini` remain intact. During play, the serial
interface receives an atomic device-change request and applies it during normal
polling. Clearing the keyboard restores its recorded original port type if the
editor still owns the active assignment; an externally changed active type is
preserved. Connection changes can take effect after the game's normal detection
delay.

The launcher's **Use selected controller** action merges the requested gamepad
slots into that same profile. It preserves keyboard bindings, custom expressions,
explicitly unbound inputs and unrelated sections; missing gamepad expressions
are filled with defaults. Its checked atomic save records the selected device's
port intent, so clearing keyboard controls does not disconnect a subsequently
selected gamepad. Startup uses that intent only while its saved device qualifier
still matches the player's configured device.

Named game-specific profiles selected by `[Controls] PadProfileN` are outside
this editor's supported scope. Startup reads the disc identity and revision to
respect those overrides before boot. Editing an overridden player reports an
error before any save or live change, rather than claiming a global edit will
survive that profile's next reload.

## Ownership and input

The window thread publishes bounded, platform-independent input events. ImGui
consumes them on the video owner thread before `NewFrame`; the menu draws before
`Render`. Actions use session-guarded host jobs so pausing, state operations and
window changes do not block while holding the ImGui drawing mutex.

Paused redraw and resize requests pass through the CPU owner before entering
the GPU event queue. Session retirement rejects stale jobs before core shutdown;
video hooks are removed after the core has joined its video thread. Focus loss
and unexpected mouse-capture loss release held input. Ordinary mouse button-up
does not discard the click. Integrated sessions disable the old native Space
fast-forward, F11/Alt+Enter fullscreen and Escape exit shortcuts. Space can
therefore serve as the default A button without changing simulation speed;
fullscreen and exit remain available in the F1 menu. The legacy shortcuts remain
separate behavior for a frontend without the integrated input callback.

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
under `work/recomp-ingame-menu`, initially with empty physical controller bindings. Its
real Win32 Vulkan surface is never shown or activated. It posts messages only
to its own HWND and checks that no native menu is attached. Actual NullSound
telemetry must show no device start or callbacks; requested mute alone is not
accepted as proof.

The test exercises F1/repeat/Escape, Space capture, real Pause/Resume button
clicks, paused redraw without guest-frame progress, resumed game-frame progress
and unchanged save-state inventory. The new keyboard checks run while paused:
first-launch defaults, a remap that resolves against the actual DInput device,
native `Pad::LoadConfig` persistence, preservation of another controller
expression and other players, reserved-key rejection, clearing/restoring the
original port, and reapplying saved port intent. A selected-gamepad fixture checks
that keyboard Clear and startup preserve its port, while stale device-qualified
intent is ignored. A positive pad sample and the
actual `Core::UpdateInputGate` path check open-menu blocking. After resume, the
test waits for Player 2's real serial-interface attachment, checks gate release
and posts Space outside the menu to check that speed stays unchanged. Before
capture, it expands the Controls panel and clicks the real Reload bindings
button, waiting for the refreshed controls revision to be drawn. A temporary
Player 1 A binding of F3 is then changed back to Space through the real Keyboard
defaults and Apply and save buttons. The diagnostic checks the resulting native
pad expressions and saved metadata before capturing that refreshed editor.
Separate report fields record the Reload click, saved Apply result and captured
controls revision. These messages target only
its own HWND; they do not synthesize physical DInput key presses. The diagnostic
restores the original profile bytes and live pad settings on success or caught
failure. A 60-second internal deadline and a
separate process-identity watchdog bound the run. It does not validate a full
save/load round trip, all graphics combinations, audible quality or gameplay.

The keyboard sources pass focused compiler syntax checks. The GC and generic
asset-free frontend configuration tests pass, including setup without a
selected gamepad and preservation during subsequent controller selection.
The full build and expanded packaged hidden diagnostic also pass, as recorded
below. Physical-key delivery, progression and independent multiplayer remain
separate acceptance work.

The diagnostic PNG reads the composed Vulkan backbuffer after game and ImGui
drawing. Its swapchain requests supported transfer-source usage only for this
test; normal presentation keeps its existing usage. Readback follows the normal
submission and explicitly converts supported SDR RGBA/BGRA output. Ordinary
XFB screenshots do not include the menu and cannot replace this check.

## Keyboard package evidence — 13 September 2026

Package `dist/DW4_Recomp/PC-Preview-20260913-114826/` passes the complete expanded
diagnostic with no adjacent `Sys` directory. The test completes in 40.59 seconds
from diagnostic initialization and 51.97 seconds overall. It records 83 menu
draws, 24,129 hidden-window checks and no input overflows. All keyboard fields
pass, including real Reload/Defaults/Apply clicks, refreshed controls revision
5, selected-gamepad port preservation, actual Player 2 port attachment and exact
test-profile restoration. Actual NullSound never starts a device or callbacks.

The 944 x 721 composed image was visually inspected: it shows the original game
behind the paused settings menu, with Space, Left Ctrl, E, R, Q, Enter and W
displayed in the scrollable Controls editor. The game-frame counter remains
1250 during the paused controls checks and capture, then advances after Resume.
Evidence is in `work/recomp-ingame-menu/20260913-114845-c37d41d5/`; package hashes
and resource checks are recorded in [PC_APPLICATION.md](PC_APPLICATION.md).
This checks native binding resolution and settings behavior without injecting
physical DInput keys; it does not establish complete keyboard gameplay.

## Earlier package evidence — 13 September 2026

The package below predates the keyboard editor and shortcut changes. Its results
establish the earlier menu sequence, not the new keyboard acceptance checks.

That packaged build passed the tested input and pause/resume sequence in 39.11
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
