#pragma once

// Texture pack streaming (docs/TEXTURES.md): a loading thread reads pack
// files into GPU-ready payloads while the game runs, so a frame never waits
// for a file. It learns a stage's textures while the stage loads: every
// document the engine holds is scanned once (its images decoded and
// hashed), and the replacements of the hashes the pack has are read ahead,
// during the loading screen. The renderer asks for any it draws that were
// not read ahead and draws the original until the replacement is there.
//
// GL-free: the renderer uploads the payloads (a budget per frame) and keeps
// the GPU textures by hash, evicting the least recently drawn ones past its
// memory budget (choose_evictions).
#include "texture_pack.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace dw4 {
class IgbDocument;
}

namespace dw4::game {

// A GPU texture's place in the memory budget.
struct BudgetEntry {
    std::uint64_t id{};
    std::size_t bytes{};
    std::uint64_t last_frame{}; // the last frame that drew it
};
// The entries to drop, least recently drawn first (ties by id), until the
// rest fits `budget`; entries drawn at or after `keep_from` are never
// dropped (a frame does not lose what it draws), so the result can leave
// the total above the budget.
std::vector<std::uint64_t> choose_evictions(std::vector<BudgetEntry> entries, std::size_t budget, std::uint64_t keep_from);

class TextureStreamer {
public:
    struct Config {
        std::shared_ptr<const PackIndex> pack; // null or empty: nothing to stream
        int target_scale = 4;                  // 2 High, 4 Ultra
        EncodingSupport gpu;
        std::size_t ready_limit = std::size_t(256) << 20; // payload bytes waiting for upload before read-ahead pauses
        std::vector<std::uint64_t> rgba_hashes;            // payloads that also carry level 0 as RGBA8
    };
    // An image's hashes as a scan found them: the pack hash
    // (TexturePack::image_hash), the hash of its decoded pixels as drawn
    // (they differ for CMPR images with transparent texels) and its size.
    struct ImageKey {
        std::uint64_t pack_hash{}, pixel_hash{};
        std::uint32_t width{}, height{};
        bool in_pack{};
    };
    struct Stats {
        std::size_t documents_scanned{}, images_scanned{}, loads{}, failures{}, queued{}, ready{}, ready_bytes{};
        double scan_ms{}, load_ms{};
        std::string last_error;
    };

    TextureStreamer();
    ~TextureStreamer();
    TextureStreamer(const TextureStreamer&) = delete;
    TextureStreamer& operator=(const TextureStreamer&) = delete;

    // A new pack or quality: queued work and waiting payloads are dropped,
    // payloads still being made are discarded when they finish.
    void configure(Config config);
    bool active() const;
    // The documents the engine holds now: new ones are scanned (and their
    // pack textures read ahead), ones no longer listed are forgotten.
    void set_documents(const std::vector<std::shared_ptr<const IgbDocument>>& documents);
    // The hashes of a scanned image (nullopt before its document's scan).
    std::optional<ImageKey> lookup(const IgbDocument* document, std::uint32_t reference) const;
    // Whether the document is listed and its scan is still to come (false
    // when nothing streams or the document is not one of set_documents').
    bool pending(const IgbDocument* document) const;
    // A replacement a draw needs (ahead of read-ahead work); no-op while it
    // is queued, being read or delivered.
    void request(std::uint64_t hash, std::uint32_t original_width, std::uint32_t original_height);
    // The next payload ready for upload (main thread).
    std::optional<TexturePayload> take();
    // The renderer dropped a delivered replacement (eviction): it may be
    // asked for again.
    void forget(std::uint64_t hash);
    // No scan or load queued or running and nothing waiting (tests).
    bool idle() const;
    // Waits until idle() (tests; at most `timeout_ms`).
    bool wait_idle(int timeout_ms);
    Stats stats() const;
    std::uint32_t generation() const;

private:
    struct Document {
        std::shared_ptr<const IgbDocument> document;
        bool scanned{};
        std::unordered_map<std::uint32_t, ImageKey> images;
    };
    enum class State : std::uint8_t { queued, loading, delivered, failed };
    struct Job {
        std::uint64_t hash{};
        std::uint32_t width{}, height{};
    };
    void run();
    void scan(const std::shared_ptr<const IgbDocument>& document);
    void load(const Job& job, std::uint32_t generation, const Config& config);
    void start_locked();

    mutable std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::thread worker_;
    bool stop_{};
    Config config_;
    std::uint32_t generation_{};
    std::unordered_map<const IgbDocument*, Document> documents_;
    std::deque<std::shared_ptr<const IgbDocument>> scans_;
    std::deque<Job> urgent_, ahead_;
    std::unordered_map<std::uint64_t, State> states_;
    std::deque<TexturePayload> ready_;
    std::size_t ready_bytes_{};
    int busy_{}; // a scan or load running outside the lock
    Stats stats_;
};

} // namespace dw4::game
