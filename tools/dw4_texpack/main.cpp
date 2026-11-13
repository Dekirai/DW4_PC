// dw4_texpack: texture pack authoring tools (docs/TEXTURES.md).
//
//   dw4_texpack export --data ROOT --out DIR [--no-uv-masks]
//
// export: every unique texture of the game data (an extracted root or the
// disc image) once as a PNG named by the renderer's pack hash, plus
// manifest.tsv, instances.tsv, atlas/ and uvmask/ (texpack_export.hpp). The
// game data is only read. The output is derived from the game's pictures:
// keep it on your machine.
#include "texpack_export.hpp"

#include "dw4/resources.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

int usage() {
    std::cerr << "usage: dw4_texpack export --data ROOT --out DIR [--no-uv-masks]\n"
                 "  ROOT: the extracted game folder or the disc image (read only)\n"
                 "  DIR:  a new or empty folder, or an earlier export (replaced)\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    // A batch tool: leave the machine to whatever else runs.
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
#endif
    if (argc < 2 || std::string(argv[1]) != "export") return usage();
    std::filesystem::path data;
    dw4::texpack::Exporter::Options options;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) data = argv[++i];
        else if (arg == "--out" && i + 1 < argc) options.out = argv[++i];
        else if (arg == "--no-uv-masks") options.masks = false;
        else return usage();
    }
    if (data.empty() || options.out.empty()) return usage();
    try {
        const auto started = std::chrono::steady_clock::now();
        const auto resources = dw4::MountedResources::open(data);
        const auto summary = dw4::texpack::export_all(resources, options, std::cout);
        const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::cout << "exported " << summary.unique << " unique textures (" << summary.usa_unique << " used by the USA release) from "
                  << summary.images << " images in " << summary.documents << " documents to " << options.out.string() << " in "
                  << seconds << " s\n";
        return summary.errors ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "dw4_texpack: " << error.what() << '\n';
        return 1;
    }
}
