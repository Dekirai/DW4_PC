# Third-party notices and provenance

The MIT license in this repository covers the new DW4_PC code, except where
an individual file or the notices below identify an upstream adaptation. It does not
license Digimon World 4 or its game content. Users supply their own local data.
Original executable and asset bytes are not distributed by this repository.

Local Windows packages include compiler-matched x64 Microsoft Visual C++
redistributable runtime DLLs. Their names, versions and hashes are recorded in
the package manifest, and the installed redistribution notice is retained
under `licenses/Microsoft-Visual-Cpp-Redist.txt` inside the package. These DLLs
are Microsoft components, not covered by the project's MIT license, and are
not committed to this repository.

The native application builds these pinned dependencies using CMake:

| Dependency | Pin | License |
| --- | --- | --- |
| [SDL](https://github.com/libsdl-org/SDL) | `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` (3.4.16) | zlib, upstream LICENSE.txt |
| [Dear ImGui](https://github.com/ocornut/imgui) | `f1cc2ae15e53a861a874c3034aae6798fde194ab` (1.92.9b) | MIT, upstream LICENSE.txt |
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) | `c871f2be022ece7ef4f64230b4fb8e1fb9eb6023` | MIT (SPDX in pl_mpeg.h), `licenses/pl_mpeg-MIT.txt` |

pl_mpeg decodes the MPEG-1 video of the opening movie; the program stream
demuxer and the movie's ADX sound use the project's own parsers.
`cmake --install` includes the dependency license files. Binary packages must
retain them. The core parsers and command-line tools have no external libraries.

Lua 4 format and execution reference:
[official Lua 4.0.1 source](https://www.lua.org/ftp/lua-4.0.1.tar.gz),
SHA-256 `df746e149cf6939e90009d2e540eee918d585b4d1bc6d68b19316a050d484d2a`.
The bounded C++ chunk reader and interpreter are independently implemented;
Lua C source is not linked or vendored. An unmodified official x86 interpreter
was built only as an ignored local validation oracle for original boot chunks.
The native runtime intentionally has no Lua file, OS or network libraries.
See `docs/LUA4.md` and `docs/STARTUP_HOST_RESEARCH.md` for the supported profile
and independent comparison evidence.

Research reference: [ivanno4317/dw4-gc](https://github.com/ivanno4317/dw4-gc)
at `794294e17fef0b3ab5eeb15fda9bd57b0dc98a2b`. Its root license is CC0-1.0;
several bundled SDK and library files have their own notices. This milestone
does not vendor its code. The DOL checksum is used to identify the supported
build. See `docs/UPSTREAM_AUDIT.md` for findings and limitations.

Additional IGB format research reference:
[mateon1's IGB exploration](https://gist.github.com/mateon1/b123a53d824305767989ebc2ba3e5814).
Its implementation is not included or executed as a dependency. The native
metadata/object readers, catalog and texture decoders are independently written,
bounds checked and validated against local files and synthetic fixtures.
This is a research loader, not an Alchemy
engine implementation.

Animation decoder adaptation:
[korenkonder/enbrip](https://github.com/korenkonder/enbrip), MIT license,
commit `461b7818ec4b1b20ed68b4a7935250f3b4b4ce18`. Version 0.6 adapts the packed
decoding and interpolation algorithms in `src/enbaya.cpp`. The upstream guest
pointers and incremental seek state are replaced by checked little-endian
cursors and bounded immutable samples. The encoder is not included.
Copyright (c) 2020-2025 korenkonder; the complete notice is retained in
`licenses/enbrip-MIT.txt` and installed in every package. See `docs/ENBAYA.md`
for tests and compatibility limits. Skinning and animation graph composition
use independently verified original matrix/graph evidence.

BNR format checks were compared with Dolphin's
[VolumeGC](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/DiscIO/VolumeGC.cpp)
and [RGB5A3 decoder](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/TextureDecoder_Generic.cpp).
They are format references, not linked dependencies or vendored code.
