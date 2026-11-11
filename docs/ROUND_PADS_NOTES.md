# Round "pads": controller families and button prompts

Requested by the owner: the game recognizes which controller is in use
(DualSense, DualShock 4, Xbox controllers from the Xbox 360 on, Switch Pro,
Switch 2 Pro, Wii U Pro, an original GameCube controller on an adapter such
as Mayflash's) and every button picture in the game follows it in real time.
Branch `feat-pads`, built on main `9fcd0681`.

## 1. What is built

| Part | Where | What it does |
| --- | --- | --- |
| Families | `src/game/pad_family.*` | `classify()` from SDL's gamepad type, USB vendor / product and name: DualSense (054c:0ce6, Edge 0df2), DualShock 4 (054c:05c4 / 09cc; PS3 pads share the symbols), Xbox 360 (SDL type XBOX360), Xbox One / Series (XBOXONE), Switch Pro (057e:2009, Joy-Con), Switch 2 Pro (057e:2069: same type and name as the Switch Pro in SDL, only the id differs), Wii U Pro (name or 057e:0330: SDL's Wii driver gives it no type and Xbox labels), GameCube (SDL type GAMECUBE, 057e:0337, 057e:2073, Mayflash / EVORETRO 0079:1843 / 1844 / 1846, PowerA 20d6:a711), other pads (generic, by position). |
| Binding spaces | `binding_state`, `binding_button`, `gamecube_input` | The `[gamepad]` table still names SDL buttons. Xbox / PlayStation / other pads read it by position exactly as before. Nintendo pads by label (default): `a` is the button printed A (SDL's EAST). A GameCube pad on its original layout (default) ignores the table and plays 1:1: A, B, X, Y, Z, Start, the D-pad, L / R at the trigger's click (SDL's MISC3 / MISC4; a deep press when a mapping lacks the clicks), main stick, C-stick. |
| Which device | `PromptTracker`, `real_input` | Player 1 is the keyboard and the first pad; the prompts follow player 1's last device with real input (a button, a stick past half way, a trigger past 16000, a key that is not an F-key or a modifier). Further players' pads never change the prompts. Unplugging player 1's pad falls back to the next pad (the slots move up as before) or the keyboard; a newly plugged pad takes over only on its input. |
| Pictures | `src/game/button_art.*` | Our own vector drawings: a signed-distance rasterizer (circles, rounded boxes, capsules, polygons) and a stroke font of our own (capitals, digits, signs). One style per family: DualSense monochrome symbols with a light rim, DualShock 4 the classic coloured symbols, Xbox 360 coloured buttons with START / BACK arrows, Xbox One / Series dark buttons with coloured letters and Menu / View, Switch Pro dark buttons with A / B / X / Y, L / R / ZL / ZR, + / -, Switch 2 Pro the same with a lighter rim plus C and GL / GR where bound, Wii U Pro light buttons, GameCube green A, red B, grey X / Y kidneys, purple Z, yellow C-stick, keyboard key caps with the bound key (Enter, Backspace and Space as signs). No logos or wordmarks. Each family has one atlas (520 x 552, mipmapped, about 7 ms to draw); each picture shows the physical input bound to that game button for the family, so rebinding redraws it. |
| Game pictures | `GlRenderer` (`set_prompts`, `set_prompt_glyphs`, `prompt_split`, `title_texture`) | Render time only. A text line's quads that sample one of the font's twelve GameCube button glyphs (bectext.igb font00.png: U+226A A, U+226B / U+2220 B, U+2252 X, U+2312 Y, U+22A5 START, U+221A L, U+4E9C R, U+2235 Z, U+2261 D-pad, U+2207 stick, U+5A03 C-stick) are drawn from the active family's atlas cell instead; the word START / ＳＴＡＲＴ (same cells in this font; a word of its own, not next to the START glyph) becomes one picture over the word's place. The title's PRESS START (mojigc.png, matched by name and pixel hash) gets a 4x variant with START replaced by the family's START picture; its other rows, which TitleEntry reads for "Online", stay the original's. Quads, places and texts stay what the engine made. |
| Port UI | `src/game/button_prompts.*`, `online_ui.cpp`, `game_main.cpp`, `overhead_labels.cpp`, `host_view.cpp` | ImGui texts take tokens: `{A}` ... `{Z}`, `{START}`, `{STICK}`, `{CSTICK}`, `{DPAD}`, `{MENU}` (the F1 menu's View / Back / Share / - button, F1 on the keyboard and the GameCube pad), `{OK}` / `{BACK}` / `{DEL}` (the menus' activate, back and delete: the A / B / X bindings on pads, Enter / Esc / Backspace on the keyboard). Used by the online screens' footer, the on-screen keyboard's hint, the instance visibility ("{OK} change"), the leave prompt ("hold {B} + {X} + {START}") in the online screens and in the loading / status line, the "Press {START} to play your online character" toasts, the instance line ("{MENU} change") and the defeated member's notice. |
| Menus follow the bindings | `menu_pad` in `host_view.cpp` | ImGui's gamepad keys now come from player 1's pad through the bindings (the backend reads no pad itself): the game's A activates, B goes back, X / Y are the face keys left / up. The same button does the same thing in the game and in the port's menus on every family (a Switch pad by label confirms with its A, a GameCube pad goes back with its B). |
| F1 > Controls | `settings_controllers_section` | A tab of its own (the keyboard and controller sections moved out of Settings): the detected controller per player with its family, *Button prompts* (Automatic / DualSense / DualShock 4 / Xbox 360 / Xbox One/Series / Switch Pro / Switch 2 Pro / Wii U Pro / GameCube / Keyboard) with a preview of the active pictures, *Nintendo layout* (by label / by position), *GameCube controller* (original 1:1 / use the bindings), *Wii U Pro Controller support* (SDL's Wii driver, next start); the binding buttons are named as on the shown family (Cross, ZL, LB ...). |
| SDL hints | `controller_hints` | `SDL_JOYSTICK_HIDAPI_WII=1` (the Wii U Pro Controller; off with `wii_driver=0`, SDL notes it does not work behind a DolphinBar), `SDL_JOYSTICK_ENHANCED_REPORTS=auto` (Bluetooth PlayStation pads keep their simple reports; nothing here needs more). GameCube, PS4 / PS5, Switch and Switch 2 drivers are on by SDL's default. |
| GameCube adapter, Wii U mode | `src/game/gc_adapter.*` | SDL opens 057e:0337 only through libusb, which this build does not have. With the WinUSB driver on the adapter (Zadig), a reader thread opens it through Windows' WinUSB API (SetupAPI finds the device's interface GUID), sends the start byte 0x13 and reads the 37-byte reports (layout of SDL's `SDL_hidapi_gamecube.c`); each connected port becomes an SDL virtual joystick with 057e:0337, "Nintendo GameCube Controller" and SDL's GameCube button layout, so it is detected and played like any GameCube pad. Looked for every two seconds; not in test runs. |

## 2. Controllers: what is verified and what needs the device

Verified without hardware: classification of every family by SDL itself
(virtual joysticks with each controller's ids and name: SDL derives type and
labels from them as for the device), the tracker on SDL's own events, the
binding spaces, the pictures, the adapter's report decoding and its virtual
pad path. Not verified: the devices themselves.

| Controller | Detection | Prompts | Needs a real device to confirm | Windows |
| --- | --- | --- | --- | --- |
| DualSense (PS5) | SDL HIDAPI PS5, type PS5 | done | buttons and Bluetooth reconnects in play | USB or Bluetooth, nothing to install |
| DualShock 4 (PS4) | SDL HIDAPI PS4, type PS4 | done | licensed / third-party pads that report other ids | USB or Bluetooth |
| Xbox 360 | XInput, type XBOX360 | done | wireless receiver (SDL reports 045e:02a1) | driver from Windows |
| Xbox One / Series | XInput, type XBOXONE | done | Elite paddles (shown as P1-P4 if bound) | USB, Bluetooth or the wireless adapter |
| Switch Pro | SDL HIDAPI Switch, type NINTENDO_SWITCH_PRO | done | by-label default with the device | USB or Bluetooth |
| Switch 2 Pro | by product id 057e:2069 | done | that SDL opens it at all: SDL 3.4.16 drives it over USB only through libusb (not in this build), Bluetooth not at all | not usable with this build; works once SDL is built with libusb (see 5) |
| Wii U Pro | name / 057e:0330 (SDL's Wii driver, enabled by this round) | done | pairing through Windows' Bluetooth (SYNC button, no PIN) and that SDL's Wii driver sees it | Bluetooth; *Wii U Pro Controller support* on (default) |
| GameCube, Mayflash PC mode | SDL HIDAPI GameCube (0079:1843 / 1844), type GAMECUBE | game's own pictures (1:1); port UI GameCube pictures | the trigger clicks (MISC3 / MISC4) on the real adapter; the id Mayflash reports (Device Manager > Hardware Ids) | the adapter's switch on PC, no driver |
| GameCube, Wii U mode (Nintendo WUP-028, Mayflash "Wii U") | own WinUSB reader, virtual pads 057e:0337 | as above | everything about the USB side: discovery through the Zadig INF's interface GUID, the start byte, the reads, plugging in and out while the game runs | WinUSB driver on "WUP-028" with Zadig (the same as for Dolphin) |
| Keyboard | key presses | key caps with the bound keys | - | - |

## 3. Settings

`[gamepad]` gains `prompts` (`auto` or a family id: `dualsense`, `dualshock4`,
`xbox360`, `xbox_one`, `switch_pro`, `switch2_pro`, `wiiu_pro`, `gamecube`,
`keyboard`), `nintendo_layout` (`label` / `position`), `gamecube_layout`
(`original` / `bindings`) and `wii_driver` (`true` / `false`). Migration: a
file without `nintendo_layout` whose face buttons (`a`, `b`, `x`, `y`) differ
from the defaults keeps reading every pad by position (the bindings were made
for that: the owner's own `a=b, b=a, x=y, y=x` stays exactly as it played);
otherwise Nintendo pads read by label. Every existing binding is kept;
keyboard and Xbox / PlayStation input is unchanged bit for bit (tests).

## 4. Why nothing reaches the simulation

The engine got one const accessor (`TextCtlManager::font`, the font's glyph
cells for the renderer). The swap is a different texture and index buffer at
draw time; the pictures' state lives in the game layer. Input changes only
which SDL button a binding names on Nintendo and GameCube pads, before
`host_pad_sample`; the wire and the logical pads are as before. Test runs
draw the original pictures unless `DW4_PROMPTS=<family id>` asks for a family,
and never open the GameCube adapter. Checked: det.sh 4004f056 / 396cb219
twice, the save state suite 17 / 17, r25c 10 / 10 (section 6).

## 5. Open points

- **libusb**: the Switch 2 Pro needs SDL built with libusb (`SDL_HIDAPI_LIBUSB`,
  loaded at run time as `libusb-1.0.dll`, LGPL; the header is needed at build
  time and is not on this PC; nothing was downloaded). Then SDL's own driver
  would also cover the Wii U mode adapter; the WinUSB reader stays as the
  path without the DLL. The owner decides.
- **Per-player pictures**: one family for every picture (player 1's). In local
  co-op with mixed controllers the character select's per-quadrant "Press A"
  of players 2-4 shows player 1's picture. A per-player choice needs the draw
  item -> text line -> player link (line names `font<p><nn>`).
- **Word slot**: START becomes one picture centred over the word's place; the
  text is not laid out again (a little space on either side).
- **Rumble, gyro, touchpad, lights**: not used.
- **Steam Input / DS4Windows**: pads they wrap appear as Xbox 360 pads;
  *Button prompts* can be set by hand then.
- **Data Overflow** (paused, not touched): its own screens write their hints as
  text ("A Choose   B Back", "A: Return to the story.", "A Take"); they keep
  the GameCube names. The shared host code (input, F1 controls) applies there
  as well.

## 6. Checks

- `ctest` (build/Clean): all tests, among them `pad_family` (classification,
  binding spaces, pictures per binding, tracker, migration, every family's
  atlas, the title variant) and `pad_detect` (SDL virtual joysticks for all
  requested controllers and variants with every real backend off: detection,
  last input wins, drift, hotplug fallback, the adapter's virtual pads).
- Hidden runs with forced families (`build/pads/shots.sh`, `f1shot.sh`,
  `online_shot.sh`, screenshots under the worktree's `build/pads`):
  the title's PRESS START, the character select's texts (Press START when
  finished, Select / Confirm / Cancel), the status window's footer, the F1
  Controls tab and the online screens' footer, for every family.
- `DW4_TRACE_PROMPTS=1` prints every split text line's quads (button glyph,
  letter, word).
