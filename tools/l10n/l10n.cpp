#include "l10n.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

namespace dw4::l10n {
namespace {

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    for (;;) {
        const auto at = text.find(separator);
        parts.push_back(text.substr(0, at));
        if (at == std::string_view::npos) break;
        text.remove_prefix(at + 1);
    }
    return parts;
}

std::vector<std::string> lines_of(std::string_view text) {
    std::vector<std::string> lines;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    for (auto line : split(text, '\n')) {
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        lines.emplace_back(line);
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

bool read_text(const std::filesystem::path& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

// A TSV with a header row: the column of each name.
struct Table {
    std::map<std::string, std::size_t> columns;
    std::vector<std::vector<std::string_view>> rows;
    std::string_view get(const std::vector<std::string_view>& row, const std::string& name) const {
        const auto found = columns.find(name);
        return found == columns.end() || found->second >= row.size() ? std::string_view{} : row[found->second];
    }
};
Table read_table(const std::vector<std::string>& lines) {
    Table table;
    bool header = true;
    for (const auto& line : lines) {
        if (line.empty() || line.front() == '#') continue;
        auto columns = split(line, '\t');
        if (header) {
            for (std::size_t i = 0; i < columns.size(); ++i) {
                std::string name(columns[i]);
                // ("targets(layer/line:...)" -> "targets")
                if (const auto paren = name.find('('); paren != std::string::npos) name.resize(paren);
                table.columns.emplace(name, i);
            }
            header = false;
            continue;
        }
        table.rows.push_back(std::move(columns));
    }
    return table;
}

bool has_letter(std::u16string_view text) {
    return std::any_of(text.begin(), text.end(), [](char16_t c) { return (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z'); });
}

float to_float(std::string_view text) {
    const std::string copy(text);
    return std::strtof(copy.c_str(), nullptr);
}

std::string number(float value) {
    char text[32];
    std::snprintf(text, sizeof text, "%.3f", double(value));
    return text;
}

const char* align_name(int align) { return align == 1 ? "R" : align == 2 ? "C" : "L"; }
int align_of(std::string_view name) { return name == "R" || name == "1" ? 1 : name == "C" || name == "2" ? 2 : 0; }

std::string digits_of(std::u16string_view text) {
    std::string numbers;
    bool in_number = false;
    for (const char16_t c : text) {
        if (c >= u'0' && c <= u'9') {
            numbers.push_back(char(c));
            in_number = true;
        } else if (in_number) {
            numbers.push_back(' ');
            in_number = false;
        }
    }
    std::vector<std::string> list;
    for (auto part : split(numbers, ' '))
        if (!part.empty()) list.emplace_back(part);
    std::sort(list.begin(), list.end());
    std::string joined;
    for (const auto& part : list) joined += part + " ";
    return joined;
}

} // namespace

float Fonts::scale(std::string_view style) const {
    const auto* font = of(style);
    if (!font || !normal || font == normal) return 1.0f;
    const auto a = font->glyphs.find(u'A'), b = normal->glyphs.find(u'A');
    if (a == font->glyphs.end() || b == normal->glyphs.end() || b->second.advance <= 0) return 1.0f;
    return a->second.advance / b->second.advance;
}

std::vector<float> measure(const engine::FontImage& font, std::u16string_view text, float squeeze) {
    std::vector<float> widths;
    (void)engine::layout_text(font, text, 0, 0.0f, squeeze, &widths);
    return widths;
}

Corpus read_research(const std::filesystem::path& folder, std::vector<std::string>& problems) {
    Corpus corpus;
    std::map<std::u16string, std::size_t> index;
    auto add = [&](std::u16string english, const std::string& text_class, const std::string& where, const std::string& box) {
        if (!has_letter(english)) return;
        auto found = index.find(english);
        if (found == index.end()) {
            found = index.emplace(english, corpus.texts.size()).first;
            SourceText text;
            text.english = std::move(english);
            text.text_class = text_class;
            text.where = where;
            corpus.texts.push_back(std::move(text));
        }
        auto& text = corpus.texts[found->second];
        ++text.copies;
        if (std::find(text.boxes.begin(), text.boxes.end(), box) == text.boxes.end()) text.boxes.push_back(box);
    };
    auto decode = [&](std::string_view token_text, std::string_view where) -> std::optional<std::u16string> {
        std::string error;
        auto english = i18n::decode_tokens(token_text, &error);
        if (!english) problems.push_back(std::string(where) + ": " + error);
        return english;
    };
    std::string text;
    // The pack lines: style, alignment and field width (one per layer/line).
    if (read_text(folder / "out" / "lines.tsv", text)) {
        for (const auto& line : lines_of(text)) {
            const auto columns = split(line, '\t');
            if (columns.size() < 10 || !columns[0].ends_with("/usa_pack.igb")) continue;
            const std::string key = std::string(columns[2]) + "/" + std::string(columns[3]);
            if (corpus.boxes.contains(key)) continue;
            Box box;
            box.key = key;
            box.field_width = to_float(columns[6]);
            box.align = std::atoi(std::string(columns[8]).c_str());
            box.style = columns[9] == "MINIMUM" ? "MINIMUM" : "NORMAL";
            corpus.boxes.emplace(key, box);
        }
    } else {
        problems.push_back("out/lines.tsv is missing: pack lines take NORMAL, left, no field width");
    }
    // The pack strings (one row per occurrence, with the lines Lua sets them on).
    if (!read_text(folder / "usa_pack_text_widths.tsv", text)) {
        problems.push_back("usa_pack_text_widths.tsv is missing");
    } else {
        const auto lines = lines_of(text); // (the table views them)
        const auto table = read_table(lines);
        for (const auto& row : table.rows) {
            const auto stage = table.get(row, "stage");
            std::string text_class(table.get(row, "class"));
            if (stage == "stag0000" || text_class == "devtest") continue;
            if (text_class == "other:Special") text_class = "special";
            const std::string where(table.get(row, "id"));
            const auto english = decode(table.get(row, "text"), where);
            if (!english) continue;
            for (const auto target : split(table.get(row, "targets"), ';')) {
                if (target.empty()) continue;
                const auto fields = split(target, ':');
                const std::string key(fields[0]);
                if (!corpus.boxes.contains(key)) {
                    Box box;
                    box.key = key;
                    box.style = fields.size() > 1 && fields[1] == "MINIMUM" ? "MINIMUM" : "NORMAL";
                    corpus.boxes.emplace(key, box);
                }
                add(*english, text_class, where, key);
            }
        }
    }
    // The database's display columns (the same in every USA pack).
    if (!read_text(folder / "usa_db_text.tsv", text)) {
        problems.push_back("usa_db_text.tsv is missing");
    } else {
        const auto lines = lines_of(text); // (the table views them)
        const auto table = read_table(lines);
        for (const auto& row : table.rows) {
            if (table.get(row, "display") != "yes") continue;
            const std::string table_name(table.get(row, "table")), column(table.get(row, "column"));
            const std::string where(table.get(row, "id"));
            const auto english = decode(table.get(row, "text"), where);
            if (!english) continue;
            const std::string key = "db:" + table_name + ":" + column;
            if (!corpus.boxes.contains(key)) {
                Box box;
                box.key = key;
                box.style = "MINIMUM";
                corpus.boxes.emplace(key, box);
            }
            std::string text_class = "db_" + column;
            std::transform(text_class.begin(), text_class.end(), text_class.begin(), [](unsigned char c) { return char(std::tolower(c)); });
            add(*english, text_class, where, key);
        }
    }
    // The card names (stagis11's aus_pack card table).
    if (read_text(folder / "aus_card_text.tsv", text)) {
        const auto lines = lines_of(text); // (the table views them)
        const auto table = read_table(lines);
        for (const auto& row : table.rows) {
            if (table.get(row, "display") != "yes") continue;
            const std::string where(table.get(row, "id"));
            const auto english = decode(table.get(row, "text"), where);
            if (!english) continue;
            const std::string key = "db:card:" + std::string(table.get(row, "column"));
            if (!corpus.boxes.contains(key)) {
                Box box;
                box.key = key;
                box.style = "MINIMUM";
                corpus.boxes.emplace(key, box);
            }
            add(*english, "card", where, key);
        }
    }
    return corpus;
}

void compute_budgets(Corpus& corpus, const Fonts& fonts) {
    for (auto& [key, box] : corpus.boxes) box.max_width = 0, box.max_lines = 0;
    for (const auto& text : corpus.texts)
        for (const auto& key : text.boxes) {
            auto& box = corpus.boxes[key];
            const auto* font = fonts.of(box.style);
            if (!font) continue;
            const auto widths = measure(*font, text.english);
            for (const float width : widths) box.max_width = std::max(box.max_width, width);
            box.max_lines = std::max(box.max_lines, int(widths.size()));
        }
}

std::string corpus_revision(const Corpus& corpus) {
    std::vector<std::string> texts;
    for (const auto& text : corpus.texts) texts.push_back(i18n::encode_tokens(text.english));
    std::sort(texts.begin(), texts.end());
    std::string joined;
    for (const auto& text : texts) joined += text + "\n";
    return i18n::sha256_hex(joined);
}

std::string write_source_tsv(const Corpus& corpus, const Fonts& fonts) {
    std::string out;
    out += "# DW4 game text: the English corpus for translators (dw4_l10n export). Derived from the game: keep it local.\n";
    out += "# revision=" + corpus_revision(corpus) + "\n";
    out += "# Columns 1-4 are game.tsv's (fill in translation; flags: squeeze=0.90..0.99 as a last resort); the rest are notes.\n";
    out += "# max_width / max_lines: the strictest box this text is shown in (the widest English line there, in its font's units).\n";
    out += "id\tenglish\ttranslation\tflags\tclass\twhere\tboxes\tfont\tmax_width\tmax_lines\talign\tfield_width\tcopies\n";
    for (const auto& text : corpus.texts) {
        const Box* strictest = nullptr;
        float best = 0;
        int lines = 0;
        std::string boxes;
        for (const auto& key : text.boxes) {
            const auto found = corpus.boxes.find(key);
            if (found == corpus.boxes.end()) continue;
            const Box& box = found->second;
            const float normalized = box.max_width / std::max(fonts.scale(box.style), 0.01f);
            if (!strictest || normalized < best) strictest = &box, best = normalized;
            lines = lines ? std::min(lines, box.max_lines) : box.max_lines;
            if (!boxes.empty()) boxes += ";";
            boxes += key;
        }
        if (!strictest) continue;
        out += "\t" + i18n::encode_tokens(text.english) + "\t\t\t" + text.text_class + "\t" + text.where + "\t" + boxes + "\t" + strictest->style +
               "\t" + number(strictest->max_width) + "\t" + std::to_string(lines) + "\t" + align_name(strictest->align) + "\t" +
               (strictest->field_width > 0 ? number(strictest->field_width) : std::string("-")) + "\t" + std::to_string(text.copies) + "\n";
    }
    return out;
}

SourceTable read_source_tsv(std::string_view text, std::vector<std::string>& problems) {
    SourceTable source;
    const auto lines = lines_of(text);
    for (const auto& line : lines)
        if (line.starts_with("# revision=")) source.revision = line.substr(11);
    const auto table = read_table(lines);
    for (const char* needed : {"english", "font", "max_width", "max_lines"})
        if (!table.columns.contains(needed)) {
            problems.push_back(std::string("the source has no '") + needed + "' column (a dw4_l10n export?)");
            return source;
        }
    for (const auto& row : table.rows) {
        std::string error;
        const auto english = i18n::decode_tokens(table.get(row, "english"), &error);
        if (!english || english->empty()) {
            problems.push_back("source row '" + std::string(table.get(row, "english")) + "': " + (error.empty() ? "empty" : error));
            continue;
        }
        SourceRow source_row;
        source_row.text_class = table.get(row, "class");
        source_row.where = table.get(row, "where");
        source_row.boxes = table.get(row, "boxes");
        source_row.font = table.get(row, "font") == "MINIMUM" ? "MINIMUM" : "NORMAL";
        source_row.max_width = to_float(table.get(row, "max_width"));
        source_row.max_lines = std::atoi(std::string(table.get(row, "max_lines")).c_str());
        source_row.align = align_of(table.get(row, "align"));
        source_row.field_width = to_float(table.get(row, "field_width"));
        source.rows.emplace(*english, std::move(source_row));
    }
    return source;
}

const char* verdict_name(Verdict verdict) {
    switch (verdict) {
    case Verdict::ok: return "OK";
    case Verdict::squeezed: return "SQUEEZED";
    case Verdict::needs_squeeze: return "NEEDS_SQUEEZE";
    case Verdict::error: return "ERROR";
    case Verdict::warning: return "WARNING";
    }
    return "?";
}

namespace {

// The text wrapped greedily into lines that fit (its own breaks become
// spaces); empty when a word is too wide or the lines are too many.
std::u16string rewrap(const engine::FontImage& font, std::u16string_view text, float squeeze, float max_width, int max_lines) {
    std::vector<std::u16string> words;
    std::u16string word;
    for (const char16_t c : text) {
        if (c == u' ' || c == u'\r' || c == u'\n') {
            if (!word.empty()) words.push_back(std::move(word)), word.clear();
        } else {
            word.push_back(c);
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    if (words.empty()) return {};
    std::vector<std::u16string> lines;
    std::u16string current;
    for (const auto& next : words) {
        const std::u16string candidate = current.empty() ? next : current + u" " + next;
        const auto widths = measure(font, candidate, squeeze);
        if (!widths.empty() && widths[0] <= max_width + 1e-4f) {
            current = candidate;
            continue;
        }
        if (current.empty()) return {}; // one word wider than the box
        lines.push_back(std::move(current));
        current = next;
        const auto alone = measure(font, current, squeeze);
        if (alone.empty() || alone[0] > max_width + 1e-4f) return {};
    }
    lines.push_back(std::move(current));
    if (max_lines > 0 && int(lines.size()) > max_lines) return {};
    std::u16string joined;
    for (std::size_t i = 0; i < lines.size(); ++i) joined += (i ? u"\r\n" : u"") + lines[i];
    return joined;
}

} // namespace

LintReport lint(const SourceTable& source, std::string_view catalog_tsv, const Fonts& fonts) {
    LintReport report;
    report.corpus = source.rows.size();
    const auto parsed = i18n::GameTextCatalog::parse(catalog_tsv);
    if (!source.revision.empty() && !parsed.revision.empty() && source.revision != parsed.revision)
        report.revision_note = "the catalog was made for revision " + parsed.revision.substr(0, 16) + ", the source is " + source.revision.substr(0, 16);
    const auto raw_lines = lines_of(catalog_tsv);
    // Rows the engine would reject (their texts stay English).
    for (const auto& issue : parsed.issues) {
        LintRow row;
        row.line = issue.line;
        if (issue.line > 0 && issue.line <= raw_lines.size()) {
            const auto columns = split(raw_lines[issue.line - 1], '\t');
            if (columns.size() > 1) row.english = columns[1];
            if (columns.size() > 2) row.text = columns[2];
        }
        row.detail = issue.message;
        row.verdict = issue.rejected ? Verdict::error : Verdict::warning;
        if (!issue.rejected && issue.message.starts_with("fuzzy")) continue; // (counted as untranslated)
        (issue.rejected ? report.errors : report.warnings)++;
        report.rows.push_back(std::move(row));
    }
    report.untranslated = parsed.skipped;
    std::set<std::u16string> covered;
    for (const auto& entry : parsed.catalog->entries()) {
        LintRow row;
        row.line = entry.line;
        row.english = i18n::encode_tokens(entry.english);
        row.text = i18n::encode_tokens(entry.text);
        const auto found = source.rows.find(entry.english);
        const SourceRow* box = found == source.rows.end() ? nullptr : &found->second;
        if (box) covered.insert(entry.english);
        const engine::FontImage* font = fonts.of(box ? box->font : "NORMAL");
        if (!font) {
            row.verdict = Verdict::error;
            row.detail = "no font";
            ++report.errors;
            report.rows.push_back(std::move(row));
            continue;
        }
        std::vector<std::string> problems, notes;
        // Every glyph in the font (else the engine keeps the English text).
        for (const char16_t c : entry.text)
            if (c != u'\r' && c != u'\n' && !font->glyphs.contains(std::uint32_t(c))) {
                char code[16];
                std::snprintf(code, sizeof code, "U+%04X", unsigned(c));
                problems.push_back("the font has no '" + i18n::utf16_to_utf8(std::u16string(1, c)) + "' (" + code +
                                   "): use ASCII or „ “ ‚ ‘ – — … ä ö ü ß");
                break;
            }
        bool squeeze_needed = false;
        if (box && problems.empty()) {
            const auto widths = measure(*font, entry.text, entry.squeeze);
            const float widest = widths.empty() ? 0.0f : *std::max_element(widths.begin(), widths.end());
            row.widest = box->max_width > 0 ? widest / box->max_width : 0;
            const bool too_many = box->max_lines > 0 && int(widths.size()) > box->max_lines;
            if (too_many)
                problems.push_back(std::to_string(widths.size()) + " lines, the box shows " + std::to_string(box->max_lines));
            if (box->max_width > 0 && widest > box->max_width + 1e-4f) {
                const auto plain = measure(*font, entry.text);
                const float plain_widest = plain.empty() ? 0.0f : *std::max_element(plain.begin(), plain.end());
                const float needed = box->max_width / plain_widest;
                char text[160];
                if (needed >= i18n::kMinSqueeze) {
                    const float squeeze = std::floor(needed * 100.0f) / 100.0f;
                    std::snprintf(text, sizeof text, "%.0f %% of the box: fits with squeeze=%.2f (the last resort; shorter is better)",
                                  double(plain_widest / box->max_width * 100.0f), double(squeeze));
                    squeeze_needed = true;
                } else {
                    std::snprintf(text, sizeof text, "%.0f %% of the box: shorten it (a squeeze reaches 111 %% at most)",
                                  double(plain_widest / box->max_width * 100.0f));
                }
                problems.push_back(text);
            }
            if ((too_many || (box->max_width > 0 && widest > box->max_width + 1e-4f)) && box->max_width > 0) {
                const auto wrapped = rewrap(*font, entry.text, entry.squeeze, box->max_width, box->max_lines);
                if (!wrapped.empty()) row.suggestion = i18n::encode_tokens(wrapped);
            }
            // Worth a look (shown as it is).
            if (digits_of(entry.english) != digits_of(entry.text)) notes.push_back("the numbers differ from the English text's");
            if (!entry.english.empty() && entry.english.back() == u':' && (entry.text.empty() || entry.text.back() != u':'))
                notes.push_back("the English text ends in ':' (the game appends a value there)");
        } else if (!box) {
            notes.push_back("not in the English corpus (stale, or a text the game composes)");
            ++report.not_in_corpus;
        }
        if (!problems.empty()) {
            row.verdict = squeeze_needed && problems.size() == 1 ? Verdict::needs_squeeze : Verdict::error;
        } else if (!notes.empty()) {
            row.verdict = Verdict::warning;
        } else {
            row.verdict = entry.squeeze < 1.0f ? Verdict::squeezed : Verdict::ok;
        }
        for (const auto& list : {problems, notes})
            for (const auto& text : list) row.detail += (row.detail.empty() ? "" : "; ") + text;
        if (entry.squeeze < 1.0f) {
            char text[48];
            std::snprintf(text, sizeof text, "%ssqueezed to %.2f", row.detail.empty() ? "" : "; ", double(entry.squeeze));
            row.detail += text;
        }
        switch (row.verdict) {
        case Verdict::ok: ++report.ok; break;
        case Verdict::squeezed: ++report.squeezed; break;
        case Verdict::needs_squeeze: ++report.needs_squeeze; break;
        case Verdict::error: ++report.errors; break;
        case Verdict::warning: ++report.warnings; break;
        }
        report.rows.push_back(std::move(row));
    }
    report.translated = covered.size();
    std::sort(report.rows.begin(), report.rows.end(), [](const LintRow& a, const LintRow& b) { return a.line < b.line; });
    return report;
}

std::string format_report(const LintReport& report, bool all_rows) {
    std::string out;
    char head[64];
    for (const auto& row : report.rows) {
        if (!all_rows && (row.verdict == Verdict::ok || row.verdict == Verdict::squeezed)) continue;
        std::snprintf(head, sizeof head, "line %zu\t%s\t%3.0f%%\t", row.line, verdict_name(row.verdict), double(row.widest * 100.0f));
        out += head + row.english + "\t" + row.text + "\t" + row.detail;
        if (!row.suggestion.empty()) out += "\tsuggested wrap: " + row.suggestion;
        out += "\n";
    }
    char summary[512];
    std::snprintf(summary, sizeof summary,
                  "summary: %zu ok, %zu squeezed, %zu need a squeeze, %zu errors, %zu warnings; %zu untranslated or repeated rows, %zu not in the "
                  "corpus; %zu of %zu English texts translated (%.1f %%)\n",
                  report.ok, report.squeezed, report.needs_squeeze, report.errors, report.warnings, report.untranslated, report.not_in_corpus,
                  report.translated, report.corpus, report.corpus ? double(report.translated) * 100.0 / double(report.corpus) : 0.0);
    out += summary;
    if (!report.revision_note.empty()) out += "note: " + report.revision_note + "\n";
    out += report.failed() ? "result: FAILED (errors or missing squeeze flags)\n" : "result: passed\n";
    return out;
}

} // namespace dw4::l10n
