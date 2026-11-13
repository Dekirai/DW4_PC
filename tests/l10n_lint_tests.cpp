// The translation lint (tools/l10n, docs/L10N.md 4.5) on synthetic data: a
// synthetic font pair, a small corpus with its boxes, the exported source
// file read back, and a translated catalog with every kind of verdict.
#include "../tools/l10n/l10n.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

// Every glyph 0.08 wide with an advance of 0.1 ('i' 0.03 / 0.05), scaled
// (MINIMUM is NORMAL x 0.66, as in the game's font).
std::unique_ptr<dw4::engine::FontImage> synthetic_font(const char* name, float scale) {
    dw4::engine::FontImage font;
    font.name = name;
    font.line_height = 0.28f * scale;
    int cell = 0;
    auto add = [&](char32_t code, float width, float advance) {
        dw4::engine::Glyph g;
        g.width = width * scale;
        g.advance = advance * scale;
        g.height = 0.27f * scale;
        g.u0 = float(cell % 16) / 16.0f, g.u1 = g.u0 + 1.0f / 32.0f;
        g.v0 = float(cell / 16) / 16.0f, g.v1 = g.v0 + 1.0f / 32.0f;
        font.glyphs[std::uint32_t(code)] = g;
        ++cell;
    };
    for (char32_t c = U'A'; c <= U'Z'; ++c) add(c, 0.08f, 0.1f);
    for (char32_t c = U'a'; c <= U'z'; ++c) add(c, c == U'i' ? 0.03f : 0.08f, c == U'i' ? 0.05f : 0.1f);
    for (char32_t c = U'0'; c <= U'9'; ++c) add(c, 0.08f, 0.1f);
    for (char32_t c : {U' ', U'.', U',', U':', U'?', U'ü', U'ä', U'ö', U'ß', U'≪', U'≫', U'”', U'’', U'-', U'ー'}) add(c, 0.08f, 0.1f);
    return dw4::engine::make_display_font(font);
}

dw4::l10n::SourceText text(const char16_t* english, const char* text_class, const char* box) {
    dw4::l10n::SourceText source;
    source.english = english;
    source.text_class = text_class;
    source.where = "test/" + std::string(text_class);
    source.boxes = {box};
    source.copies = 1;
    return source;
}

void test_lint() {
    const auto normal = synthetic_font("NORMAL", 1.0f);
    const auto minimum = synthetic_font("MINIMUM", 0.66f);
    const dw4::l10n::Fonts fonts{normal.get(), minimum.get()};
    check(std::fabs(fonts.scale("MINIMUM") - 0.66f) < 1e-5f && fonts.scale("NORMAL") == 1.0f, "MINIMUM measures 0.66 of NORMAL");

    dw4::l10n::Corpus corpus;
    corpus.boxes["TALK/font00"] = {"TALK/font00", "NORMAL", 0, 1.0f};
    corpus.boxes["LIST/font000"] = {"LIST/font000", "MINIMUM", 0, 1.68f};
    corpus.boxes["db:weapon:TXT"] = {"db:weapon:TXT", "MINIMUM", 0, 0.0f};
    corpus.texts = {text(u"Hello there.\r\nHow are you?", "dialog", "TALK/font00"), text(u"Press ≪ now.", "dialog", "TALK/font00"),
                    text(u"Press ≫ now.", "dialog", "TALK/font00"),                text(u"See you.", "dialog", "TALK/font00"),
                    text(u"Short", "ui_status", "LIST/font000"),                   text(u"Long", "ui_status", "LIST/font000"),
                    text(u"Lv 5", "ui_status", "LIST/font000"),                    text(u"Shield", "ui_status", "LIST/font000"),
                    text(u"Sword Mods:", "db_txt", "db:weapon:TXT")};
    dw4::l10n::compute_budgets(corpus, fonts);
    const auto& talk = corpus.boxes["TALK/font00"];
    check(std::fabs(talk.max_width - 1.18f) < 1e-4f && talk.max_lines == 2, "The talk box's budget is its widest English line: " +
                                                                                 std::to_string(talk.max_width));
    check(std::fabs(corpus.boxes["LIST/font000"].max_width - 0.3498f) < 1e-4f && corpus.boxes["LIST/font000"].max_lines == 1,
          "The list box's budget comes from the MINIMUM font");

    // The exported source reads back.
    const auto exported = dw4::l10n::write_source_tsv(corpus, fonts);
    std::vector<std::string> problems;
    const auto source = dw4::l10n::read_source_tsv(exported, problems);
    check(problems.empty(), "The export reads back without problems");
    check(source.rows.size() == 9, "Every English text is a row");
    check(source.revision == dw4::l10n::corpus_revision(corpus) && source.revision.size() == 64, "The revision travels with the export");
    const auto& hello = source.rows.at(u"Hello there.\r\nHow are you?");
    check(hello.font == "NORMAL" && std::fabs(hello.max_width - 1.18f) < 1e-3f && hello.max_lines == 2 && hello.boxes == "TALK/font00",
          "A row carries its box's metrics");
    check(source.rows.at(u"Sword Mods:").font == "MINIMUM", "A database text is measured in MINIMUM");
    check(exported.find("\tHello there.{BR}How are you?\t\t\tdialog\t") != std::string::npos, "The export writes the token form");
    // The exported file is a valid (empty) game.tsv.
    const auto empty = dw4::i18n::GameTextCatalog::parse(exported);
    check(empty.accepted == 0 && empty.rejected == 0 && empty.skipped == 9, "The export is a game.tsv with nothing translated yet");

    std::vector<std::string> missing;
    (void)dw4::l10n::read_source_tsv("id\tenglish\n\tHello\n", missing);
    check(!missing.empty(), "A source without the metric columns is refused");

    const std::string catalog = "# revision=" + source.revision +
                                "\n"
                                "\tHello there.{BR}How are you?\tHallo dort, wie{BR}geht es?\n" // 2: too wide, a rewrap fits
                                "\tPress {A} now.\tDrücke {B}.\n"                             // 3: other button: rejected
                                "\tPress {A} now.\tDrück {A} jetzt\n"                         // 4: fits only squeezed, no flag
                                "\tPress {B} now.\tDrück {B} jetzt\tsqueeze=0.92\n"           // 5: squeezed
                                "\tSee you.\tBis{BR}bald{BR}dann.\n"                          // 6: three lines
                                "\tShort\tKurz\n"                                             // 7: ok
                                "\tLong\tLang€\n"                                             // 8: a glyph the font lacks
                                "\tSword Mods:\tSchwert\n"                                    // 9: the ':' ending lost
                                "\tLv 5\tLv 6\n"                                              // 10: numbers differ
                                "\tUnknown text\tUnbekannt\n"                                 // 11: not in the corpus
                                "\tShield\t\n";                                               // 12: untranslated
    const auto report = dw4::l10n::lint(source, catalog, fonts);
    auto verdict_at = [&](std::size_t line) {
        for (const auto& row : report.rows)
            if (row.line == line) return row.verdict;
        throw std::runtime_error("no lint row for line " + std::to_string(line));
    };
    auto row_at = [&](std::size_t line) -> const dw4::l10n::LintRow& {
        for (const auto& row : report.rows)
            if (row.line == line) return row;
        throw std::runtime_error("no lint row for line " + std::to_string(line));
    };
    using dw4::l10n::Verdict;
    check(verdict_at(2) == Verdict::error && row_at(2).detail.find("shorten") != std::string::npos, "A line far too wide is an error");
    check(row_at(2).suggestion == "Hallo dort,{BR}wie geht es?", "A greedy rewrap that fits is suggested: " + row_at(2).suggestion);
    check(verdict_at(3) == Verdict::error && row_at(3).detail.find("tokens") != std::string::npos && row_at(3).english == "Press {A} now.",
          "A row the engine rejects is an error with its text");
    check(verdict_at(4) == Verdict::needs_squeeze && row_at(4).detail.find("squeeze=0.92") != std::string::npos,
          "A line up to 111 % needs a squeeze flag: " + row_at(4).detail);
    check(verdict_at(5) == Verdict::squeezed, "A squeezed line that fits is flagged as squeezed");
    check(verdict_at(6) == Verdict::error && row_at(6).detail.find("3 lines, the box shows 2") != std::string::npos, "Too many lines are an error");
    check(verdict_at(7) == Verdict::ok, "A text that fits is ok");
    check(verdict_at(8) == Verdict::error && row_at(8).detail.find("U+20AC") != std::string::npos, "A glyph the font lacks is an error");
    check(verdict_at(9) == Verdict::warning && row_at(9).detail.find("':'") != std::string::npos, "A lost ':' ending is a warning");
    check(verdict_at(10) == Verdict::warning && row_at(10).detail.find("numbers") != std::string::npos, "Changed numbers are a warning");
    check(verdict_at(11) == Verdict::warning && row_at(11).detail.find("not in the English corpus") != std::string::npos,
          "A text outside the corpus is a warning");
    check(report.ok == 1 && report.squeezed == 1 && report.needs_squeeze == 1 && report.errors == 4 && report.warnings == 3,
          "The summary counts every verdict");
    check(report.untranslated == 1 && report.not_in_corpus == 1 && report.translated == 8 && report.corpus == 9,
          "Coverage: 8 of 9 English texts translated");
    check(report.failed() && report.revision_note.empty(), "Errors fail the lint; the revision matches");
    const auto text_report = dw4::l10n::format_report(report, false);
    check(text_report.find("NEEDS_SQUEEZE") != std::string::npos && text_report.find("summary: 1 ok") != std::string::npos &&
              text_report.find("\tOK\t") == std::string::npos && text_report.find("result: FAILED") != std::string::npos,
          "The report lists the rows that are not ok and the summary");
    check(dw4::l10n::format_report(report, true).find("\tOK\t") != std::string::npos, "--all lists the ok rows too");

    // A clean catalog passes; another revision is noted.
    const auto clean = dw4::l10n::lint(source, "# revision=0000\n\tShort\tKurz\n\tPress {B} now.\tDrück {B} jetzt\tsqueeze=0.92\n", fonts);
    check(!clean.failed() && clean.ok == 1 && clean.squeezed == 1 && !clean.revision_note.empty(), "A clean catalog passes");

    // A box's field wider than its English words is its budget: "Short" is
    // 0.35 wide in a 1.68 field, a German word up to 1.68 fits.
    const auto field = dw4::l10n::lint(source, "\tShort\tKurzzeitiger\n\tLong\tLangzeitigerAlsDasFeldBreitIst\n", fonts);
    check(field.ok == 1 && field.errors == 1, "The field is the budget of a box whose English is narrower");
}

// The wrap: a translation's own line breaks are kept where the lines then
// fit the box (a heading line, options), else the text is wrapped as one
// paragraph; apply_wraps puts the suggestions into the catalog's translation
// column and leaves every other line as it is.
void test_wrap() {
    const auto normal = synthetic_font("NORMAL", 1.0f);
    const auto minimum = synthetic_font("MINIMUM", 0.66f);
    const dw4::l10n::Fonts fonts{normal.get(), minimum.get()};
    dw4::l10n::Corpus corpus;
    corpus.boxes["INFO/font00"] = {"INFO/font00", "NORMAL", 0, 1.0f};
    corpus.texts = {text(u"Head line\r\nA text below it\r\nEnd.", "dialog", "INFO/font00"), text(u"One more line", "dialog", "INFO/font00"),
                    text(u"Fine.", "dialog", "INFO/font00")};
    dw4::l10n::compute_budgets(corpus, fonts);
    const auto& info = corpus.boxes["INFO/font00"];
    check(std::fabs(info.max_width - 1.43f) < 1e-4f && info.max_lines == 3, "The info box: 1.43 wide, three lines");
    std::vector<std::string> problems;
    const auto source = dw4::l10n::read_source_tsv(dw4::l10n::write_source_tsv(corpus, fonts), problems);
    auto suggestion = [&](const std::string& catalog, std::size_t line) {
        const auto report = dw4::l10n::lint(source, catalog, fonts);
        for (const auto& row : report.rows)
            if (row.line == line) return row.suggestion;
        throw std::runtime_error("no lint row for line " + std::to_string(line));
    };
    check(suggestion("\tHead line{BR}A text below it{BR}End.\tKopf{BR}Ein Text unten drunter\n", 1) == "Kopf{BR}Ein Text unten{BR}drunter",
          "The translation's own break is kept and the long line wrapped");
    check(suggestion("\tHead line{BR}A text below it{BR}End.\tKopf{BR}Ein Text unten drunter dran{BR}Ende.\n", 1) ==
              "Kopf Ein Text{BR}unten drunter{BR}dran Ende.",
          "Four lines with the breaks kept: the text is wrapped as one paragraph");
    check(suggestion("\tFine.\tEin unglaublichlangeswort\n", 1).empty(), "A word wider than the box: no suggestion");
    check(suggestion("\tOne more line\tEr steigt um 1 Punkt\n", 1) == "Er steigt um{BR}1 Punkt",
          "A number stays on the line of the word after it");

    const std::string catalog = "# a comment\n"
                                "\tHead line{BR}A text below it{BR}End.\tKopf{BR}Ein Text unten drunter\tsqueeze=0.95\tnote\n"
                                "\tOne more line\tNoch eine Zeile mit mehr Text drin\n"
                                "\tFine.\tGut.\n";
    const auto report = dw4::l10n::lint(source, catalog, fonts);
    std::size_t applied = 0;
    const auto wrapped = dw4::l10n::apply_wraps(catalog, report, &applied);
    check(applied == 2, "Two rows are wrapped: " + std::to_string(applied));
    check(wrapped == "# a comment\n"
                     "\tHead line{BR}A text below it{BR}End.\tKopf{BR}Ein Text unten{BR}drunter\tsqueeze=0.95\tnote\n"
                     "\tOne more line\tNoch eine Zeile{BR}mit mehr Text{BR}drin\n"
                     "\tFine.\tGut.\n",
          "apply_wraps changes only the translation column of the wrapped rows:\n" + wrapped);
    const auto again = dw4::l10n::lint(source, wrapped, fonts);
    check(!again.failed() && again.squeezed == 1 && again.ok == 2, "The wrapped catalog passes the lint");

    // A memory card text keeps its English line count: the box shows two
    // lines, but its next line holds the other half of a one-line sentence.
    dw4::l10n::Corpus cards;
    cards.boxes["MEM/font000"] = {"MEM/font000", "NORMAL", 0, 1.0f};
    cards.texts = {text(u"Card one here", "memcard", "MEM/font000"), text(u"Pick it\r\nnow.", "memcard", "MEM/font000")};
    dw4::l10n::compute_budgets(cards, fonts);
    const auto card_source = dw4::l10n::read_source_tsv(dw4::l10n::write_source_tsv(cards, fonts), problems);
    const auto card_report = dw4::l10n::lint(card_source, "\tCard one here\tKarte eins{BR}hier\n\tPick it{BR}now.\tNimm es{BR}jetzt.\n", fonts);
    check(card_report.errors == 1 && card_report.ok == 1 && card_report.rows[0].detail.find("2 lines, the box shows 1") != std::string::npos,
          "A one-line memory card text may not take two lines: " + card_report.rows[0].detail);
}
} // namespace

int main() {
    try {
        test_lint();
        test_wrap();
    } catch (const std::exception& error) {
        std::cerr << "l10n lint test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
    std::cout << "l10n lint tests passed (" << checks << " checks)\n";
    return 0;
}
