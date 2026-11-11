# Round "pads": controller families and button prompts

Requested by the owner: the game recognizes which controller is in use
(DualSense, DualShock 4, Xbox controllers from the Xbox 360 on, Switch Pro,
Switch 2 Pro, Wii U Pro, an original GameCube controller on an adapter such
as Mayflash's) and every button picture in the game follows it in real time.
Branch `feat-pads`, built on main `9fcd0681`.

## 1. What is built

| Part | Where | What it does |
| --- | --- | --- |
| Families | `src/game/pad_family.*` | `classify()` from SDL's gamepad type, USB vendor / product and name: DualSense (054c:0ce6, Edge 0df2), DualShock 4 (054c:05c4 / 09cc; PS3 pads share the symbols), Xbox 360 (SDL type XBOX360), Xbox One / Series (XBOXONE), Switch Pro (057e:2009, Joy-Con), Switch 2 Pro (057e:2069: same type and name as the Switch Pro in SDL, only the id differs), Wii U Pro (SDL's Wii driver: the extension in its GUID's last byte, else its name; 057e:0330 alone is also every Wii Remote Plus), GameCube (SDL type GAMECUBE, 057e:0337, 057e:2073, Mayflash / EVORETRO 0079:1843 / 1844 / 1846, PowerA 20d6:a711), other pads (generic, by position). `player_pad()`: the Wii remotes SDL's Wii driver makes gamepads of (a DolphinBar's slots, a Wii Remote Plus with or without its extensions) take no player slot. |
| Binding spaces | `binding_state`, `binding_button`, `gamecube_input` | The `[gamepad]` table still names SDL buttons. Xbox / PlayStation / other pads read it by position exactly as before. Nintendo pads by label (default): `a` is the button printed A (SDL's EAST). A GameCube pad on its original layout (default) ignores the table and plays 1:1: A, B, X, Y, Z, Start, the D-pad, L / R at the trigger's click (SDL's MISC3 / MISC4; a deep press when a mapping lacks the clicks), main stick, C-stick. |
| Which device | `PromptTracker`, `real_input`, `pad_host.*` | Player 1 is the keyboard and the first pad; player 1's prompts follow his last device with real input (a button, a stick past half way, a trigger past 16000, a key that is not an F-key or a modifier); players 2-4 have their own pad's. Unplugging player 1's pad falls back to the next pad (the slots move up as before) or the keyboard; a newly plugged pad takes over only on its input. `pad_host.*` holds the host's pad code (the slots on SDL's events, a pad's input in its binding space, the menus' keys) without ImGui, so the tests run it as the game does. |
| Pictures | `src/game/button_art.*` | Our own vector drawings: a signed-distance rasterizer (circles, rounded boxes, capsules, polygons) and a stroke font of our own (capitals, digits, signs). One style per family: DualSense monochrome symbols with a light rim (Options three lines, Create a burst), DualShock 4 the classic coloured symbols and its OPTIONS / SHARE pills with the words, Xbox 360 coloured buttons with START / BACK arrows, Xbox One / Series dark buttons with coloured letters and Menu / View, Switch Pro dark buttons with A / B / X / Y, L / R / ZL / ZR, + / -, Switch 2 Pro the same with a lighter rim plus C and GL / GR where bound, Wii U Pro light buttons, GameCube green A, red B, grey X / Y kidneys, purple Z, yellow C-stick, keyboard key caps with the bound key (Enter and Space as signs, Backspace as the erase sign; the stick, the D-pad and the C-stick one cap with their four keys: W over A S D, the arrows, 8 over 4 2 6). Sticks and D-pads are light on every family (they stand on the game's black text boxes); middle buttons (Start, Menu, +, ...) are as large as A. No logos or wordmarks. One atlas per family (520 x 690, mipmapped, about 7 ms to draw), drawn only while the family is on screen (`PromptAtlasCache`); each picture shows the physical input bound to that game button for the family, so rebinding redraws the families on screen. |
| Game pictures | `GlRenderer` (`set_prompts`, `set_prompt_glyphs`, `prompt_split`, `title_texture`), `src/game/prompt_layout.*` | Render time only. A text line's quads that sample one of the font's twelve GameCube button glyphs (bectext.igb font00.png: U+226A A, U+226B / U+2220 B, U+2252 X, U+2312 Y, U+22A5 START, U+221A L, U+4E9C R, U+2235 Z, U+2261 D-pad, U+2207 stick, U+5A03 C-stick) are drawn from the atlas cell of the line's player's family (the character select's lines `CHRSELFONTnn/font<player><line>` are the players' own, every other line player 1's); the word START / ＳＴＡＲＴ (same cells in this font; a word of its own: no letter or digit touching it on either side, not next to the START glyph) becomes one picture as wide as the family's START picture, and the rest of its row closes up as the line is aligned (left: the start stays, right: the end, centre: the middle; `TextCtlManager::line_of` gives the line, presentation only). The title's PRESS START (mojigc.png, matched by name and the original's pixel hash; a texture pack's replacement in the same layout too, unless its START does not stand alone where the original's does) gets a variant 2048 texels wide (sharp up to 4K) with START replaced by player 1's START picture; its other rows, which TitleEntry reads for "Online", stay as they are. Three variants are kept at most. The engine's quads and texts stay what it made. |
| Port UI | `src/game/button_prompts.*`, `online_ui.cpp`, `game_main.cpp`, `overhead_labels.cpp`, `host_view.cpp` | ImGui texts take tokens: `{A}` ... `{Z}`, `{START}`, `{STICK}`, `{CSTICK}`, `{DPAD}`, `{MENU}` (the F1 menu's View / Back / Share / - button, F1 on the keyboard and the GameCube pad), `{OK}` / `{BACK}` / `{DEL}` / `{OPT}` (the menus' activate, back, delete and capitals: the pad's own bottom / right / left / top face buttons, or with `menu_buttons=bindings` the A / B / X / Y bindings; Enter / Esc / Backspace / Shift on the keyboard). Used by the online screens' footer, the on-screen keyboard's hint, the instance visibility ("{OK} change"), the leave prompt ("hold {B} + {X} + {START}") in the online screens and in the loading / status line, the "Press {START} to play your online character" toasts, the instance line ("{MENU} change") and the defeated member's notice. |
| The menus' buttons | `menu_keys` in `pad_host.cpp`, `menu_pad` in `host_view.cpp` | ImGui's gamepad keys come from player 1's pad (the backend reads no pad itself). `menu_buttons=layout` (the default): the pad's own A / Cross activates and B / Circle goes back whatever the game's bindings, as before this round on Xbox and PlayStation pads; a Nintendo pad as its Nintendo layout reads it (by label its A), a GameCube pad's A and B. `menu_buttons=bindings`: the game's A activates, B goes back, X / Y are the face keys left / up. |
| F1 > Controls | `settings_controllers_section` | A tab of its own (the keyboard and controller sections moved out of Settings): the detected controller per player with its family, what is connected but cannot play (an adapter without its driver or held by another program, a Switch 2 Pro Controller), *Button prompts* (Automatic / DualSense / DualShock 4 / Xbox 360 / Xbox One/Series / Switch Pro / Switch 2 Pro / Wii U Pro / GameCube / Keyboard) with a preview of the active pictures and the other players' families, *Nintendo layout* (by label / by position), *GameCube controller* (original 1:1 / use the bindings), *Menu buttons* (the controller's own / the game's bindings), *Wii U Pro support* (automatic / on / off, at once), *GameCube adapter in Wii U mode*; the binding buttons are named as on the shown family (Cross, ZL, LB ...). |
| SDL hints | `controller_hints`, `update_controllers` | SDL's Wii driver (SDL leaves it off: it does not work behind a DolphinBar, and it opens every Wii remote): `wii_driver=auto` (the default) turns it on only while a 057e:0330 HID device is connected (looked for with `SDL_hid_enumerate` when SDL counts a device change), never in test runs; `on` / `off`. `SDL_JOYSTICK_ENHANCED_REPORTS=auto` (Bluetooth PlayStation pads keep their simple reports; nothing here needs more). GameCube, PS4 / PS5, Switch and Switch 2 drivers are on by SDL's default. |
| GameCube adapter, Wii U mode | `src/game/gc_adapter.*` | SDL opens 057e:0337 only through libusb; since round "libusb" SDL has libusb but is told to leave the adapter (`SDL_HIDAPI_LIBUSB_GAMECUBE=0`, [ROUND_LIBUSB_NOTES.md](ROUND_LIBUSB_NOTES.md) 4), so this reader stays its only owner. With the WinUSB driver on the adapter (Zadig), a reader thread opens it through Windows' WinUSB API (SetupAPI finds the device's interface GUID), sends the start byte 0x13 and reads the 37-byte reports (layout of SDL's `SDL_hidapi_gamecube.c`); each connected port becomes an SDL virtual joystick with 057e:0337, "Nintendo GameCube Controller" and SDL's GameCube button layout, so it is detected and played like any GameCube pad. Windows' device list is looked at when SDL counts a device change (every two seconds only while a found adapter is held by another program); the adapter is let go while the window is in the background (its ports stay as idle pads, no slot moves), so Dolphin can take it; `gc_adapter=false` turns the reader off. The same look notices a Switch 2 Pro Controller (USB or Bluetooth ids). Not in test runs. |

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
| Switch 2 Pro | by product id 057e:2069 | done (C, GL, GR when bound) | the play test over USB, and that Windows puts WinUSB on its interface 1 by itself | USB-C cable with `libusb-1.0.dll` beside the game (round "libusb", [ROUND_LIBUSB_NOTES.md](ROUND_LIBUSB_NOTES.md)); Bluetooth not at all (SDL); F1 > Controls and a notice say what is missing |
| Wii U Pro | SDL's Wii driver: GUID extension / name (the driver runs while 057e:0330 is connected) | done | pairing through Windows' Bluetooth (SYNC button, no PIN) and that SDL's Wii driver sees it | Bluetooth; *Wii U Pro support* automatic (default) |
| GameCube, Mayflash PC mode | SDL HIDAPI GameCube (0079:1843 / 1844), type GAMECUBE | game's own pictures (1:1); port UI GameCube pictures | the trigger clicks (MISC3 / MISC4) on the real adapter; the id Mayflash reports (Device Manager > Hardware Ids) | the adapter's switch on PC, no driver |
| GameCube, Wii U mode (Nintendo WUP-028, Mayflash "Wii U") | own WinUSB reader, virtual pads 057e:0337 | as above | everything about the USB side: discovery through the Zadig INF's interface GUID, the start byte, the reads, plugging in and out while the game runs, letting go in the background and Dolphin taking it | WinUSB driver on "WUP-028" with Zadig (the same as for Dolphin) |
| Keyboard | key presses | key caps with the bound keys | - | - |

## 3. Settings

`[gamepad]` gains `prompts` (`auto` or a family id: `dualsense`, `dualshock4`,
`xbox360`, `xbox_one`, `switch_pro`, `switch2_pro`, `wiiu_pro`, `gamecube`,
`keyboard`), `nintendo_layout` (`label` / `position`), `gamecube_layout`
(`original` / `bindings`), `wii_driver` (`auto` / `on` / `off`; `true` /
`false` of an earlier build read as on / off), `menu_buttons` (`layout` /
`bindings`) and `gc_adapter` (`true` / `false`). Migration: a
file without `nintendo_layout` whose face buttons (`a`, `b`, `x`, `y`) differ
from the defaults keeps reading every pad by position (the bindings were made
for that: the owner's own `a=b, b=a, x=y, y=x` stays exactly as it played);
otherwise Nintendo pads read by label. Every existing binding is kept;
keyboard and Xbox / PlayStation input is unchanged bit for bit (tests), and
so are the menus' buttons on Xbox and PlayStation pads (`menu_buttons=layout`:
A / Cross confirms whatever the bindings, as before this round).

## 4. Why nothing reaches the simulation

The engine got two const accessors (`TextCtlManager::font`, the font's glyph
cells for the renderer, and `TextCtlManager::line_of`, a laid-out geometry's
line: layer, name, alignment). The swap is a different texture and vertex
buffer at draw time; the pictures' state lives in the game layer. Input changes only
which SDL button a binding names on Nintendo and GameCube pads, before
`host_pad_sample`; the wire and the logical pads are as before. Test runs
draw the original pictures unless `DW4_PROMPTS=<family id>` asks for a family,
and never open the GameCube adapter. Checked: det.sh 4004f056 / 396cb219
twice, the save state suite 17 / 17, r25c 10 / 10 (section 6).

## 5. Open points

- **libusb**: done in round "libusb" ([ROUND_LIBUSB_NOTES.md](ROUND_LIBUSB_NOTES.md)):
  the owner approved libusb as a separate `libusb-1.0.dll` (LGPL-2.1) that
  SDL loads at run time; the Switch 2 Pro Controller plays over USB. The Wii
  U mode adapter stays the WinUSB reader's (SDL is told to leave it).
- **Per-player pictures**: done for the lines that are a player's own (the
  character select's `font<player><line>`); every other game text, the title
  and the port's menus show player 1's family. In play, a local co-op status
  window or message that another player opened still shows player 1's
  pictures (the original has no owner on those lines).
- **DualShock 4's OPTIONS / SHARE**: the pills carry the words as on the pad;
  in a single glyph's place (the START glyph, the menu button in a line) the
  word is small at 1080p.
- **Rumble, gyro, touchpad, lights**: not used.
- **Steam Input / DS4Windows**: pads they wrap appear as Xbox 360 pads;
  *Button prompts* can be set by hand then.
- **Data Overflow** (paused, not touched): its own screens write their hints as
  text ("A Choose   B Back", "A: Return to the story.", "A Take"); they keep
  the GameCube names. The shared host code (input, the menus' buttons, F1
  controls) applies there as well.

## 6. Checks

- `ctest` (build/Clean): all tests, among them `pad_family` (classification
  with SDL's Wii driver names and GUIDs, the player slots, binding spaces,
  pictures per binding, the menus' buttons, tracker, migration, every
  family's atlas and its legibility: light sticks and D-pads, START as large
  as A, the word picture's width, the keyboard's clusters and erase sign, the
  DualShock 4's words; the atlas cache; the title variant at 2048 texels and
  a texture pack's replacement; the prompts in text lines laid out by the
  engine's own `layout_text`: words, their rows closing up per alignment,
  longer words, the button glyphs; the device list's ids) and `pad_detect`
  (SDL virtual joysticks for all requested controllers and variants with
  every real backend off, through the host's own `pad_host` code: detection,
  last input wins, drift and trigger noise, hotplug fallback, Wii remotes
  without a slot, a Nintendo pad by label, the menus' keys by layout and by
  bindings, the adapter's virtual pads with the main stick's up).
- Each fix's test was shown to catch it: the fix taken out again (one at a
  time, `build/fix/mutations.log`), the test fails (18 of 18).
- Hidden runs with forced families (`DW4_PROMPTS=<id>[,<id> ...]`, one per
  player; screenshots under the worktree's `build/fix/shots`, `f1`, `online`):
  the title's PRESS START (1080p and 4K), the character select's texts (Press
  START when finished, Select / Confirm / Cancel, the other players' "Press
  A" with their own families), the status window's footer, the tutorial's
  texts, the F1 Controls tab and the online screens' footer, for every
  family.
- `DW4_TRACE_PROMPTS=1` prints every text line with prompts (its player,
  alignment, pictures and words).

Results (11 November 2026, this PC, hidden runs):

| Check | Build | Result |
| --- | --- | --- |
| ctest, build/Clean | 1d297291 | 64 / 64 (pad_family 1682 checks, pad_detect 133 checks) |
| det.sh (newgame_dng101b1, 6000 frames) | 1d297291, three runs of two | 4004f056 / 396cb219 every time |
| save state suite (st_all.sh) | 1d297291 | 17 / 17 identical after loading with a loaded state |
| r25c (segments.sh, 5 at a time) | 1d297291 | 10 / 10 reached, 1,748,011 frames (the reference's numbers per segment) |

## 7. Review fixes

| Finding | Fix |
| --- | --- |
| Switch 2 Pro unusable (SDL without libusb) | The game noticed a connected one (Windows' device list: USB or Bluetooth ids) and said so; round "libusb" then made it playable over USB (ROUND_LIBUSB_NOTES.md) |
| SDL's Wii driver on by default (DolphinBar, Wii remotes taking slots, start-up stalls) | `wii_driver=auto`: on only while a 057e:0330 device is connected (a DolphinBar's 057e:0306 never turns it on), never in test runs; Wii remotes never take a player slot (`player_pad`) |
| 057e:0330 is also the Wii Remote Plus | Wii U Pro only by SDL's GUID extension byte or the name SDL sets after reading the extension |
| Untested paths (adapter y, trigger noise, host code) | `pad_host.*` is the host's code the tests run; tests for the adapter's up, trigger noise through SDL, the slots, the menus' keys |
| START followed by a letter | No letter or digit may touch the word on either side (`STARTS`, `STARTED`, `RESTART`, `START1` stay text; punctuation does not count) |
| The menus followed the game's A binding | `menu_buttons=layout` (default): the controller's own A / Cross confirms as before this round; `bindings` follows the game's A / B |
| Adapter reader polled every two seconds, held the adapter from Dolphin | Device list only when SDL counts a device change (two-second retries only while another program holds a found adapter); let go in the background with the ports kept idle; `gc_adapter` setting; F1 says when the driver is missing or the adapter busy |
| Ten atlases redrawn on every rebinding, 25 title variants | Atlases drawn only for the families on screen; three title variants at most |
| Pictures per player | The character select's lines follow their player's controller |
| Dark sticks and D-pads | Light on every family, the stick as large as a face button |
| Keyboard caps unreadable, Backspace as an arrow | Stick, D-pad and C-stick one cap with large labels; Backspace the erase sign |
| START smaller than A, alone in a five-letter gap | Middle buttons as large as A; the word's picture takes its own width and the row closes up as the line is aligned |
| PlayStation START inconsistent | DualShock 4: OPTIONS / SHARE pills with the words everywhere; DualSense: three lines and the Create burst everywhere (title included) |
| Stale contact sheet | Made again from the test: `build/fix/art/atlases.png` |
| Comment against code (Wii driver) | Rewritten with the automatic mode |
| Title variant 4x, replaced textures kept GC | 2048 texels wide; a replacement in the original's layout gets the picture too |
| Data Overflow's own hint texts | Not touched (paused) |
