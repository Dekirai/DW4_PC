# Native viewer milestone validation

Local validation completed 2026-09-08 on Windows x64, MSVC 19.51, NVIDIA
GeForce RTX 5070 Ti. This records the version 0.6 original-clip inspection milestone,
not gameplay compatibility or a finished port.

| Check | Result |
| --- | --- |
| Release core and native app build | Passed |
| CTest: assets, IGB metadata, IGB documents, resources, textures, meshes, materials, skinning, animation math, Enbaya, animations, camera, settings | 13/13 suites passed in Release |
| Python ISO verifier tests | 6 synthetic tests passed |
| Native CLI end-to-end tests | 8 synthetic tests passed |
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

Run `scripts/build.ps1 -Package` to reproduce the Windows build and CTest.
The additional synthetic checks are:

```powershell
python -m unittest discover -s tests -p 'test_*.py' -v
python tests/cli_tests.py build/Release/dw4_inspect.exe
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
.\build\Release\dw4_renderer_tests.exe
```

`--smoke-test` deliberately renders finite hidden frames and skips normal frame
limiting. It is not a benchmark or proof of 144 FPS game simulation. The 4K
check used a local settings file requesting 3840×2160 and MSAA 8x. Borderless
desktop mode was implemented but not exercised in the hidden-window test.

No physical controller was connected during validation; Xbox, PlayStation and
GameCube hardware behavior remains unverified. Scene construction, actor and
animation-combiner behavior, gameplay, audio, saves, online co-op and modding remain
unimplemented. Original clip inspection includes helper transforms; skinning
still requires single influences and identity selector transforms. See
[SKINNING.md](SKINNING.md) and [ANIMATIONS.md](ANIMATIONS.md).
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
