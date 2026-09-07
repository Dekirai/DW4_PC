# Native viewer milestone validation

Local validation completed 2026-09-08 on Windows x64, MSVC 19.51, NVIDIA
GeForce RTX 5070 Ti. This records the version 0.2 native asset-loading milestone,
not gameplay compatibility or a finished port.

| Check | Result |
| --- | --- |
| Debug core build and Release native app build | Passed |
| CTest: assets, IGB metadata, IGB documents, resources, textures, settings | 6/6 suites passed in Debug and Release |
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
```

`--smoke-test` deliberately renders finite hidden frames and skips normal frame
limiting. It is not a benchmark or proof of 144 FPS game simulation. The 4K
check used a local settings file requesting 3840×2160 and MSAA 8x. Borderless
desktop mode was implemented but not exercised in the hidden-window test.

No physical controller was connected during validation; Xbox, PlayStation and
GameCube hardware behavior remains unverified. Mesh rendering, gameplay, audio,
saves, online co-op and modding remain unimplemented. Base texture decoding
does not establish final appearance with the original materials or renderer.
No exhaustive visual comparison of the 25,906 image instances was performed.
See [TEXTURES.md](TEXTURES.md) for layout constraints and sample observations.
Big-endian IGB metadata and documents were tested synthetically; all recognized
local game IGB metadata was little-endian. The original data remains outside
the repository.

GitHub Actions independently builds/tests the portable core on Windows/Linux
and builds/packages the Windows app. The workflow does not require game files
and does not perform an original-data or physical-GPU test.
