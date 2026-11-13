#pragma once

// Language packs (docs/L10N.md): a folder lang/<code>/ next to the program.
// English needs none; every other language is one folder with
//
//   language.ini   [language] code, name (in its own language), revision
//                  (SHA-256 of the English corpus the catalog was made for),
//                  token_format; [files] game (default game.tsv), textures
//                  (default textures.tsv) and ui (the port's own UI later,
//                  unused so far). Committed: it holds no game text.
//   game.tsv       the game text catalog (dw4/i18n/game_text.hpp). Derived
//                  from the game's script: never committed or shipped.
//   textures.tsv   texture swaps by the USA texture's hash (below).
//
// Language data is host presentation state: never a content mod (mods/
// would change the data hash), never in a fingerprint, tick state, save
// state, snapshot or the online start message.
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::i18n {

struct LanguageInfo {
    std::string code;                // "de" (the folder's name when the ini has none)
    std::string name;                // "Deutsch (Fan-Übersetzung)", UTF-8
    std::string revision;            // the English corpus' SHA-256 the pack was made for
    int token_format = 1;
    std::filesystem::path folder;    // lang/<code>
    std::string game_file = "game.tsv";
    std::string textures_file = "textures.tsv";
    std::string ui_file;             // reserved for the port UI catalog
};

// language.ini's text; nullopt (with a reason) when it names no usable
// language (a code that is not 2..8 lowercase letters, digits or '-', "en",
// a token format this build does not know).
std::optional<LanguageInfo> parse_language_ini(std::string_view text, std::string_view folder_name, std::string* error = nullptr);

// The language packs below a lang/ folder (sorted by code; English is not
// listed). Folders without a usable language.ini are skipped and reported.
std::vector<LanguageInfo> discover_languages(const std::filesystem::path& lang_root, std::vector<std::string>* problems = nullptr);

// textures.tsv: "<usa hash> <TAB> <image name> <TAB> <source>" per line,
// '#' comments. The hash is TexturePack::hash (16 hex digits) of the USA
// texture. Sources:
//   disc:<pack>:<hash>  the same image from the player's own disc, in a
//                       stage's separate/<pack>.igb (ger_pack: the German
//                       originals), checked by its hash
//   render:<recipe>     drawn at run time from the game's font (the recipe
//                       is known to the program; its lines come from the
//                       catalog by their English text)
struct TextureRule {
    std::uint64_t usa_hash = 0;
    std::string image_name;
    enum class Kind : std::uint8_t { disc, render } kind = Kind::disc;
    std::string pack;                // disc: "ger_pack"
    std::uint64_t source_hash = 0;   // disc: the image's own hash
    std::string recipe;              // render
};
struct TextureRules {
    std::vector<TextureRule> rules;
    std::vector<std::string> problems; // "line N: ..." for rejected lines
};
TextureRules parse_texture_rules(std::string_view text);

std::optional<std::uint64_t> parse_hash16(std::string_view text);

} // namespace dw4::i18n
