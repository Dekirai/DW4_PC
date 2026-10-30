#pragma once

// The cloud backup of the local save (docs/SEAMLESS.md 9, F1 > Saves, outside
// rooms only): one backup per account on the server plus its five earlier
// revisions.
//
//   GET  /cloud-save/meta            -> latest {revision, format, sizeBytes, sha256, deviceName, clientVersion,
//                                       uploadedAt}, boxes [{box, used, playerName, digimon, level}], history [...]
//   GET  /cloud-save[?revision=N]    -> the save bytes
//   PUT  /cloud-save                 the save bytes, X-Sha256 (hex), If-Match: "<revision>" or If-None-Match: *
//   POST /cloud-save/restore/{N}     -> the kept revision N becomes the latest (a new revision)
//
// Uploads never overwrite a backup made elsewhere unseen: they carry the
// revision the player looked at (409 otherwise). A download is checked
// against the SHA-256 the server listed, then the current local save is
// copied to the backups folder before the download replaces it.
#include "http.hpp"

#include "dw4/card_store.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dw4::game::online {

// SHA-256 as lower-case hex (FIPS 180-4).
std::string sha256_hex(std::span<const std::uint8_t> bytes);

namespace cloud {

struct Meta {
    int revision = 0;
    std::string format;        // "gci" or "bin"
    std::int64_t size = 0;
    std::string sha256;        // hex
    std::string device, client_version, uploaded_at;
};
struct Box {
    int box = 0;
    bool used = false;
    std::string player_name, digimon;
    int level = 0;
};
struct Overview {
    Meta latest;
    std::vector<Box> boxes;
    std::vector<Meta> history; // newest first
};

HttpRequest meta_request();
// `revision` 0: the latest.
HttpRequest download_request(int revision);
// `revision` 0: the first backup (If-None-Match: *).
HttpRequest upload_request(std::span<const std::uint8_t> save, int revision, const std::string& device, const std::string& client_version);
HttpRequest restore_request(int revision);

std::optional<Overview> parse_meta(const HttpResponse& response);
std::optional<Meta> parse_uploaded(const HttpResponse& response);

// The controller behind the F1 Saves panel (no UI): one request at a time,
// answers through on_response.
class CloudBackup {
public:
    enum class Busy : std::uint8_t { none, meta, upload, download, restore };
    struct Config {
        std::function<std::uint64_t(HttpRequest)> submit;
        std::string device = "DW4_PC";
        std::string client_version;
    };
    explicit CloudBackup(Config config) : config_(std::move(config)) {}

    // Reads the overview (the panel shows it, an upload carries its revision).
    void refresh();
    // Uploads `save` (the local save's GCI bytes) over the revision the
    // overview showed (the first backup when there is none).
    void upload(std::vector<std::uint8_t> save);
    // Downloads `revision` (0: the latest) into slot A of `store`: checked
    // against the overview's SHA-256, the current save copied to `backups`
    // first.
    void download(int revision, save::CardStore store, std::filesystem::path backups);
    // The kept revision becomes the latest again (on the server only).
    void restore(int revision);

    bool on_response(std::uint64_t id, const HttpResponse& response);
    Busy busy() const { return busy_; }
    bool known() const { return known_; }   // the overview was read (or "none yet")
    const std::optional<Overview>& overview() const { return overview_; }
    const std::string& message() const { return message_; }
    bool message_is_error() const { return error_; }
    // The HTTP status of the last answer (401: the login is no longer valid).
    int last_status() const { return last_status_; }

private:
    void start(Busy what, HttpRequest request);
    void say(std::string text, bool error);
    void finish_download(const HttpResponse& response);

    Config config_;
    Busy busy_ = Busy::none;
    std::uint64_t id_ = 0;
    bool known_ = false;
    std::optional<Overview> overview_;
    std::string message_;
    bool error_ = false;
    int last_status_ = 0;
    int download_revision_ = 0;
    std::string download_sha_;
    save::CardStore download_store_;
    std::filesystem::path download_backups_;
};

} // namespace cloud
} // namespace dw4::game::online
