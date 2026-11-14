# The console F1 menu (Android)

Android's F1 menu in console style: the categories as large entries on the
left, a list of large rows on the right, everything reachable with the D-pad
and A / B (L1 / R1 change the category, Left / Right change a value), touch
with large targets, taps on release, drags and flings. The same settings as
the PC's F1 menu through the same apply layer; a different presentation, one
source of truth. The PC's F1 menu is unchanged (the proof: P0 / P1 below); on
the PC the console menu shows only in a test run with
`DW4_MENU_STYLE=console`.

## Files

| File | What |
|---|---|
| `src/game/menu_choices.hpp/.cpp` | The option tables both menus read (labels byte for byte), the choice / stepper arithmetic with a value settings.ini holds that no list has (`with_foreign`, `step_value`) |
| `src/game/menu_ops.hpp/.cpp` | The one apply layer: each op is the code the PC menu ran inline plus `settings_changed`; side effects (swap interval, display rate, renderer graphics, the Thor's style) through `ops::Effects` (`host_ops` gives the window's) |
| `src/game/f1_actions.hpp` | `F1Actions`: the story loop's actions and facts (preset, loading, language, save states, rebalancing, randomizer, player cheats, the Saves / Online texts, the cloud panel), built once in `run()`; the PC menu calls them where it ran the code before |
| `src/game/console_nav.hpp/.cpp` | Navigation without ImGui or SDL: focus areas, page stack and memory, button edges / repeat / latch, modals, touch gestures, the capture's clock |
| `src/game/console_menu.hpp/.cpp` | `ConsoleMenu`: update (once per pass), draw (top screen or the lower screen's panel), modals, touch |
| `src/game/console_pages.cpp` | One function per category and sub-page; the only place with visibility rules |
| `src/game/console_glyphs.hpp/.cpp` | The hint bar's button pictures (`console_face`), their own texture cache (`draw_prompt`), `back_toggles_menu` |
| `src/game/menu_script.hpp/.cpp`, `menu_script_parse.cpp` | The menu script and its virtual SDL gamepad |
| `tests/menu_ops_tests.cpp`, `console_nav_tests.cpp`, `console_glyph_tests.cpp` | Unit tests |

## Activation

The style is fixed when F1 opens and stays until it closes:

- Android: the console menu, except a Data Overflow run (its own F1,
  `overflow_host.cpp`, unchanged), an opening where Developer > *Classic menu*
  was chosen, or a run with `--env DW4_MENU_STYLE=classic`.
- PC: only a test run with `DW4_MENU_STYLE=console`.

`HostView::console_menu` carries it (host_event needs it). While the console
menu shows, ImGui's keyboard / gamepad navigation is off in both contexts:
the menu reads player 1's `MenuKeys` itself (`player1_menu_keys`, once per
pass) and the keys `host_event` buffers (`HostView::console_keys`).

## Categories

Saves, General, Display, Graphics, Controls, Cheats, Achievements, Online,
and Developer (only with `DW4_MENU_DEVELOPER=1`). Left out on Android: the
PC's window functions (fullscreen, window size), Discord, the keyboard
bindings, Wii U Pro support, the GameCube adapter, Dump textures, Open
folder, the F5 / F9 texts, Data Overflow's switches, and *Import a GameCube
save* (`load_import` reads with `std::ifstream`; the `content://` URI of
Android's file dialog is not readable that way yet). Developer > *Classic
menu* shows the PC's F1 menu for one opening.

| Category | Rows |
|---|---|
| Saves | 4 save states as cells *Save* / *Load* (an occupied slot's Save and every Load ask first; a loaded state closes F1), *Where the save lives ›*, *Cloud backup ›* (account, password on the system keyboard, *Remember me*, *Log in*; logged in: *Refresh*, the latest backup, *Download into my save* (confirm), *Earlier revisions ›* with *Download* / *Make latest* (confirm), *Upload this save* (confirm), *Log out*, *Forget this login* (confirm)) |
| General | Language, Preset (Original / Modernized after a confirm; Custom only shown), About the presets, Loading, Volume, Name tags, Name tag size, HP / MP numbers, Pause while the menu is open, Pause in the background, Skip intro, *Mods ›*, *Files ›* |
| Display | Menu size, Widescreen, Frame limit (Off / 30 / 60 / 120 fps), Smooth motion, Anti-aliasing, Lower screen (Automatic / Off), the second screen's state, Panels over the game, *Advanced ›* (VSync, *Performance ›*) |
| Graphics | Texture quality, the pack's state, *Pack warnings ›*, Texture filtering, Sharpening, Shadow quality, Texture memory, *Advanced ›* (Mip LOD bias, Texture pack folder, Reload texture pack) |
| Controls | The players' controllers, Button prompts with a preview, AYN Thor controller style, Nintendo layout, Menu buttons, GameCube controller (with a GameCube pad), *Buttons / Main stick / D-pad / C-stick ›* (three slots per input: A or a tap captures, X or a long press clears), Default controller layout (confirm) |
| Cheats | God mode, Infinite MP, One-hit defeats (this session), EXP / damage rates (steppers 10 .. 1000 %), *Randomizer ›* (Seed on the number keyboard, New seed, Item drops, Enemies, Loading zones, the save's bond), *Player N ›* (refill, levels, bits) |
| Achievements | Achievements, Unlock notices, the count and play time, *About achievements*, one link per category (entries with the date or the progress) |
| Online | Network indicator, the direct session's or room's lines (*Leave the session*, *Go offline* ask first), "Online play on Android comes in a later version." while only the lobby exists, *Connection ›* |

## Controls

| Input | Sidebar | List | Picker | Confirm | Text sheet | Capture |
|---|---|---|---|---|---|---|
| Up / Down | category | row (sections and plain infos skipped; wraps only on a fresh press) | entry | - | - | bound |
| Left | - | value - / cell -; other rows: to the sidebar | - | button left | - | bound |
| Right | to the list | value + / cell +; a link opens | - | button right | - | bound |
| A | to the list | the row's action | choose | answer (250 ms after it opened, after a release) | Done | bound |
| B | close F1 | up a page; at a root: the sidebar | cancel | cancel | cancel | bound |
| X | - | a binding's slot cleared | - | - | - | bound |
| L1 / R1 | category (wraps) | category (its root, its remembered row) | - | - | - | bound |
| L2 / R2 | - | a page of rows (menu_buttons=layout) | a page | - | - | bound |
| Start | close F1 | close F1 | choose | - | Done | bound |
| Back key | close F1 | close F1 | close F1 | close F1 | the keyboard first, then F1 | the capture only |

Directions repeat after 350 ms, then every 80 ms; a stepper held 1.5 s steps
five at a time. Buttons held when F1, a modal or a page opens (or a capture
ends) count only after they were let go. In touch mode the first direction,
A or X only shows the cursor. Keyboard: arrows, Enter / Space (A), Escape /
Backspace (B), PageUp / PageDown (L1 / R1), Home / End, Delete (X).

The menu opens where it was last (category, page stack, row; only in
memory); the first opening in a process shows the sidebar on Saves. Every
close by the console menu, and every close of a console menu by Back, the
touch button or F1, latches the game's pad (`pad_latch`).

On the AYN Thor on Android the Back key opens and closes F1, and SELECT
stays a game button (`back_toggles_menu`: false only for Android and the
Thor's family); `{MENU}` shows the Back key there (`Face::back_key`, a
stand-in picture until the owner confirms the Thor's printing). Other pads
and the PC are unchanged.

## The lower screen

`Screen2Menu::own_chrome`: the Menu panel shows only the console menu (its
own header with *Close menu*, no panel scrollbar or drag). Navigation is off
in the lower context; the menu takes the pad itself. The lower screen draws
every pass while the menu animates (a drag or fling, the capture's bar, a
press, a lock reason's blink, the Performance page), else every other pass
as before.

## Text input

A text, password or number row opens the text sheet (`##console_text`, an
ImGui window without NoNav) with one field; `SetKeyboardFocusHere` makes it
active in its first frame (logged as `menu: edit <row> active (frame 2)`),
so ImGui's SDL backend starts SDL's text input: Android's system keyboard
(a password field's without suggestions as before; a number field's with
`SDL_TEXTINPUT_TYPE_NUMBER`). Done, Enter, A or Start take the text; Cancel,
B or Escape keep the old value. Nothing typed is logged.

## Test switches (test runs; Android: `--env` with files/dw4args.allow)

| Switch | Effect |
|---|---|
| `DW4_MENU_EXPAND=1` | the PC F1 menu's headers and tree nodes open |
| `DW4_MENU_STILL=1` | the PC F1 menu's live values fixed |
| `DW4_MENU_SCROLL=page:<n>` or `<px>` | the PC F1 window scrolled there ("menu-scroll: page n of m") |
| `DW4_MENU_STYLE=console\|classic` | the style |
| `DW4_MENU_PLATFORM=android` | Android's rules on the PC (the Back key, Select, the CLOSE picture, the lower screen's rows; a fake display 4) |
| `DW4_MENU_PAGE=<cat>[/<page>...][#<row>]` | the first page and row |
| `DW4_MENU_DIALOG=picker:<row>\|confirm:<row>\|text:<row>\|capture:<row>` | a modal for a picture |
| `DW4_MENU_INPUT=pad\|touch` | the input mode |
| `DW4_MENU_PAD=thor\|xbox_one\|xbox360\|ps5\|ps4\|switch_pro\|generic` | the virtual pad (also without a script) |
| `DW4_MENU_LOWER=1` | the PC window stands for the lower screen's Menu panel |
| `DW4_MENU_DEVELOPER=1` | the Developer category |
| `DW4_MENU_TRACE=1` | `menu: focus ...`, `menu: game pad after close: ...` |
| `DW4_MENU_LAYOUT_LOG=1` | `menu-layout:` rows, `menu-hit:` touch targets, `menu-hint:` the hint bar |
| `--menu-script FILE`, `DW4_MENU_SCRIPT=FILE` | the menu script (menu_script.hpp) |

Log lines: `menu: <Category> > <Row> = <Value>`, `menu: <Category> > <Row>:
<action>[ (confirmed)]`, `menu: page <path>`, `menu: dialog <kind> '<title>' ->
<answer>`, `menu: capture ...`, `menu: edit <row> start|active|commit|cancel`.

## Checks (14.11.2026)

- P0: the PC F1 menu (8 tabs, Settings at Graphics and Second screen; 100 %
  1280x960 Xbox and 231 % 1920x1080 Thor) identical to feat-android-cux
  059ea201 outside a noise mask (the live frame time graph) after every
  commit; P1: with EXPAND / STILL / SCROLL every page of every tab bytewise
  identical to the K0 baseline (50 pictures).
- D: menu scripts on the PC (`work/f1/scripts`): every root page's rows by the
  pad, settings checked in settings.ini's form; Back / Select / the capture
  (A, analog and digital L2 / R2, Select bindable; cancelled by Back, by the
  deadline, by touch; cleared by X and a long press); save / load states with
  their confirms; the text sheet (account, password, a number with its
  range); the preset's confirm; touch (categories, toggles, arrows, a picker,
  a drag, a fling, Close); the lower screen's panel by touch and pad.
- C: pictures of every category and sub-page in pad and touch mode, every
  modal, the lower screen, 100 % / 300 % and Xbox prompts, with a layout
  check of the touch targets (`work/f1/layout_check.py`).
