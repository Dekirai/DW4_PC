#include "texture_stream.hpp"

#include "dw4/igb_document.hpp"
#include "dw4/textures.hpp"

#include <algorithm>
#include <chrono>
#include <exception>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dw4::game {

std::vector<std::uint64_t> choose_evictions(std::vector<BudgetEntry> entries, std::size_t budget, std::uint64_t keep_from) {
    std::size_t total = 0;
    for (const auto& e : entries) total += e.bytes;
    std::vector<std::uint64_t> out;
    if (total <= budget) return out;
    std::sort(entries.begin(), entries.end(), [](const BudgetEntry& a, const BudgetEntry& b) {
        return a.last_frame != b.last_frame ? a.last_frame < b.last_frame : a.id < b.id;
    });
    for (const auto& e : entries) {
        if (total <= budget) break;
        if (e.last_frame >= keep_from) break; // (sorted: the rest are newer)
        out.push_back(e.id);
        total -= e.bytes;
    }
    return out;
}

TextureStreamer::TextureStreamer() = default;

TextureStreamer::~TextureStreamer() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void TextureStreamer::start_locked() {
    if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
}

void TextureStreamer::configure(Config config) {
    std::lock_guard lock(mutex_);
    config_ = std::move(config);
    ++generation_;
    urgent_.clear();
    ahead_.clear();
    states_.clear();
    ready_.clear();
    ready_bytes_ = 0;
    const bool on = config_.pack && !config_.pack->textures.empty() && config_.target_scale > 0;
    for (auto& [_, doc] : documents_) {
        if (!doc.scanned) {
            if (on && std::find(scans_.begin(), scans_.end(), doc.document) == scans_.end()) scans_.push_back(doc.document);
            continue;
        }
        for (auto& [ref, key] : doc.images) {
            key.in_pack = on && config_.pack->textures.count(key.pack_hash) != 0;
            if (key.in_pack && states_.emplace(key.pack_hash, State::queued).second) ahead_.push_back({key.pack_hash, key.width, key.height});
        }
    }
    if (on && (!scans_.empty() || !ahead_.empty())) start_locked();
    wake_.notify_all();
}

bool TextureStreamer::active() const {
    std::lock_guard lock(mutex_);
    return config_.pack && !config_.pack->textures.empty() && config_.target_scale > 0;
}

void TextureStreamer::set_documents(const std::vector<std::shared_ptr<const IgbDocument>>& documents) {
    std::lock_guard lock(mutex_);
    std::unordered_set<const IgbDocument*> listed;
    for (const auto& d : documents)
        if (d) listed.insert(d.get());
    for (auto it = documents_.begin(); it != documents_.end();)
        it = listed.count(it->first) ? std::next(it) : documents_.erase(it);
    std::erase_if(scans_, [&](const auto& d) { return !listed.count(d.get()); });
    const bool on = config_.pack && !config_.pack->textures.empty() && config_.target_scale > 0;
    for (const auto& d : documents) {
        if (!d || documents_.count(d.get())) continue;
        documents_[d.get()].document = d;
        if (on) scans_.push_back(d);
    }
    if (on && !scans_.empty()) {
        start_locked();
        wake_.notify_all();
    }
}

std::optional<TextureStreamer::ImageKey> TextureStreamer::lookup(const IgbDocument* document, std::uint32_t reference) const {
    std::lock_guard lock(mutex_);
    const auto doc = documents_.find(document);
    if (doc == documents_.end() || !doc->second.scanned) return std::nullopt;
    const auto image = doc->second.images.find(reference);
    if (image == doc->second.images.end()) return std::nullopt;
    return image->second;
}

void TextureStreamer::request(std::uint64_t hash, std::uint32_t original_width, std::uint32_t original_height) {
    std::lock_guard lock(mutex_);
    if (!config_.pack || !config_.pack->textures.count(hash) || config_.target_scale <= 0) return;
    const auto state = states_.find(hash);
    if (state != states_.end() && state->second != State::queued) return;
    if (state == states_.end()) states_[hash] = State::queued;
    for (const auto& job : urgent_)
        if (job.hash == hash) return;
    urgent_.push_back({hash, original_width, original_height});
    start_locked();
    wake_.notify_all();
}

std::optional<TexturePayload> TextureStreamer::take() {
    std::lock_guard lock(mutex_);
    if (ready_.empty()) return std::nullopt;
    TexturePayload payload = std::move(ready_.front());
    ready_.pop_front();
    ready_bytes_ -= std::min(ready_bytes_, payload.bytes());
    wake_.notify_all();
    return payload;
}

void TextureStreamer::forget(std::uint64_t hash) {
    std::lock_guard lock(mutex_);
    const auto state = states_.find(hash);
    if (state != states_.end() && state->second == State::delivered) states_.erase(state);
}

bool TextureStreamer::idle() const {
    std::lock_guard lock(mutex_);
    return busy_ == 0 && scans_.empty() && urgent_.empty() && ahead_.empty();
}

bool TextureStreamer::wait_idle(int timeout_ms) {
    std::unique_lock lock(mutex_);
    return idle_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                          [&] { return busy_ == 0 && scans_.empty() && urgent_.empty() && (ahead_.empty() || ready_bytes_ >= config_.ready_limit); });
}

TextureStreamer::Stats TextureStreamer::stats() const {
    std::lock_guard lock(mutex_);
    Stats s = stats_;
    s.queued = urgent_.size() + ahead_.size() + scans_.size();
    s.ready = ready_.size();
    s.ready_bytes = ready_bytes_;
    return s;
}

std::uint32_t TextureStreamer::generation() const {
    std::lock_guard lock(mutex_);
    return generation_;
}

void TextureStreamer::run() {
#ifdef _WIN32
    // (Below the game's own threads: the reads and decodes use idle time.)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    std::unique_lock lock(mutex_);
    for (;;) {
        const bool on = config_.pack && !config_.pack->textures.empty() && config_.target_scale > 0;
        const bool room = ready_bytes_ < config_.ready_limit;
        if (stop_) break;
        if (on && !urgent_.empty()) {
            const Job job = urgent_.front();
            urgent_.pop_front();
            auto state = states_.find(job.hash);
            if (state == states_.end() || state->second != State::queued) continue;
            state->second = State::loading;
            const Config config = config_;
            const auto generation = generation_;
            ++busy_;
            lock.unlock();
            load(job, generation, config);
            lock.lock();
            --busy_;
            idle_.notify_all();
            continue;
        }
        if (on && !scans_.empty()) {
            const auto document = scans_.front();
            scans_.pop_front();
            ++busy_;
            lock.unlock();
            scan(document);
            lock.lock();
            --busy_;
            idle_.notify_all();
            continue;
        }
        if (on && room && !ahead_.empty()) {
            const Job job = ahead_.front();
            ahead_.pop_front();
            auto state = states_.find(job.hash);
            if (state == states_.end() || state->second != State::queued) continue;
            state->second = State::loading;
            const Config config = config_;
            const auto generation = generation_;
            ++busy_;
            lock.unlock();
            load(job, generation, config);
            lock.lock();
            --busy_;
            idle_.notify_all();
            continue;
        }
        idle_.notify_all();
        wake_.wait(lock);
    }
}

void TextureStreamer::scan(const std::shared_ptr<const IgbDocument>& document) {
    const auto started = std::chrono::steady_clock::now();
    std::vector<std::pair<std::uint32_t, ImageKey>> keys;
    try {
        for (const auto& info : list_igb_textures(*document)) {
            if (!info.supported) continue;
            try {
                const auto image = decode_igb_texture(*document, info.reference_index);
                ImageKey key;
                key.pack_hash = TexturePack::image_hash(image);
                key.pixel_hash = image.block_compressed
                                     ? TexturePack::hash(image.rgba.data(), image.rgba.size(), int(image.width), int(image.height))
                                     : key.pack_hash;
                key.width = image.width;
                key.height = image.height;
                keys.emplace_back(info.reference_index, key);
            } catch (const std::exception&) {
                // (The renderer reports undecodable textures when it draws them.)
            }
        }
    } catch (const std::exception&) {
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::lock_guard lock(mutex_);
    stats_.scan_ms += ms;
    const auto doc = documents_.find(document.get());
    if (doc == documents_.end() || doc->second.document != document) return; // dropped meanwhile
    const bool on = config_.pack && !config_.pack->textures.empty() && config_.target_scale > 0;
    for (auto& [ref, key] : keys) {
        key.in_pack = on && config_.pack->textures.count(key.pack_hash) != 0;
        if (key.in_pack && states_.emplace(key.pack_hash, State::queued).second) ahead_.push_back({key.pack_hash, key.width, key.height});
        doc->second.images[ref] = key;
    }
    doc->second.scanned = true;
    ++stats_.documents_scanned;
    stats_.images_scanned += keys.size();
}

void TextureStreamer::load(const Job& job, std::uint32_t generation, const Config& config) {
    const auto started = std::chrono::steady_clock::now();
    std::optional<TexturePayload> payload;
    std::string error;
    const auto files = config.pack->textures.find(job.hash);
    if (files != config.pack->textures.end()) {
        const auto choice = choose_pack_file(files->second, config.target_scale, config.gpu);
        const bool want_rgba = std::find(config.rgba_hashes.begin(), config.rgba_hashes.end(), job.hash) != config.rgba_hashes.end();
        if (choice.file) {
            try {
                payload = load_pack_file(*choice.file, choice.first_level, config.target_scale, job.width, job.height, config.gpu,
                                         want_rgba, error);
            } catch (const std::exception& e) {
                error = e.what();
            }
        } else {
            error = "no file in an encoding this build reads";
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    std::lock_guard lock(mutex_);
    stats_.load_ms += ms;
    if (generation != generation_) return; // configured again meanwhile
    if (!payload) {
        states_[job.hash] = State::failed;
        ++stats_.failures;
        stats_.last_error = error;
        return;
    }
    payload->hash = job.hash;
    payload->generation = generation;
    ready_bytes_ += payload->bytes();
    ready_.push_back(std::move(*payload));
    states_[job.hash] = State::delivered;
    ++stats_.loads;
}

} // namespace dw4::game
