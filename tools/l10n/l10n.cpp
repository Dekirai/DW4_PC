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

int visible_lines(const std::vector<float>& widths) {
    std::size_t lines = widths.size();
    while (lines > 1 && widths[lines - 1] <= 0.0f) --lines;
    return int(lines);
}

std::u16string ink_text(std::u16string_view text, int align) {
    if (align != 0) return std::u16string(text);
    std::u16string out;
    std::size_t spaces = 0; // (held until something follows on the line)
    for (const char16_t c : text) {
        if (c == u' ') {
            ++spaces;
            continue;
        }
        if (c != u'\r' && c != u'\n') out.append(spaces, u' ');
        spaces = 0;
        out.push_back(c);
    }
    return out;
}

// A field whose data width reaches under its frame's decoration is no
// budget: there only the English text's own width is usable. The enemy info
// card's description (DMINFOFONT02 font02) has a 2.56 field whose right part
// lies under the card's yellow bars; the widest English line (2.18) stays
// clear of them, German lines up to the field ran into them (in-game review).
bool framed_field(std::string_view key) { return key == "DMINFOFONT02/font02"; }

bool in_framed_field(std::string_view boxes) {
    for (const auto box : split(boxes, ';'))
        if (framed_field(box)) return true;
    return false;
}

WrapMode wrap_mode(std::string_view text_class, std::string_view boxes) {
    if (text_class != "db_txt" && text_class != "db_explanation") return WrapMode::free;
    for (const auto box : split(boxes, ';')) {
        // A heading over running text: "MP 8 Heal Skill Up" (techniques),
        // "Blunt Skills Specialist" (Digivolutions).
        if (box.starts_with("db:pcTech:") || box.starts_with("db:shinka:")) return WrapMode::keep_header;
        // Fixed lines: requirement / text / "Mods:" (equipment), value /
        // slots / data (chips).
        if (box.starts_with("db:weapon:") || box.starts_with("db:armor1:") || box.starts_with("db:armor2:") || box.starts_with("db:abChip:"))
            return WrapMode::keep_lines;
    }
    return WrapMode::free;
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
                // An Item's name and text are shown in the windows' lists
                // and description box: STWINFONT14 (field 1.68) and
                // STWINFONT29/font000 (3.52), both MINIMUM (the shops, the
                // status window's folders; subwnd_shop.cpp, subwnd.cpp).
                static const std::set<std::string> items = {"weapon", "armor1", "armor2", "abChip", "conItem", "impItem", "insItem"};
                if (items.contains(table_name)) box.field_width = column == "NAME" ? 1.68f : column == "TXT" ? 3.52f : 0.0f;
                // An MP technique's text: the same description box
                // (subwnd_skill.cpp: STWINFONT29/font000). Its name also
                // stands in the dungeon panel's narrower list: no field.
                if (table_name == "pcTech" && column == "TXT") box.field_width = 3.52f;
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
            const auto widths = measure(*font, ink_text(text.english, box.align));
            for (const float width : widths) box.max_width = std::max(box.max_width, width);
            box.max_lines = std::max(box.max_lines, visible_lines(widths));
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
    out += "# max_width / max_lines: the strictest box this text is shown in (the widest English line there, in its font's units);\n";
    out += "# the budget is max_width or field_width where that is wider.\n";
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
            // (The box's budget: its widest English line or its field, unless
            // the field runs under a frame, framed_field.)
            const float field = framed_field(box.key) ? 0.0f : box.field_width;
            const float normalized = std::max(box.max_width, field) / std::max(fonts.scale(box.style), 0.01f);
            if (!strictest || normalized < best) strictest = &box, best = normalized;
            lines = lines ? std::min(lines, box.max_lines) : box.max_lines;
            if (!boxes.empty()) boxes += ";";
            boxes += key;
        }
        if (!strictest) continue;
        out += "\t" + i18n::encode_tokens(text.english) + "\t\t\t" + text.text_class + "\t" + text.where + "\t" + boxes + "\t" + strictest->style +
               "\t" + number(strictest->max_width) + "\t" + std::to_string(lines) + "\t" + align_name(strictest->align) + "\t" +
               (strictest->field_width > 0 && !framed_field(strictest->key) ? number(strictest->field_width) : std::string("-")) + "\t" +
               std::to_string(text.copies) + "\n";
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

// A word the next one stays with on its line: a number ("1 Punkt", "450 HP")
// or one that ends in an Item / element icon ("{I_ARMOR1} Rüstungs-Board").
bool binds_to_next(std::u16string_view word) {
    static constexpr std::u16string_view icons = u"∈∋∨∧¶⊂⊃∪∩⊆";
    if (word.empty()) return false;
    if (icons.find(word.back()) != std::u16string_view::npos) return true;
    return std::all_of(word.begin(), word.end(), [](char16_t c) { return (c >= u'0' && c <= u'9') || (c >= u'０' && c <= u'９'); });
}

// The words of a text wrapped greedily into lines that fit, added to lines
// (a text without words adds one empty line); false when a word is too wide.
bool wrap_words(const engine::FontImage& font, std::u16string_view text, float squeeze, float max_width, std::vector<std::u16string>& lines) {
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
    // (A number or an icon and the word after it are one unit.)
    for (std::size_t i = 0; i + 1 < words.size();) {
        if (binds_to_next(words[i])) {
            words[i] += u" " + words[i + 1];
            words.erase(words.begin() + std::ptrdiff_t(i) + 1);
        } else {
            ++i;
        }
    }
    if (words.empty()) {
        lines.emplace_back();
        return true;
    }
    std::u16string current;
    for (const auto& next : words) {
        const std::u16string candidate = current.empty() ? next : current + u" " + next;
        const auto widths = measure(font, candidate, squeeze);
        if (!widths.empty() && widths[0] <= max_width + 1e-4f) {
            current = candidate;
            continue;
        }
        if (current.empty()) return false; // one word wider than the box
        lines.push_back(std::move(current));
        current = next;
        const auto alone = measure(font, current, squeeze);
        if (alone.empty() || alone[0] > max_width + 1e-4f) return false;
    }
    lines.push_back(std::move(current));
    return true;
}

// The text's lines (split at its line breaks, CR LF or a lone CR / LF).
std::vector<std::u16string_view> own_lines(std::u16string_view text) {
    std::vector<std::u16string_view> lines;
    for (std::size_t start = 0;;) {
        const auto end = text.find_first_of(u"\r\n", start);
        lines.push_back(text.substr(start, end == std::u16string_view::npos ? end : end - start));
        if (end == std::u16string_view::npos) break;
        start = end + (text.substr(end).starts_with(u"\r\n") ? 2 : 1);
    }
    return lines;
}

// The text wrapped greedily into lines that fit. Its edges (leading and
// trailing spaces and line breaks: structure, a vertically placed box) stay
// as they are. Its own line breaks are kept first (structure such as a
// heading line or options; a line that fits stays as it is, a longer one is
// wrapped on its own); when that is too many lines, the free mode wraps the
// whole text as one paragraph (its breaks become spaces), keep_header the
// lines after the first one, keep_lines gives up. Empty when nothing fits.
// (A line's width is its ink: ink_text with the box's alignment.)
std::u16string rewrap(const engine::FontImage& font, std::u16string_view text, float squeeze, float max_width, int max_lines, WrapMode mode,
                      int align) {
    const auto first = text.find_first_not_of(u" \r\n");
    if (first == std::u16string_view::npos) return {};
    const auto last = text.find_last_not_of(u" \r\n");
    const std::u16string_view lead = text.substr(0, first), trail = text.substr(last + 1), core = text.substr(first, last + 1 - first);
    // (Leading breaks are lines of the box; trailing ones show nothing.)
    const int lead_lines = int(own_lines(lead).size()) - 1;
    const int core_lines = max_lines > 0 ? max_lines - lead_lines : 0;
    if (max_lines > 0 && core_lines <= 0) return {};
    auto fits = [&](const std::vector<std::u16string>& lines) { return core_lines <= 0 || int(lines.size()) <= core_lines; };
    auto joined = [&](const std::vector<std::u16string>& lines) {
        std::u16string out(lead);
        for (std::size_t i = 0; i < lines.size(); ++i) out += (i ? u"\r\n" : u"") + lines[i];
        return out + std::u16string(trail);
    };
    auto fits_width = [&](std::u16string_view line) {
        const auto widths = measure(font, ink_text(line, align), squeeze);
        return widths.empty() || widths[0] <= max_width + 1e-4f;
    };
    const auto parts = own_lines(core);
    std::vector<std::u16string> lines;
    bool wrapped = true;
    for (const auto part : parts) {
        if (fits_width(part)) lines.emplace_back(part);
        else if (!(wrapped = wrap_words(font, part, squeeze, max_width, lines))) break;
    }
    if (wrapped && fits(lines)) return joined(lines);
    if (mode == WrapMode::keep_lines) return {};
    lines.clear();
    std::u16string_view rest = core;
    if (mode == WrapMode::keep_header && parts.size() > 1) {
        if (!fits_width(parts[0])) return {};
        lines.emplace_back(parts[0]);
        rest = core.substr(std::size_t(parts[1].data() - core.data()));
    }
    if (!wrap_words(font, rest, squeeze, max_width, lines) || !fits(lines)) return {};
    return joined(lines);
}

// The line breaks at a text's start and end.
std::pair<int, int> edge_breaks(std::u16string_view text) {
    int lead = 0, trail = 0;
    for (std::size_t i = 0; i < text.size() && (text[i] == u' ' || text[i] == u'\r' || text[i] == u'\n'); ++i)
        if (text[i] == u'\n' || (text[i] == u'\r' && (i + 1 >= text.size() || text[i + 1] != u'\n'))) ++lead;
    for (std::size_t i = text.size(); i > 0 && (text[i - 1] == u' ' || text[i - 1] == u'\r' || text[i - 1] == u'\n'); --i)
        if (text[i - 1] == u'\n' || (text[i - 1] == u'\r' && (i >= text.size() || text[i] != u'\n'))) ++trail;
    if (text.find_first_not_of(u" \r\n") == std::u16string_view::npos) trail = 0;
    return {lead, trail};
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
            // The budget: the widest English line in the box, or the box's
            // field where that is wider (a short English word in a wide
            // label, e.g. the status window's "Spirit" in its 0.84 field);
            // not a field that runs under a frame (framed_field: a source
            // exported before that rule still names its field).
            const float budget = in_framed_field(box->boxes) ? box->max_width : std::max(box->max_width, box->field_width);
            // The lines: the box's, but a memory card text keeps its English
            // line count (its box's next line is often the other half of the
            // sentence, CHRSELFONT06 font000 / font001, or the options).
            // (Lines count up to the last one that shows something: a
            // trailing line break adds none.)
            int max_lines = box->max_lines;
            if (box->text_class == "memcard") {
                const int english_lines = visible_lines(measure(*font, entry.english));
                max_lines = max_lines > 0 ? std::min(max_lines, english_lines) : english_lines;
            }
            // (A left-aligned line's trailing spaces show nothing.)
            const auto ink = ink_text(entry.text, box->align);
            const auto widths = measure(*font, ink, entry.squeeze);
            const float widest = widths.empty() ? 0.0f : *std::max_element(widths.begin(), widths.end());
            row.widest = budget > 0 ? widest / budget : 0;
            const int lines = visible_lines(widths);
            const bool too_many = max_lines > 0 && lines > max_lines;
            if (too_many)
                problems.push_back(std::to_string(lines) + " lines, the box shows " + std::to_string(max_lines) +
                                   (max_lines < box->max_lines ? " (a memory card text keeps the English lines)" : ""));
            if (budget > 0 && widest > budget + 1e-4f) {
                const auto plain = measure(*font, ink);
                const float plain_widest = plain.empty() ? 0.0f : *std::max_element(plain.begin(), plain.end());
                const float needed = budget / plain_widest;
                char text[160];
                if (needed >= i18n::kMinSqueeze) {
                    const float squeeze = std::floor(needed * 100.0f) / 100.0f;
                    std::snprintf(text, sizeof text, "%.0f %% of the box: fits with squeeze=%.2f (the last resort; shorter is better)",
                                  double(plain_widest / budget * 100.0f), double(squeeze));
                    squeeze_needed = true;
                } else {
                    std::snprintf(text, sizeof text, "%.0f %% of the box: shorten it (a squeeze reaches 111 %% at most)",
                                  double(plain_widest / budget * 100.0f));
                }
                problems.push_back(text);
            }
            if ((too_many || (budget > 0 && widest > budget + 1e-4f)) && budget > 0) {
                const auto wrapped = rewrap(*font, entry.text, entry.squeeze, budget, max_lines, wrap_mode(box->text_class, box->boxes), box->align);
                if (!wrapped.empty()) row.suggestion = i18n::encode_tokens(wrapped);
            }
            // Worth a look (shown as it is).
            if (digits_of(entry.english) != digits_of(entry.text)) notes.push_back("the numbers differ from the English text's");
            if (!entry.english.empty() && entry.english.back() == u':' && (entry.text.empty() || entry.text.back() != u':'))
                notes.push_back("the English text ends in ':' (the game appends a value there)");
            if (edge_breaks(entry.english) != edge_breaks(entry.text))
                notes.push_back("the English text starts or ends with other {BR}s (keep them: they place the text in its box)");
        } else if (!box) {
            notes.push_back("not in the English corpus (stale, or a text the game composes)");
            ++report.not_in_corpus;
        }
        // An id row is used only while the engine knows the line's pack
        // string; a line rebuilt from a save state, a seamless join or a
        // rerun has no index and shows the content row (docs/L10N.md 2).
        if (i18n::id_kind(entry.id) == i18n::IdKind::pack && !parsed.catalog->find(entry.english))
            notes.push_back("an id row without a content row for its English text: lines rebuilt from a save state, a co-op join or a rerun "
                            "show English until the next TEXTSET (add a row with an empty id)");
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

std::string apply_wraps(std::string_view catalog_tsv, const LintReport& report, std::size_t* applied) {
    std::map<std::size_t, const LintRow*> wraps;
    for (const auto& row : report.rows)
        if (!row.suggestion.empty() && row.line > 0) wraps[row.line] = &row;
    std::size_t count = 0;
    std::string out;
    const auto lines = lines_of(catalog_tsv);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto found = wraps.find(i + 1);
        auto columns = split(lines[i], '\t');
        if (found == wraps.end() || columns.size() < 3 || columns[2] != found->second->text) {
            out += lines[i] + "\n";
            continue;
        }
        columns[2] = found->second->suggestion;
        for (std::size_t c = 0; c < columns.size(); ++c) out += (c ? "\t" : "") + std::string(columns[c]);
        out += "\n";
        ++count;
    }
    if (applied) *applied = count;
    return out;
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
