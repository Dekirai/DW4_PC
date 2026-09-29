# Original switch-controller commands

The native static inspector can evaluate the original `SWITCHCTRL` commands
`ALLON` and `ALLOF` for an explicitly chosen scene root. `ALLOF` is the exact
original spelling. The command effect and its use in `first.lua` are documented
with executable and resource provenance in [SCENE_ACTIVATION.md](SCENE_ACTIVATION.md).

This is an explicit command evaluation, not a running game startup. It does not
execute Lua, initialize package stages, register controllers, build outer layers,
spawn actors or implement later gameplay visibility changes.

## Core API

`list_igb_switch_controls(document)` follows only the explicit `igInfoList`.
Exact exported `beSwitchCtrlInfo` descriptors identify their target through slot
7. A supported target must be an exact `igSwitch`, with checked child-list types,
references and bit-mask storage. Unsupported descriptors retain their reference
and diagnostic. A missing info list yields no eligible controllers; malformed,
null, non-info or repeated-controller export entries are rejected.

`evaluate_igb_switch_control(document, request)` revalidates an exported controller
and returns its checked metadata, command and selected-child vector. `ALLON`
selects every existing child slot; `ALLOF` selects none. The original routine
writes all stored words, including unused tail bits. Those tail bits never become
additional children in the native result. Null child slots remain null.

`SceneOptions::root_control` supplies the request to `decode_igb_scene`. Its
controller target must equal the selected scene root, and it cannot coexist with
`inspect_root_child`. `SceneData::root_control` retains the applied request.
Nested switches keep their stored selection; the existing static traversal,
unsupported-state diagnostics and rendering rules continue to apply. `ALLOF`
can produce a complete empty scene, although a finite smoke run requesting a
rendered scene returns code 3 when no geometry can be drawn.

Neither evaluation nor scene decoding changes IGB bytes. Re-decoding without a
request returns the original stored selection, independently of earlier commands.

## Bounds and checks

Enumeration accepts at most 4,096 info-list entries and capacity, with 4,096 bytes
per controller name. Mask bit counts are limited to 4,194,304; word count must
equal `ceil(bits / 32)`, capacity must contain those words and remain bounded,
and memory size must exactly equal capacity times four. The bit count must cover
every child slot. Underlying IGB list limits also apply.

Child validation has an aggregate budget of 4,194,304 links per enumeration,
including repeated references across controllers. This bounds work even when a
small graph exports many controllers sharing large child lists. A descriptor
that exceeds the remaining budget is unsupported with a diagnostic. Enumeration
does not allocate a child-selection vector for each controller; evaluation only
creates the selected controller's result.

The scene suite includes valid cross-word masks, tail-bit handling, null slots,
zero-bit targets, unchanged source bytes, nested stored switches, command
provenance and comparison with explicit inspection. Rejection checks cover
unexported/derived controllers, wrong targets and list types, malformed masks,
unknown commands, conflicting options and cumulative work limits. Fixtures are
generated from source and contain no game data.

## Verified original sample

`dng101b1.igb` resolves to descriptor entry 80 in `ndmw.afs`. Its member 1,
`[0.179]`, has exported controller 3471 targeting `BG02` root 2877 through scene
info 3372. The package's explicit scene index is 1; the legacy raw-entry list
uses index 6. `ALLON` produces a complete traversal with 235 geometry instances,
219 transforms, 31,886 placed vertices and 26,352 triangles. `ALLOF` is empty.
For this single-child root, `ALLON` preserves every instance path, geometry
reference and transform from the independent explicit-child inspection.

See PACKAGE_VIEWER.md for the CLI and State-tab workflow.
Full material passes, game cameras and original-frame comparison remain pending.
