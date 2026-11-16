# Player guide

Everything about playing Digimon World 4 PC in detail. For a quick start
(download, first start, the most common questions) see the
[README](../README.md).

The release's program is `DW4_PC.exe`. A build made from the source code
calls the same program `dw4_game.exe`; the command lines below use
`DW4_PC.exe`.

## Contents

- [Game data](#game-data)
- [Files next to the program](#files-next-to-the-program)
- [The F1 menu](#the-f1-menu)
- [Display and graphics](#display-and-graphics)
- [Fast loading](#fast-loading)
- [Controls](#controls)
- [Controllers and button pictures](#controllers-and-button-pictures)
- [Saves](#saves)
- [Randomizer, rebalancing and cheats](#randomizer-rebalancing-and-cheats)
- [Achievements](#achievements)
- [Name tags](#name-tags)
- [Discord Rich Presence](#discord-rich-presence)
- [Online play](#online-play)
- [LAN and direct play](#lan-and-direct-play)
- [Data Overflow (survival mode)](#data-overflow-survival-mode)
- [Content mods](#content-mods)
- [Texture packs](#texture-packs)
- [Command line](#command-line)
- [settings.ini reference](#settingsini-reference)

## Game data

The game reads the USA disc (`GDJEB2`, revision 0) straight from an
uncompressed disc image (`.iso` / `.gcm`) or from a Dolphin extraction
(*Properties > Filesystem > Extract Entire Disc*). Other discs are refused
("This disc is ..., not Digimon World 4 USA (GDJEB2, revision 0)").

- **First start:** a dialog asks for a *Disc image...* or an *Extracted
  folder...*, checks the choice and keeps it in `settings.ini`. A wrong choice
  shows the reason and asks again; *Cancel* quits.
- **RVZ:** Dolphin's RVZ images (Zstandard or uncompressed, GameCube only) are
  converted once into `data/<name>.iso` next to the program (a few seconds,
  bit-identical to the original disc; a progress window shows it, closing it
  cancels). The game then uses that ISO. An RVZ with bzip2 or LZMA
  compression has to be converted again in Dolphin, with Zstandard (the
  default) or to ISO. `DW4_PC.exe --convert-rvz IN.rvz OUT.iso` does the
  conversion from the command line.
- **WIA, CISO and NKit** images cannot be read; convert them to ISO or RVZ in
  Dolphin (right-click the game > *Convert File...*).
- **Changing it later:** the choice is `[data] path` in `settings.ini`;
  `--data PATH` overrides it for one run. When the stored path no longer
  works, the game shows the problem and closes: correct the line or delete it,
  and the next start asks again.

```ini
[data]
path=C:\Games\Digimon World 4 (USA).iso
```

- **Checking your copy:** `tools/verify_game.py` (in the release folder and
  in the source code; Python 3.10 or newer) checks a Dolphin extraction
  against the expected `main.dol` SHA-1 `e409a88a7379ed1a536f93b0a303a0ce7cd5d877` without copying
  data: `python tools/verify_game.py <extraction folder> [--iso <image>]`
  (`--iso` also compares an uncompressed ISO byte for byte).

## Files next to the program

The game is portable: everything it writes is next to the program.

| File or folder | Content |
| --- | --- |
| `settings.ini` | All settings (see the [reference](#settingsini-reference)). The game reads `local\settings.ini` or `settings.ini` in the working folder if one exists, else the one next to the program, and creates it there. F1 > Settings shows which file is used. |
| `saves\savedata.dat`, `savedata_b.dat` | The memory cards in slots A and B (`.bak`: the previous version) |
| `saves\backups\` | Copies of the save made before an import or a cloud download |
| `saves\states\` | Save states (`state<N>.dw4state`) |
| `saves\profile.dat` | Achievements |
| `saves\overflow.dat` | Data Overflow's progress |
| `saves\randomizer.ini` | The randomizer bound to each saved character, with a spoiler file per character |
| `saves\online\` | Remembered online logins (protected for your Windows account; no password is stored) |
| `saves\online.log`, `saves\controllers.log` | Logs to attach to a bug report (see [Reporting problems](../README.md#reporting-problems)) |
| `data\` | Disc images converted from RVZ |
| `mods\` | Content mods (`mods\<id>`), texture packs (`mods\textures`) and texture dumps (`mods\dump\textures`) |
| `lang\` | Language packs (`lang\<code>\`) |

The console window that opens with the game prints what it found (lines such
as `saves: ...`, `mod: ...`, `language: ...`, `controllers: ...`) and the
reason when something stops the game.

## The F1 menu

**F1** (or a controller's View, Back, Share, Create or − button, unless a
binding uses it) opens the menu over the game; settings are saved when it
closes. While it is open the game pauses (*Pause the game while this menu is
open*, `[menu] pause`; online nothing pauses).

| Tab | What is in it |
| --- | --- |
| **Settings** | Language, the *Original* / *Modernized* preset, loading speed (Original / Fast), display (fullscreen, widescreen, VSync, frame limit, smooth motion, anti-aliasing, window size), graphics, volume, name tags, Discord, achievements, the online status box, save states (four slots), mods, skip intro, and where the save and the settings file are |
| **Controls** | The detected controllers, the button pictures, the Nintendo and GameCube layouts, keyboard and controller bindings |
| **Gameplay** | *Show the simulator NPC* and *Data Overflow (test)* (see [Data Overflow](#data-overflow-survival-mode)) |
| **Saves** | Where the save lives, the boxes of slots A and B, importing GameCube saves, the cloud backup |
| **Online** | The online session: instance, host, members, *Leave instance* |
| **Cheats** | Session cheats (god mode, infinite MP, one-hit defeats, refills, levels and Bits per player; off by default and never saved), plus the saved rebalancing and randomizer options |
| **Achievements** | The 72 achievements |
| **Debug** | Stage loader, player state, notes, script log |

Other keys: **F11** fullscreen on / off, **F2** a free camera (turned with the
right mouse button), **F5** / **F9** save / load save state 1.

**Presets.** *Original* is the GameCube: 4:3, 60 fps motion, no anti-aliasing,
the game's own textures and shadows, the intro, GameCube load times and the
original balance. *Modernized* sets widescreen, smooth motion, 4x
anti-aliasing, skip intro, fast loading and the Modernized graphics (a
texture pack's 4x textures, 16x anisotropic filtering, the High shadow map).
Anything else shows as *Custom*.

**Language.** F1 > Settings > *Language* lists English and every language pack
found in `lang\<code>\` next to the program, each in its own language. The
choice applies at once. A pack changes the game's own text (and some
pictures); the port's menus stay English. A pack without its text catalog
says "(pictures only, no game.tsv)". The release comes with the German pack
in this form, *Deutsch (Fan-Übersetzung)*: it shows the German originals of
two pictures from your disc (the status labels and BITS), the game text stays
English. Players with different languages can play together. Without packs
the list is greyed out. Details: [L10N.md](L10N.md).

## Display and graphics

**Display** (F1 > Settings; `[window]` in settings.ini):

- *Fullscreen* is borderless (also **F11**).
- *Widescreen*: the 3D view fills wide windows at the original vertical field
  of view while the 2D layers stay 4:3 and full-screen fades stretch.
- *VSync*, *Frame limit* (presented frames per second, 0 = off; the game
  always updates 60 times a second).
- *Smooth motion*: above about 75 presents per second the picture blends
  between the last two updates: objects, skinned characters, UV scrolls, the
  camera and the shadow light. Each placement of a shared object blends on its
  own; a looping or stepped animation that jumps back or steps (and an object
  that turns or moves too far in one update) is drawn as it is in that update,
  UV scroll included, instead of being blended backwards, and the picture also
  keeps what the previous update's camera still saw at its edges. None of it
  changes the game.
- *Anti-aliasing* (MSAA 2x / 4x / 8x) applies at the next start. If the
  driver refuses it, the window opens without it.
- *Window size*: 1280x960, 1440x1080, 1920x1440, 1280x720, 1920x1080,
  2560x1440, or drag the window; the size is remembered.

**Graphics options** (F1 > Settings > Graphics, `[graphics]`; every change
applies at once and nothing reaches the game itself, so online partners may
differ):

- *Texture quality*: Original (the game's own textures), High (a texture
  pack's 2x level) or Ultra (its 4x level, the default; without a pack both
  show the game's textures). It changes the running stage too: the pack's
  textures come in within a few frames.
- *Texture filtering*: Bilinear (the GameCube's sampling, the default),
  Trilinear (mipmaps for the game's textures: less shimmer on distant floors)
  or Anisotropic 2x to 16x (slanted surfaces stay sharp).
- *Mip LOD bias* (-2 .. 2) for mipmapped textures.
- *Sharpening*: Off, Low or High, a contrast adaptive sharpening pass over the
  game picture (not the menus).
- *Shadow quality*: Original (the game's 128 x 128 shadow map), High (4x) or
  Ultra (8x the texels per side: sharper character shadows).
- *Texture memory*: 512 MiB to 4 GiB for textures; past it the least recently
  drawn ones make room.

The *Original* preset sets all of them to the GameCube's look; *Modernized*
picks Ultra, Anisotropic 16x and the High shadow map. A settings.ini from
before these options keeps the preset it showed (Original or Modernized
graphics).

## Fast loading

The game loads a stage the way the original does: every file comes off a
simulated GameCube disc drive in 64-sector reads, the loading thread spends
the original's time on each, the outgoing stage's teardown and the
registration of the new one take the updates they took on the GameCube, the
loading screen plays its minimum time, and the music is ready only once its
first blocks came off the disc. A stage change therefore takes 400-900 frames
(7-15 seconds) like the original.

*Loading: Fast* in the F1 menu's Settings tab (also part of the Modernized
preset; `[gameplay] loading=fast`, `--loading fast` for one run) is a
deliberate deviation from that timing: the loader runs the same states in the
same order and the scripts run as always, but no file waits for the drive,
none of the GameCube's teardown / routing / registration time is spent, the
loading screen goes as soon as the files are in, and the sound driver's disc
reads end at its next update, so the stage's music is ready a few frames after
it starts. A stage change then takes about 16 frames (a quarter of a second)
plus the stage's own fades and opening scenes, which stay as they are. Large
files are parsed one per frame, smaller ones are grouped up to 1 MiB per
frame, so no frame takes much longer than with the original timing; a Fast
stage change is over in about a third of a second of real time.

- A change applies from the next stage change on.
- The option changes the frames a session plays, so save states keep it (a
  state replays with the timing it was made with; afterwards the current
  choice applies), and online sessions use the host's choice on every machine.
  *Original* stays frame-identical to the original timing.
- Memory card access keeps its original speed: it takes well under two
  seconds per save and the save room's messages follow it.

| Stage change (frames from the change to the stage start; 60 frames = 1 s) | Original | Fast |
| --- | --- | --- |
| Boot: logos / opening / title (`stagIS08` / `09` / `10`) | 234 / 62 / 118 | 15 / 15 / 15 |
| Boot straight to the title (skip intro) | 218 | 15 |
| Dungeon `dng101b1` (to player control) | 490 (550) | 16 (76) |
| Field `stag0101` / `stag0204` | 769 / 797 | 16 / 16 |
| Quest `dngQ1B1e` (to player control) | 575 (635) | 16 (76) |
| Boss `stagBS01` | 262 | 16 |
| Lobby `stagML11` | 516 | 15 |

## Controls

| Original | Keyboard (default, rebindable) | Gamepad (default, rebindable) |
| --- | --- | --- |
| Main stick (walk, menus) | W / A / S / D | Left stick |
| D-pad (menus, quick panel cursor) | Arrow keys | D-pad |
| C-stick (quick panel cursor) | Keypad 8 / 4 / 2 / 6 | Right stick |
| A (talk, confirm, use the panel's line) | Enter, Space, K | South (Nintendo pads: the A button) |
| B (cancel) | Backspace, L | East (Nintendo pads: the B button) |
| X (jump) | J | West (Nintendo pads: the X button) |
| Y | I | North (Nintendo pads: the Y button) |
| START | Esc, Tab | Start |
| L / R (disk / technique panel) | Q / E | Left / right trigger (L also on the left shoulder) |
| Z | Z | Right shoulder |

As on the GameCube the three direction inputs are separate: the main stick
walks, the quick panels (R techniques, L disks) move their cursor with the
D-pad or the C-stick only, so walking with a panel open never picks another
line, and menus take the main stick and the D-pad. With the default keys the
left hand walks with WASD and opens the panels with Q / E, the right hand
picks the line with the arrow keys and Space or Enter uses it.

**Bindings.** F1 > Controls edits the keyboard and controller bindings, up to
three inputs per control. In settings.ini, `[keyboard]` holds up to three key
names per control (`a=Return, Space, K`) and `[gamepad]` up to three SDL
controller inputs per control (`l=lefttrigger, leftshoulder`). The controls
are `a`, `b`, `x`, `y`, `start`, `l`, `r`, `z`, `stick_up` / `stick_down` /
`stick_left` / `stick_right` (the main stick), `dpad_up` ... `dpad_right` (the
D-pad) and `cstick_up` ... `cstick_right` (the C-stick); a controller's left
and right stick are always the main stick and the C-stick.

**Older settings files.** Files from before the separate D-pad and C-stick
(one `up` / `down` / `left` / `right` input that set the D-pad bit and walked)
are migrated when they are read and saved with the new names: the keyboard's
keys go to the main stick except the arrow keys, which become the D-pad (so
the old default `W, Up` becomes WASD walking and arrow-key D-pad); a
direction bound to arrow keys only keeps them for walking and leaves the D-pad
without keys (bind them in the F1 menu; the keypad C-stick still moves the
quick panel cursor). The controller's old inputs (default the D-pad buttons)
become the D-pad, which no longer walks, as on the GameCube (bind D-pad
buttons to `stick_*` to walk with them). A default key that the file uses for
another control is left out.

**Players.** Player 1 uses the keyboard and the first gamepad; the second to
fourth gamepads control players 2 to 4. As on the original, holding B and X
and then START for a second on player 1's controller returns to the title
(outside the logo, opening, title and ending stages). Holding B, X and START
together on any controller for more than three seconds presses the console's
reset button, as the GameCube controller does: the game reboots with the
license screen, the memory card check, the logos and the opening (memory
cards, mods and PC options stay).

## Controllers and button pictures

The game recognizes the controller in use and draws every button picture for
it, in the game's own texts (the GameCube button symbols, the word START, the
title's PRESS START) and in the port's menus and notices. The pictures follow
the last device player 1 used (keyboard or controller) from the next frame on;
in the character select every player's own lines show his own controller's
pictures. F1 > Controls > *Button prompts* can fix one family instead. Each
picture shows the button bound to that game button, so rebinding changes it.
All pictures are drawn by the program itself (no console logos).

| Controller | How it connects on Windows |
| --- | --- |
| DualSense (PS5), DualShock 4 (PS4) | USB or Bluetooth, nothing to install |
| Xbox 360, Xbox One, Xbox Series | USB, Bluetooth or the Xbox wireless adapter (XInput) |
| Switch Pro Controller | USB or Bluetooth. Read by the letters on its buttons (A is A) unless F1 > Controls > *Nintendo layout* says by position |
| Wii U Pro Controller | Bluetooth: Windows Settings > Bluetooth > Add device, press the red SYNC button, no PIN. *Wii U Pro support* (F1 > Controls) is automatic: SDL's Wii driver runs only while such a controller (057e:0330) is connected, so a Wii remote bar such as Mayflash's DolphinBar never reaches it; Wii remotes never take a player's place |
| Original GameCube controller, adapter in PC mode (Mayflash, EVORETRO) | Set the adapter's switch to PC; no driver. Every port is its own controller |
| Original GameCube controller, adapter in Wii U mode (Nintendo's WUP-028, Mayflash on Wii U) | Install the WinUSB driver on "WUP-028" with Zadig, as for Dolphin; the game finds the adapter when it is plugged in and every connected port is a controller. It lets go of the adapter while the game is in the background (Dolphin can use it then); F1 > Controls says when the driver is missing or another program holds it |
| Switch 2 Pro Controller | USB-C cable only (it does not play over Bluetooth). Nothing to install: Windows runs it with its own drivers (its HID driver for the buttons, the WinUSB driver Windows loads by itself on the part through which the game starts it). Read like the Switch Pro (by its labels unless F1 > Controls > *Nintendo layout* says by position); ZL and ZR are digital on this controller; C, GL and GR do nothing until bound in F1 > Controls. Steps below |

**Switch 2 Pro Controller, step by step.**

1. Connect the controller with a USB-C data cable (a charge-only cable shows
   nothing) and start the game, or plug it in while the game runs; it becomes
   a player's controller with the Switch 2 Pro pictures within a second or
   two, and its player lights show the player. F1 > Controls shows a line for
   it: *playing, 250 reports/s* when it works.
2. If F1 > Controls instead says what is wrong, do that:
   - *another program is using it*: Steam with its Switch controller support,
     an emulator, a controller tool or a second copy of the game holds it (the
     part through which the game starts it, or its buttons for itself): close
     that program (the game tries again every two seconds).
   - *its input device cannot be opened* or *a driver installed on it is in
     the way*: a driver was put on the controller by hand (Zadig: WinUSB or
     libusbK on one of its parts): Device Manager > View > Devices by
     connection, find the controller, uninstall that part with *Attempt to
     remove the driver for this device* ticked and plug the controller in
     again; Windows sets its own drivers up again.
   - *Windows has no driver on its interface 1*: unplug it and plug it in
     again or use another USB port (only if that does not help:
     [Zadig](https://zadig.akeo.ie), Options > List All Devices, the entry
     whose USB ID reads `057E 2069 01`, WinUSB; never the entry ending in
     `00`: that part must keep Windows' own HID driver, the game reads the
     buttons through it).
   - *it did not start*: unplug it and plug it in again, and attach the log
     to a bug report if it stays.
3. Over Bluetooth it does not play; Windows does not list it as a controller
   then, so the game may say nothing (it says *connect it with a USB cable*
   only when Windows lists it as a Bluetooth input device).

The game reads this controller itself, the way Steam does on Windows; it keeps
reading while the game is in the background (the game ignores its buttons
there, as every controller's). If Windows drops the connection for a moment
without the controller being unplugged (standby, a USB reset), the game opens
it again a second later and it plays on in the same player slot; only when
Windows no longer lists it does its pad go. Earlier builds shipped a
`libusb-1.0.dll` (and `libusb-1.0-COPYING.txt`) for this controller; nothing
uses them any more, they can be deleted.

**Logs.** `saves\controllers.log` beside the game records each start's
controllers, what Windows lists and every step of the Switch 2 Pro
Controller's start (attach it to a
[bug report](../README.md#reporting-problems) when a controller does not
work).

**GameCube controllers.** A GameCube controller plays 1:1 (its A is the game's
A, L and R count at the trigger's click, the C-stick is the C-stick) and the
game keeps its own GameCube pictures for it; F1 > Controls > *GameCube
controller* can put it on the bindings instead.

**Menu buttons.** The menus (F1, the online screens, the on-screen keyboard)
take the controller's own buttons: A / Cross confirms, B / Circle goes back,
whatever the game's bindings (a Nintendo pad as its Nintendo layout reads it);
`menu_buttons=bindings` makes them follow the game's A and B bindings instead.

settings.ini keeps the choices in `[gamepad]`:

| Key | Values |
| --- | --- |
| `prompts` | `auto`, `dualsense`, `dualshock4`, `xbox360`, `xbox_one`, `switch_pro`, `switch2_pro`, `wiiu_pro`, `gamecube`, `keyboard` |
| `nintendo_layout` | `label` / `position` (a file whose face buttons were rebound before keeps `position`, so those bindings play as before) |
| `gamecube_layout` | `original` / `bindings` |
| `wii_driver` | `auto` / `on` / `off` |
| `menu_buttons` | `layout` / `bindings` |
| `gc_adapter` | `true` / `false` |

## Saves

The game saves like on the GameCube: in the save room, onto the memory card
in slot A or B, four boxes per card. The PC version keeps each card's Digimon
World 4 file in one file next to the program: `saves/savedata.dat` (slot A)
and `saves/savedata_b.dat` (slot B; the slot holds a card once that file
exists, or with `[data] card_b = inserted`). The file holds exactly the bytes
of Dolphin's `B2-GDJE-NDMWSaveData.gci`, so renaming it moves a save to
Dolphin and back. Every save first keeps the previous file as
`savedata.dat.bak`, and the new file replaces the old one only once it is
completely written. The F1 menu's Saves tab and the Settings tab show where
the save lives.

- **From an older build:** a save of an older build in `saves/GC/USA/Card A`
  (or `Card B`) is copied into `savedata.dat` (`savedata_b.dat`) once at the
  first start; the old file stays where it is (`[data] save_migrated` in
  settings.ini records it). Should the copy fail, the game says so and tries
  again at the next start; a damaged old save is not copied, and the game says
  that too (a notice at the start and the F1 Saves tab).
- **Playing on Dolphin's saves:** `[data] cards = <folder>` plays from a GCI
  folder root instead (for example Dolphin's `GC/USA` folder, so both play on
  the same save). `--card-dir` does the same for one run.
- **Importing GameCube saves** (F1, Saves tab, or `--import-save`): Dolphin /
  GCMM / Swiss `.gci`, GameShark `.gcs`, MaxDrive / Action Replay `.sav`,
  memory card images `.raw` / `.gcp` (Dolphin, Nintendont, card dumps) and
  GCI folders. The preview shows each box (name, Digimon, level, Bits, story
  phase, forms); the chosen boxes go into the chosen boxes of slot A or B, or
  the whole save replaces the current one. The current save is first copied
  to `saves/backups/savedata-<date>-<time>.dat`. The game keeps no play time
  per box; the preview shows when the file was last saved (its card
  directory entry's time). Importing is off during a LAN session.
- **Command line:** `DW4_PC.exe --save-info FILE` prints the preview,
  `DW4_PC.exe --import-save FILE[:BOXES] [--import-slot A|B]` imports
  (`BOXES`: `all`, the default, `1,3` for the same boxes or `1>2,4>1` to move
  them) into the save files (or `--card-dir DIR` / `--save-file FILE`), then
  exits. `--save-file FILE` makes any run use that file for slot A
  (`<name>_b<ext>` for slot B).

More about the files: [SAVES.md](SAVES.md).

**Save states.** F5 saves and F9 loads state 1; the Settings tab has four
slots, kept as `saves/states/state<N>.dw4state` next to the program. A state
is the snapshot taken when the running stage loaded plus every controller
input since, replayed on a new engine (a few hundred steps per second of play
load in well under a second); the game is deterministic, so the loaded state
matches the saved one exactly. They work after the title screens. Save states
are off in online sessions.

**Cloud backup.** F1 > Saves > *Cloud backup (online account)* keeps your
local save (slot A) on your online account: log in (or use a remembered
login), look at the backup and its five kept revisions, upload the local save
with a preview of its boxes, download the latest or a kept revision into the
local save (the current file is copied to the backups folder first) or make a
kept revision the latest again. Every overwrite is asked for first. It works
only outside online sessions; your online character is separate from it.

## Randomizer, rebalancing and cheats

All of these are in the F1 menu's Cheats tab and off by default. The online
world has no cheats and no rebalancing; the randomizer of an online instance
is a setting of its host. A [LAN session](#lan-and-direct-play) plays with the
host's cheats, rebalancing and randomizer.

**Cheats** (god mode, infinite MP, one-hit defeats, refills, levels and Bits
per player) last for the session and are never saved. **Rebalancing**
(`[gameplay]` `exp_rate`, `damage_dealt`, `damage_taken`: percent, 10-1000,
100 = original) is saved. Both pause the achievements.

**The randomizer** (`settings.ini [randomizer]`; with it off the game is the
original frame for frame): one seed drives three parts, each switched on its
own.

- **Item drops** change into other items of the same kind (weapons into
  weapons, disks into disks); key items stay. The same seed gives the same
  drops as the item drop seed of earlier versions.
- **Enemies** become other Digimon: every enemy of a wave drawn on its own
  (`each`, the default: four Numemon become four different Digimon) or one
  family per wave (`wave`). A swapped enemy keeps the original enemy's record:
  its HP, strength, defence, EXP and the wave's drops stay, only the body, its
  attacks and sounds change. Each enemy stage draws from a pool of up to six
  families: its own, filled up from the whole game weighted by area (a family
  of the stage's own area is eight times as likely as one three areas away).
  Families a stage does not have are loaded from the smallest story stage
  where they are ordinary enemies (their six model infos, attack effects, hit
  and AI scripts, sound effect bank and cues), like the original's own enemy
  packages (`stag0202/separate/e_arachne.igb`); a stage load with the
  randomizer reads up to five such members more (no extra loading time is
  simulated). Bosses, the one-of-a-kind mid-bosses (Blossomon, Mammothmon,
  SkullGreymon, Anomalocarimon, Tonosama Gekomon, Diaboromon, LordKnightmon),
  stag0404's Imperialdramon, the boss stages (`stagBS*`), the stagGF03 fights,
  every wave a stage script starts next to a model name (intro and demo fights
  wait for that model), a family whose only waves in the stage are such fights
  and every generator whose family the stage does not load itself (it never
  spawns in the original: no RAM for it, e.g. stag0403's e_mecha3 and
  stag0202's SkullGreymon generators) stay as they are. A wave keeps its entry
  move when the new family has it, else the family's plain entry; when all of
  a family's bodies are out, the next family of the pool (then the original)
  takes the place. A swapped enemy that falls off the stage or never lands
  (six seconds in the air or 150 units below its spawn point) is removed and
  counts as defeated, without a drop.
- **Loading zones** (`dungeons`): the dungeon entrances of each area are
  shuffled among the area's dungeons (area 1: Humid Cave / Cliff Dungeon, area
  2: Sand Labyrinth / Ancient Ruins, area 3: Geko Path / Vine Tunnel / Vein,
  area 4: Mecha Nest / Electro Mine); an entrance may keep its own dungeon
  (but at least one entrance leads somewhere new), so the seeds give 47
  layouts (2 x 2 x 6 x 2 without the unchanged one), and a dungeon's way out
  leads back to the entrance that was used. The exit prompt names the new
  target. A game bound by an older version (where no entrance kept its own
  dungeon, which left only two layouts) keeps playing its old layout; its bond
  has no `zone_rule` and the F1 menu names it "rule 1". Towers, quests, the
  fortress, boss stages, the lobby gate, Gate Disks and defeats keep their
  targets, and every key item stays reachable (the gate in stag0301 that led
  to the Vein still opens with the stag0301 lever and the stag0302 console,
  whatever dungeon now lies behind it).

settings.ini: `seed` (0 = off), `drops`, `enemies` (`off`, `each`, `wave`) and
`zones` (`off`, `dungeons`) for new games (an old `[gameplay] drop_seed`
becomes `seed` with `drops` on).

**The randomizer belongs to the save.** A new game binds the randomizer it
starts with to its save: `randomizer.ini` next to the save
(`saves/randomizer.ini` beside `saves/savedata.dat`, or beside the
`[data] save` / `--save-file` file; the GCI root with `--card-dir`; `saves/`
next to the program for a `[data] cards` folder, so nothing is written into a
Dolphin folder) keeps the seed and modes per saved character (by the
character's UNIQUE id), so changing the settings later does not change a
running game, and `randomizer_<UNIQUE>_spoiler.txt` beside it lists the seed,
the loading zone table and every stage's enemy pool.

- Only a new game is bound by itself: a save from before (or one started with
  the randomizer off) keeps playing the original game when it is called with
  the randomizer on, and a notice says so.
- The F1 section shows what the running save plays (the seed and modes of its
  bond, also when the settings or the Settings tab's preset say otherwise),
  can bind the current settings to it and can let it play the original game
  (*Play this save without the randomizer*: its bond then says seed 0). Delete
  a character's section in `randomizer.ini` to let it follow the settings
  again.
- Save states replay with the randomizer they were made with. Online, every
  machine plays the host's randomizer and nothing is bound.
- Randomized games are not the original game, so they count as their own
  category wherever that matters (achievements unlock into their own list
  with any of the three parts on).

## Achievements

The F1 menu's Achievements tab lists 72 achievements in eleven groups (story,
bosses, challenges, levels, forms, techniques, collection, quests, combat,
time, together) with the date of each unlock and the progress of the counted
ones; a notice shows at the top right when one unlocks (*Unlock notices*).
They are kept in `saves/profile.dat` next to the program (a `.bak` of the
previous file stays) and belong to this PC. Online play also counts for your
online account (see [Online play](#online-play)). Rules:

- Nothing unlocks or counts while a cheat (god mode, infinite MP, one-hit
  defeats) or a rebalancing value other than 100% has been on in the running
  stage, after a save state was loaded in the running stage, while a save
  state replays, and in test runs. Fast loading does not matter.
- A run with the randomizer (item drops, enemies or loading zones) unlocks
  into its own list (shown beside the date).
- The run achievements (a story clear never defeated, under 8 / 4 hours, only
  rookie forms, Very Hard alone) need a new character tracked since level 1
  that never played with a closed gate (cheats, rebalancing, a loaded state).
  Speed counts the in-game time without stage loads.
- In local co-op every player of this PC counts; online only this machine's
  player (story achievements count for everyone in the session).

## Name tags

Name, level and HP / MP bars float over the players' Digimon (Settings tab,
*Name tags, Discord, achievements*; `settings.ini`):

```ini
[hud]
overhead_labels=auto   ; auto (only with two or more players), all, others (not over your own), off
label_scale=100        ; percent, 50-200 (the size also follows the window height)
label_numbers=false    ; HP / MP numbers beside the bars
```

They are drawn by the PC over the game picture and change nothing in the
game. The tag sits just above the head and follows it when the head rises (a
jump, the leap of a charged attack), fades with the distance from the camera
and is hidden during stage loads, demos and cutscenes, status and shop
windows, the title and menu screens, and for a defeated player. Tags that
would overlap stack: the nearer player's keeps its place, the one behind moves
up with a thin line down to its player. A local player's name starts with the
colour of its HUD ring (red, blue, yellow, green). When the picture hides a
player (a wall or a pillar between it and the camera) its tag turns faint, so
it still shows where the player is. The text uses Windows' Segoe UI Bold
(else Arial Bold, else the menu's font). Online, other players in your stage
show their tags too.

## Discord Rich Presence

With the Discord desktop app running, Discord shows what you play:
`<Form> Lv <level>` (for example *WarGreymon Lv 42*), the area (*Death Valley:
Humid Cave*, names from the loading cards), the area's picture with the form's
picture on it, the difficulty as the small picture's text, and the time since
the game started; *In Menus* on the title and menu screens.

The program talks only to the Discord app on your PC, over its local pipe,
with its own small client (no SDK, no DLL, no login): it looks for the app
again every 15 seconds (a Discord started later is found), sends a change at
most every 15 seconds and says goodbye when the game ends. Without Discord, or
with the setting off, nothing happens. A Discord that hangs never holds the
game: every message to or from it must get through within 2 seconds (the
handshake's answer within 3), otherwise the connection is dropped and tried
again after 15 seconds, then 30, 60 ... up to 4 minutes while it keeps
failing (the Settings tab shows why), and quitting waits at most about a
second for it.

It is on by default. Switch it off in the Settings tab's *Discord Rich
Presence* box, or:

```ini
[discord]
enabled=false
```

Leave `client_id` (the Discord application whose name shows as "Playing
...") as the game wrote it; a value that is not a number falls back to the
default.

## Online play

The project's own server (DW4Online) runs one online world. The released game
knows the server (fixed in the build); all traffic is relayed by the server,
so nobody opens a port, and every account plays its own online character,
which starts from zero, separate from the local save.

### Getting in

The title menu has a third entry below Game Start and Option: **Online** (the
same lettering; the cursor moves to it with down from Option). It opens the
account screens over the title:

- **Log in** with *Remember me on this PC* (the login stays protected for your
  Windows account in `saves/online`; no password is stored).
- **Register:** user names have 3 to 20 letters, digits, `_` and `-`;
  passwords at least 10 characters; the e-mail address is optional and never
  shown to others (password resets by e-mail are not offered yet; the
  recovery code is the way back in). The **recovery code** is shown once:
  write it down. An account can also be created on the server's website.
- **Forgot the password?** sets a new password with the recovery code (and
  gives you a new code).

The home screen then has:

- **Play: <form> Lv <n> (Box 1)**: it ends an offline game (after asking),
  restarts the game online with your online character (a new account starts at
  Create Digimon; the save on this PC is not used) and takes you to **your own
  lobby, on your own story**: NPCs, shops, quest boards and the Digi-Lab
  behave as in single player. The character is saved on the server when the
  save room writes, at every stage change and when you go offline.
- **Settings**: the visibility of the instances you start (*Public*,
  *Friends*, *Private*), camera and light mode, fast loading and the
  randomizer of the instances you host. Your settings count at home and in
  every instance you host; as a guest you play with the host's. The difficulty
  is the character's, chosen at the Game Start; online there are no cheats and
  no rebalancing.
- **Account** (password, online character reset, log out everywhere) and
  **Log out**, and "Online now: <n> players, <m> instances".

B (or Esc) goes back a screen and from the first one back to the title menu.
The screens work with a controller first (text fields open an on-screen
keyboard), the keyboard and the mouse.

**The server's website** (its address is in the notes of each release on the
[Releases page](https://github.com/Dekirai/DW4_PC/releases); log in with the
same account) keeps your friends
(friend requests, the friends list with where they play, recently played
with), blocked players, your online preferences and profile (public or
private), the online achievements, your sessions, the cloud save (download or
delete) and the deletion of the account. Its privacy page lists what the
server stores and for how long. Friends are added there; the game has no
friends list of its own yet (it marks a friend's instance in the arrival
prompt).

### The world

**The lobby.** Other players in the same lobby stage appear as ghost figures:
their Digimon in its current form with its weapon and name tag, walking and
standing where they really are (look only: no collision, no talks, nothing
they do changes your game; a figure in your way or inside your own Digimon
turns see-through).

**Instances.** Leaving your lobby for a field or a dungeon (the lobby gate, a
quest board, the return mat) puts you into an *instance* of that stage's area:
a running one or a new one that you host. An instance belongs to its area (a
region of the map); inside it everyone travels freely, with their own camera:
members in the same field, dungeon or boss stage play one shared stage
instance (up to four, shared enemies and drops), a member who arrives later
joins it mid-stage, and exits and the gate disc take only the members who say
yes.

**The arrival prompt.** When instances you may see run in the area, the game
asks which: while you confirm the gate's destination (or the return mat's
*Yes*) it asks the server first and shows the rows over the gate's menu: each
instance with its host, where the host is, *Here (n)* for players at the very
stage you arrive in, its story point, ping and settings, and states such as
*Boss fight* (choosing it waits for the fight's end), *Loading*, *Arriving
now* (its players are on their way to that stage: you join them there),
*Full* or *Story not here yet* (greyed). The cursor starts on the row the
server suggests: a friend's instance, or one with players at your arrival
stage; otherwise on **New instance**, with Left/Right choosing its visibility
(*Public*, *Friends*, *Private*; remembered as your default). A chooses, X
shows a row's members, B keeps you standing at the gate (the pictures are
those of your controller). If the answer cannot come in time, the same prompt
appears on the loading screen, with B taking you back to the lobby.

- Players who take the same gate within a few seconds of you arrive together
  (also two who both choose *New instance*), as far as the visibility each of
  you plays with lets the other in.
- **Private stays private.** With *Private*, whether you choose it for the
  new instance or it is your remembered default when you choose nothing (no
  prompt came, or its answer got lost), you never land in anyone else's
  instance, not even a friend's: not with players who take the gate at the
  same moment, not in one that is just starting, not in one you would
  otherwise join automatically. Only a row you choose yourself in the prompt
  takes you into another player's instance.
- **Friends means the host is your friend.** With *Friends*, whether you
  choose it or it is your remembered default, you land only in an instance
  whose host is your friend (and nobody blocked either way). The other
  players in it, and others who take the gate with you, can be strangers to
  you. When several players take a gate at the same moment, who lands with
  whom can depend on who arrives first.
- Your own group stays together: the members of your instance who play the
  same stage with you and take the same exit or gate arrive together,
  whatever the visibility.
- Without anything to choose you start a new instance with your default
  visibility and host it ("New instance (Public). View: Online menu").
- Only instances whose visibility lets you in are listed (*Public*; *Friends*
  for the host's friends; *Private* for nobody else; a moderator sees every
  instance in the prompt as his own choice, never in an automatic join).

**Go along.** When members of your instance go into a dungeon while you are in
a field, a line at the top asks "Kim went into Ancient Ruins B1. Go along?"
for 20 s; the F1 menu's button opens the *Instance* page, whose *Go along* box
sends you after them (to the floor where the first of them is, waiting while
that floor is in a boss fight or loading) or closes the question. "Going
along..." follows; B on a loading screen that waits too long takes you back to
the lobby (the host is asked "End the instance for everyone?" first).

**Removing and reporting.** On the *Instance* page every other member's row
opens a player card: **Report** with a reason (griefing, cheating, name,
other; "Report sent. Thank you.", a daily limit per player) for everyone, and
for the host (and a moderator playing) **Remove** ("Remove Kim from your
instance? They can't rejoin it for a while."): the removed player goes home on
his own story and the instance is hidden from him for a while. Moderators work
off the reports on the server's website (*Admin > Reports*).

**The instance's story is its host's.** A guest plays the host's story (its
chapter, quests and opened ways) with his own level, items, Bits and Digimon,
and everybody's field progress counts for the host's story. Going home (the
gate disc, an exit into the lobby, the game over, *Leave instance*) gives you
your own story back ("Back on your own story."); the EXP, items and Bits you
earned stay yours.

**The Online menu** is the F1 menu's *Online* tab (View/Back on the pad, F1 on
the keyboard; online it pauses nothing): the instance's host, its visibility
(the host changes it with A; the members are told), the members with their
stage, level and connection, and **Leave instance** ("Leave Kim's instance?
You return to your lobby."; for the host "End the instance for everyone?").
When the host leaves, is defeated or does not come back within a minute after
losing the connection ("Kim lost the connection. Waiting for Kim..."), the
instance ends for everyone ("Kim left. The instance ends.") and every member
goes home. A host who comes back in time after a single drop plays on with
the group. One whose connection keeps dropping gets less time to come back
with each drop until he has played on long enough again (the countdown shows
it), and if he still holds the others up he is set apart: he plays on in a
stage instance of his own, still the host, and the instance goes on. So he
cannot hold the others up for long. A host whose connection drops right as
the group takes an exit gets the full minute: the others go on without
waiting. The same goes for every member whose connection keeps stalling
for a few seconds at a time: the stalls add up, and whoever holds the others
up much longer than he plays along with them is set apart (he plays on alone
in his stage and meets the others again at a stage change); a hiccup now and
then costs nothing. While the host's connection hangs, the others' game
decides when a newcomer may join. **Go offline** saves the online character
and returns to the title.

**Menus do not pause.** Everyone plays for themselves as in an online game:
the status window (START), NPC talks and the map recorders in fields and
dungeons are the member's own - they open on that member's screen only (the
others see "Kim is in the menu" or "Kim is talking" over the name tag),
several members can have their menus open at once, and they pause nothing:
enemies, generators, the other members, timers and music run on, and a member
in a menu stands still and can be hit. Only one NPC talk runs in a stage
instance at a time, as in local co-op: while Kim talks, another member's NPC
does not answer yet. An NPC's exchange trades with the talking member's own
folders.

### Connection, logs and achievements

While you play online, a small box in the lower left corner shows the ping
(the network's round trip to the server) and, while others share your stage,
the input delay: every member's input waits this many 60 Hz ticks so that all
machines run the same frames, about the ping plus 25-40 ms (3 ticks, 50 ms, at
20 ms of ping; 4 ticks, 67 ms, at 25-30 ms with a 60 Hz display). It is green,
yellow or red by how the link holds up; F1 > Settings can hide it, F1 > Online
shows the details.

Every online session writes `saves/online.log` (the versions and protocols,
the input delay and why it changed, the link every ten seconds, the game's
pictures per second, vsync, the display's refresh rate and whether its window
was in front, losses and resyncs; no password or token; rotated at about 1 MB;
a second game started from the same folder writes `saves/online-2.log`):
attach that file to a [bug report](../README.md#reporting-problems) when
online play lags (F1 > Online has "Copy path" and "Open folder" for it).

Online, achievements also count for the online account (only your own player;
the story challenges only on your own story, at home or as a host; cheats,
rebalancing and save states never, randomizer runs as their own category),
shown on the server's website with their rarity.

### Requirements and messages

- Online needs a CPU with FMA3 and AVX2 (most Intel Core CPUs from 2013, the
  4th generation, on and AMD CPUs from 2015 on, every Ryzen included; some
  Pentium, Celeron and Atom models lack it), because every machine must
  compute alike; without it the game says "This PC cannot play online" after
  you log in. Offline play has no CPU requirement.
- The game asks the server which versions it accepts. "Update required: the
  server needs a newer version of the game" means: download the newest
  release. "A newer version of the game is available. This one still works
  with the server." is only a hint.
- "The server does not answer. Check the connection and try again." and "The
  server is in maintenance." come with a *Try again* button.
- "Online play is not available in this build yet": a build made from the
  source code without the server's certificate pins (see
  [DEVELOPMENT.md](DEVELOPMENT.md#release-builds-and-the-server-pins)).

### Known limits

- The instance browser (joining a friend wherever he is) and parties come
  later; until then friends meet by taking the gate into the area of the
  friend's instance (the arrival prompt lists it), or by arriving together.
  Friends are added on the server's website; the game has no friends list of
  its own yet.
- Camera tours and tutorials on mats, the shaking mats and the story scenes
  still pause the whole stage instance (every member in it), as in local
  co-op; only the status window, NPC talks and the map recorders are each
  member's own so far. A map recorder's floor map shows for the member who
  read it.
- Pits still darken every member's screen in the stage instance.
- Story progress that guests make while the host reconnects is held and kept
  only if he comes back; an instance that ends drops what was not saved yet.
- Nothing of the online play comes back into the local save; F1 > Saves >
  Cloud backup keeps one copy of the local save on the server.
- Ghost figures appear only in the lobby stages; in the fields and dungeons
  members of other stage instances show only as name tags.

## LAN and direct play

Up to four machines play one session in lockstep (TCP, the host relays),
started from the command line; everyone then starts at the title together.
These sessions are separate from the online world: they need no account and
no server. The F1 menu's Online tab shows such a session's state.

```bat
DW4_PC.exe --host --players 2            rem waits for one guest on port 52040
DW4_PC.exe --join 192.168.1.20           rem or --join host:port
```

- `--port N` and `--net-delay N` (input delay in steps, default 3) adjust the
  session.
- Every machine starts the same way (the host's stage, cheats, rebalancing and
  loading choice) and each step waits for every player's input, so the
  simulation stays identical; guests report state fingerprints and a desync
  shows on screen. All machines need the same program version ("Another
  program version.") and the same game data, mods included ("Different game
  data.").
- Each machine's keyboard and first controller play its own player (host 1,
  guests 2-4).
- The host's save (slot A) and the first guest's save (as slot B) are copied
  to every machine, so both can call their Digimon at the character select;
  only the Digimon World 4 save travels, never other files of a card folder.
  When the session ends each owner gets its save back with what was saved.
- Save states are off; a guest that leaves plays on released, a host that
  leaves ends the session for the others (they play on alone).
- The host needs the port open (router / firewall) for guests outside the
  local network.

## Data Overflow (survival mode)

*Work in progress* ([SURVIVAL.md](SURVIVAL.md)). Offline, a Gekomon stands by
the Home lobby's Outside World Gate once the lobby intro is done and the gate
leads somewhere; talking to it alone opens the battle simulator (with more
players it says it takes one Digimon at a time).

Choose one of the four Rookies with a starting armour and chip, then hold out
on the Violet Ring while the enemies pour in: the Digimon strikes on its own,
the stick moves it, level-ups offer Forges, Training, gear and Plugs, a
Guardian comes every five minutes, LordKnightmon at 20:00 under the quest
timer; beating it clears the run, the timer's 0:00 is TIME UP. The run's Sim
Bits buy permanent plugs in the Plug Lab, Data Records open more gear.

Nothing of a run reaches the story: the story engine waits and goes on bit for
bit. The progress file is `saves/overflow.dat` (a `.bak` beside it). F1 >
Gameplay *Show the simulator NPC* (settings.ini `[overflow] npc`) hides
Gekomon, and F1 > Gameplay *Data Overflow (test)* starts a run directly.

## Content mods

A mod is a folder `mods/<id>` (next to the program, else in the working
folder) with a versioned manifest `mod.ini` and the replacement files under
`files/` at their logical paths, for example
`mods/mydesert/files/loadingusa/ldag0101.igb` for the Death Valley loading
screen or `files/stag0101.igb` for a stage package (models, collision, scripts
and databases live inside the packages):

```ini
[mod]
name=My Desert
version=1.0
author=me
description=Replaces the Death Valley loading screen
priority=10     ; higher wins when two mods replace the same file
game=GDJEB2
```

The F1 menu lists the mods and switches them on or off for the next start
(`[mods] disabled=id,...`). Online sessions only accept machines whose game
data, mods included, match.

**For mod authors:** `dw4_catalog ROOT --find TEXT` lists logical paths and
`--read PATH --out FILE` writes an original file as a template (ROOT may be
the disc image). `dw4_catalog.exe` ships next to `DW4_PC.exe` in the release
folder (and builds with the source code, see [DEVELOPMENT.md](DEVELOPMENT.md)).

## Texture packs

A texture pack is a folder (F1 > Settings > Graphics > *Texture pack folder*,
default `mods/textures`, settings.ini `[mods] textures`; *Reload texture pack*
applies changes while playing, the folder read in the background while the
game runs on). `mods` is looked up next to the program first, then in the
working folder. The project ships no texture packs; *High* and *Ultra* texture
quality only change something with a pack. Two layouts work:

- **Pack format 2**: a manifest `dw4pack.tsv` lists per texture hash its files
  (BC7, BC3, BC1 or RGBA8 DDS files with their mip chains, or PNG), each
  file's scale over the original and its levels; one 4x file serves Ultra and
  High. The format, the encodings and the rules a pack builder follows are in
  [TEXTURES.md](TEXTURES.md#texture-packs-format-2).
- **Loose files** (format 1): a PNG (or DDS) of any size whose name ends in
  `_<hash>.png` up to 8 folders below the folder replaces that texture (a
  folder of more than 200,000 files and folders is not read as a pack).

Pack textures load on a background thread: while a stage loads its textures
are read ahead, and until a replacement is on the GPU the game's own texture
is drawn, so no frame waits for a file. Identical textures load once and stay
in memory across stages within the *Texture memory* budget.

**Dumping textures.** With *Dump textures* in the F1 menu (or
`[mods] dump_textures=true`) every decoded texture is written once to
`mods/dump/textures` as `<name>_<width>x<height>_<hash>.png`, the name a
replacement needs. The hash of a CMPR texture is taken with its transparent
texels black (as decoded before the GX average colour was kept there), so
older packs still match.

Textures dumped or packs made from the game are game-derived: keep them on
your machine and do not share them. Tools that build a whole pack from your
own copy are described in [DEVELOPMENT.md](DEVELOPMENT.md#texture-pack-tools).

## Command line

The game needs no command line; these options are for special cases. `--help`
prints a short list.

| Option | What it does |
| --- | --- |
| `--data PATH` | Use this disc image or extraction for this run (instead of `[data] path`) |
| `--convert-rvz IN.rvz OUT.iso` | Convert an RVZ image to an ISO and exit |
| `--loading original\|fast` | The loading speed for this run |
| `--stage NAME` | Start straight in a stage, for example `--stage stagml11` for the Home lobby |
| `--card-dir DIR` | Play on a GCI folder root (`Card A` / `Card B`) for this run |
| `--save-file FILE` | Use this save file for slot A (`<name>_b<ext>` for slot B) |
| `--save-info FILE` | Print the boxes of a GameCube save and exit |
| `--import-save FILE[:BOXES] [--import-slot A\|B]` | Import a GameCube save and exit (see [Saves](#saves)) |
| `--host [--players N] [--port N] [--net-delay N]` | Host a LAN session (see [LAN and direct play](#lan-and-direct-play)) |
| `--join HOST[:PORT]` | Join a LAN session |

Without arguments the game boots the logos, the title and a new game:

```bat
DW4_PC.exe                    rem boot logos, title, new game
DW4_PC.exe --stage stagml11   rem jump straight to the Home lobby
```

Developer options (test runs, scripted input, screenshots) are in
[DEVELOPMENT.md](DEVELOPMENT.md#test-runs).

## settings.ini reference

The F1 menu writes all of these; editing the file by hand is only needed for
the few keys without a menu entry. Close the game before editing it.

| Section | Keys |
| --- | --- |
| `[window]` | `width`, `height` (default 1280 x 960), `fullscreen` (borderless, also F11), `widescreen`, `vsync`, `fps_limit` (0 = off), `smooth_motion`, `msaa` (2/4/8 samples, applied at start-up) |
| `[audio]` | `volume` (percent) |
| `[menu]` | `pause` (pause while the menu is open) |
| `[preferences]` | `skip_intro` (boot straight to the title), `language` (`en` or a language pack's code) |
| `[gameplay]` | `exp_rate`, `damage_dealt`, `damage_taken` (percent, 100 = original), `loading` (`original`, the default, or `fast`) |
| `[randomizer]` | `seed` (0 = off), `drops`, `enemies` (`off`, `each`, `wave`), `zones` (`off`, `dungeons`); see [Randomizer](#randomizer-rebalancing-and-cheats) |
| `[graphics]` | `texture_quality` (`original`, `high`, `ultra`), `texture_filter` (`bilinear`, `trilinear`, `anisotropic2` ... `anisotropic16`), `lod_bias`, `sharpening` (`off`, `low`, `high`), `shadow_quality` (`original`, `high`, `ultra`), `texture_memory` (MiB) |
| `[hud]` | `overhead_labels`, `label_scale`, `label_numbers`; see [Name tags](#name-tags) |
| `[discord]` | `enabled`, `client_id` |
| `[achievements]` | `enabled`, `popups` (unlock notices) |
| `[online]` | `net_indicator` (the ping box) |
| `[overflow]` | `npc` (Data Overflow's Gekomon in the lobby) |
| `[mods]` | `textures` (texture pack folder), `dump_textures`, `disabled` (mod ids) |
| `[data]` | `path` (the game data); `save` / `save_b` (the save files of memory card slots A and B, default `saves/savedata.dat` / `saves/savedata_b.dat`; relative paths are next to the program); `cards` (instead of the save files: a GCI folder root with `Card A` / `Card B`, e.g. Dolphin's `GC/USA` folder to play on its saves); `card_a` / `card_b` (what the slots hold: `none` for an empty slot, `unformatted`, `inserted`, and / or the card's blocks such as `251`; default a 2043-block card in slot A and one in slot B when its file or folder exists); `save_migrated` (set by the one-time copy of an older save) |
| `[keyboard]` | Up to three key names per control; see [Controls](#controls) |
| `[gamepad]` | Up to three controller inputs per control, plus `prompts`, `nintendo_layout`, `gamecube_layout`, `wii_driver`, `menu_buttons`, `gc_adapter`; see [Controllers](#controllers-and-button-pictures) |
