// The console F1 menu's pages (docs/CONSOLE_MENU.md 5): one function per
// category and sub-page, the only place with the visibility rules. Every
// row reads its value from Settings and F1Actions in every pass and changes
// it through menu_ops.hpp or F1Actions: the same code as the PC's F1 menu.
#include "console_internal.hpp"

#include "achievements.hpp"
#include "app_paths.hpp"
#include "cloud_backup_panel.hpp"
#include "host_view.hpp"
#include "language.hpp"
#include "menu_choices.hpp"
#include "mods.hpp"
#include "online/cpu_gate.hpp"
#include "screen2.hpp"
#include "texture_pack.hpp"

#include "dw4/engine/managers.hpp"
#include "dw4/save_import.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace dw4::game::console {

namespace {

std::string percent(double v) { return std::to_string(int(std::lround(v))) + " %"; }

struct Builder {
    State& st;
    HostView& view;
    F1Actions& a;
    Page& page;
    int infos = 0;

    Row& add(RowKind kind, const std::string& id, const std::string& label) {
        page.rows.emplace_back();
        Row& r = page.rows.back();
        r.kind = kind;
        r.id = id;
        r.label = label;
        // (A row's result line for a while: a saved state, a cloud answer.)
        if (!st.note_row.empty() && st.note_row == id && st.now < st.note_until) r.extra.push_back({st.note_tone, st.note_text});
        return r;
    }
    void section(const std::string& label) { add(RowKind::section, "section." + std::to_string(page.rows.size()), label); }
    Row& info(const std::string& text, InfoTone tone = InfoTone::dim) {
        Row& r = add(RowKind::info, "info." + std::to_string(infos++), text);
        r.tone = tone;
        return r;
    }
    void infos_of(const std::vector<InfoLine>& lines) {
        for (const auto& line : lines) info(line.text, line.tone);
    }
    Row& more(const std::string& id, const std::string& label, const std::string& text) {
        Row& r = add(RowKind::info, id, label);
        r.more = text;
        return r;
    }
    Row& toggle(const std::string& id, const std::string& label, bool on, std::function<void(bool)> set) {
        Row& r = add(RowKind::toggle, id, label);
        r.on = on;
        r.toggle = std::move(set);
        return r;
    }
    // A choice of labels (the current index; -1: none of them).
    Row& choice(const std::string& id, const std::string& label, std::vector<std::string> labels, int current, std::function<void(int)> pick) {
        Row& r = add(RowKind::choice, id, label);
        for (std::size_t i = 0; i < labels.size(); ++i) r.options.push_back({labels[i], double(i)});
        r.current = current;
        r.pick = std::move(pick);
        return r;
    }
    // A choice or stepper over values (a foreign current value among them).
    Row& values(RowKind kind, const std::string& id, const std::string& label, const std::vector<double>& list, double current,
                const std::function<std::string(double)>& name, std::function<void(double)> set) {
        Row& r = add(kind, id, label);
        const auto placed = menu::with_foreign(list, current);
        for (const double v : placed.values) r.options.push_back({name(v), v});
        r.current = int(placed.index);
        r.pick = [values = placed.values, set = std::move(set)](int i) { set(values[std::size_t(i)]); };
        return r;
    }
    Row& action(const std::string& id, const std::string& label, std::function<void()> run) {
        Row& r = add(RowKind::action, id, label);
        r.run = std::move(run);
        return r;
    }
    Row& link(const std::string& id, const std::string& label, const std::string& target, const std::string& desc = {}) {
        Row& r = add(RowKind::link, id, label);
        r.target = target;
        r.desc = desc;
        return r;
    }
    ops::Target ops() { return page_ops(view); }
};

std::shared_ptr<Confirm> confirm(const std::string& title, const std::string& text, std::vector<std::string> buttons,
                                 std::vector<std::function<void()>> actions, int focus, int danger = -1) {
    auto c = std::make_shared<Confirm>();
    c->title = title;
    c->text = text;
    c->buttons = std::move(buttons);
    c->actions = std::move(actions);
    c->focus = focus;
    c->danger = danger;
    return c;
}

void note(State& st, const std::string& row, const std::string& text, InfoTone tone, double seconds = 6.0) {
    st.note_row = row;
    st.note_text = text;
    st.note_tone = tone;
    st.note_until = st.now + seconds;
}

PadFamily binding_family(const HostView& view) {
    // (The names of the inputs: the prompts' family when it is a pad, else the first pad's.)
    PadFamily family = view.prompt_family();
    if (family == PadFamily::keyboard) family = view.pads.pads().empty() ? PadFamily::xbox_one : view.pads.pads().front().family;
    return family;
}

// ---------------------------------------------------------------- Saves

void saves_root(Builder& b) {
    auto& a = b.a;
    State& st = b.st;
    b.section("Save states");
    // (Saving can be refused for every slot at once, as on the title: one
    // line above them; Load is its own question, docs/CONSOLE_MENU.md.)
    const std::string refused = a.can_save ? a.can_save() : std::string();
    if (!refused.empty()) b.info(refused, InfoTone::warn);
    for (std::size_t slot = 0; slot < a.state_count; ++slot) {
        const std::string n = std::to_string(slot + 1);
        const std::string text = a.slot_text ? a.slot_text(slot) : std::string();
        Row& r = b.add(RowKind::cells, "saves.state." + n, "State " + n);
        r.desc = text.empty() ? "empty" : text;
        Cell save;
        save.label = "Save";
        save.locked = refused;
        const std::string id = r.id;
        const auto do_save = [&a, &st, slot, id] {
            a.save_state(slot);
            note(st, id, a.state_message ? *a.state_message : std::string(), a.state_message && a.state_message->rfind("Saved state", 0) == 0 ? InfoTone::ok : InfoTone::warn);
        };
        if (!text.empty()) save.confirm = confirm("Overwrite state " + n + "?", "It holds " + text + ".", {"Save", "Cancel"}, {do_save, {}}, 0);
        else save.run = do_save;
        Cell load;
        load.label = "Load";
        load.locked = a.can_load ? a.can_load(slot) : text.empty() ? "State " + n + " is empty." : std::string();
        const std::string stage = a.slot_stage ? a.slot_stage(slot) : std::string();
        // (The load runs at the pass after its card showed: console_menu.cpp.)
        load.confirm = confirm("Load state " + n + "?", "Play continues from " + stage + ". Progress since then is lost.", {"Load", "Cancel"},
                               {[&st, slot, id] {
                                    st.pending_load = int(slot);
                                    st.pending_load_drawn = false;
                                    st.pending_load_row = id;
                                },
                                {}},
                               0);
        r.cells = {save, load};
        if (!load.locked.empty()) r.cells[1].confirm.reset();
    }
    // (Android: the folder is under General > Files, not in the player's way.)
    if (st.platform != MenuPlatform::android) b.info("Files: " + (a.state_dir ? a.state_dir().string() : std::string()));
    b.section("Memory card");
    {
        // (Android: the card in the player's words, the first line of the
        // page; the PC: slot A's file.)
        std::string where = a.save_slot_a ? a.save_slot_a() : std::string();
        if (st.platform == MenuPlatform::android && a.saves_summary)
            if (const auto lines = a.saves_summary(true); !lines.empty()) where = lines.front().text;
        b.link("saves.card", "Where the save lives", "card", where);
    }
    {
        // (Android: only the refusal, online::cloud_refusal; nothing to log in to.)
        const std::string refusal = a.cloud_panel ? a.cloud_panel->refusal() : std::string();
        std::string desc = refusal.empty() ? "Not logged in" : "";
        if (a.cloud_panel && refusal.empty()) {
            const auto v = a.cloud_panel->view();
            if (v.logged_in) desc = "Logged in as " + v.account;
        }
        Row& r = b.link("saves.cloud", "Cloud backup", "cloud", desc);
        if (a.cloud_available && !a.cloud_available())
            r.locked = "Not while you play online: the cloud backup is your local save's. Leave the room or session first.";
        if (!refusal.empty()) r.locked = refusal;
        if (!a.cloud_panel) r.locked = "This run has no cloud backup.";
    }
}

void saves_card(Builder& b) {
    // (Android: in the player's words; the files, the store's choice and the
    // Dolphin note fold under "Technical details".)
    const bool player_words = b.st.platform == MenuPlatform::android;
    if (b.a.saves_summary) b.infos_of(b.a.saves_summary(player_words));
    if (b.a.saves_details) {
        std::string text;
        for (const auto& line : b.a.saves_details()) text += (text.empty() ? "" : "\n\n") + line.text;
        if (!text.empty()) b.more("saves.card.details", "Technical details", text);
    }
}

std::string when(const std::string& iso) { return iso.size() < 16 ? iso : iso.substr(0, 10) + " " + iso.substr(11, 5) + " UTC"; }

void saves_cloud(Builder& b, bool revisions_page) {
    auto& a = b.a;
    State& st = b.st;
    if (!a.cloud_panel || !a.card_store || !a.cloud_panel->refusal().empty() || (a.cloud_available && !a.cloud_available())) {
        b.page.valid = false;
        return;
    }
    CloudBackupPanel& panel = *a.cloud_panel;
    const save::CardStore& store = *a.card_store;
    panel.test_step(store); // (DW4_MENU_CLOUD: the test login and action, as the PC's panel runs them)
    const auto v = panel.view();
    // A request's answer as a notice too (the page's line shows it as well).
    auto& local = st.cloud_local;
    const bool finished = (local.was_busy && !v.busy) || (local.was_logging_in && !v.logging_in);
    if (finished && st.actions.toast) {
        if (local.was_busy && !v.message.empty()) st.actions.toast("Cloud backup", v.message);
        else if (local.was_logging_in && !v.login_message.empty()) st.actions.toast("Cloud backup", v.login_message);
        else if (local.was_logging_in && v.logged_in) st.actions.toast("Cloud backup", "Logged in as " + v.account + ".");
    }
    local.was_busy = v.busy;
    local.was_logging_in = v.logging_in;
    // This device's save: read every 2 s, and at once after a request
    // (an upload or a download changed it), not at every build.
    if (local.read_at < 0 || st.now - local.read_at >= 2.0 || finished) {
        local.read_at = st.now;
        const auto bytes = store.read(0);
        local.has = bytes.has_value();
        local.describe = local.has ? store.describe(0) : std::string();
        local.boxes.clear();
        if (bytes)
            if (const auto boxes = save::summarize_save(*bytes))
                for (const auto& box : *boxes) local.boxes.push_back(save::describe_box(box));
    }
    if (revisions_page) {
        if (!v.logged_in || !v.overview) {
            b.page.valid = false;
            return;
        }
        for (const auto& h : v.overview->history) {
            const std::string n = std::to_string(h.revision);
            Row& r = b.add(RowKind::cells, "cloud.revision." + n, "Revision " + n);
            r.desc = when(h.uploaded_at) + " from " + (h.device.empty() ? std::string("an unnamed PC") : h.device);
            if (v.busy) r.locked = "Wait for the request.";
            Cell download;
            download.label = "Download";
            const int rev = h.revision;
            download.confirm = confirm("Replace your save with revision " + n + "?",
                                       "Your current save is copied to " + save::backup_folder(store).generic_string() + " first.", {"Download", "Cancel"},
                                       {[&panel, &store, rev] { panel.request_download(rev, store); }, {}}, 1, 0);
            Cell latest;
            latest.label = "Make latest";
            latest.confirm = confirm("Make revision " + n + " the latest?", "The latest cloud backup becomes revision " + n + " again (on the server only).",
                                     {"Make latest", "Cancel"}, {[&panel, rev] { panel.request_restore(rev); }, {}}, 1);
            r.cells = {download, latest};
        }
        if (v.overview->history.empty()) b.info("No earlier revisions.");
        return;
    }
    if (!v.message.empty()) b.info(v.message, v.message_error ? InfoTone::error : InfoTone::ok);
    // (Android keeps no login: the token store is Windows' DPAPI, token_store.cpp.)
    const bool remembers = st.platform != MenuPlatform::android;
    if (!v.logged_in) {
        b.info("One backup of your local save per online account, plus its five earlier revisions.");
        Row& account = b.add(RowKind::text, "cloud.account", "Account");
        account.text = v.typed_account;
        account.max_length = 63;
        account.plain = true; // (a name: no suggestions, no autocorrection, no capitals)
        account.commit = [&panel](const std::string& text) { panel.set_account(text); };
        Row& password = b.add(RowKind::password, "cloud.password", "Password");
        password.text = v.password_typed ? "****" : "";
        password.max_length = 127;
        password.next_focus = "cloud.login";
        password.commit = [&panel](const std::string& text) { panel.set_password(text); };
        if (remembers) b.toggle("cloud.remember", "Remember me", v.remember, [&panel](bool on) { panel.set_remember(on); });
        Row& login = b.action("cloud.login", "Log in", [&panel, remembers] {
            if (!remembers) panel.set_remember(false);
            panel.request_login();
        });
        if (remembers) login.desc = std::string("Without a password a remembered login of ") + kThisMachine + " is used.";
        if (v.typed_account.empty()) login.locked = "Enter the account and the password.";
        else if (v.logging_in) login.locked = "Logging in...";
        if (!v.login_message.empty()) b.info(v.login_message, v.login_message.rfind("Logg", 0) == 0 ? InfoTone::dim : InfoTone::warn);
        return;
    }
    b.info("Logged in as " + v.account, InfoTone::normal);
    if (!v.login_message.empty()) b.info(v.login_message);
    const std::string wait = v.busy ? "Wait for the request." : "";
    b.action("cloud.refresh", "Refresh", [&panel] { panel.request_refresh(); }).locked = wait;
    if (v.busy) {
        Row& p = b.add(RowKind::progress, "cloud.working", "Working...");
        p.progress = -1;
    }
    b.section("Latest backup");
    if (v.overview) {
        const auto& latest = v.overview->latest;
        std::string boxes;
        for (const auto& box : v.overview->boxes)
            boxes += "Box " + std::to_string(box.box + 1) + ": " +
                     (box.used ? box.player_name + " - " + save::form_label(box.digimon) + " Lv " + std::to_string(box.level) : std::string("(empty)")) + "\n";
        b.info("Revision " + std::to_string(latest.revision) + ", " + when(latest.uploaded_at) + " from " +
                   (latest.device.empty() ? std::string("an unnamed PC") : latest.device),
               InfoTone::normal);
        b.more("cloud.boxes", "Boxes in the backup", boxes);
        Row& d = b.action("cloud.download", "Download into my save (slot A)", {});
        d.destructive = true;
        d.locked = wait;
        d.confirm = confirm("Replace your save with the backup of " + when(latest.uploaded_at) + "?", "Your current save is copied to backups first.",
                            {"Download", "Cancel"}, {[&panel, &store] { panel.request_download(0, store); }, {}}, 1, 0);
        if (!v.overview->history.empty())
            b.link("cloud.revisions", "Earlier revisions", "revisions", std::to_string(v.overview->history.size()) + " kept");
    } else if (v.known) {
        b.info("There is no cloud backup yet.");
    } else {
        b.info("Reading the backup...");
    }
    b.section(std::string(kThisMachine) == "this PC" ? "This PC's save" : "This device's save");
    if (st.platform == MenuPlatform::android) b.info(local.has ? std::string("Slot A: has a save") : std::string("Slot A: no save yet"), InfoTone::normal);
    else b.info(std::string("Slot A: ") + (local.has ? local.describe : std::string("none")), InfoTone::normal);
    for (std::size_t i = 0; i < local.boxes.size(); ++i) b.info("Box " + std::to_string(i + 1) + ": " + local.boxes[i]);
    Row& up = b.action("cloud.upload", "Upload this save", {});
    up.locked = !local.has ? "There is no local save." : !v.known ? "Wait for the backup to be read." : wait;
    up.confirm = confirm("Replace the cloud backup with this save?",
                         v.overview ? "The replaced one (revision " + std::to_string(v.overview->latest.revision) + ") stays as an earlier revision."
                                    : std::string("Your first cloud backup."),
                         {"Upload", "Cancel"}, {[&panel, &store] { panel.request_upload(store); }, {}}, 1);
    b.action("cloud.logout", "Log out", [&panel] { panel.request_logout(false); });
    Row& forget = b.action("cloud.forget", "Forget this login", {});
    forget.destructive = true;
    forget.confirm = confirm("Forget this login?", std::string("The remembered login is removed from ") + kThisMachine + ".", {"Forget", "Cancel"},
                             {[&panel] { panel.request_logout(true); }, {}}, 1, 0);
    if (!remembers) b.page.rows.pop_back(); // (nothing remembered on Android: Log out is all)
}

// ---------------------------------------------------------------- General

void general_root(Builder& b) {
    auto& a = b.a;
    State& st = b.st;
    Settings& settings = b.view.settings;
    HostView& view = b.view;
    if (a.languages) {
        const auto& entries = a.languages->entries();
        std::vector<std::string> labels;
        int current = 0;
        for (std::size_t i = 0; i < entries.size(); ++i) {
            labels.push_back(entries[i].texts ? entries[i].name : entries[i].name + " (pictures only, no game.tsv)");
            if (entries[i].code == a.languages->selected()) current = int(i);
        }
        Row& r = b.choice("general.language", "Language", labels, current, [&a, &entries](int i) { a.select_language(entries[std::size_t(i)].code); });
        if (!a.languages->has_packs()) r.locked = "Language packs go into lang/<code>/";
        else if (a.languages->selected() != "en") r.desc = "Game text only; the menus stay English.";
    }
    {
        const int current = a.preset_index ? a.preset_index() : 2;
        Row& r = b.choice("general.preset", "Preset", {menu::kPresetNames[0], menu::kPresetNames[1], menu::kPresetNames[2]}, current, {});
        r.picker_hidden = {2};
        r.badge = "Next start";
        const auto ask = [&a, &st](int target) {
            const bool modern = target == 1;
            const std::string name = menu::kPresetNames[target];
            auto c = confirm("Apply " + name + "?",
                             modern ? "Modernized: widescreen, smooth motion, 4x anti-aliasing, a texture pack's 4x textures, 16x anisotropic filtering, "
                                      "a 4x shadow map, straight to the title and fast loading. Anti-aliasing and the intro apply at the next start."
                                    : "Original: 4:3, 60 fps motion, no anti-aliasing, the game's own textures and shadows, the intro, GameCube load "
                                      "times and the original balance.",
                             {"Apply", "Cancel"}, {[&a, modern] { a.apply_preset(modern); }, {}}, 1);
            if (!modern && a.preset_notes)
                for (auto line : a.preset_notes()) {
                    line.tone = InfoTone::warn;
                    c->notes.push_back(line);
                }
            c->row = "general.preset";
            open_confirm(st, c);
        };
        const auto target_of = [current](int dir) { return current == 2 ? (dir < 0 ? 0 : 1) : std::clamp(current + (dir < 0 ? -1 : 1), 0, 1); };
        r.step = [current, ask, target_of](int dir) {
            if (const int target = target_of(dir); target != current) ask(target);
        };
        // (From Custom both arrows lead somewhere: Original and Modernized.)
        r.can_step = [current, target_of](int dir) { return target_of(dir) != current; };
        r.choose = [current, ask](int index) {
            if (index != current && index < 2) ask(index);
        };
    }
    b.more("general.preset_info", "About the presets",
           "Original: 4:3, 60 fps motion, no anti-aliasing, the game's own textures and shadows, the intro, GameCube load times and the original "
           "balance. Modernized: widescreen, smooth motion, anti-aliasing, a texture pack's 4x textures, sharper filtering and shadows, straight to the "
           "title, fast loading.");
    if (a.preset_notes)
        for (const auto& line : a.preset_notes()) b.info(line.text, InfoTone::warn);
    {
        const bool fast = a.loading_fast ? a.loading_fast() : settings.fast_loading;
        Row& r = b.choice("general.loading", "Loading", {menu::kLoadingNames[0], menu::kLoadingNames[1]}, fast ? 1 : 0,
                          [&a](int i) { a.set_loading(i == 1); });
        r.badge = "Next stage";
        r.desc = fast ? "A stage change skips the GameCube's disc reads and loading waits." : "A stage change takes as long as on the GameCube.";
        if (a.net_active && a.net_active()) r.locked = "Online, the host's choice applies.";
    }
    b.section("Sound");
    b.values(RowKind::stepper, "general.volume", "Volume", menu::grid(0, 100, 5), settings.volume, percent,
             [&view](double v) { ops::set_volume(host_ops(view), int(std::lround(v))); });
    b.section("Name tags");
    b.choice("general.label_mode", "Name tags",
             {menu::kLabelModeNames[0], menu::kLabelModeNames[1], menu::kLabelModeNames[2], menu::kLabelModeNames[3]},
             int(parse_label_mode(settings.overhead_labels)), [&view](int i) { ops::set_label_mode(host_ops(view), LabelMode(i)); });
    b.values(RowKind::stepper, "general.label_scale", "Name tag size", menu::grid(50, 200, 10), settings.label_scale, percent,
             [&view](double v) { ops::set_label_scale(host_ops(view), int(std::lround(v))); });
    b.toggle("general.label_numbers", "HP / MP numbers", settings.label_numbers, [&view](bool on) { ops::set_label_numbers(host_ops(view), on); });
    b.section("Pause");
    b.toggle("general.pause", "Pause while the menu is open", settings.pause_in_menu, [&view](bool on) { ops::set_pause_in_menu(host_ops(view), on); });
    {
        // ([menu] pause_unfocused on both platforms: default on Android, off on the PC.)
        Row& r = b.toggle("general.pause_unfocused", "Pause in the background", settings.pause_unfocused,
                          [&view](bool on) { ops::set_pause_unfocused(host_ops(view), on); });
        r.desc = st.platform == MenuPlatform::android ? "While notifications, quick settings or another window are in front."
                                                      : "While another window is in front (Alt+Tab) or the game is minimized.";
    }
    b.section("Start");
    b.toggle("general.skip_intro", "Skip intro", settings.skip_intro, [&view](bool on) { ops::set_skip_intro(host_ops(view), on); }).badge = "Next start";
    if (a.mods && !a.mods->empty()) {
        std::size_t on = 0;
        for (const auto& mod : *a.mods) on += settings.disabled_mods.count(mod.id) ? 0 : 1;
        b.link("general.mods", "Mods", "mods", std::to_string(a.mods->size()) + " mods, " + std::to_string(on) + " on");
    }
    b.link("general.files", "Files", "files");
}

void general_mods(Builder& b) {
    if (!b.a.mods || b.a.mods->empty()) {
        b.page.valid = false;
        return;
    }
    HostView& view = b.view;
    for (const auto& mod : *b.a.mods) {
        Row& r = b.toggle("general.mod." + mod.id, mod.name + " " + mod.version, !view.settings.disabled_mods.count(mod.id),
                          [&view, id = mod.id](bool on) { ops::set_mod_enabled(host_ops(view), id, on); });
        r.desc = (mod.author.empty() ? std::string() : "by " + mod.author + ", ") + std::to_string(mod.file_count) + " files" +
                 (mod.description.empty() ? std::string() : ". " + mod.description);
        if (!mod.problem.empty()) r.extra.push_back({InfoTone::error, mod.problem});
        r.badge = "Next start";
    }
    b.info("Folder: " + b.a.mods_root.string());
}

void general_files(Builder& b) {
    auto& a = b.a;
    b.info("Save slot A: " + (a.save_slot_a ? a.save_slot_a() : std::string()), InfoTone::normal);
    b.info("Settings file: " + (a.settings_file ? a.settings_file().string() : std::string()), InfoTone::normal);
    b.info("Save states: " + (a.state_dir ? a.state_dir().string() : std::string()), InfoTone::normal);
    if (a.controller_log && !a.controller_log().empty()) b.info("Controller log: " + a.controller_log(), InfoTone::normal);
}

// ---------------------------------------------------------------- Display

Screen2Display display_now(const State& st) {
    Screen2Display d = screen2_display();
#ifndef SDL_PLATFORM_ANDROID
    // (A PC test run with DW4_MENU_PLATFORM=android: the Thor's lower screen.)
    if (st.platform == MenuPlatform::android) {
        d.found = d.surface = true;
        d.id = 4;
        d.name = "Screen-2";
        d.width = 1240;
        d.height = 1080;
        d.dpi = 369;
    }
#else
    (void)st;
#endif
    return d;
}

void display_root(Builder& b) {
    Settings& settings = b.view.settings;
    HostView& view = b.view;
    State& st = b.st;
    b.section("Game screen");
    {
        const float display_scale = view.window ? SDL_GetWindowDisplayScale(view.window) : 1.0f;
        const bool follow = st.platform == MenuPlatform::android;
        const int automatic = int(std::lround(ui_scale_factor(0, display_scale, follow) * 100.0f));
        std::vector<double> list;
        for (const int v : kUiScaleChoices) list.push_back(v);
        b.values(RowKind::choice, "display.ui_scale", "Menu size", list, settings.ui_scale,
                 [automatic](double v) { return v == 0 ? "Automatic (now " + std::to_string(automatic) + " %)" : percent(v); },
                 [&view](double v) { ops::set_ui_scale(host_ops(view), int(std::lround(v))); });
    }
    b.toggle("display.widescreen", "Widescreen", settings.widescreen, [&view](bool on) { ops::set_widescreen(host_ops(view), on); }).desc =
        "3D fills the screen, 2D stays 4:3.";
    {
        std::vector<double> list(std::begin(menu::kFrameLimitsAndroid), std::end(menu::kFrameLimitsAndroid));
        b.values(RowKind::choice, "display.frame_limit", "Frame limit", list, settings.fps_limit,
                 [](double v) { return v == 0 ? std::string("Off") : std::to_string(int(std::lround(v))) + " fps"; },
                 [&view](double v) { ops::set_frame_limit(host_ops(view), int(std::lround(v))); });
    }
    b.toggle("display.smooth_motion", "Smooth motion above 60 fps", settings.smooth_motion, [&view](bool on) { ops::set_smooth_motion(host_ops(view), on); })
        .desc = "Now " + st.live_fps + " fps. The game itself runs at 60 steps per second.";
    {
        const int offered = msaa_levels_offered();
        std::vector<std::string> labels;
        for (int i = 0; i < offered; ++i) labels.push_back(menu::kMsaaNames[i]);
        int current = 0;
        for (int i = 0; i < offered; ++i)
            if (settings.msaa >= menu::kMsaaValues[i]) current = i;
        Row& r = b.choice("display.msaa", "Anti-aliasing", labels, current, [&view](int i) { ops::set_msaa(host_ops(view), menu::kMsaaValues[i]); });
        const int now_samples = view.samples > 1 ? view.samples : 0;
        r.desc = "Now " + std::to_string(now_samples) + "x.";
        if (st.platform == MenuPlatform::android && menu::kMsaaValues[current] != now_samples)
            r.desc += " Applies after you close and reopen the app.";
        r.badge = "Next start";
    }
    b.section("Second screen");
    if (st.platform == MenuPlatform::android) {
        b.choice("display.screen2_mode", "Lower screen", {menu::kScreen2ModeShort[0], menu::kScreen2ModeShort[1]},
                 settings.screen2_mode == Screen2Mode::off ? 1 : 0,
                 [&view](int i) { ops::set_screen2_mode(host_ops(view), i == 1 ? Screen2Mode::off : Screen2Mode::automatic); });
        const Screen2Display d = display_now(st);
        if (settings.screen2_mode == Screen2Mode::off) b.info("The panels are not shown on a second screen.");
        else if (d.surface)
            b.info("Now on display " + std::to_string(d.id) + " \"" + d.name + "\", " + std::to_string(d.width) + " x " + std::to_string(d.height));
        else b.info("No second screen now");
    }
    b.toggle("display.screen2_overlay", "Panels over the game", settings.screen2_overlay, [&view](bool on) { ops::set_screen2_overlay(host_ops(view), on); })
        .desc = "The Status and Online panels as a window over the game.";
    b.link("display.advanced", "Advanced", "advanced");
}

void display_advanced(Builder& b) {
    HostView& view = b.view;
    b.toggle("display.vsync", "VSync", view.settings.vsync, [&view](bool on) { ops::set_vsync(host_ops(view), on); });
    b.link("display.performance", "Performance", "performance");
}

void display_performance(Builder& b) {
    State& st = b.st;
    b.page.live = true;
    float worst = 0, total = 0;
    for (const float t : st.live_frames) {
        worst = std::max(worst, t);
        total += t;
    }
    char text[64];
    std::snprintf(text, sizeof text, "Average %.1f ms", total / float(st.live_frames.size()));
    b.info(text, InfoTone::normal);
    std::snprintf(text, sizeof text, "Worst %.1f ms", worst);
    b.info(text, InfoTone::normal);
    b.info("Now " + st.live_fps + " fps", InfoTone::normal);
    // The frame time graph (the last 240 frames; taken at 2 Hz like the
    // numbers, so the lower screen does not swap every pass).
    Row& graph = b.add(RowKind::progress, "display.frame_graph", "Frame time, the last 240 frames (the line: 16.7 ms)");
    graph.graph.assign(st.live_frames.begin(), st.live_frames.end());
    graph.graph_full = std::max(33.4f, worst);
}

// ---------------------------------------------------------------- Graphics

void graphics_root(Builder& b) {
    HostView& view = b.view;
    auto& g = view.settings.graphics;
    const GlRenderer::Caps caps = view.renderer ? view.renderer->caps() : GlRenderer::Caps{};
    const auto set = [&view](GraphicsOptions options) { ops::set_graphics(host_ops(view), options); };
    b.choice("graphics.texture_quality", "Texture quality",
             {texture_quality_label(TextureQuality::original), texture_quality_label(TextureQuality::high), texture_quality_label(TextureQuality::ultra)},
             int(g.texture_quality), [&g, set](int i) {
                 GraphicsOptions o = g;
                 o.texture_quality = TextureQuality(i);
                 set(o);
             });
    if (b.a.texture_panel) {
        const TexturePackPanel panel = b.a.texture_panel();
        if (panel.loading) b.info("Reading the texture pack folder " + panel.folder.string() + " ...");
        else if (panel.pack) {
            const auto& index = panel.pack->index();
            if (!index.error.empty()) b.info("The texture pack is not used: " + index.error, InfoTone::error);
            else if (index.textures.empty()) b.info("No texture pack in " + panel.folder.string() + ".");
            else b.info((index.name.empty() ? std::string() : index.name + ": ") + std::to_string(index.textures.size()) + " pack textures");
            if (!index.other_encodings.empty()) {
                std::string names;
                for (const auto& name : index.other_encodings) names += (names.empty() ? "" : ", ") + name;
                b.info("The pack also holds files for other devices (" + names + ").");
            }
            if (!index.warnings.empty()) b.link("graphics.pack_warnings", "Pack warnings (" + std::to_string(index.warnings.size()) + ")", "warnings");
        }
    }
    {
        const int count = texture_filters_offered(caps);
        std::vector<std::string> labels;
        for (int i = 0; i < count; ++i) labels.push_back(texture_filter_label(TextureFilter(i)));
        b.choice("graphics.texture_filter", "Texture filtering", labels, std::min(int(g.texture_filter), count - 1), [&g, set](int i) {
            GraphicsOptions o = g;
            o.texture_filter = TextureFilter(i);
            set(o);
        });
    }
    b.choice("graphics.sharpening", "Sharpening", {sharpening_label(Sharpening::off), sharpening_label(Sharpening::low), sharpening_label(Sharpening::high)},
             int(g.sharpening), [&g, set](int i) {
                 GraphicsOptions o = g;
                 o.sharpening = Sharpening(i);
                 set(o);
             });
    b.choice("graphics.shadow_quality", "Shadow quality",
             {shadow_quality_label(ShadowQuality::original), shadow_quality_label(ShadowQuality::high), shadow_quality_label(ShadowQuality::ultra)},
             int(g.shadow_quality), [&g, set](int i) {
                 GraphicsOptions o = g;
                 o.shadow_quality = ShadowQuality(i);
                 set(o);
             });
    {
        const std::vector<double> list(std::begin(menu::kTextureMemoryValues), std::end(menu::kTextureMemoryValues));
        Row& r = b.values(RowKind::choice, "graphics.texture_memory", "Texture memory", list, g.texture_memory_mb,
                          [](double v) {
                              for (std::size_t i = 0; i < std::size(menu::kTextureMemoryValues); ++i)
                                  if (menu::kTextureMemoryValues[i] == int(v)) return std::string(menu::kTextureMemoryNames[i]);
                              return std::to_string(int(v)) + " MiB (settings.ini)";
                          },
                          [&g, set](double v) {
                              GraphicsOptions o = g;
                              o.texture_memory_mb = int(v);
                              set(o);
                          });
        r.desc = b.st.live_memory;
    }
    b.link("graphics.advanced", "Advanced", "advanced");
}

void graphics_warnings(Builder& b) {
    if (!b.a.texture_panel) return;
    const TexturePackPanel panel = b.a.texture_panel();
    if (!panel.pack || panel.pack->index().warnings.empty()) {
        b.page.valid = false;
        return;
    }
    for (const auto& w : panel.pack->index().warnings) b.info(w, InfoTone::warn);
}

void graphics_advanced(Builder& b) {
    HostView& view = b.view;
    auto& g = view.settings.graphics;
    char buffer[16];
    b.values(RowKind::stepper, "graphics.lod_bias", "Mip LOD bias", menu::grid(GraphicsOptions::kLodBiasMin, GraphicsOptions::kLodBiasMax, menu::kLodBiasStep),
             g.lod_bias,
             [&buffer](double v) {
                 std::snprintf(buffer, sizeof buffer, "%+.2f", v);
                 return std::string(buffer);
             },
             [&view](double v) {
                 GraphicsOptions o = view.settings.graphics;
                 o.lod_bias = float(v);
                 ops::set_graphics(host_ops(view), o);
             })
        .desc = "For mipmapped textures: below 0 sharper (may shimmer), above 0 softer.";
    F1Actions& a = b.a;
    const bool android = b.st.platform == MenuPlatform::android;
    if (android) {
        // (Android: the packs' folder is the app's own; a typed path could
        // point nowhere the app may read.)
        // (A relative folder, while the app's mods folder does not exist
        // yet: where it goes, under the app's own folder.)
        std::filesystem::path folder = a.texture_panel ? a.texture_panel().folder : std::filesystem::path();
        if (folder.is_relative() && !app_root().empty()) folder = app_root() / folder;
        b.info("Put texture packs into " + folder.generic_string() + ".", InfoTone::normal);
    } else {
        Row& folder = b.add(RowKind::text, "graphics.pack_folder", "Texture pack folder");
        folder.text = view.settings.texture_pack.string();
        folder.hint = "mods/textures";
        folder.max_length = 255;
        folder.plain = true; // (a path: no suggestions, no autocorrection, no capitals)
        folder.commit = [&a, &view](const std::string& text) {
            const auto reload = a.texture_panel ? a.texture_panel().reload : std::function<void()>();
            ops::set_texture_pack_folder(host_ops(view), std::filesystem::path(std::u8string(text.begin(), text.end())), reload);
        };
    }
    b.action("graphics.reload", "Reload texture pack", [&a] {
        if (a.texture_panel)
            if (const auto reload = a.texture_panel().reload) reload();
    });
    b.info(std::string("A pack is a folder with a dw4pack.tsv manifest and DDS files, or loose PNG / DDS files named ..._<16 hex digit hash>.png / .dds") +
           (android ? "." : " (docs/TEXTURES.md)."));
}

// ---------------------------------------------------------------- Controls

void controls_root(Builder& b) {
    HostView& view = b.view;
    Settings& settings = view.settings;
    State& st = b.st;
    const auto& known = view.pads.pads();
    if (known.empty()) b.info("No controller: touch only", InfoTone::normal);
    for (std::size_t i = 0; i < known.size() && i < 4; ++i)
        b.info("Player " + std::to_string(i + 1) + ": " + family_name(known[i].family) + " (" + known[i].name + ")", InfoTone::normal);
    {
        int current = 0;
        for (int i = 0; i < int(std::size(menu::kPromptFamilies)); ++i)
            if (settings.prompts == family_id(menu::kPromptFamilies[i])) current = i + 1;
        std::vector<std::string> labels(std::begin(menu::kPromptNames), std::end(menu::kPromptNames));
        b.choice("controls.prompts", "Button prompts", labels, current, [&view](int i) {
            ops::set_prompts(host_ops(view), i == 0 ? std::nullopt : std::optional<PadFamily>(menu::kPromptFamilies[i - 1]));
        });
        const PadFamily family = view.prompt_family();
        // (Each GameCube control with the picture it shows: "L {L2}".)
        Row& preview = b.info(std::string("The game's buttons on ") + family_name(family) + ":");
        preview.glyph_family = family;
        static const std::pair<PromptControl, const char*> kPreview[] = {
            {PromptControl::a, "A"},         {PromptControl::b, "B"},     {PromptControl::x, "X"},           {PromptControl::y, "Y"},
            {PromptControl::start, "START"}, {PromptControl::l, "L"},     {PromptControl::r, "R"},           {PromptControl::z, "Z"},
            {PromptControl::stick, "Stick"}, {PromptControl::c_stick, "C-stick"}, {PromptControl::dpad, "D-pad"}};
        for (const auto& [control, name] : kPreview) {
            preview.glyphs.push_back(prompt_face(family, settings, control));
            preview.glyph_captions.push_back(name);
        }
    }
    {
        bool thor_pad = false, gc_pad = false;
        for (const auto& pad : known) {
            thor_pad = thor_pad || pad.family == PadFamily::thor;
            gc_pad = gc_pad || pad.family == PadFamily::gamecube;
        }
        // (As on the PC: with a Thor's pad or a chosen style; a PC test run
        // with Android's rules shows it always.)
        if (thor_pad || settings.thor_style != ThorStyle::automatic || (view.test_run && st.platform == MenuPlatform::android)) {
            const std::string automatic = std::string("As the Thor's setting (") +
                                          (view.thor_device ? (*view.thor_device == ThorStyle::xbox ? "Xbox" : "Standard") : "unknown") + ")";
            const int current = settings.thor_style == ThorStyle::standard ? 1 : settings.thor_style == ThorStyle::xbox ? 2 : 0;
            b.choice("controls.thor_style", "AYN Thor controller style", {automatic, menu::kThorStyleNames[0], menu::kThorStyleNames[1]}, current,
                     [&view](int i) {
                         ops::set_thor_style(host_ops(view), i == 1 ? ThorStyle::standard : i == 2 ? ThorStyle::xbox : ThorStyle::automatic);
                     })
                .desc = "Thor Settings > Controller style.";
        }
        b.choice("controls.nintendo_layout", "Nintendo layout", {menu::kLayoutNames[0], menu::kLayoutNames[1]},
                 settings.nintendo_layout == NintendoLayout::by_position ? 1 : 0,
                 [&view](int i) { ops::set_nintendo_layout(host_ops(view), i ? NintendoLayout::by_position : NintendoLayout::by_label); });
        Row& menu_row = b.choice("controls.menu_buttons", "Menu buttons", {menu::kMenuButtonNames[0], menu::kMenuButtonNames[1]},
                                 settings.menu_buttons == MenuButtons::bindings ? 1 : 0,
                                 [&view](int i) { ops::set_menu_buttons(host_ops(view), i ? MenuButtons::bindings : MenuButtons::layout); });
        (void)menu_row;
        if (gc_pad || settings.gamecube_layout != GameCubeLayout::original)
            b.choice("controls.gamecube_layout", "GameCube controller", {menu::kGameCubeNames[0], menu::kGameCubeNames[1]},
                     settings.gamecube_layout == GameCubeLayout::bindings ? 1 : 0,
                     [&view](int i) { ops::set_gamecube_layout(host_ops(view), i ? GameCubeLayout::bindings : GameCubeLayout::original); });
        Row& glyphs = b.info("The menus' buttons now:");
        glyphs.glyph_family = view.prompt_family();
        glyphs.glyphs = {console_face(ConsoleGlyph::ok, glyphs.glyph_family, settings, st.platform),
                         console_face(ConsoleGlyph::back, glyphs.glyph_family, settings, st.platform)};
        glyphs.glyph_captions = {"Confirm", "Back"};
    }
    b.section("Bindings");
    for (int group = 0; group < 4; ++group) {
        static const char* ids[] = {"buttons", "stick", "dpad", "cstick"};
        b.link(std::string("controls.bind.") + ids[group], pad_group_name(PadGroup(group)), std::string("bindings.") + ids[group]);
    }
    Row& reset = b.action("controls.reset", "Default controller layout", {});
    reset.destructive = true;
    reset.confirm = confirm("Reset all controller bindings to the defaults?", "Every button, stick, D-pad and C-stick binding goes back to the default.",
                            {"Reset", "Cancel"}, {[&view] { ops::reset_pad_bindings(host_ops(view)); }, {}}, 1, 0);
    if (view.controller_log) b.info("Controller log: " + view.controller_log->path().string());
}

void controls_bindings(Builder& b, int group) {
    HostView& view = b.view;
    Settings& settings = view.settings;
    const PadFamily family = binding_family(view);
    {
        // ("Clear a slot: long-press it or {DEL}": the clear button's picture
        // after its caption, on a line of its own.)
        Row& how = b.info("Tap a slot, then press a button or pull a trigger.");
        how.glyph_family = view.prompt_family();
        how.glyphs = {console_face(ConsoleGlyph::del, how.glyph_family, settings, b.st.platform)};
        how.glyph_captions = {"Clear a slot: long-press it or"};
        if (how.glyphs.front().face == Face::none && how.glyphs.front().key.empty()) {
            how.glyphs.clear();
            how.glyph_captions.clear();
            how.label = "Tap a slot, then press a button or pull a trigger. To clear a slot, long-press it.";
        }
    }
    if (family == PadFamily::gamecube && settings.gamecube_layout == GameCubeLayout::original)
        b.info("A GameCube controller plays 1:1 and does not use these bindings.", InfoTone::warn);
    b.info(std::string("Pictures: ") + family_name(family));
    static const char* ids[] = {"buttons", "stick", "dpad", "cstick"};
    for (std::size_t k = 0; k < settings.pad.size(); ++k) {
        if (pad_key_group(PadKey(k)) != PadGroup(group)) continue;
        Row& r = b.add(RowKind::binding, std::string("controls.bind.") + ids[group] + "." + pad_key_ini_name(PadKey(k)), pad_key_name(PadKey(k)));
        r.key = PadKey(k);
        for (std::size_t slot = 0; slot < settings.pad[k].size(); ++slot) {
            Cell cell;
            const PadInput& input = settings.pad[k][slot];
            cell.label = pad_input_label(family, settings, input);
            if (cell.label.empty()) cell.label = "-";
            const Face face = face_of(family, settings.nintendo_layout, input);
            if (face != Face::none && face != Face::misc) {
                cell.glyph.face = face;
                cell.has_glyph = true;
            }
            r.cells.push_back(cell);
        }
    }
}

// ---------------------------------------------------------------- Cheats

void cheats_root(Builder& b) {
    auto& a = b.a;
    HostView& view = b.view;
    Settings& settings = view.settings;
    const bool off = a.cheats_online && a.cheats_online();
    b.section("This session (not saved)");
    if (a.cheats) {
        auto& cheats = a.cheats();
        const auto session = [&](const char* id, const char* label, bool& field) {
            Row& r = b.toggle(id, label, field, [&field](bool on) { field = on; });
            r.badge = "This session";
            if (off) r.locked = "Cheats are off online.";
        };
        session("cheats.god", "God mode", cheats.god);
        session("cheats.infinite_mp", "Infinite MP", cheats.infinite_mp);
        session("cheats.one_hit", "One-hit defeats", cheats.one_hit);
    }
    b.section("Balance (saved)");
    const std::vector<double> rates(std::begin(menu::kRateSteps), std::end(menu::kRateSteps));
    const auto rate = [&](const char* id, const char* label, int& field) {
        Row& r = b.values(RowKind::stepper, id, label, rates, field, percent, [&field, &a](double v) {
            field = int(std::lround(v));
            if (a.gameplay_changed) a.gameplay_changed();
        });
        if (off) r.locked = "Online, the host's settings apply.";
    };
    rate("cheats.exp_rate", "EXP from enemies", settings.exp_rate);
    rate("cheats.damage_dealt", "Damage of your hits", settings.damage_dealt);
    rate("cheats.damage_taken", "Damage enemies do", settings.damage_taken);
    {
        std::string status;
        if (a.randomizer_status)
            for (const auto& line : a.randomizer_status()) status += (status.empty() ? "" : " ") + line.text;
        b.link("cheats.randomizer", "Randomizer", "randomizer", settings.rand_seed ? "Seed " + std::to_string(settings.rand_seed) + ". " + status : status);
    }
    if (a.players) {
        const auto players = a.players();
        if (!players.empty()) b.section("Players");
        for (const auto& p : players) b.link("cheats.player." + std::to_string(p.slot + 1), "Player " + std::to_string(p.slot + 1), "player." + std::to_string(p.slot + 1), p.stats);
    }
}

void cheats_randomizer(Builder& b) {
    auto& a = b.a;
    HostView& view = b.view;
    Settings& settings = view.settings;
    const bool off = a.cheats_online && a.cheats_online();
    if (a.randomizer_status) b.infos_of(a.randomizer_status());
    // (Online the PC shows them disabled: the same reason as the rows below.)
    const std::string lock = off ? "Online, the host's settings apply." : "";
    if (a.randomizer_can_unbind && a.randomizer_can_unbind()) {
        Row& r = b.action("cheats.unbind", "Play this save without the randomizer", {});
        r.locked = lock;
        r.confirm = confirm("Play this save without the randomizer?", "The save plays the original game from now on, also after a restart.",
                            {"Play original", "Cancel"},
                            {[&a] {
                                 a.randomizer_unbind();
                                 if (a.gameplay_changed) a.gameplay_changed();
                             },
                             {}},
                            1);
    }
    if (a.randomizer_can_bind && a.randomizer_can_bind()) {
        Row& r = b.action("cheats.bind", "Bind the settings below to this save", {});
        r.locked = lock;
        r.confirm = confirm("Bind the settings below to this save?", "The save plays these randomizer settings from now on.", {"Bind", "Cancel"},
                            {[&a] {
                                 a.randomizer_bind();
                                 if (a.gameplay_changed) a.gameplay_changed();
                             },
                             {}},
                            1);
    }
    b.section("For new games");
    Row& seed = b.add(RowKind::number, "cheats.seed", "Seed");
    seed.text = std::to_string(settings.rand_seed);
    seed.desc = "0 is off.";
    seed.min = 0;
    seed.max = 2147483647.0; // (settings.ini keeps 31 bits: settings.cpp store)
    seed.max_length = 10;
    seed.locked = lock;
    seed.commit = [&settings, &a](const std::string& text) {
        settings.rand_seed = std::uint32_t(std::strtoull(text.c_str(), nullptr, 10));
        if (a.gameplay_changed) a.gameplay_changed();
    };
    b.action("cheats.new_seed", "New seed", [&a] {
         a.randomizer_new_seed();
         if (a.gameplay_changed) a.gameplay_changed();
     }).locked = lock;
    b.toggle("cheats.drops", "Item drops", settings.rand_drops, [&settings, &a](bool on) {
         settings.rand_drops = on;
         if (a.gameplay_changed) a.gameplay_changed();
     }).locked = lock;
    b.choice("cheats.enemies", "Enemies", {menu::kEnemyModeNames[0], menu::kEnemyModeNames[1], menu::kEnemyModeNames[2]}, std::clamp(settings.rand_enemies, 0, 2),
             [&settings, &a](int i) {
                 settings.rand_enemies = i;
                 if (a.gameplay_changed) a.gameplay_changed();
             })
        .locked = lock;
    b.choice("cheats.zones", "Loading zones", {menu::kZoneModeNames[0], menu::kZoneModeNames[1]}, std::clamp(settings.rand_zones, 0, 1),
             [&settings, &a](int i) {
                 settings.rand_zones = i;
                 if (a.gameplay_changed) a.gameplay_changed();
             })
        .locked = lock;
    b.info("These apply to new games.");
}

void cheats_player(Builder& b, int slot) {
    auto& a = b.a;
    const auto players = a.players ? a.players() : std::vector<PlayerLine>{};
    const auto found = std::find_if(players.begin(), players.end(), [&](const PlayerLine& p) { return p.slot == slot; });
    if (found == players.end()) {
        b.page.valid = false;
        return;
    }
    b.info(found->stats, InfoTone::normal);
    b.action("cheats.refill", "Refill HP / MP", [&a, slot] { a.cheat_refill(slot); });
    b.action("cheats.level1", "+1 level", [&a, slot] { a.cheat_levels(slot, 1); });
    b.action("cheats.level10", "+10 levels", [&a, slot] { a.cheat_levels(slot, 10); });
    b.action("cheats.bits", "+100,000 bits", [&a, slot] { a.cheat_bits(slot, 100000); });
}

// ---------------------------------------------------------------- Achievements

std::string day(std::int64_t unix_seconds) {
    const std::chrono::sys_days days = std::chrono::floor<std::chrono::days>(std::chrono::sys_seconds(std::chrono::seconds(unix_seconds)));
    const std::chrono::year_month_day date(days);
    char text[16];
    std::snprintf(text, sizeof text, "%04d-%02u-%02u", int(date.year()), unsigned(date.month()), unsigned(date.day()));
    return text;
}

void achievements_root(Builder& b) {
    auto& a = b.a;
    HostView& view = b.view;
    b.toggle("achievements.enabled", "Achievements", view.settings.achievements, [&view](bool on) { ops::set_achievements(host_ops(view), on); });
    b.toggle("achievements.popups", "Unlock notices", view.settings.achievement_popups,
             [&view](bool on) { ops::set_achievement_popups(host_ops(view), on); });
    if (!a.achievements) return;
    const auto list = achievement_list();
    const auto& profile = a.achievements->profile();
    b.info(std::to_string(profile.unlocked.size()) + " of " + std::to_string(list.size()) + " unlocked - play time " + (a.play_time ? a.play_time() : ""),
           InfoTone::normal);
    if (a.achievement_notes) b.infos_of(a.achievement_notes());
    b.more("achievements.about", "About achievements",
           std::string("Offline achievements stay on ") + kThisMachine +
               ". Cheats, rebalancing and a save state loaded in the running stage pause them; runs with the randomizer count separately.");
    for (int category = 0; category <= int(AchievementCategory::multiplayer); ++category) {
        const auto cat = AchievementCategory(category);
        std::size_t total = 0, done = 0;
        for (const auto& def : list)
            if (def.category == cat) {
                ++total;
                done += profile.unlocked.count(def.id) ? 1 : 0;
            }
        b.link("achievements.cat." + std::to_string(category), achievement_category_name(cat), "cat." + std::to_string(category),
               std::to_string(done) + " / " + std::to_string(total));
    }
}

void achievements_category(Builder& b, int category) {
    auto& a = b.a;
    if (!a.achievements || category < 0 || category > int(AchievementCategory::multiplayer)) {
        b.page.valid = false;
        return;
    }
    const auto& profile = a.achievements->profile();
    for (const auto& def : achievement_list()) {
        if (def.category != AchievementCategory(category)) continue;
        Row& r = b.add(RowKind::entry, std::string("achievements.") + def.id, def.title);
        r.desc = def.description;
        const auto found = profile.unlocked.find(def.id);
        if (found != profile.unlocked.end()) {
            r.right = day(found->second);
            r.right_tone = InfoTone::ok;
        } else if (def.stat && def.goal > 0) {
            const auto value = std::min<std::int64_t>(profile.stat(def.stat), def.goal);
            r.progress = float(value) / float(def.goal);
            r.right = std::to_string(value) + " / " + std::to_string(def.goal);
        } else {
            r.right = "-";
        }
        if (const auto random = profile.unlocked_randomizer.find(def.id); random != profile.unlocked_randomizer.end())
            r.extra.push_back({InfoTone::dim, "Randomizer: " + day(random->second)});
    }
}

// ---------------------------------------------------------------- Online (v1)

void online_root(Builder& b) {
    auto& a = b.a;
    HostView& view = b.view;
    b.toggle("online.net_indicator", "Network indicator", view.settings.net_indicator, [&view](bool on) { ops::set_net_indicator(host_ops(view), on); })
        .desc = "Ping and input delay in online sessions.";
    const bool net = a.net_active && a.net_active();
    const bool in_lobby = a.net_in_lobby && a.net_in_lobby();
    const bool room = a.online_room && a.online_room();
    if (net) {
        if (a.session_lines) b.infos_of(a.session_lines());
        Row& leave = b.action("online.leave_session", "Leave the session", {});
        leave.destructive = true;
        leave.confirm = confirm("Leave the session?", "The direct session ends for this machine.", {"Leave", "Cancel"}, {[&a] { a.end_session(); }, {}}, 1, 0);
    } else if (in_lobby) {
        if (a.session_lines) b.infos_of(a.session_lines());
        b.action("online.cancel", "Cancel", [&a] { a.net_close(); });
    } else if (room) {
        if (a.session_lines) b.infos_of(a.session_lines());
        Row& off = b.action("online.go_offline", "Go offline", {});
        off.confirm = confirm("Go offline?", "You leave the world session.", {"Go offline", "Stay"}, {[&a] { a.leave_room(); }, {}}, 1);
    } else if (a.has_lobby && a.has_lobby()) {
        b.info(online::kAndroidRefusal, InfoTone::warn);
    } else {
        b.info("This run has no online screens.");
    }
    if (room) b.link("online.connection", "Connection", "connection");
}

void online_connection(Builder& b) {
    auto& a = b.a;
    if (!(a.online_room && a.online_room())) {
        b.page.valid = false;
        return;
    }
    if (a.connection_lines) b.infos_of(a.connection_lines());
    b.action("online.copy_path", "Copy path", [&a] { a.copy_log_path(); });
}

// ---------------------------------------------------------------- Developer

void developer_root(Builder& b) {
    State& st = b.st;
    if (b.a.stage_info) b.info(b.a.stage_info(), InfoTone::normal);
    b.action("developer.classic", "Classic menu", [&st] { st.classic = true; }).desc = "The PC's F1 menu until it closes.";
}

const char* sub_title(const std::string& id) {
    if (id == "card") return "Where the save lives";
    if (id == "cloud") return "Cloud backup";
    if (id == "revisions") return "Earlier revisions";
    if (id == "mods") return "Mods";
    if (id == "files") return "Files";
    if (id == "advanced") return "Advanced";
    if (id == "performance") return "Performance";
    if (id == "warnings") return "Pack warnings";
    if (id == "randomizer") return "Randomizer";
    if (id == "connection") return "Connection";
    if (id == "bindings.buttons") return "Buttons";
    if (id == "bindings.stick") return "Main stick";
    if (id == "bindings.dpad") return "D-pad";
    if (id == "bindings.cstick") return "C-stick";
    return nullptr;
}

std::string page_title(const std::string& root, const std::string& id) {
    if (const char* t = sub_title(id)) return t;
    if (id.rfind("player.", 0) == 0) return "Player " + id.substr(7);
    if (root == "achievements" && id.rfind("cat.", 0) == 0) return achievement_category_name(AchievementCategory(std::atoi(id.c_str() + 4)));
    return id;
}

} // namespace

std::vector<std::string> category_roots(const State& st) {
    std::vector<std::string> roots = {"saves", "general", "display", "graphics", "controls", "cheats", "achievements", "online"};
    if (st.developer) roots.push_back("developer");
    return roots;
}

const char* category_title(const std::string& root) {
    if (root == "saves") return "Saves";
    if (root == "general") return "General";
    if (root == "display") return "Display";
    if (root == "graphics") return "Graphics";
    if (root == "controls") return "Controls";
    if (root == "cheats") return "Cheats";
    if (root == "achievements") return "Achievements";
    if (root == "online") return "Online";
    if (root == "developer") return "Developer";
    return "";
}

std::string page_trail(State& st, HostView& view) {
    (void)view;
    const auto& stack = st.nav.stack();
    if (stack.empty()) return {};
    std::string trail = category_title(stack.front());
    for (std::size_t i = 1; i < stack.size(); ++i) trail += " > " + page_title(stack.front(), stack[i]);
    return trail;
}

std::string row_value_text(const Row& row) {
    switch (row.kind) {
    case RowKind::toggle: return row.on ? "On" : "Off";
    case RowKind::choice:
    case RowKind::stepper: return row.current >= 0 && row.current < int(row.options.size()) ? row.options[std::size_t(row.current)].label : row.value;
    case RowKind::text:
    case RowKind::number: return row.text;
    case RowKind::password: return row.text.empty() ? "" : "****";
    case RowKind::cells:
    case RowKind::binding: {
        std::string out;
        for (const auto& cell : row.cells) out += (out.empty() ? "" : "|") + cell.label;
        return out;
    }
    default: return row.label;
    }
}

void build_page(State& st, HostView& view, Page& page) {
    page.rows.clear();
    page.rows.reserve(96);
    page.valid = true;
    page.live = false;
    const auto& stack = st.nav.stack();
    if (stack.empty()) return;
    const std::string& root = stack.front();
    const std::string sub = stack.size() > 1 ? stack[1] : std::string();
    const std::string sub2 = stack.size() > 2 ? stack[2] : std::string();
    page.title = sub.empty() ? category_title(root) : page_title(root, stack.back());
    // (The texture memory's "In use" line, at 2 Hz like the other live texts.)
    if (root == "graphics" && view.renderer && (st.live_memory.empty() || st.live_at == st.now)) {
        const auto s = menu_still() ? GlRenderer::TextureStats{} : view.renderer->texture_stats();
        constexpr double mib = 1024.0 * 1024.0;
        char text[160];
        std::snprintf(text, sizeof text, "In use: %zu game texture%s (%.0f MiB), %zu pack texture%s (%.0f MiB)%s.", s.originals, s.originals == 1 ? "" : "s",
                      double(s.original_bytes) / mib, s.replacements, s.replacements == 1 ? "" : "s", double(s.replacement_bytes) / mib,
                      s.pending ? ", more loading" : "");
        st.live_memory = text;
    }
    Builder b{st, view, st.actions, page};
    if (root == "saves") {
        if (sub.empty()) saves_root(b);
        else if (sub == "card") saves_card(b);
        else if (sub == "cloud") saves_cloud(b, sub2 == "revisions");
        else page.valid = false;
    } else if (root == "general") {
        if (sub.empty()) general_root(b);
        else if (sub == "mods") general_mods(b);
        else if (sub == "files") general_files(b);
        else page.valid = false;
    } else if (root == "display") {
        if (sub.empty()) display_root(b);
        else if (sub == "advanced" && sub2.empty()) display_advanced(b);
        else if (sub == "advanced" && sub2 == "performance") display_performance(b);
        else page.valid = false;
    } else if (root == "graphics") {
        if (sub.empty()) graphics_root(b);
        else if (sub == "warnings") graphics_warnings(b);
        else if (sub == "advanced") graphics_advanced(b);
        else page.valid = false;
    } else if (root == "controls") {
        if (sub.empty()) controls_root(b);
        else if (sub == "bindings.buttons") controls_bindings(b, 0);
        else if (sub == "bindings.stick") controls_bindings(b, 1);
        else if (sub == "bindings.dpad") controls_bindings(b, 2);
        else if (sub == "bindings.cstick") controls_bindings(b, 3);
        else page.valid = false;
    } else if (root == "cheats") {
        if (sub.empty()) cheats_root(b);
        else if (sub == "randomizer") cheats_randomizer(b);
        else if (sub.rfind("player.", 0) == 0) cheats_player(b, std::atoi(sub.c_str() + 7) - 1);
        else page.valid = false;
    } else if (root == "achievements") {
        if (sub.empty()) achievements_root(b);
        else if (sub.rfind("cat.", 0) == 0) achievements_category(b, std::atoi(sub.c_str() + 4));
        else page.valid = false;
    } else if (root == "online") {
        if (sub.empty()) online_root(b);
        else if (sub == "connection") online_connection(b);
        else page.valid = false;
    } else if (root == "developer") {
        developer_root(b);
    }
}

} // namespace dw4::game::console
