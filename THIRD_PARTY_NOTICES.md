# Third-party notices and provenance

The MIT license in this repository covers the new DW4_PC code. It does not
license Digimon World 4 or its game content. Users supply their own local data.
Original executable and asset bytes are not distributed by this repository.

The native application builds these pinned dependencies using CMake:

| Dependency | Pin | License |
| --- | --- | --- |
| [SDL](https://github.com/libsdl-org/SDL) | `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` (3.4.16) | zlib, upstream LICENSE.txt |
| [Dear ImGui](https://github.com/ocornut/imgui) | `f1cc2ae15e53a861a874c3034aae6798fde194ab` (1.92.9b) | MIT, upstream LICENSE.txt |

`cmake --install` includes both dependency license files. Binary packages must
retain them. The core parsers and command-line tools have no external libraries.

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

BNR format checks were compared with Dolphin's
[VolumeGC](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/DiscIO/VolumeGC.cpp)
and [RGB5A3 decoder](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/TextureDecoder_Generic.cpp).
They are format references, not linked dependencies or vendored code.
