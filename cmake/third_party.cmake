# The third-party sources of the native application, one list for the PC
# build and the Android build (docs/ANDROID.md): exact upstream commits and
# SHA-256 archives, no prebuilt game binaries. A build that must not touch the
# network points FETCHCONTENT_SOURCE_DIR_<NAME> at an existing copy (for
# example build/Release/_deps/*-src) and sets FETCHCONTENT_FULLY_DISCONNECTED.
include(FetchContent)
FetchContent_Declare(SDL3
  URL https://codeload.github.com/libsdl-org/SDL/zip/fa2c02bb6e21974a89ea9824bc53c9932abe5f9c
  URL_HASH SHA256=ab7a774d41332dc73e6cad6ed9cb14b91cb1de76497dca116be7080f069a703a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imgui_src
  URL https://codeload.github.com/ocornut/imgui/zip/f1cc2ae15e53a861a874c3034aae6798fde194ab
  URL_HASH SHA256=008e1f3398e1c6e4d317ae4a05d2dec597bd360f4f3717b1d96e9edb66dba34f
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# MPEG-1 video of the Sofdec movies (single header, no CMake project).
FetchContent_Declare(pl_mpeg_src
  URL https://codeload.github.com/phoboslab/pl_mpeg/zip/c871f2be022ece7ef4f64230b4fb8e1fb9eb6023
  URL_HASH SHA256=67dd194f9de4af65937c057cf630e5e189e163a0ed9f489de2fcc75ee2117daf
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# PNG reading and writing for texture packs and dumps (single headers).
FetchContent_Declare(stb_src
  URL https://codeload.github.com/nothings/stb/zip/2c980bb59875b0d32144a71867fbdebb2f77cd20
  URL_HASH SHA256=8e59f72b0780690cda64726804269f638a3be77b9d1506ea95f443f7964bccf0
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# Zstandard decoding for RVZ disc images (decoder sources only).
FetchContent_Declare(zstd_src
  URL https://codeload.github.com/facebook/zstd/zip/refs/tags/v1.5.6
  URL_HASH SHA256=3b1c3b46e416d36931efd34663122d7f51b550c87f74de2d38249516fe7d8be5
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
