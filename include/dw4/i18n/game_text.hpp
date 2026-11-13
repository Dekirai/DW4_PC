#pragma once

// Display-only translations of the game's text (docs/L10N.md). The English
// text of the USA disc stays the source and the key everywhere: the
// simulation, Lua, the database, the tick state and saves only ever see
// English. A language pack's game.tsv maps that English text to the text a
// player of that language reads, and TextCtlManager lays the translation out
// instead of the English text (presentation only, never part of the state).
//
// game.tsv (UTF-8, one entry per line, '#' lines are comments):
//
//   id <TAB> english <TAB> translation [<TAB> flags [<TAB> notes ...]]
//
// - id: empty (or "*") for a content entry, which applies wherever the
//   English text is shown; "<stage>/<mswkName>" for an override of one pack
//   string (applies only where the engine knows that the line shows exactly
//   that pack string and only while its English text still matches);
//   "db:<table>:<row>:<column>" records a database cell's origin and applies
//   as a content entry (the engine does not know which cell a text came from).
// - english / translation: the token form below.
// - flags: comma separated; "squeeze=0.95" scales the laid-out text
//   horizontally (0.90 at least, the last resort for a text that does not fit
//   its box), "fuzzy" keeps the English text.
// - A first line whose id column reads "id" is a header.
//
// Token form (format 1): {BR} is a line break (CR LF), {A} {B} {B2} {X} {Y}
// {START} {L} {R} {Z} {DPAD} {STICK} {CSTICK} are the controller button
// glyphs, {I_WEAPON} {I_ARMOR1} {I_ARMOR2} {I_CHIP} {I_DATA} {I_FIRE} {I_ICE}
// {I_THUNDER} {I_DARK} {I_EQUIPPED} the item icons; '/' and ';' are the US
// font's slash and semicolon cells (U+3042, U+3044: the font has no ASCII
// '/' or ';'). Everything else is literal text.
//
// A broken entry (bad UTF-8, an unknown token, other glyph tokens than the
// English text has, a START word lost, an empty translation, a bad flag)
// is rejected on its own: that string stays English.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace dw4::i18n {

inline constexpr int kTokenFormat = 1;
// The least horizontal scale a translation may ask for (flag squeeze=).
inline constexpr float kMinSqueeze = 0.9f;

std::string utf16_to_utf8(std::u16string_view text);
// nullopt for invalid UTF-8 (overlong forms, surrogates, code points above
// U+FFFF: the game's text is UTF-16 without surrogate pairs).
std::optional<std::u16string> utf8_to_utf16(std::string_view text);

// Game text (UTF-16) in the token form; decode_tokens is its inverse and
// rejects unknown tokens, stray braces, control characters and bad UTF-8.
std::string encode_tokens(std::u16string_view text);
std::optional<std::u16string> decode_tokens(std::string_view text, std::string* error = nullptr);

// The button and icon glyphs of a text in order (no line breaks).
std::u16string glyph_codes(std::u16string_view text);
// How often the word START (also full width) stands in a text: the prompt
// layout draws it as a picture, so a translation keeps it.
int start_words(std::u16string_view text);
// The equip markers C++ puts before a database name (U+2286 equipped,
// U+8D6B not: subwnd_shop.cpp).
bool is_equip_marker(char16_t c);

struct TextEntry {
    std::string id;          // "" content entry, "<stage>/<name>", "db:<table>:<row>:<col>"
    std::u16string english;  // the source text (decoded)
    std::u16string text;     // the display text (decoded)
    float squeeze = 1.0f;    // horizontal scale of the laid-out text
    std::size_t line = 0;    // line number in game.tsv (1-based)
};

struct CatalogIssue {
    std::size_t line = 0;    // 1-based, 0 for the file
    std::string message;
    bool rejected = false;   // the entry was dropped (its string stays English)
};

class GameTextCatalog {
public:
    struct Parsed {
        std::shared_ptr<const GameTextCatalog> catalog; // never null
        std::vector<CatalogIssue> issues;
        std::size_t rows = 0, accepted = 0, rejected = 0, skipped = 0; // skipped: empty, fuzzy or repeated translations
        std::string revision; // "# revision=<hex>" header comment, if any
    };
    // The text of a game.tsv; never throws for bad content.
    static Parsed parse(std::string_view tsv);

    // What a line shows for an English text (nullopt: the English text):
    // the id override when `id` names one whose English matches, else the
    // content entry, else the compound rules (a leading equip marker, a
    // trailing rank digit: the core translated, marker or digit kept).
    struct Resolved {
        std::u16string text;
        float squeeze = 1.0f;
        bool by_id = false;
    };
    std::optional<Resolved> resolve(std::u16string_view english, std::string_view id = {}) const;

    const TextEntry* find(std::u16string_view english) const;
    const TextEntry* find_id(std::string_view id) const;
    const std::vector<TextEntry>& entries() const { return entries_; }
    std::size_t size() const { return entries_.size(); }

private:
    std::vector<TextEntry> entries_;
    std::unordered_map<std::u16string, std::size_t> by_text_;
    std::unordered_map<std::string, std::size_t> by_id_;
};

// Kind of a game.tsv id (also used by the tools).
enum class IdKind : std::uint8_t { content, pack, database, invalid };
IdKind id_kind(std::string_view id);

// SHA-256 as lowercase hex (the English corpus revision of a language pack).
std::string sha256_hex(std::string_view data);

} // namespace dw4::i18n
