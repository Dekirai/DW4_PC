#pragma once

// Dev-only presentation tools for test runs (--smoke-test). Everything here is
// off unless its environment variable is set; interactive play never reads
// them. They exist to look at what a player sees at a high refresh rate
// (smooth motion, game_main.cpp) frame by frame, see docs/DEVELOPMENT.md ("Dev
// presentation tools") and tools/flicker.py.
//
//   DW4_DEV_PRESENT=HZ      present at a fixed virtual refresh of HZ per second:
//                           every pass of the main loop adds 1/HZ s to the step
//                           accumulator and runs the 60 Hz steps it covers, as
//                           interactive play does with the real clock, and smooth
//                           motion blends exactly as there (present interval 1/HZ).
//   DW4_DEV_SETTINGS=FILE   take [window] width, height, widescreen, msaa,
//                           smooth_motion, [gameplay] loading and [preferences]
//                           skip_intro from a settings.ini (only read).
//   DW4_DEV_SMOOTH=0|1, DW4_DEV_WIDESCREEN=0|1, DW4_DEV_MSAA=N, DW4_DEV_WINDOW=WxH
//                           override those.
//   DW4_DEV_DUMP=FROM-TO[,FROM-TO...]  write every picture presented after step
//   DW4_DEV_DUMP_DIR=DIR    FROM up to step TO as DIR/present_NNNNNN.png (RGB,
//                           NNNNNN counts the run's presents) and a line per
//                           present in DIR/presents.csv (step, alpha and what
//                           the smooth motion blend did, BlendReport), the items
//                           it flagged in DIR/blend_NNNNNN.txt.
//   DW4_DEV_PICTURES=0      no pictures (presents.csv and blend_NNNNNN.txt only).
//   DW4_DEV_IDS=1           with each dumped picture an item-id picture
//                           DIR/ids_NNNNNN.png (RGB = draw list index + 1, see
//                           GlRenderer::set_id_pass) and DIR/items_NNNNNN.txt (one
//                           line per draw item: index, node name, kind, the IGB
//                           file and object reference of the node, texture,
//                           flags, model translation, node / geometry addresses).
//   DW4_DEV_HIDE=PAT,...    leave draw items out whose node or texture name
//                           contains PAT (case-insensitive) or that belong to a
//                           category: :blend, :opaque, :2d, :3d, :skinned,
//                           :particles, :texmatrix, :shadow (no projected shadow).
#include "dw4/engine/scene_graph.hpp"
#include "interpolation.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>
#include <string>
#include <unordered_map>
#include <vector>

namespace dw4::game {

struct Settings;

struct DevPresent {
    double hz = 0;
    std::filesystem::path settings_file;
    std::optional<bool> smooth, widescreen;
    std::optional<int> msaa, width, height;
    std::vector<std::pair<int, int>> dump_ranges; // step FROM..TO, inclusive
    std::filesystem::path dump_dir;
    bool ids = false;
    bool pictures = true; // DW4_DEV_PICTURES=0: presents.csv and blend_*.txt only
    std::vector<std::string> hide;

    static DevPresent from_environment();
    bool presenting() const { return hz > 0; }
    // Whether the run takes its window from Settings (DW4_DEV_SETTINGS or an
    // override) instead of the test-run defaults.
    bool window_settings() const { return !settings_file.empty() || smooth || widescreen || msaa || width; }
    bool dumping(int frame) const {
        if (dump_dir.empty()) return false;
        for (const auto& [from, to] : dump_ranges)
            if (frame >= from && frame <= to) return true;
        return false;
    }
    // Copies the settings file's values and the overrides into `settings`.
    void apply(Settings& settings) const;
};

// DW4_DEV_HIDE: removes the matching draw items (and the shadow for :shadow).
void dev_hide(const DevPresent& dev, std::vector<engine::DrawItem>& items, engine::ShadowFrame& shadow);

// Writes the dumped presents.
class DevDumper {
public:
    explicit DevDumper(const DevPresent& dev);
    // `rgba`: glReadPixels rows (bottom row first).
    // `report`: the smooth motion blend of this present (null: not blended);
    // its flagged items go to DIR/blend_NNNNNN.txt.
    void picture(int present, int frame, int steps, float alpha, const BlendReport* report, const std::string& stage,
                 int width, int height, const std::vector<std::uint8_t>& rgba);
    // `files`: IGB document -> logical path (Engine::loaded_files).
    void ids(int present, int width, int height, const std::vector<std::uint8_t>& rgba,
             const std::vector<engine::DrawItem>& items, const std::unordered_map<const void*, std::string>& files);

private:
    std::filesystem::path dir_;
    std::ofstream csv_;
};

} // namespace dw4::game
