#pragma once

// Translation tools (docs/L10N.md 4): the English corpus exported for
// translators with the box each text is shown in, and the lint that checks
// a translated game.tsv with the engine's own font metrics and layout
// (engine::layout_text, engine::make_display_font). Development tools: the
// corpus and every translation are derived from the game and stay local.
#include "dw4/engine/managers.hpp"
#include "dw4/i18n/game_text.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace dw4::l10n {

// A kind of text line a string is shown in: a pack line ("TALKFONT01/font00",
// one per layer and line name over all stages) or a database / card column
// ("db:weapon:TXT", "db:card:NAME").
struct Box {
    std::string key;
    std::string style = "NORMAL"; // the font: NORMAL or MINIMUM
    int align = 0;                // 0 left, 1 right, 2 centre (beFontInfoData [9])
    float field_width = 0;        // beFontInfoData [7] (0: not a pack line); not a hard limit, English passes it too
    // The budget: the widest line (or the field where that is wider) and the
    // most lines an English text shown in this box has, measured with the
    // engine's layout (compute_budgets).
    float max_width = 0;
    int max_lines = 0;
};

// One unique English text with where it is shown.
struct SourceText {
    std::u16string english;  // decoded (game code points)
    std::string text_class;  // dialog, ui_status, memcard, speaker, special, db_name, db_txt, db_explanation, card
    std::string where;       // its first id: "<stage>/<mswkName>" or "db:<table>:<row>:<column>"
    std::vector<std::string> boxes;
    int copies = 0;
};

struct Corpus {
    std::map<std::string, Box> boxes;
    std::vector<SourceText> texts;
};

// The two game fonts as translations are laid out (display fonts).
struct Fonts {
    const engine::FontImage* normal = nullptr;
    const engine::FontImage* minimum = nullptr;
    const engine::FontImage* of(std::string_view style) const { return style == "MINIMUM" ? minimum : normal; }
    // How wide a font's text is against NORMAL (MINIMUM is NORMAL x 0.66).
    float scale(std::string_view style) const;
};

// Every line's width (its right edge, after the squeeze) as the engine lays
// the text out in that font.
std::vector<float> measure(const engine::FontImage& font, std::u16string_view text, float squeeze = 1.0f);
// The lines a box shows of measured widths: up to the last one with
// something on it (a trailing line break adds none; at least 1).
int visible_lines(const std::vector<float>& widths);
// The text as far as it shows ink: for a left-aligned box (align 0) every
// line's trailing spaces are dropped (they move nothing); centred and
// right-aligned text keeps them. Budgets and the lint measure this.
std::u16string ink_text(std::u16string_view text, int align);

// How the lint's wrap suggestion may move a text's own line breaks when
// keeping them gives too many lines: free (the whole text as one
// paragraph), keep_header (the first line stays, the rest is one
// paragraph: an MP technique's "MP 8 Heal Skill Up"), keep_lines (never:
// equipment and chip texts, one fact per line). By the text's class and
// boxes (a source row's columns).
enum class WrapMode : std::uint8_t { free, keep_header, keep_lines };
WrapMode wrap_mode(std::string_view text_class, std::string_view boxes);

// The research inventory (work/l10n_research/text: usa_pack_text_widths.tsv,
// out/lines.tsv, usa_db_text.tsv, aus_card_text.tsv) as a corpus; the dev
// test stage stag0000 and texts without a letter are left out.
Corpus read_research(const std::filesystem::path& folder, std::vector<std::string>& problems);
// Each box's budget from the English texts shown in it.
void compute_budgets(Corpus& corpus, const Fonts& fonts);
// SHA-256 over the unique English texts (token form, sorted, one per line):
// the English corpus a catalog was made for (language.ini revision).
std::string corpus_revision(const Corpus& corpus);

// The translator's file (also a valid game.tsv: the translation column is
// empty): one row per unique English text, its strictest box's metrics.
//   id  english  translation  flags  class  where  boxes  font  max_width  max_lines  align  field_width  copies
std::string write_source_tsv(const Corpus& corpus, const Fonts& fonts);

// What the lint knows of each English text (a row of the source file).
struct SourceRow {
    std::string text_class, where, boxes, font = "NORMAL";
    float max_width = 0;
    int max_lines = 0;
    int align = 0;
    float field_width = 0;
};
struct SourceTable {
    std::map<std::u16string, SourceRow> rows;
    std::string revision;
};
SourceTable read_source_tsv(std::string_view text, std::vector<std::string>& problems);

// The lint's verdict on one row of a translated game.tsv.
enum class Verdict : std::uint8_t {
    ok,            // fits as it is
    squeezed,      // fits with its squeeze flag (0.90..0.99, the last resort)
    needs_squeeze, // fits only squeezed and has no (or too weak a) squeeze flag: an error
    error,         // does not fit, broken tokens, a glyph the font lacks, too many lines ...
    warning,       // shown, but worth a look (numbers changed, a ':' ending lost, other edge {BR}s, an id row
                   // without a content row, not in the corpus)
};
const char* verdict_name(Verdict verdict);
struct LintRow {
    std::size_t line = 0;    // in the translated game.tsv
    Verdict verdict = Verdict::ok;
    std::string english;     // token form
    std::string text;        // token form
    std::string detail;
    // A greedy rewrap that fits (token form), if one does: the translation's
    // edges (leading / trailing spaces and {BR}s) and own line breaks kept,
    // else as its WrapMode allows (the whole text as one paragraph, the
    // heading line kept, or none).
    std::string suggestion;
    float widest = 0;        // the widest line against the budget (1 = exactly full)
};
struct LintReport {
    std::vector<LintRow> rows;
    std::size_t ok = 0, squeezed = 0, needs_squeeze = 0, errors = 0, warnings = 0, untranslated = 0, not_in_corpus = 0;
    std::size_t corpus = 0;  // English texts in the source
    std::size_t translated = 0; // source texts the catalog translates
    std::string revision_note;
    bool failed() const { return errors || needs_squeeze; }
};
LintReport lint(const SourceTable& source, std::string_view catalog_tsv, const Fonts& fonts);
// The report as text: one line per row (all rows, or only those that are
// not ok) and the summary.
std::string format_report(const LintReport& report, bool all_rows);
// The catalog with every suggested wrap put into its translation column
// (dw4_l10n wrap): the rows the lint found too wide or too long and could
// wrap; every other line stays as it is. applied: how many rows changed.
std::string apply_wraps(std::string_view catalog_tsv, const LintReport& report, std::size_t* applied = nullptr);

} // namespace dw4::l10n
