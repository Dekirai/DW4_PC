# Digimon World 4 PC

An early native PC port project for **Digimon World 4 (USA, GameCube)**.
**Version 0.2 provides a native original-texture viewer and resource loader;
the game is not playable yet.** Gameplay, 3D scenes, combat and game saves
have not been ported.

The Windows x64 application uses SDL3, OpenGL and Dear ImGui. It reads your
original local files directly. The PowerPC `main.dol` is inspected as data;
game code is not executed. This repository does not contain game files.

## Current capabilities

- Validate GameCube headers and DOL sections; identify the USA release.
- Index both AFS archives without loading the entire large files into memory.
- Browse archive entries and read bounded byte previews.
- Read Alchemy IGB v6 metadata, serialized objects, memory buffers and references.
- Resolve logical game paths from `ndmw.igb` to checked AFS archive entries.
- Decode original indexed8/RGBA8 and BC1 textures into native RGBA pixels.
- Browse textures with zoom, pan, filtering and an alpha checkerboard.
- Decode and display the original 96×32 banner from `opening.bnr` natively.
- Configure workbench window resolution, display mode, VSync and frame limit.
- Request OpenGL MSAA and report the actual sample count provided by the driver.
- Inspect SDL-detected gamepads and live input.
- Save settings locally and validate malformed configuration values.

Graphics settings currently affect the **development window**. Saved gameplay,
aspect ratio and controller preferences are preparation for future work;
they do not implement game features. A 144 FPS workbench does not establish
that DW4 gameplay runs correctly at 144 FPS.

## Build on Windows

Requirements: Visual Studio with **Desktop development with C++**, Windows SDK
and CMake/Ninja. Visual Studio Insiders installations are supported. The first
application build downloads two open-source dependencies pinned by commit and SHA-256.

```powershell
git clone https://github.com/Dekirai/DW4_PC.git
cd DW4_PC
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Package
```

The script builds and tests the project. The application package is written to
`dist/DW4_PC/`. Run `Start-DW4-PC.cmd` to launch it using the local configuration
in `local/settings.ini`. On first launch, enter your extracted game directory
under **Original data** and select **Remember directory**. Alternatively:

```powershell
.\scripts\run.ps1 -Data 'E:\path\Digimon World 4 - Extracted'
```

You can also build manually in an x64 developer shell:

```powershell
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DDW4_BUILD_APP=ON
cmake --build build/Release --parallel
ctest --test-dir build/Release --output-on-failure
cmake --install build/Release --prefix dist/DW4_PC
```

Build only the portable data readers and tests, without dependency downloads:

```sh
cmake -S . -B build/core -DDW4_BUILD_APP=OFF
cmake --build build/core --config Debug
ctest --test-dir build/core -C Debug --output-on-failure
python -m unittest discover -s tests -p "test_*.py" -v
```

## Run with your own game data

The expected input is Dolphin's extracted directory layout:

```text
Digimon World 4 - Extracted/
  sys/boot.bin
  sys/main.dol
  sys/fst.bin
  files/area.afs
  files/ndmw.afs
  files/ndmw.igb
  files/opening.bnr
```

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted'
.\dist\DW4_PC\dw4_inspect.exe 'E:\path\Digimon World 4 - Extracted' --json
.\dist\DW4_PC\dw4_inspect.exe 'E:\path\Digimon World 4 - Extracted\files\area.afs' --list
.\dist\DW4_PC\dw4_catalog.exe 'E:\path\Digimon World 4 - Extracted' --find agumon
```

Open **Textures** to choose a logical resource or a raw archive entry. These
examples display the original font atlas and an Agumon card, respectively:

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --resource bectext.igb
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted' --archive area.afs --entry 621
```

All 2,274 recognized original IGB documents passed native object loading. All
25,906 `igImage` objects found in the two archives decoded without errors.
Those totals include repeated asset instances and verify loading, not each
image's appearance against the original renderer. The viewer currently decodes
base images; mipmaps, materials and mesh rendering remain separate work.

With Python 3.10+, you can also compare the extracted files against the
unmodified ISO. This read-only tool outputs a JSON verification report:

```powershell
python tools/verify_game.py 'E:\path\Digimon World 4 - Extracted' --iso 'E:\path\Digimon World 4 (USA).iso'
```

Reference build: `GDJEB2`, DOL SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
The verified disc header has revision byte **0**, while the reference README
calls its supported version "Rev 1". The DOL hash matches exactly. The workbench
performs structural checks; the Python verifier additionally checks this hash.

## Next milestones

Original texture viewing and logical asset lookup are available. The next step
is mesh decoding with verified transforms and materials, followed by an original
scene with a controllable Digimon. Reconstructing
the missing gameplay and engine logic is the largest remaining part of the project.

[Roadmap and acceptance criteria](docs/ROADMAP.md) ·
[Architecture](docs/ARCHITECTURE.md) ·
[Resource catalog](docs/RESOURCES.md) ·
[Native texture formats](docs/TEXTURES.md) ·
[Upstream source audit](docs/UPSTREAM_AUDIT.md) ·
[Milestone validation](docs/VALIDATION.md)

The reference [ivanno4317/dw4-gc](https://github.com/ivanno4317/dw4-gc) is an
ongoing GameCube decompilation. It provides useful symbols and reconstructed
libraries, but does not currently contain complete gameplay source.
Dependencies, provenance and implementation boundaries are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
