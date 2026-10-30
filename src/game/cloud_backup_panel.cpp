#include "cloud_backup_panel.hpp"

#include "online/api.hpp"
#include "online/server_config.hpp"
#include "online/token_store.hpp"

#include "dw4/save_import.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dw4::game {

namespace {
std::string server_key(const online::ServerEndpoint& endpoint) { return endpoint.host + ":" + std::to_string(endpoint.port); }

// "2026-10-30T10:00:00.123Z" -> "2026-10-30 10:00 UTC".
std::string when(const std::string& iso) {
    if (iso.size() < 16) return iso;
    return iso.substr(0, 10) + " " + iso.substr(11, 5) + " UTC";
}

void wrapped_colored(const ImVec4& color, const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}
} // namespace

CloudBackupPanel::CloudBackupPanel(std::filesystem::path online_folder, std::string client_version, int protocol)
    : folder_(std::move(online_folder)), version_(std::move(client_version)), protocol_(protocol) {}

CloudBackupPanel::~CloudBackupPanel() = default;

void CloudBackupPanel::ensure_worker() {
    if (http_) return;
    http_ = std::make_unique<online::HttpWorker>(online::server_endpoint(), "DW4_PC/" + version_);
    online::cloud::CloudBackup::Config config;
    config.submit = [http = http_.get()](online::HttpRequest request) { return http->submit(std::move(request)); };
    config.client_version = version_;
    char host[256] = "DW4_PC";
    if (const char* name = std::getenv("COMPUTERNAME"); name && *name) std::snprintf(host, sizeof host, "%s", name);
    config.device = host;
    cloud_ = std::make_unique<online::cloud::CloudBackup>(std::move(config));
}

void CloudBackupPanel::log_in() {
    ensure_worker();
    std::string account = account_;
    for (auto& c : account) c = c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
    if (account.empty()) {
        login_message_ = "Enter your account name.";
        return;
    }
    const auto server = server_key(http_->endpoint());
    // A remembered login of this account (Online rooms or this panel) first.
    if (!*password_)
        if (const auto token = online::load_token(folder_, server, account)) {
            http_->set_token(*token);
            account_name_ = account;
            logged_in_ = true;
            token_from_store_ = true;
            login_message_.clear();
            cloud_->refresh();
            return;
        }
    if (!*password_) {
        login_message_ = "Enter the password (this PC has no remembered login for " + account + ").";
        return;
    }
    login_id_ = http_->submit(online::api::login_request(account, password_, version_, protocol_));
    login_message_ = "Logging in...";
    account_name_ = account;
}

void CloudBackupPanel::log_out(bool forget) {
    if (forget && !account_name_.empty()) online::forget_token(folder_, account_name_);
    logged_in_ = false;
    token_from_store_ = false;
    confirm_ = 0;
    if (http_) http_->set_token({});
    // A fresh controller: nothing of the last account's backup stays shown.
    http_.reset();
    cloud_.reset();
}

void CloudBackupPanel::poll() {
    if (!http_) return;
    while (auto done = http_->poll()) {
        const auto& [id, response] = *done;
        if (id == login_id_) {
            login_id_ = 0;
            const auto login = online::api::parse_login(response);
            std::memset(password_, 0, sizeof password_);
            if (!login) {
                login_message_ = "Login failed: " + online::api::problem_text(response);
                continue;
            }
            if (!login->account.empty()) account_name_ = login->account;
            http_->set_token(login->token);
            if (remember_ && !online::save_token(folder_, server_key(http_->endpoint()), account_name_, login->token))
                login_message_ = "Logged in (the login could not be remembered).";
            else
                login_message_.clear();
            logged_in_ = true;
            token_from_store_ = false;
            cloud_->refresh();
            continue;
        }
        if (cloud_) cloud_->on_response(id, response);
    }
    if (cloud_ && cloud_->last_status() == 401 && logged_in_) {
        const bool forget = token_from_store_;
        log_out(forget);
        login_message_ = "The login is no longer valid; log in again.";
    }
}

void CloudBackupPanel::draw(const save::CardStore& store, bool available) {
    ImGui::SeparatorText("Cloud backup (online account)");
    poll();
    if (!available) {
        ImGui::TextWrapped("Not while you play online: the cloud backup is your local save's. Leave the room or session first.");
        return;
    }
    ImGui::PushTextWrapPos(0.0f);
    if (!logged_in_) {
        ImGui::TextDisabled("One backup of your local save per online account, plus its five earlier revisions.");
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("Account##cloud", account_, sizeof account_);
        ImGui::SetNextItemWidth(220);
        ImGui::InputText("Password##cloud", password_, sizeof password_, ImGuiInputTextFlags_Password);
        ImGui::Checkbox("Remember me##cloud", &remember_);
        ImGui::BeginDisabled(login_id_ != 0);
        if (ImGui::Button("Log in##cloud")) log_in();
        ImGui::EndDisabled();
        ImGui::TextDisabled("(Without a password a remembered login of this PC is used.)");
        if (!login_message_.empty()) ImGui::TextWrapped("%s", login_message_.c_str());
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::Text("Logged in as %s", account_name_.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Log out##cloud")) {
        log_out(false);
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Forget this login##cloud")) {
        log_out(true);
        ImGui::PopTextWrapPos();
        return;
    }
    if (!login_message_.empty()) ImGui::TextDisabled("%s", login_message_.c_str());
    auto& cloud = *cloud_;
    const bool busy = cloud.busy() != online::cloud::CloudBackup::Busy::none;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Refresh##cloud")) {
        confirm_ = 0;
        cloud.refresh();
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("Working...");
    }

    // The cloud side.
    const auto& overview = cloud.overview();
    if (overview) {
        const auto& latest = overview->latest;
        ImGui::Text("Latest backup: revision %d, %s", latest.revision, when(latest.uploaded_at).c_str());
        ImGui::TextDisabled("    from %s%s, %lld bytes", latest.device.empty() ? "an unnamed PC" : latest.device.c_str(),
                            latest.client_version.empty() ? "" : (" (" + latest.client_version + ")").c_str(), (long long)latest.size);
        for (const auto& box : overview->boxes)
            ImGui::TextDisabled("    Box %d: %s", box.box + 1,
                                box.used ? (box.player_name + " - " + save::form_label(box.digimon) + " Lv " + std::to_string(box.level)).c_str()
                                         : "(empty)");
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Download into my save (slot A)##cloud")) {
            confirm_ = 2;
            confirm_revision_ = 0;
        }
        ImGui::EndDisabled();
        if (!overview->history.empty() && ImGui::TreeNode("Earlier revisions##cloud")) {
            for (const auto& h : overview->history) {
                ImGui::PushID(h.revision);
                ImGui::Text("Revision %d, %s from %s", h.revision, when(h.uploaded_at).c_str(), h.device.empty() ? "an unnamed PC" : h.device.c_str());
                ImGui::SameLine();
                ImGui::BeginDisabled(busy);
                if (ImGui::SmallButton("Download")) {
                    confirm_ = 2;
                    confirm_revision_ = h.revision;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Make it the latest")) {
                    confirm_ = 3;
                    confirm_revision_ = h.revision;
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    } else if (cloud.known()) {
        ImGui::TextDisabled("There is no cloud backup yet.");
    }

    // The local side (what an upload would send).
    const auto local = store.read(0);
    ImGui::Text("This PC's save (slot A): %s", local ? store.describe(0).c_str() : "none");
    if (local)
        if (const auto boxes = save::summarize_save(*local))
            for (std::size_t i = 0; i < boxes->size(); ++i) ImGui::TextDisabled("    Box %zu: %s", i + 1, save::describe_box((*boxes)[i]).c_str());
    ImGui::BeginDisabled(busy || !local || !cloud.known());
    if (ImGui::Button("Upload this save as the cloud backup##cloud")) confirm_ = 1;
    ImGui::EndDisabled();

    // Every overwrite is asked for.
    if (confirm_) {
        std::string question;
        if (confirm_ == 1)
            question = overview ? "Replace the cloud backup (revision " + std::to_string(overview->latest.revision) +
                                      ") with this PC's save? The replaced one stays as an earlier revision."
                                : std::string("Upload this PC's save as your first cloud backup?");
        else if (confirm_ == 2)
            question = "Replace this PC's save with cloud revision " +
                       std::to_string(confirm_revision_ ? confirm_revision_ : overview ? overview->latest.revision : 0) +
                       "? The current save is copied to " + save::backup_folder(store).generic_string() + " first.";
        else
            question = "Make revision " + std::to_string(confirm_revision_) + " the latest cloud backup again (on the server only)?";
        wrapped_colored(ImVec4(1, 0.85f, 0.3f, 1), question);
        ImGui::BeginDisabled(busy);
        if (ImGui::Button("Yes##cloudconfirm")) {
            if (confirm_ == 1 && local) cloud.upload(*local);
            else if (confirm_ == 2) cloud.download(confirm_revision_, store, save::backup_folder(store));
            else if (confirm_ == 3) cloud.restore(confirm_revision_);
            confirm_ = 0;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("No##cloudconfirm")) confirm_ = 0;
    }
    if (!cloud.message().empty()) {
        if (cloud.message_is_error()) wrapped_colored(ImVec4(1, 0.5f, 0.4f, 1), cloud.message());
        else ImGui::TextWrapped("%s", cloud.message().c_str());
    }
    ImGui::PopTextWrapPos();
}

} // namespace dw4::game
