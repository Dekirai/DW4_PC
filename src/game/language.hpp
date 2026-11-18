#pragma once

// The game text's display language (docs/L10N.md): English, the game's own
// text (no files), and the language packs in lang/<code>/ next to the
// program. Host presentation state only: the engine gets an immutable
// catalog between two steps (Engine::set_display_text, which lays every text
// line out again), the renderer the language's pictures
// (GlRenderer::set_language_textures). None of it is part of the game
// state, a fingerprint, a save, a save state, a snapshot or an online
// session: players with different languages play the same session.
#include "language_switches.hpp"
#include "texture_pack.hpp"

#include "dw4/i18n/game_text.hpp"
#include "dw4/i18n/language_pack.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dw4::engine {
class Engine;
struct FontImage;
} // namespace dw4::engine

namespace dw4::game {

class GlRenderer;

class Languages {
public:
    struct Entry {
        std::string code;  // "en" for English
        std::string name;  // in its own language ("English", "Deutsch (Fan-Übersetzung)"), UTF-8
        bool texts = true; // false: the pack has no game.tsv (its pictures only, the text stays English)
        // The F1 menus' name for it (the PC's and the console's alike): a pack
        // without its game.tsv, the release's form, swaps pictures only.
        std::string menu_label() const { return texts ? name : name + " (pictures only)"; }
    };
    // The language packs below lang_root (lang/ next to the program);
    // English is always the first entry.
    void discover(const std::filesystem::path& lang_root);
    const std::vector<Entry>& entries() const { return entries_; }
    bool has_packs() const { return entries_.size() > 1; }
    // What happened (unusable packs, rejected catalog lines, pictures not
    // found), for the log; taken once.
    std::vector<std::string> take_notes();
    // Selects a language by its code. A pack's files are read again at once
    // (game.tsv, textures.tsv); the engine and the renderer take them at the
    // next sync. False (the selection stays) for an unknown code or a pack
    // that cannot be read; `message` says why.
    bool select(std::string_view code, std::string* message = nullptr);
    const std::string& selected() const { return selected_; }
    // Between two steps: the engine gets the selected language's catalog
    // (null for English), the renderer its pictures and the font atlas the
    // composed glyph cells. Cheap when nothing changed.
    void sync(engine::Engine& engine, GlRenderer* renderer);
    // Test runs: language changes at given frames (DW4_L10N_SWITCH
    // "F:code,F:code"), or every N frames between English and `other`
    // (DW4_L10N_SWITCH_EVERY); applied by at_frame before that frame's step,
    // once however many passes run at that frame (LanguageSwitches).
    bool parse_schedule(std::string_view text, std::string* error = nullptr);
    void switch_every(int frames, std::string other);
    void at_frame(int frame);
    // Test runs: the built language pictures are written there as PNG
    // files (DW4_L10N_DUMP).
    void set_dump(std::filesystem::path folder) { dump_ = std::move(folder); }

private:
    struct Pack {
        i18n::LanguageInfo info;
        std::shared_ptr<const i18n::GameTextCatalog> catalog;
        i18n::TextureRules rules;
        // The pictures the rules make (built once the engine's font is
        // there: a render rule draws with it).
        LanguageTextures textures;
        bool textures_built = false;
    };
    bool load(Pack& pack, std::string& message);       // (never throws)
    bool load_files(Pack& pack, std::string& message); // (load's body)
    void build_textures(Pack& pack, engine::Engine& engine);
    void note(std::string text) { notes_.push_back(std::move(text)); }

    std::vector<Entry> entries_;
    std::map<std::string, Pack> packs_;
    std::string selected_ = "en";
    std::vector<std::string> notes_;
    std::uint64_t generation_ = 1;          // changes with the selection and the built pictures
    std::uint64_t renderer_generation_ = 0; // what the renderer was given last
    const GlRenderer* renderer_ = nullptr;
    LanguageSwitches switches_;
    std::filesystem::path dump_;
};

// The travel dialog's picture (stcngmoji.png, 256 x 64: "Go to the
// destination." over "Stay here.") drawn in the catalog's language with
// the game font (render:travel_dialog in textures.tsv); empty when the
// catalog lacks one of the two lines or the font cannot draw it.
TextureImage render_travel_dialog(const i18n::GameTextCatalog& catalog, const engine::FontImage& font, const std::vector<std::uint8_t>& atlas,
                                  int atlas_width, int atlas_height, std::string* problem = nullptr);

} // namespace dw4::game
