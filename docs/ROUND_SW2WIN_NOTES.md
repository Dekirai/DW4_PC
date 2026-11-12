# Round "sw2win": the Switch 2 Pro Controller on Windows, no driver install

Requested by the owner after the hand test of main `52d504d9` (round
"libusb", [ROUND_LIBUSB_NOTES.md](ROUND_LIBUSB_NOTES.md)): the Switch 2 Pro
Controller (057e:2069) playable over a USB cable without installing any
driver. Branch `feat-sw2win`, built on main `52d504d9`.

## 1. The evidence

`saves/controllers.log` of main `52d504d9`, Windows 11, the controller on a
USB-C cable (the DualSense and the Switch Pro played over USB in the same
test):

```
85.200 sdl: HIDAPI_SetupDeviceDriver() couldn't open 5-2:1.0: open_device: (0x00000002) Das System kann die angegebene Datei nicht finden.
85.200 sdl: Added HIDAPI device 'Nintendo Switch Pro Controller' VID 0x057e, PID 0x2069, bluetooth 0, version 513, serial NONE, interface 0, interface_class 3, ..., path = 5-2:1.0, driver = NONE (DISABLED)
85.386 windows: Switch 2 Pro Controller on USB, interface 1 driver WINUSB with a device interface GUID; no GameCube adapter in Wii U mode
85.386 windows node: USB\VID_057E&PID_2069&REV_0201&MI_00 [HidUsb]
85.386 windows node: USB\VID_057E&PID_2069&REV_0201&MI_01 [WINUSB]
85.386 windows node: USB\VID_057E&PID_2069&REV_0201&MI_02 [usbaudio]
85.386 windows node: USB\VID_057E&PID_2069&REV_0201 [usbccgp]
85.386 windows node: HID\VID_057E&PID_2069&REV_0201&MI_00 []
88.389 switch 2 pro: not opened by SDL
130.362 sdl: Removing HIDAPI device 'Nintendo Switch Pro Controller' VID 0x057e, PID 0x2069, ..., serial 00, ..., path = 5-2:1.0, driver = NONE (DISABLED)
```

What it shows:

- **Windows had exactly what the controller asks for**, nothing installed:
  its own HID driver (HidUsb) on interface 0 with one HID collection (an HID
  game controller runs without a function driver, hence `[]`), WinUSB on
  interface 1 with a device interface GUID (from the controller's Microsoft
  OS descriptor), USB audio on interface 2 (the firmware 2.x headset part,
  `REV_0201`).
- **The error text is not libusb's.** `SDL_hid_open_path` (SDL_hidapi.c)
  tries SDL's Windows HID backend first, then libusb; the message is the
  Windows backend's `CreateFile("5-2:1.0")` (a libusb path string is no file
  name). libusb's own reason is not logged (SDL's `libusb/hid.c` only has
  debug `LOG`s, and `SDL_GetError` keeps the Windows backend's text).
- **libusb could open the device later.** At the add SDL listed `serial
  NONE` (its libusb enumeration could not open the device to read string
  descriptor 3), at the removal `serial 00`: `HIDAPI_UpdateDeviceList`
  (SDL_hidapijoystick.c) fills an empty serial from a later enumeration
  (`HIDAPI_SerialIsEmpty`), and the controller's serial string is `00`
  (HIDMaestro issue 66). So a later `libusb_open` of the same device
  succeeded, but SDL opens a device for its driver only when the device is
  added (`HIDAPI_SetupDeviceDriver` from `HIDAPI_AddDevice`, or after a hint
  change), so it never tried again.

## 2. Why libusb 1.0.30 failed at that moment

From `libusb/os/windows_winusb.c` of the 1.0.30 release:

- A composite device's interfaces are assigned from **device interfaces**:
  the HID pass enumerates `GUID_DEVINTERFACE_HID` (`HidD_GetHidGuid`) and
  gives the HID collection to interface 0 of its composite ancestor
  (`get_ancestor` walks from `HID\...&MI_00` up through `USB\...&MI_00` to
  the composite node; `set_composite_interface` reads `MI_00`); the EXT
  passes enumerate the GUIDs the GEN pass read from the nodes' registry keys
  (`get_guid`: `DeviceInterfaceGUIDs` / `DeviceInterfaceGUID`) and give
  WinUSB's interface 1 its path.
- `libusb_open` -> `winusb_open` -> `composite_open` opens every WinUSB
  interface that has a path (`winusbx_open`: `CreateFile` per interface;
  file not found -> `LIBUSB_ERROR_NO_DEVICE`, access denied ->
  `LIBUSB_ERROR_ACCESS`), then the HID interface (`hid_open`: `CreateFile`
  read / write, else without access; access denied is ignored). With no
  interface path at all it returns `LIBUSB_ERROR_NOT_FOUND`.
- SDL's libusb HID backend (`hidapi_initialize_device`) then claims
  interface 0: `composite_claim_interface` -> `hid_claim_interface`, which
  needs that interface's path; an interface without one keeps the
  "unsupported" API (`LIBUSB_ERROR_NOT_SUPPORTED`).

So interface 0 does not block libusb because HidUsb owns it: libusb reads a
composite device's HID interface through hid.dll. What breaks the open is
timing: Windows announces the composite device before its interfaces'
device interfaces (the HID collection, WinUSB's GUID) are registered, and an
enumeration in that moment lists interface 0 from the configuration
descriptor without a path for it (or for interface 1), so `libusb_open` or
the claim fails; a later enumeration had them (the serial `00`). This is the
most likely reading; the exact libusb error was not logged. Another program
holding interface 1 (WinUSB opens a device once) would have made the later
`libusb_open` fail as well, so it was not that.

## 3. SDL after 3.4.16

SDL's main branch (read 2026-11-12) still has 057e:2069 in
`SDL_libusb_required` ("devices we know to be accessible _exclusively_ via
libusb"), its Switch 2 driver still takes the libusb handle of SDL's libusb
HID device (`SDL_PROP_HIDAPI_LIBUSB_DEVICE_HANDLE_POINTER`) and claims
interface 1, no platform-specific code, Bluetooth still "not supported".
The Switch 2 commits since (February 2026: `libusb_set_auto_detach_kernel_driver`,
the Steam Link detection fix "if a device needs to be opened via libusb,
don't expose it via hidraw", SL / SR, the LED patterns) concern Linux and
the Steam Link. Release branches take their fixes from main, so no 3.4.x
after 3.4.16 reads this controller through Windows' HID either.

## 4. How others drive it on Windows

- **Steam** (beta November 2025, stable December 2025, re-enabled January
  2026): "Added support for Nintendo Switch 2 controllers connected over USB
  on Windows". Steam installs no driver: Windows' HID driver and the WinUSB
  driver the controller's descriptor asks for.
- **Linux's driver** (Vicki Pfau, "HID: nintendo: Add preliminary Switch 2
  controller driver", credits Valve): two drivers for one device, the HID
  driver (`hid-nintendo`) for the input reports and the rumble, a USB driver
  (`nintendo-switch2-usb`, `USB_CLASS_VENDOR_SPEC`) for the bulk commands,
  joined by the USB path. Its start: serial, firmware info, calibration,
  feature mask, report format (03/0A), player lights, 03/0D.
- **macOS** (switch2bridge-macos issue 14): IOKit claims interface 0, libusb
  claims interface 1, the same split; commands sent as HID output reports on
  interface 0 do nothing.
- **The device** (ndeadly/switch2_controller_research `descriptors.md`,
  HIDMaestro issue 66): interface 0 HID, interrupt IN 0x81 / OUT 0x01 (64
  bytes, 4 ms); interface 1 class 0xFF, bulk OUT 0x02 / IN 0x82; interfaces
  2-4 audio (firmware 2.x). The 97-byte HID report descriptor declares input
  0x05 (63 vendor bytes), input 0x09 (21 buttons, X / Y / Rx / Rz at 12 bits)
  and output 0x02 in one top-level collection (the owner's PC lists one HID
  node), so Windows' HID driver hands report 0x05 to `ReadFile`. The
  Microsoft OS descriptor: compatible id `WINUSB` and a `DeviceInterfaceGUID`
  for interface 1.
- **The protocol** (SDL_hidapi_switch2.c; the sources above): a command is
  `command, 0x91, 0x00 (USB), subcommand, 0x00, length, 0x00, 0x00, data`;
  a reply `command, 0x01, transport, subcommand, 0x00, 0xF8, 0x00, 0x00,
  data`; the controller sends no input report until 03/0D, then one every 4
  ms; 03/0A selects report 0x05 (all controls and motion).

## 5. Decision: the port's own reader, libusb removed

| Criterion | (a) SDL patch on a private copy of SDL's sources | (b) the port's own reader (chosen) |
| --- | --- | --- |
| The driver state of the evidence, nothing to install | yes (Windows' HID for input, libusb only for interface 1) | yes (Windows' HID and WinUSB APIs) |
| The plug-in race of the evidence | stays: SDL opens a device once when it is added, `InitUSB` once; a second patch for a retry | the reader looks again on every device change and every two seconds until the controller plays |
| Unplug / plug in again | SDL's | its own, tested on a fake transport |
| Game in the background | SDL's | keeps reading; SDL ignores a pad's presses without the focus, as for every pad |
| Other controllers, the GameCube adapter | unchanged | unchanged (SDL's paths as before; `gc_adapter.cpp` untouched but for its comments) |
| Diagnostics of every step | more patching (SDL logs only failed flash reads) | every step in the log |
| Main thread | SDL starts it on the main thread (7 flash reads, 10 commands, 100 ms - 1 s timeouts each) | the reader's threads |
| Maintenance | a patch to re-apply at every SDL update; libusb's composite emulation and `libusb-1.0.dll` (LGPL, with its source archive) stay in the chain | about 1000 lines of the port's own code, the protocol SDL's (zlib) and documented; no DLL |

**libusb removed.** No device needs it any more: the GameCube adapter in
Wii U mode and the Switch 2 Pro Controller are the port's own readers'. The
build configures SDL without libusb (`SDL_HIDAPI_LIBUSB` off, also over an
old cache), drops the `libusb_dll` target, the DLL, its license files and the
source archive from the package, and the game tells SDL never to load a
`libusb-1.0.dll` (one left over beside the game from the libusb round is
named in the log and on the console as deletable, never loaded). Cost:
SDL's libusb path also reached the Switch 2 Joy-Con (through the charging
grip) and the Nintendo Switch Online GameCube controller (057e:2073), never
tested and without prompts; on Windows they went through the same libusb
composite chain. They are not read now; the reader's protocol layer can take
the GameCube controller (same start, SDL's `HandleGameCubeState` layout)
when one is at hand.

## 6. What the game does

`src/game/switch2_pro.*` (new):

- **Finding it** (cfgmgr32, on the reader's thread): the present USB device
  ids `USB\VID_057E&PID_2069\<instance>`, their interface children
  (`...&MI_xx`), interface 0's driver and its HID collection's
  `GUID_DEVINTERFACE_HID` path, interface 1's driver, the device interface
  GUID its hardware key names (`DeviceInterfaceGUID(s)`) and its WinUSB path.
  Nothing is opened to look.
- **One thread per controller** runs its state machine:

  | State | What happens | Next |
  | --- | --- | --- |
  | opening | open the HID collection (`CreateFile` read / write, else read only; `HidD_GetAttributes` must say 057e:2069; `HidP_GetCaps` for the report length; 64 input buffers), then interface 1 (`CreateFile` + `WinUsb_Initialize`, its bulk pipes, 1 s write timeout) | starting; waiting without interface 1; no input without the HID collection |
  | starting | the flash (serial number block, factory sticks at 0x13080 / 0x130C0, user sticks at 0x1FC040 / 0x1FC080), SDL's ten start commands, each with its reply (100 ms) | waiting |
  | waiting | read input (overlapped `ReadFile`, 100 ms) | running at the first report; after 1.5 s start again (three starts), then no reports; without interface 1: interface 1 unavailable |
  | running | each report: the controls, the rate; the player lights when the pad's slot changes | after 1 s without a report: start again (or wait, without interface 1) |
  | interface 1 unavailable | keep reading (another program may have started it: it then plays, "started by another program"); open interface 1 again every two seconds | starting / running |
  | no reports | keep reading; one more start every ten seconds | running |
  | no input | look again every two seconds or when Windows' list changes | opening |
  | (a lost channel) | a read or a command fails as if unplugged (`ERROR_DEVICE_NOT_CONNECTED`, `ERROR_OPERATION_ABORTED`, ...): both channels closed, a look at Windows' list asked for | opening a second later (the pad stays, at rest); unplugged once the list no longer has it |
  | unplugged | Windows no longer lists it (a look at the list: a device change, a lost channel, or every two seconds while it does not play) | the pad goes |

- **The pad**: an SDL virtual joystick with 057e:2069, "Nintendo Switch 2
  Pro Controller", and SDL's own driver's layout (positional: B SOUTH, A
  EAST, Y WEST, X NORTH; Minus BACK, Home GUIDE, Plus START, Capture MISC1,
  C MISC2, GR RIGHT_PADDLE1, GL LEFT_PADDLE1), attached at the first report,
  kept at rest through a stall, a restart or a lost channel (no player slot
  moves), detached when unplugged. Family Switch 2 Pro by its ids, read by label as before,
  the Switch 2 Pro prompts. **ZL / ZR are digital** on this controller
  (report 0x05 has them as bits; SDL makes them full or zero too). The
  sticks through SDL's arithmetic with the factory calibration (or the
  user's, when the flash holds one); a calibration that is not plausible (an
  erased flash) is not used (the raw range then; SDL would take it).
- **Player lights**: the pad's player slot, SDL's patterns.
- **The background**: nothing changes; SDL ignores a pad's presses while the
  window has no focus and takes its releases, as for every pad.
- **SDL's other paths**: `SDL_JOYSTICK_BLACKLIST_DEVICES=0x057e/0x2069`, so
  DirectInput, RawInput, Windows.Gaming.Input and GameInput make no second,
  silent joystick of the HID game controller (SDL's virtual joysticks are
  not filtered by that list); SDL's HIDAPI skips the device anyway
  (`RequiresLibUSB`).

## 7. Diagnostics

`saves/controllers.log` (synthetic lines from the tests; paths shortened):

```
switch 2 pro: found on USB (USB\VID_057E&PID_2069\...): interface 0 [HidUsb], HID input \\?\hid#vid_057e&pid_2069&mi_00#...; interface 1 [WINUSB] with a device interface GUID, \\?\usb#vid_057e&pid_2069&mi_01#...
switch 2 pro: input: opened \\?\hid#... (HID 057e:2069 version 0201, usage page 0x0001 usage 0x0005, input reports 64 bytes, output 64)
switch 2 pro: interface 1: opened (WinUSB, interface 1 class 0xff, bulk out 0x02 (64 bytes), bulk in 0x82 (64 bytes))
switch 2 pro: flash 0x013080 (left stick, factory): ok, rest 1971,2100 travel +1654,+1553 -1582,-1606
switch 2 pro: sticks: left factory, right factory calibration
switch 2 pro: start 1/10 07/01 (unknown purpose): reply 07 01 00 01 00 f8 00 00
...
switch 2 pro: start 10/10 03/0D start output: reply 03 01 00 0d 00 f8 00 00
switch 2 pro: first input report: id 0x05, 64 bytes, 4 ms after the start: 05 04 00 00 00 08 ...
switch 2 pro: report rate: 250 per second (id 0x05, 64 bytes)
switch 2 pro: player lights of player 1: reply 09 01 00 07 00 f8 00 00
switch 2 pro notice: playable
```

Failures say which step and the system's error (`busy (error 5)`, `could
not be sent: failed (error 31)`, `no reply`, `no input report came by
itself`, `lost the controller (gone (error 1167)); opening it again in a
second unless Windows no longer lists it`, `unplugged (Windows no longer lists
it)`); a repeat is not
logged again. `switch 2 pro: none on USB` after a look that found none. The
start line names a left-over `libusb-1.0.dll`; `sdl: started; libusb-1.0.dll
not in use` proves SDL loaded none.

F1 > Controls: one line per controller (`Switch 2 Pro Controller (USB):
playing, 250 reports/s, sticks: factory calibration; ZL / ZR are digital`,
`starting...`, `interface 1 unavailable (busy (error 5))`, ...) and, after
three seconds of trouble, what to do:

| Notice | When | What it says |
| --- | --- | --- |
| busy | interface 1 held by another program, no reports; or its HID input refused (access denied, a sharing violation: held by another program for itself) | close Steam (its Switch support), an emulator, a controller tool or a second copy; the game tries again every two seconds (nothing to remove) |
| no reports | started (or a start command could not be sent), no reports | the failed step and error; unplug and plug in again; send the log |
| no input | no HID collection or it cannot be opened | the error; if a driver was put on interface 0 (`057E 2069 00`, Zadig), uninstall it with its driver in Device Manager, plug in again |
| another driver on interface 1 | libusbK, libusb0, ... | uninstall it with its driver, Windows puts WinUSB back by itself |
| WinUSB without a device interface GUID | a hand-installed WinUSB | uninstall it with its driver, Windows sets WinUSB up again |
| no driver on interface 1 | Windows loaded none | plug in again / another port; only then Zadig on `057E 2069 01` (never `00`) |
| no interface 1 | a driver on the whole device | uninstall that device with its driver |
| Bluetooth | a Bluetooth HID node of it | use the cable |

With the driver state of the evidence no text tells to install anything.
The advice never to put a driver on interface 0 stands, and matters more
now: the game reads the buttons through Windows' HID driver there.

## 8. Checks

| Check | Result |
| --- | --- |
| ctest (build/Clean of the worktree) | 66 / 66 (`switch2` new; `pad_usb` rewritten; `pad_usb_missing` gone with the DLL) |
| `switch2` | the start commands as SDL sends them (length = header + data, 03/0A selects 0x05, 03/0D last), flash reads, player lights; replies; the calibration from blocks built from the documented format with the values measured on a controller (factory, user B2 A1, erased flash refused); SDL's stick arithmetic; reports 0x05 and 0x09 built byte by byte from the documented layouts (each of the 21 buttons alone, the sticks) and one written out as a capture; the state machine on a fake transport with the test's clock: open, the 15 commands in order, the first report, 250 reports/s, player lights once, a stall and the restart without the flash, unplugged (a read, or Windows' list), another program on interface 1 (logged once, reports by themselves play, started here once it is free), the four driver states, the HID collection listed late, a failed open, a start command that fails, no reports (three starts, then one line every ten seconds), an unknown report, report 0x09; Windows' platform on this PC (its look opens nothing, missing paths fail cleanly); the whole reader with SDL's virtual pads: family Switch 2 Pro, SDL's labels, A B X Y Plus ZL ZR R and the stick through the host's own pad code by label, C / GL / GR unbound, player lights, unplug and plug in again, two controllers, "none on USB"; the window in the background (a hidden window without the focus): the release counts, a press is ignored as for every pad, the reader keeps reading and the pad, the next press counts in front |
| `pad_usb` | the hints (no libusb, the blacklist), the owners, SDL's own filter with the game's hints (the Switch 2 Pro and the adapter not through Windows' HID, a DualSense and a Switch Pro as before; no libusb loaded, also with a `libusb-1.0.dll` stand-in beside the test, which is noticed); Windows' list exactly as on the owner's PC; every notice by state, the texts (none asks for an install in the evidence's state), the grace, F1's lines; SDL's virtual pads as before with the game's hints |
| Mutations (each applied alone, the test built and run, then undone) | 14 of 14 caught: A / B swapped on the pad, report format 0x09 selected, C / Capture swapped, the travel sides swapped, an erased flash taken as calibration, no retry of interface 1, no restart after a stall, the pad not detached when unplugged, held buttons kept through a stall, the player lights sent before the player is known, unknown reports counted as input, the libusb hint back on, the busy text asking for Zadig, a playing pad hiding another one's trouble |
| det.sh (newgame_dng101b1, 6000 frames), a copy of the final exe | 4004f056 / 396cb219, twice (four runs) |
| save state suite (a private copy of st_all.sh with its own run folder, same scripts and card), the copy | 17 / 17 identical after loading with a loaded state |
| Hidden starts of the copy (a screenshot run with `DW4_TEST_SWITCH2=1` and a controller log, and a plain test run), with the `libusb-1.0.dll` and `libusb-1.0-COPYING.txt` of main `52d504d9`'s build beside it and without | exit 0 each; with the DLL the console and the log name it as left over and unused; `sdl: started; libusb-1.0.dll not in use` both times; the reader's look: `switch 2 pro: none on USB`; the two screenshots identical |
| SDL's configuration (the worktree's build folders) | `SDL_HIDAPI_LIBUSB OFF`, `HAVE_LIBUSB` and `SDL_LIBUSB_DYNAMIC` undefined in `SDL_build_config.h` |

No Switch 2 Pro Controller was connected to this PC; nothing was installed
and no driver or device setting changed.

## 9. Open points

- **The hand test** (owner): the controller over USB with this build;
  `saves/controllers.log` shows every step.
- **Report 0x09**: read with procon2-mac's measured layout, used only when
  the controller does not take the report format command (or another
  program set 0x09); not seen with the device here.
- **Rumble and motion**: not used (the game has neither for pads).
- **Bluetooth**: not supported (SDL neither); Windows does not list the
  controller as an input device over Bluetooth per reports.
- **Other Switch 2 controllers** (Joy-Con 2, NSO GameCube controller): not
  read (section 5).
- **Earlier builds' files**: `libusb-1.0.dll` and `libusb-1.0-COPYING.txt`
  beside an existing game folder (also the online client folder on the
  Desktop) are left over and can be deleted; nothing loads them.
- **Windows' HID and WinUSB paths without hardware**: report 0x05 through
  `ReadFile`, opening interface 1 through WinUSB, the replies' lengths and
  the controller after its start are traced against the sources and the
  descriptor (the review found no fault there), not run: no controller here.

## 10. Review fixes

The review of `940e6d1c` found, and this round fixed:

- **A lost channel ended the controller for good.** A read or a command
  that failed as if the controller were unplugged (`ERROR_OPERATION_ABORTED`,
  `ERROR_BAD_COMMAND`, ...; say an HID read aborted over standby and resume
  without a removal) ended the controller's thread. While Windows still
  listed it the manager made no new one: it looked at the list again only
  on SDL's device change count, or every two seconds while a controller
  that had not ended did not play, and slept until asked otherwise. The log
  said gone, F1 showed nothing, no notice came. Now only Windows' list says
  that a controller is gone. A lost channel closes both channels, asks the
  manager for a look at the list and opens them again a second later
  (state opening; the pad stays at rest, no player slot moves, the
  calibration is kept, the ten start commands go out again). The look (or a
  device change, or the look every two seconds while it does not play)
  finds an unplugged controller gone; then its pad goes. The manager now
  wakes every two seconds and looks whenever a controller does not play.
- **Listed again under the same instance id.** A controller the list lost
  takes no list entry any more, also while its thread still ends: the
  manager marks it unlisted when it tells it so, and a controller listed
  again starts afresh at once (number 1 again, a new pad); the old pad goes
  as soon as the list lost it, its line leaves F1. Before, an entry could
  reach a thread that was about to end.
- **Mutants that survived the tests** (the reviewer's, built from copies of
  `switch2_pro.cpp`): the look every two seconds switched off, a re-listed
  controller handed to the old one, the pad kept while the thread is busy
  restarting (the main thread's one-second check), the flush before the start
  removed. New checks catch each of them (below), and the new code's own.
- **Another program holding the HID input for itself** (access denied, a
  sharing violation): the notice is *busy* with its own text (close that
  program, the game tries again every two seconds; nothing to remove), not
  the *no input* text that told to uninstall a driver. F1's line names the
  error (`its input device cannot be opened (busy (error 5))`); the log's
  notice name is `held by another program` for both.
- **The error codes** are one function, `switch2_io_of_error`, by number
  (checked against winerror.h at compile time on Windows) and tested: 121,
  258, 1460 timeout; 5, 32, 170 busy; 2, 3, 22, 55, 433, 995, 1167, 1617
  gone; anything else failed.
- **The hand test's face buttons**: the owner's game folder has
  `nintendo_layout=position` with the face bindings swapped (`a=b`, `b=a`,
  `x=y`, `y=x`), which plays by label again; a fresh folder plays by label.
  Both are checked now with the Switch 2 Pro's virtual pad (by position B
  is the game's A; by position with those bindings A is A).

| Check | Result |
| --- | --- |
| ctest (build/Clean of the worktree) | 66 / 66; `switch2` 368 checks (was 309), `pad_usb` 119 (was 109) |
| `switch2`, new | the error codes; the state machine: a read lost while listed (995): both closed and not started, a look asked for once, a second's pause, opened again, the ten start commands without the flash, playing, the calibration kept; a start command lost (22): the same; an unplugged read: opening until the list says gone; replies an earlier owner left in the pipe dropped before the start (the calibration right, no foreign reply); the reader with no device change and no look by the clock: a lost read plays on with the same pad and player slot and its look happens, an unplugged controller is found gone by the look its failed read asks for; the look every two seconds finds an HID input listed late; listed again under the same id while the old thread still hangs: a fresh controller number 1 at once, the old pad gone at once, a new pad; a slow restart (three seconds of replies): the pad at rest a second after the last report; by position: B the game's A, A its B, Y its X; by position with the face bindings swapped: A B X Y as labelled |
| `pad_usb`, new | the HID input held (errors 5 and 32): busy, its controller, its text (the error, Steam, close that program; no uninstall, no Zadig, no interface 1), F1's line with the error; interface 1's busy text unchanged; the notice name |
| Mutations (copies of `switch2_pro.cpp` / `pad_usb.cpp` built in a scratch folder and linked against the tests' objects; the worktree untouched) | 34 of 34 caught: the reviewer's four survivors; the new code's own (a lost channel ending the controller, no look asked for, the device thread ignoring the request, no pause, the manager not marking it unlisted, F1 showing an unlisted one, its pad kept until its thread ends, its number or its pad still counted, `started` kept after a loss); three error codes; the three notice changes; and the 14 of the round before |
| det.sh (newgame_dng101b1, 6000 frames), a copy of the final exe | 4004f056 / 396cb219, twice (four runs) |
| save state suite (the private copy of st_all.sh, its own run folder), the copy | 17 / 17 identical after loading with a loaded state |
| Hidden starts of the copy (a screenshot run with `DW4_TEST_SWITCH2=1` and a controller log, and a plain test run), with main `52d504d9`'s `libusb-1.0.dll` beside it and without | exit 0 each; the DLL named as left over and unused; `sdl: started; libusb-1.0.dll not in use`; `switch 2 pro: none on USB`; the two screenshots identical |
| SDL's configuration (both build folders) | `SDL_HIDAPI_LIBUSB OFF`, no `HAVE_LIBUSB` / `SDL_LIBUSB_DYNAMIC`; SDL's and libusb's shared sources untouched |

Still no Switch 2 Pro Controller connected here; nothing installed, no
driver or device setting changed.
