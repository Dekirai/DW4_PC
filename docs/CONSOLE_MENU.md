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
| `tools/menu/` | The PC checks: `run.sh` (one menu script, the D scripts, a picture), `scripts/` (the menu scripts), `input/` (their stage input), `menu_layout_check.py` (C) |

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
| Saves | 4 save states as cells *Save* / *Load*. Saving refused for every slot (the title, before the first stage) shows its reason once above the slots and locks only the *Save* cells; *Load* works there too (locked only online or on an empty slot). An occupied slot's Save and every Load ask first; a load shows the card "Loading state N..." for a pass (the replay holds the game's thread for seconds), then runs and closes F1. Saves opens on the state slot used last. *Where the save lives ›* (Android: the cards in the player's words; the files, the store's choice and the Dolphin note folded under *Technical details*), *Cloud backup ›* (account (a plain field: no suggestions, autocorrection or capitals), password on the system keyboard, *Remember me* (not on Android: it keeps no login, the token store is Windows' DPAPI), *Log in*; logged in: *Refresh*, the latest backup, *Download into my save* (confirm), *Earlier revisions ›* with *Download* / *Make latest* (confirm), *Upload this save* (confirm), *Log out*, *Forget this login* (confirm; not on Android); a request's answer also as a notice). With the PC's rules "Files: <the state folder>" stands here; Android has it under General > Files |
| General | Language, Preset (Original / Modernized after a confirm; Custom only shown, both arrows lit from it), About the presets, Loading, Volume, Name tags, Name tag size, HP / MP numbers, Pause while the menu is open, Pause in the background, Skip intro, *Mods ›*, *Files ›* (paths wider than a line broken after a `/` or `\`) |
| Display | Menu size, Widescreen, Frame limit (Off / 30 / 60 / 120 fps), Smooth motion, Anti-aliasing (Android, after a change: "Applies after you close and reopen the app."), Lower screen (Automatic / Off), the second screen's state, Panels over the game, *Advanced ›* (VSync, *Performance ›*: the frame times as a line graph, taken at 2 Hz) |
| Graphics | Texture quality, the pack's state, *Pack warnings ›*, Texture filtering, Sharpening, Shadow quality, Texture memory, *Advanced ›* (Mip LOD bias, Texture pack folder (Android: where packs go, as a line; no path field), Reload texture pack) |
| Controls | The players' controllers, Button prompts with a preview (each GameCube control with its picture: "L {L2}"), AYN Thor controller style (as on the PC: with a Thor's pad or a chosen style), Nintendo layout, Menu buttons (the menus' Confirm / Back pictures below), GameCube controller (with a GameCube pad), *Buttons / Main stick / D-pad / C-stick ›* (three slots per input: A or a tap captures, X ({DEL}) or a long press clears), Default controller layout (confirm) |
| Cheats | God mode, Infinite MP, One-hit defeats (this session), EXP / damage rates (steppers 10 .. 1000 %), *Randomizer ›* (Seed on the number keyboard, New seed, Item drops, Enemies, Loading zones, the save's bond: locked online, as the PC's buttons are disabled), *Player N ›* (refill, levels, bits) |
| Achievements | Achievements, Unlock notices, the count and play time, *About achievements*, one link per category (entries with the date or the progress) |
| Online | Network indicator, the direct session's or room's lines (*Leave the session*, *Go offline* ask first; a room comes before the lobby's line), "Online play on Android comes with a later update." (the online gate's text, `kAndroidRefusal`) while only the lobby exists, *Connection ›* |

## Controls

| Input | Sidebar | List | Picker | Confirm | Text sheet | Capture |
|---|---|---|---|---|---|---|
| Up / Down | category | row (sections and plain infos skipped; wraps only on a fresh press) | entry | - | - | bound |
| Left | - | value - / cell -; other rows: to the sidebar | - | button left | - | bound |
| Right | to the list | value + / cell +; a link opens | - | button right | - | bound |
| A | to the list | the row's action | choose | answer (250 ms after it opened, after a release) | Done | bound |
| B | close F1 | up a page; at a root: the sidebar | cancel | cancel | cancel | bound |
| X | - | a binding's slot cleared | - | - | - | bound |
| Y held | - | the top screen shows the game instead of the menu (a look at a Graphics / Display change; "{Y} Look" on their value rows) | - | - | - | bound |
| L1 / R1 | category (wraps) | category (its root, its remembered row) | - | - | - | bound |
| L2 / R2 | - | a page of rows (menu_buttons=layout) | a page | - | - | bound |
| Start | close F1 | close F1 | choose | - | Done | bound |
| Back key | close F1 | close F1 | close F1 | close F1 | the keyboard first, then F1 | the capture only |

Directions repeat after 350 ms, then every 80 ms; a stepper held 1.5 s steps
five at a time. Buttons held when F1, a modal or a page opens (or a capture
ends) count only after they were let go. In touch mode the first direction,
A or X only shows the cursor. Keyboard: arrows, Enter / Space (A), Escape /
Backspace (B), PageUp / PageDown (L1 / R1), Home / End, Delete (X); the
system's key repeat is no press (held arrows repeat as the D-pad does).

The sidebar scrolls to the chosen category (changed by a button, the menu
opened, a new menu size), with a scroll bar when its entries do not fit (a
large menu size, a ninth category).

Touch: the hint bar keeps its room in touch mode too (its right side
dimmed), so nothing moves when a tap follows a button; a tap counts when the
finger lets go on the target it went down on (as it was then); a drag or a
fling starts anywhere in a column (also on a section title, an info line or
empty space); a choice's value zone is as wide as its widest option, so its
arrows stay under the finger while it steps. A press counts only on the
console's own window (the lower screen: its panel), and the panels' window
over the game is not drawn over a console menu on the same screen.

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
own header with *Close menu*, no panel scrollbar or drag; on a sub-page the
header shows only the page's title, shortened with "...", without the
chip). *Show it here* (the menu is open on the top screen) is a full-width
button at least 48 u high there. Moved between the screens, the menu brings
the focus row into view on the new one. Navigation is off
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
`SDL_TEXTINPUT_TYPE_NUMBER`; a plain field's (`Row::plain`: the cloud
account, a path) without suggestions, autocorrection or capitals:
`set_plain_text_field`, Android's `TYPE_TEXT_VARIATION_VISIBLE_PASSWORD`
with `TYPE_TEXT_FLAG_NO_SUGGESTIONS`: Gboard ignores the flag alone). Done,
Enter, A or Start take the text; Cancel, B or Escape keep the old value.
After Done the focus goes to the row's next one (the password's: *Log in*).
Nothing typed is logged.

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
| `DW4_MENU_TRACE=1` | `menu: focus ...`, `menu: scroll <page> <u>` (where the list settled), `menu: touch down` / `up ...`, `menu: game pad after close: ...` |
| `DW4_MENU_LAYOUT_LOG=1` | `menu-layout:` rows, `menu-hit:` touch targets, `menu-hint:` the hint bar |
| `--menu-script FILE`, `DW4_MENU_SCRIPT=FILE` | the menu script (menu_script.hpp); on the PC only with `--smoke-test` |

Log lines: `menu: <Category> > <Row> = <Value>`, `menu: <Category> > <Row>:
<action>[ (confirmed)]`, `menu: page <path>`, `menu: dialog <kind> '<title>' ->
<answer>`, `menu: capture ...`, `menu: edit <row> start|active|commit|cancel`.

## Checks

The PC checks run hidden, at low priority, each run with its own card
folder: `tools/menu/run.sh` (the menu scripts in `tools/menu/scripts`, the
stage input in `tools/menu/input`; `DW4_DATA` names the game data) and
`tools/menu/menu_layout_check.py`.

- P0, the PC F1 menu unchanged: hidden pictures of its 8 tabs plus Settings
  at Graphics and at Second screen, at 100 % 1280x960 with Xbox prompts and
  at 231 % 1920x1080 with the Thor's (20 pictures), identical to the base
  outside a noise mask (the live frame time graph's box). Taken at K0, K1,
  K5a (against feat-android-cux 059ea201), at the merged head (against
  feat-android-cux 1b6cf72d) and at the review fixes (against
  feat-android-cux 0216d9db, whose code is 1b6cf72d's); not after every
  commit.
- P1, the PC F1 menu complete: the same tabs with `DW4_MENU_EXPAND`,
  `DW4_MENU_STILL` and `DW4_MENU_SCROLL=page:0..m` (50 pictures), bytewise
  identical to the K0 hooks' baseline: at K1, K5a, the merged head and the
  review fixes.
- Unit tests (ctest), `det.sh` twice (the determinism reference) and the
  save state suite at the merged head and at the review fixes.
- D, the menu scripts (`run.sh d`): d1 every root page's rows by the pad,
  settings checked in settings.ini's form; d2 the keyboard keys, Back /
  Select, the capture (A, analog and digital L2 / R2, Select bindable;
  cancelled by Back, by the deadline, by touch; cleared by X and a long
  press); d3 save / load states with their confirms, the text sheet
  (account, password, a number with its range); d4 / d5 touch (categories,
  toggles, arrows, a picker, a drag, a fling, Close; a slot captured and
  cleared by touch); d6 the lower screen's panel by touch and pad; d7 the
  review's fixes in a game (Save refused once on the title while Load is
  not, Saves' home row, the load's card, Load on the title after a soft
  reset, Back while a capture waits for its button, Y's look); d8 touch
  after a button (the first tap counts), a value zone that stays put, a drag
  that starts on a section title.
- C: pictures of every category and sub-page in pad and touch mode, every
  modal, the lower screen, 100 % / 300 % and Xbox prompts, the Developer
  category, with the layout check of the touch targets.
- On the emulator (`dw4thor33`, headless, its second display 1240x1080 at
  369 dpi): `e1_top` (the virtual Thor pad; Select does nothing, Back
  closes and opens, the captures of L2 and the Thor's digital R2, Back
  cancels a capture, the account (a plain field) and the password on the
  system keyboard) and `e2_lower` (the lower screen by its touch queue and
  the pad) without a failed expect; the lower screen at rest with the
  console menu open there (no virtual pad, `DW4_FRAME_STATS`): 30 of its
  turns a second, none drawn or swapped. The AYN Thor (the owner's files backed up and
  restored byte for byte): `t4_fixes` (the virtual Thor pad, the top and
  the lower screen) without a failed expect; real taps by adb: the first tap
  after a button counts, the language's arrows stay under the finger, a
  drag from a section title scrolls, *Show it here*, the move and a drag on
  the lower screen, Back closes; Gboard on the account field (top and lower
  screen): input type 0xa0091, no suggestions, no capitals, "uxtest" stays
  "uxtest".

Not covered: the logged-in part of the cloud backup page (no cloud server
for test runs: the request flow is the panel's own, the page only calls
it); the layout rule "no text leaves its row" (no automatic check; paths
are broken after their separators).

## Open

- `CloudView`-style unit tests of the cloud page's flow (spec K2) are not
  written; the PC's panel was left as it was (`view()` / `request_*` beside
  `draw()`).
- *Log in as <name>* (a remembered login): not on Android, which keeps no
  login; on the PC an empty password uses the remembered one, as the
  classic panel does.
- The PC tabs and the console menu still have their own copies of a few
  texts (the Saves summary, the Online connection lines); the predicates
  (whether F1 pauses, whether the cloud backup is available, the
  randomizer's bond) are shared.
- Import a GameCube save on Android (`load_import` reads with
  `std::ifstream`, not a `content://` URI).
- The Back key's picture (`Face::back_key`) stays a stand-in until the
  owner confirms the Thor's printing.
