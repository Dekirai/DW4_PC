# Native viewer milestone validation

Local validation completed 2026-09-08 on Windows x64, MSVC 19.51, NVIDIA
GeForce RTX 5070 Ti. This records the version 0.9 placed animated player visual
milestone, not gameplay compatibility or a finished port. Earlier archive,
texture, skin, animation and scene corpus measurements are retained from their
respective milestones. Player graph/placement, dynamic batch updates, focus,
app lifecycle and serialized-string budget checks are the new acceptance work.

| Check | Result |
| --- | --- |
| Release core and native app build | Passed |
| CTest: assets, IGB metadata, IGB documents, resources, packages, textures, meshes, materials, scenes, player placement, player visual, skinning, animation math, Enbaya, animations, camera, settings | 17/17 suites passed in Release |
| Python ISO verifier tests | 6 synthetic tests passed |
| Native CLI end-to-end tests | 8 synthetic tests passed |
| Catalog/package CLI arguments | 5 tests passed, including missing/duplicate/conflicting selectors, empty paths, signed/partial/overflow indices and index-zero handling |
| Native app CLI without graphics | 11 test methods with table-driven subcases passed; explicit player selectors, duplicates, ranges, source conflicts, finite clip time and playback prerequisites; runs in Windows app CI |
| Player placement fixtures | 66 synthetic checks passed in Release and fully instrumented AddressSanitizer; exported identity, exact groups/slots, affine rotation/scale/reflection, immutable source, invalid links, overflow and aggregate serialized-name budgets |
| Original entry matrices | Both groups and all four slots in ndmw entry 179 decoded; all 128 matrix components compare bit-for-bit with eight original stored matrices |
| Player visual fixtures | 118 synthetic checks passed in Release and fully instrumented AddressSanitizer; model instance/ONE/LOD/actor/appearance/skin provenance, malformed or ambiguous ownership, attachments, modifiers, visibility and allocation guards |
| Original player visual | 12 additional checks passed in Release and AddressSanitizer: Agumon's three explicit branches and fourth model instance select the expected original geometry |
| Dynamic scene GPU and focus | Release and AddressSanitizer passed isolated actor-batch updates, every static pixel unchanged, UV/sampler retention, occlusion, exact reset, MSAA, stable normalization, source-bounds focus and invalid-input state preservation |
| App lifecycle integration | 37 ignored local harness checks passed: original shadow Solid to rejected Textured to Solid restores exact pixels; failed clip index/time/post-decode placement retains prior clip/time/vertices/pixels; member changes remove player; invalid resource leaves 235 static batches |
| Combined original map and player | BG02 plus high Agumon: 236 textured batches, six images, 31,886 static vertices plus 1,371 player vertices; full entry matrix replaces authored model-node transform |
| Original player native variants | Package and direct-AFS sources; high/low appearance, Solid shadow, both entry groups, slots 0 and 3, waitS/runD and finite playback; unsupported shadow material and invalid resource/model/point/group/branch/clip/time fail explicitly |
| Compact playback and 4K | 1280×720 and 3840×2160 host windows render combined scenes; actual targets 650×243 at MSAA 0 and 3210×1683 at MSAA 8; elevated inspection camera shows group 0, while nearby geometry can obscure group 1 |
| Startup research | Original file-list manager dispatch order func/boot, then start, then first traced; start calls funcPlayerSet; embedded filename lookup and outer ONE-model matrix replacement documented; no host script execution claimed |
| Package synthetic fixtures | 109 checks passed in Release and AddressSanitizer: export/member order, catalog identity, exact `/s` handling, malformed input, forged metadata, NUL-padded repeated names, boundary compatibility and allocation limits |
| Original package integration | 8 checks passed in Release and AddressSanitizer; 97 descriptors / 195 members, including 98 preliminary `/s` members; both dng101b1 members match their AFS bytes exactly |
| Named map export identity | `dng101b1.igb` -> ndmw entry 80 -> member 1 `[0.179]` -> explicit export 7 / scene index 1 / info 3372 / BG02 root 2877 |
| Original switch command evidence | `first.lua` submits `SWITCHCTRL / ALLON`; original dispatcher, controller registration callback and target-mask effect traced; controller 3471 is explicit export 9 and targets root 2877 |
| Switch-controller and scene fixtures | 127 combined scene checks pass in Release and AddressSanitizer, including all-on/off, cross-word and nested masks, unchanged bytes, wrong/exported targets, malformed state and cumulative child-validation budget |
| Original native command replay | ALLON produces 235 instances / 26,352 triangles; ALLOF is complete and empty; every instance path, geometry reference and world transform agrees with explicit child-zero inspection |
| Package/control UI regression | 36 finite native checks rerun on version 0.9 passed: descriptor-only inspection, original commands, source/selector guards, invalid controller/target, empty/no-scene results, all eight pages, font, raw scene, Agumon animation and manual rig |
| Final compact scene controls | 1280×720 screenshot inspected after tab-fitting adjustment: Instances, Details, State and Status visible; ALLON, ALLOF and Stored selection fit |
| Named package at 1080p and 4K | All 235 textured batches / 5 images retained; 1290×603 and 3210×1683 scene targets inside 1920×1080 and 3840×2160 host windows, actual 8-sample MSAA |
| USA DOL identity | SHA-1 matches `e409a88a7379ed1a536f93b0a303a0ce7cd5d877` |
| ISO versus extracted data | Boot header, DOL, filesystem table and all four filesystem files match |
| Native boot/DOL inspection | `GDJEB2`, revision byte 0, 10 loaded sections, entry `0x80005240` |
| Native archive index | 1,382 `area.afs` entries; 973 `ndmw.afs` entries |
| Native IGB metadata integration | 2,274 inputs, zero failures (details in IGB_FORMAT.md) |
| Native IGB object integration | All 2,274 inputs and their object lists passed (details in IGB_OBJECTS.md) |
| Logical resource catalog | 2,080 paths, 2,079 available; archive counts match; 20 logical/direct reads identical |
| Native texture integration | 25,906 base images decoded, zero failures: 22,254 indexed8/RGBA8 and 3,652 BC1 |
| Independent BC1 sample | Original card payload decoded by Pillow's DDS reader matches native RGBA byte-for-byte |
| Texture viewer | Original font atlas and card rendered on the native GPU; screenshots inspected |
| Logical texture loading | `--resource bectext.igb` resolves and displays `ndmw.afs[79]` |
| Invalid explicit texture source | Missing resource reports an error and exits with code 3 in a finite smoke run |
| Native mesh corpus integration | 198,068 geometry instances decoded; 3,793,429 emitted triangles; zero unsupported profiles or failures |
| Original UV corpus | All 198,068 geometries checked: 46,988 valid UV streams, 151,080 absent; zero size or non-finite failures |
| UV wrapping evidence | 48,360 UV pairs on 3,825 geometries lie outside [0,1]; source coordinates are preserved |
| Stored base-texture binding corpus | 3,208 supported bindings; all have valid UVs and decodable bound images; remaining material profiles explicitly unsupported |
| Mesh synthetic fixtures | Triangle lists, grouped strips, winding, degenerates, counts, references, finite values and allocation bounds passed |
| Original topology identification | DOL enum table confirms triangle list 3 and triangle strip 4; all source index/length constraints checked |
| Portable orbit camera | Projection/frustum, orbit, pan, zoom bounds, reset and invalid-input tests passed |
| Native GPU renderer test | Pixel coverage, orbit, wireframe, resize, MSAA resolve, textured colors/UVs/wrapping/filtering/alpha, state restoration and cleanup passed |
| GPU pose updates | Position changes retain texture, UVs, indices, normalization and frame counter; reset reproduces pixels, invalid updates preserve the prior pose |
| Joint overlay projection | Uses the same fixed normalization/camera; fitted center, clipping and unchanged reference projection passed |
| Original skin reference mapping | Agumon geometry 5665 -> selector 5481 -> skin 404 -> database 810 -> skeleton 701; 25 bones, 20 joints, 24 palette entries |
| Original CPU manual pose | All three Agumon geometries reset with zero measured position error; bone 8 Z+30 degrees changes only vertices mapped to its joint descendants |
| Original rig corpus | All 894 `ndmw.afs` IGBs screened: 476 geometries with skin buffers, 290 supported with 222,694 vertices; no supported reset/manual-pose failures |
| Rig reset accuracy | Maximum coordinate error 1.97209e-15 across supported corpus; remaining 186 skinned profiles explicitly unsupported |
| Skinning synthetic fixtures | 153 checks passed, including full helper ancestry, original quaternion signs/norm, noncommuting child rotation, reset and malformed graph/matrix/palette/cache rejection |
| Native manual Agumon pose | Textured high geometry, bone 8 (`LArm01`) Z+45 degrees: 252 changed vertices, maximum displacement 4.47705; inverse-bind reset changes zero vertices |
| Original animation graph and poses | All 82 Agumon clips decoded and evaluated at zero, quarter, three-quarter and endpoint times: 8,200 bone transforms with complete mesh/joint outputs |
| Independent pose composition | 328 high-detail Agumon poses / 449,688 vertices compared with separate float32 composition using the same resolved channels; maximum vertex component difference 2.38419e-6, below 1e-5 |
| Reference codec compatibility | 55,950 quaternion/translation tuples over every half-sample interval and endpoint match the pinned enbrip implementation at nine significant digits; separate original engine interpolation is documented in ENBAYA.md |
| Original interpolation math | 49 synthetic checks; 83,400 quaternion comparisons against a bounded scalar interpreter of the original DOL routine match exactly on MSVC; the comparison does not emulate floating exception/status flags |
| Animation graph fixtures | 50 synthetic checks cover binding identity/ambiguity, masks including bits 31/32, duration, unsupported channels, owned data and original pose guards |
| Bounded Enbaya fixtures | 201 checks passed in Release and AddressSanitizer, covering packed widths, state changes, malformed/truncated streams, allocation limits and both sampling paths |
| Native clip inspection | Original textured waitS/runD at zero and quarter time produce distinct inspected frames; 720p controls fit and 1080p uses actual 8-sample MSAA |
| Native live playback | 32 frames upload 33 evaluated poses and cross the runD endpoint with Loop enabled while retaining texture and UVs |
| Animation error handling | Missing/case-mismatched names, out-of-duration time, unsupported rig and missing source exit 3; malformed, non-finite, negative or mixed manual/animation CLI inputs exit 1 |
| Original 3D sample | `ndmw.afs[179]`, reference 3: 24 vertices and 8 triangles rendered |
| Agumon stored-space preview | `_common/p_agumon.igb`, mesh 2, reference 5665: 1,371 vertices and 1,966 triangles; solid and wireframe screenshots inspected |
| Agumon bound-image verification | Image 5609, `P_agumon01.dds`, 256×256 BC1: all 65,536 texels opaque; independent Pillow DDS decode matches native RGBA exactly |
| Agumon textured GPU preview | Original high-detail mesh, 1,371 UV pairs and bound image rendered; yellow skin, green eye, mouth/teeth and white claws visually inspected |
| Low Agumon textured preview | Mesh 0, image 5597 (`P_agumon02.dds`, 64×64), 570 UV pairs rendered |
| Unsupported original material | Shadow mesh 1 in Textured mode reports its incomplete/ambiguous scope and exits 3; its Solid preview still succeeds |
| Mesh MSAA target | 1920×1080 window, 1290×554 textured offscreen mesh target with rig inspection and actual 8 samples |
| Compact rig layout | 1280×720 window: scrollable left Rig tab, visible manual controls and complete posed model in a 636×211 viewport |
| Manual rig source routing | Logical resource and direct AFS entry both work; low-detail default, high-detail textured/wireframe and combined head-axis deltas render successfully |
| Explicit rig failures | Helper bone 0, out-of-range bone 25, unsupported box and missing resource exit 3; malformed angles and CLI combinations exit 1 |
| Seven UI pages | Finite native smoke runs passed, including the existing texture CLI |
| Textured source routing | Both logical resource and direct AFS entry select the verified original image; missing source exits 3 |
| Invalid explicit mesh source | Missing resource exits 3; `--mesh` without a source exits 1 |
| Native BNR decode | 96×32 RGB5A3 banner displayed correctly |
| Initial five UI pages | Version 0.1 hidden rendering tests passed; overview, graphics and archive screenshots inspected |
| 4K/MSAA context | 3840×2160 framebuffer, actual 8 samples, VSync Off reported by native graphics page |
| Start without game data | Passed; data can be selected later |
| Static scene fixtures | 80 active checks passed in Release and AddressSanitizer: transforms, DAG instances, ordered geometry emission, masks, explicit inspection, malformed data, incomplete diagnostics and limits |
| Instance-path materials | 96 synthetic checks passed in Release and AddressSanitizer, including independently inherited bind/enable scopes and full-path texture-modifier rejection |
| Original BG02 stored state | `ndmw.afs[179]`, scene index 6 / info 3372 / root 2877: stored root mask is zero, so default traversal remains empty |
| Original BG02 explicit inspection | Root child 0: 462 node visits, 219 transforms, 235 instances from 29 geometry attributes; 31,886 placed vertices and 26,352 triangles; complete static traversal |
| Independent scene placement | Separate raw-graph traversal and original instruction arithmetic match all 235 owner/geometry/path tuples; matrix component error 0 and maximum world-position component error 9.5367431640625e-7 |
| Original scene texture coverage | All 235 instances have verified path bindings and correctly sized UVs; 5 unique images decode to 528,384 RGBA bytes |
| Native scene GPU | Shared placement, differently textured batches, opaque occlusion independent of draw order, texture sharing, one MSAA target, state restoration and malformed/budget rejection passed in Release and AddressSanitizer |
| Native BG02 scene | Solid, Wireframe and Textured finite renders passed; 720p controls fit and the 1080p scene target uses actual 8-sample MSAA |
| Native scene at 4K | 3840×2160 host window, 3210×1683 textured scene target, actual 8-sample MSAA, all 235 batches and 5 images retained |
| Eight-page and viewer regression | All eight pages, original font, manual Agumon pose and textured runD inspection passed finite native runs |
| Scene error handling | Empty stored selection, invalid root/child, non-switch override, missing source and unsupported dynamic branch exit 3; invalid/mixed CLI options exit 1 |

Run `scripts/build.ps1 -Package` to reproduce the Windows build and CTest.
The additional synthetic checks are:

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
python tests/cli_tests.py build/Release/dw4_inspect.exe
python tests/catalog_cli_tests.py build/Release/dw4_catalog.exe
python tests/app_cli_tests.py build/Release/dw4_pc.exe
```

Example finite rendering check (output is local and ignored by Git):

```powershell
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 3 --page graphics --screenshot work/graphics.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource bectext.igb --screenshot work/font.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --archive area.afs --entry 621 --screenshot work/card.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --screenshot work/agumon.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --screenshot work/agumon-textured.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --rig --bone 8 --rotate-z 45 --screenshot work/agumon-manual-pose.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --resource _common/p_agumon.igb --mesh 2 --mesh-mode textured --animation runD --animation-time 0.2 --screenshot work/agumon-animation.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --archive ndmw.afs --entry 179 --scene-root 6 --scene-inspect-child 0 --scene-mode textured --screenshot work/bg02-scene.bmp
.\build\Release\dw4_pc.exe --data 'PATH\Extracted' --config work/check.ini --smoke-test 5 --package dng101b1.igb --package-member 1 --scene-root 1 --scene-switch-control 3471 --scene-switch-command all-on --scene-mode textured --screenshot work/bg02-package.bmp
.\build\Release\dw4_renderer_tests.exe
```

`--smoke-test` deliberately renders finite hidden frames and skips normal frame
limiting. It is not a benchmark or proof of 144 FPS game simulation. The 4K
check used a local settings file requesting 3840×2160 and MSAA 8x. Borderless
desktop mode was implemented but not exercised in the hidden-window test.

No physical controller was connected during validation; Xbox, PlayStation and
GameCube hardware behavior remains unverified. Gameplay scene activation, actor and
animation-combiner behavior, gameplay, audio, saves, online co-op and modding remain
unimplemented. Original clip inspection includes helper transforms; skinning
still requires single influences and identity selector transforms. See
[SKINNING.md](SKINNING.md) and [ANIMATIONS.md](ANIMATIONS.md).
Static scene inspection now reconstructs a bounded export/subtree profile with
original placements and explicit root selection. The inspected BG02 switch is
stored disabled. An explicit original ALLON command can now enable it through
its exported controller, alongside the separate manual-child inspection option.
Native script scheduling, staged initialization and the full runtime controller
lifecycle remain unimplemented. Nested masks and source data remain unchanged.
Explicit player visual placement now replaces the verified outer model matrix
and joins original single-clip playback to the scene. It does not execute player
generation, choose startup state, compose weapons/shadows or simulate movement.
The host inspection camera can be occluded by map geometry and does not reproduce
the original camera. See [PLAYER_VIEWER.md](PLAYER_VIEWER.md),
[PLAYER_PLACEMENT_RESEARCH.md](PLAYER_PLACEMENT_RESEARCH.md) and
[STARTUP_RESEARCH.md](STARTUP_RESEARCH.md).
See [PACKAGES.md](PACKAGES.md) and [SCENE_ACTIVATION.md](SCENE_ACTIVATION.md).
The scene renderer applies base
images with inspection shading; original lighting, render passes, culling and
transparent-surface ordering are still outstanding. See [SCENES.md](SCENES.md),
[SCENE_RESEARCH.md](SCENE_RESEARCH.md) and [SCENE_RENDERER.md](SCENE_RENDERER.md).
The mesh viewer displays stored or posed positions with inspection shading or a
verified base-texture binding. Full original materials and comparison against
recorded gameplay frames remain outstanding. The 3,208 supported bindings are a limited profile, not a claim that all
198,068 meshes have fully supported materials. See [MATERIALS.md](MATERIALS.md)
and the corpus diagnostics in [MATERIAL_RESEARCH.md](MATERIAL_RESEARCH.md).
Base texture decoding does not establish final appearance with the original renderer.
No exhaustive visual comparison of the 25,906 image instances was performed.
See [TEXTURES.md](TEXTURES.md) for layout constraints and sample observations.
Big-endian IGB metadata and documents were tested synthetically; all recognized
local game IGB metadata was little-endian. The original data remains outside
the repository.

GitHub Actions independently builds/tests the portable core on Windows/Linux
and builds/packages the Windows app. The workflow does not require game files
and does not perform an original-data or physical-GPU test. The renderer GPU
test is built there but is run explicitly on a machine with an OpenGL driver.
