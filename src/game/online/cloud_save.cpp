#include "cloud_save.hpp"

#include "api.hpp"
#include "json.hpp"

#include "dw4/save_import.hpp"

#include <array>
#include <cstdio>

namespace dw4::game::online {

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
// ---------------------------------------------------------------------------
namespace {
constexpr std::array<std::uint32_t, 64> kRound{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void block(std::array<std::uint32_t, 8>& h, const std::uint8_t* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = std::uint32_t(p[4 * i]) << 24 | std::uint32_t(p[4 * i + 1]) << 16 | std::uint32_t(p[4 * i + 2]) << 8 | p[4 * i + 3];
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kRound[std::size_t(i)] + w[i];
        const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        k = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}
} // namespace

std::string sha256_hex(std::span<const std::uint8_t> bytes) {
    std::array<std::uint32_t, 8> h{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::size_t i = 0;
    for (; i + 64 <= bytes.size(); i += 64) block(h, bytes.data() + i);
    std::uint8_t tail[128]{};
    const std::size_t rest = bytes.size() - i;
    for (std::size_t k = 0; k < rest; ++k) tail[k] = bytes[i + k];
    tail[rest] = 0x80;
    const std::size_t blocks = rest + 9 <= 64 ? 1 : 2;
    const std::uint64_t bits = std::uint64_t(bytes.size()) * 8;
    for (int k = 0; k < 8; ++k) tail[blocks * 64 - 1 - std::size_t(k)] = std::uint8_t(bits >> (8 * k));
    for (std::size_t b = 0; b < blocks; ++b) block(h, tail + b * 64);
    std::string out;
    char hex[9];
    for (const auto v : h) {
        std::snprintf(hex, sizeof hex, "%08x", v);
        out += hex;
    }
    return out;
}

namespace cloud {

namespace {
std::string lower(std::string text) {
    for (auto& c : text) c = c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
    return text;
}

Meta meta_of(const json::Value& v) {
    Meta m;
    m.revision = int(v["revision"].i64().value_or(0));
    m.format = v["format"].str().value_or("");
    m.size = v["sizeBytes"].i64().value_or(0);
    m.sha256 = lower(v["sha256"].str().value_or(""));
    m.device = v["deviceName"].str().value_or("");
    m.client_version = v["clientVersion"].str().value_or("");
    m.uploaded_at = v["uploadedAt"].str().value_or("");
    return m;
}
} // namespace

HttpRequest meta_request() {
    HttpRequest r;
    r.path = "/cloud-save/meta";
    return r;
}

HttpRequest download_request(int revision) {
    HttpRequest r;
    r.path = revision > 0 ? "/cloud-save?revision=" + std::to_string(revision) : std::string("/cloud-save");
    return r;
}

HttpRequest upload_request(std::span<const std::uint8_t> save, int revision, const std::string& device, const std::string& client_version) {
    HttpRequest r;
    r.method = "PUT";
    r.path = "/cloud-save";
    r.content_type = "application/octet-stream";
    r.body.assign(reinterpret_cast<const char*>(save.data()), save.size());
    r.headers.emplace_back("X-Sha256", sha256_hex(save));
    if (revision > 0) r.headers.emplace_back("If-Match", "\"" + std::to_string(revision) + "\"");
    else r.headers.emplace_back("If-None-Match", "*");
    if (!device.empty()) r.headers.emplace_back("X-Device-Name", device);
    if (!client_version.empty()) r.headers.emplace_back("X-Client-Version", client_version);
    return r;
}

HttpRequest restore_request(int revision) {
    HttpRequest r;
    r.method = "POST";
    r.path = "/cloud-save/restore/" + std::to_string(revision);
    return r;
}

std::optional<Overview> parse_meta(const HttpResponse& response) {
    if (response.status != 200) return std::nullopt;
    const auto v = json::parse(response.body);
    if (!v) return std::nullopt;
    Overview o;
    o.latest = meta_of((*v)["latest"]);
    if (o.latest.revision < 1) return std::nullopt;
    for (const auto& b : (*v)["boxes"].items) {
        Box box;
        box.box = int(b["box"].i64().value_or(0));
        box.used = b["used"].flag().value_or(false);
        box.player_name = b["playerName"].str().value_or("");
        box.digimon = b["digimon"].str().value_or("");
        box.level = int(b["level"].i64().value_or(0));
        o.boxes.push_back(std::move(box));
    }
    for (const auto& h : (*v)["history"].items) o.history.push_back(meta_of(h));
    return o;
}

std::optional<Meta> parse_uploaded(const HttpResponse& response) {
    if (response.status != 200 && response.status != 201) return std::nullopt;
    const auto v = json::parse(response.body);
    if (!v) return std::nullopt;
    auto m = meta_of(*v);
    if (m.revision < 1) return std::nullopt;
    return m;
}

void CloudBackup::start(Busy what, HttpRequest request) {
    busy_ = what;
    id_ = config_.submit(std::move(request));
}

void CloudBackup::say(std::string text, bool error) {
    message_ = std::move(text);
    error_ = error;
}

void CloudBackup::refresh() {
    if (busy_ != Busy::none) return;
    start(Busy::meta, meta_request());
}

void CloudBackup::upload(std::vector<std::uint8_t> save) {
    if (busy_ != Busy::none) return;
    if (!save::valid_save_gci(save)) {
        say("The local save is missing or damaged; nothing was uploaded.", true);
        return;
    }
    // Over the revision the player saw: another PC's newer backup is a 409.
    const int revision = overview_ ? overview_->latest.revision : 0;
    start(Busy::upload, upload_request(save, revision, config_.device, config_.client_version));
}

void CloudBackup::download(int revision, save::CardStore store, std::filesystem::path backups) {
    if (busy_ != Busy::none || !overview_) return;
    download_revision_ = revision > 0 ? revision : overview_->latest.revision;
    download_sha_.clear();
    if (download_revision_ == overview_->latest.revision) download_sha_ = overview_->latest.sha256;
    for (const auto& h : overview_->history)
        if (h.revision == download_revision_) download_sha_ = h.sha256;
    download_store_ = std::move(store);
    download_backups_ = std::move(backups);
    start(Busy::download, download_request(revision > 0 ? revision : 0));
}

void CloudBackup::restore(int revision) {
    if (busy_ != Busy::none || revision < 1) return;
    start(Busy::restore, restore_request(revision));
}

void CloudBackup::finish_download(const HttpResponse& response) {
    if (!response.ok()) {
        say("The download failed: " + api::problem_text(response), true);
        return;
    }
    const std::span<const std::uint8_t> bytes(reinterpret_cast<const std::uint8_t*>(response.body.data()), response.body.size());
    if (download_sha_.empty() || sha256_hex(bytes) != download_sha_) {
        say("The downloaded save does not match the server's checksum; the local save was not touched.", true);
        return;
    }
    std::string error;
    const auto source = save::parse_import(bytes, error);
    if (!source || !save::valid_save_gci(source->gci)) {
        say("The downloaded save is not a valid Digimon World 4 save" + (error.empty() ? std::string() : " (" + error + ")") +
                "; the local save was not touched.",
            true);
        return;
    }
    // The current save first (backups/<name>-<time>), then the download
    // (CardStore::write keeps the previous file as .bak as well).
    std::string note;
    if (download_store_.read(0)) {
        const auto kept = save::backup_save(download_store_, 0, download_backups_, &error);
        if (!kept) {
            say("The current save could not be backed up (" + error + "); the download was not applied.", true);
            return;
        }
        note = " The previous save was copied to " + kept->generic_string() + ".";
    }
    if (!download_store_.write(0, source->gci, &error)) {
        say("Writing the downloaded save failed: " + error, true);
        return;
    }
    say("Cloud backup revision " + std::to_string(download_revision_) + " is now your local save (slot A)." + note +
            " The game reads it at the next Call Digimon.",
        false);
}

bool CloudBackup::on_response(std::uint64_t id, const HttpResponse& response) {
    if (id == 0 || id != id_) return false;
    const Busy what = busy_;
    busy_ = Busy::none;
    id_ = 0;
    switch (what) {
    case Busy::meta:
        if (const auto o = parse_meta(response)) {
            overview_ = o;
            known_ = true;
            say("", false);
        } else if (response.status == 404) {
            overview_.reset();
            known_ = true;
            say("There is no cloud backup yet.", false);
        } else {
            say("Reading the cloud backup failed: " + api::problem_text(response), true);
        }
        break;
    case Busy::upload:
        if (const auto m = parse_uploaded(response)) {
            say("Uploaded: cloud backup revision " + std::to_string(m->revision) + ".", false);
            start(Busy::meta, meta_request());
        } else if (response.status == 409) {
            say("The cloud backup changed on another PC since you looked at it. Check it (Refresh) before you upload again; "
                "nothing was overwritten.",
                true);
            known_ = false;
        } else {
            say("The upload failed: " + api::problem_text(response), true);
        }
        break;
    case Busy::download: finish_download(response); break;
    case Busy::restore:
        if (response.ok()) {
            say("The older revision is the latest cloud backup again. Download it to use it.", false);
            start(Busy::meta, meta_request());
        } else {
            say("Restoring failed: " + api::problem_text(response), true);
        }
        break;
    case Busy::none: break;
    }
    return true;
}

} // namespace cloud
} // namespace dw4::game::online
