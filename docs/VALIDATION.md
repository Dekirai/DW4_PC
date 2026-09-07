# Native viewer milestone validation

Local validation completed 2026-09-08 on Windows x64, MSVC 19.51, NVIDIA
GeForce RTX 5070 Ti. This records the version 0.3 native mesh-viewer milestone,
not gameplay compatibility or a finished port.

| Check | Result |
| --- | --- |
| Release core and native app build | Passed |
| CTest: assets, IGB metadata, IGB documents, resources, textures, meshes, camera, settings | 8/8 suites passed in Release |
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
| Mesh synthetic fixtures | Triangle lists, grouped strips, winding, degenerates, counts, references, finite values and allocation bounds passed |
| Original topology identification | DOL enum table confirms triangle list 3 and triangle strip 4; all source index/length constraints checked |
| Portable orbit camera | Projection/frustum, orbit, pan, zoom bounds, reset and invalid-input tests passed |
| Native GPU renderer test | Pixel coverage, orbit, wireframe, resize, MSAA resolve, state restoration and cleanup passed |
| Original 3D sample | `ndmw.afs[179]`, reference 3: 24 vertices and 8 triangles rendered |
| Agumon stored-space preview | `_common/p_agumon.igb`, mesh 2, reference 5665: 1,371 vertices and 1,966 triangles; solid and wireframe screenshots inspected |
| Mesh MSAA target | 1920×1080 window, 1355×634 offscreen mesh target with actual 8 samples |
| Seven UI pages | Finite native smoke runs passed, including the existing texture CLI |
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
.\build\Release\dw4_renderer_tests.exe
```

`--smoke-test` deliberately renders finite hidden frames and skips normal frame
limiting. It is not a benchmark or proof of 144 FPS game simulation. The 4K
check used a local settings file requesting 3840×2160 and MSAA 8x. Borderless
desktop mode was implemented but not exercised in the hidden-window test.

No physical controller was connected during validation; Xbox, PlayStation and
GameCube hardware behavior remains unverified. Scene construction, skinning,
animation, gameplay, audio, saves, online co-op and modding remain unimplemented.
The mesh viewer displays stored positions with inspection shading; it does not
apply original materials or final character poses. Base texture decoding
does not establish final appearance with the original materials or renderer.
No exhaustive visual comparison of the 25,906 image instances was performed.
See [TEXTURES.md](TEXTURES.md) for layout constraints and sample observations.
Big-endian IGB metadata and documents were tested synthetically; all recognized
local game IGB metadata was little-endian. The original data remains outside
the repository.

GitHub Actions independently builds/tests the portable core on Windows/Linux
and builds/packages the Windows app. The workflow does not require game files
and does not perform an original-data or physical-GPU test. The renderer GPU
test is built there but is run explicitly on a machine with an OpenGL driver.
