#pragma once

// F1 > Saves > Cloud backup (docs/SEAMLESS.md 9): the player's own local
// save (slot A of the run's store) backed up to the online account, outside
// online rooms and sessions only. Log in with the online account (or use a
// remembered login of saves/online), look at the backup and its five kept
// revisions, upload the local save with a preview of its boxes, download
// the latest or a kept revision into the local save (the current file is
// copied to the backups folder first), make a kept revision the latest
// again. Every overwrite is asked for in the panel first.
#include "online/cloud_save.hpp"
#include "online/http.hpp"

#include "dw4/card_store.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace dw4::game {

class CloudBackupPanel {
public:
    // `online_folder`: saves/online beside the program (remembered logins).
    CloudBackupPanel(std::filesystem::path online_folder, std::string client_version, int protocol);
    ~CloudBackupPanel();
    // The section inside the F1 Saves tab. `store`: the player's own saves;
    // `available`: no online room or session runs (the cloud backup is the
    // local save's).
    void draw(const save::CardStore& store, bool available);

private:
    void poll();
    void ensure_worker();
    void log_in();
    void log_out(bool forget);

    std::filesystem::path folder_;
    std::string version_;
    int protocol_ = 0;
    std::unique_ptr<online::HttpWorker> http_;
    std::unique_ptr<online::cloud::CloudBackup> cloud_;
    char account_[64]{};
    char password_[128]{};
    bool remember_ = true;
    std::string account_name_;   // logged in as
    std::uint64_t login_id_ = 0;
    std::string login_message_;
    bool logged_in_ = false;
    bool token_from_store_ = false; // a 401 then forgets it
    // An overwrite waiting for its "Yes": 1 upload, 2 download, 3 restore.
    int confirm_ = 0;
    int confirm_revision_ = 0;
};

} // namespace dw4::game
