// Translations (docs/L10N.md): the game.tsv catalog, its token form, the
// compound rules, language.ini, textures.tsv and the font additions, all on
// synthetic input (no game data).
#include "dw4/engine/managers.hpp"
#include "dw4/i18n/game_text.hpp"
#include "dw4/i18n/language_pack.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

using dw4::i18n::GameTextCatalog;

void test_utf8() {
    using dw4::i18n::utf16_to_utf8;
    using dw4::i18n::utf8_to_utf16;
    const std::u16string text = u"Stärke „ok“ ≪ 亜";
    const auto round = utf8_to_utf16(utf16_to_utf8(text));
    check(round && *round == text, "UTF-16 survives the trip through UTF-8");
    check(!utf8_to_utf16("\xC0\xAF"), "An overlong form is invalid");
    check(!utf8_to_utf16("\xED\xA0\x80"), "A surrogate is invalid");
    check(!utf8_to_utf16("\xF0\x9F\x98\x80"), "Code points above U+FFFF are rejected");
    check(!utf8_to_utf16("ab\xE2\x82"), "A truncated sequence is invalid");
    check(!utf8_to_utf16("\x80"), "A stray continuation byte is invalid");
    check(!utf8_to_utf16("\xFF"), "0xFF is invalid");
    check(utf8_to_utf16("") && utf8_to_utf16("")->empty(), "The empty text is valid");
}

void test_tokens() {
    using dw4::i18n::decode_tokens;
    using dw4::i18n::encode_tokens;
    const std::u16string game = u"Press ≪ to talk.\r\nHPあMP andい ⊥ ∈Sword ⊆";
    const auto encoded = encode_tokens(game);
    check(encoded == "Press {A} to talk.{BR}HP/MP and; {START} {I_WEAPON}Sword {I_EQUIPPED}", "Game text encodes to the token form: " + encoded);
    const auto decoded = decode_tokens(encoded);
    check(decoded && *decoded == game, "The token form decodes back to the game's code points");
    check(decode_tokens("{CSTICK}{B2}{DPAD}") == std::u16string(u"娃∠≡"), "Tokens with digits and long names decode");
    check(decode_tokens("a{CR}b{LF}c{TAB}d") == std::u16string(u"a\rb\nc\td"), "Lone CR, LF and TAB have tokens");
    std::string error;
    check(!decode_tokens("{FOO}", &error) && error.find("unknown token") != std::string::npos, "An unknown token is rejected");
    check(!decode_tokens("{a}", &error), "A token in lowercase is rejected");
    check(!decode_tokens("open {A", &error), "A '{' without '}' is rejected");
    check(!decode_tokens("close }", &error), "A stray '}' is rejected");
    check(!decode_tokens("{THIS_NAME_IS_FAR_TOO_LONG}", &error), "An overlong token name is rejected");
    check(!decode_tokens("tab\there", &error), "A raw control character is rejected");
    check(!decode_tokens("line\nbreak", &error), "A raw line break is rejected ({BR} is the line break)");
    check(!decode_tokens("bad \xC3", &error) && error == "invalid UTF-8", "Bad UTF-8 is rejected");
    check(dw4::i18n::glyph_codes(u"a≪b\r\n∈c≫") == u"≪∈≫", "glyph_codes lists the button and icon glyphs in order");
    check(dw4::i18n::start_words(u"Press START.") == 1 && dw4::i18n::start_words(u"START-Taste, ＳＴＡＲＴ") == 2,
          "START counts as a word (also full width, also before a hyphen)");
    check(dw4::i18n::start_words(u"RESTART STARTS START1 xSTART") == 0, "START inside a word does not count");
    check(dw4::i18n::is_equip_marker(u'⊆') && dw4::i18n::is_equip_marker(u'赫') && !dw4::i18n::is_equip_marker(u'∈'),
          "The equip markers are U+2286 and U+8D6B");
}

void test_ids() {
    using dw4::i18n::id_kind;
    using dw4::i18n::IdKind;
    check(id_kind("") == IdKind::content && id_kind("*") == IdKind::content, "An empty id or '*' is a content entry");
    check(id_kind("dng101b1/mswk62009") == IdKind::pack, "<stage>/<name> is a pack id");
    check(id_kind("db:weapon:12:TXT") == IdKind::database, "db:<table>:<row>:<column> is a database id");
    for (const char* bad : {"dng101b1", "a/b/c", "/name", "stage/", "st age/x", "db:weapon:x:TXT", "db:weapon:1", "db::1:NAME", "../x/y"})
        check(id_kind(bad) == IdKind::invalid, std::string("An invalid id is rejected: ") + bad);
}

void test_parse() {
    const std::string tsv =
        "\xEF\xBB\xBF# revision=abc123\r\n"
        "id\tenglish\ttranslation\tflags\tnotes\r\n"
        "\tYes\tJa\r\n"
        "*\tPress {A}.\tDrücke {A}.\t\tdialog TALKFONT01\r\n"
        "dng101b1/mswk1\tHello.\tHallo (Dungeon).\r\n"
        "\tHello.\tHallo.\r\n"
        "db:weapon:3:TXT\tSword Mods:\tSchwert-Mods:\r\n"
        "\tWide text\tSehr breiter Text\tsqueeze=0.92\r\n"
        "\tNot yet\t\r\n"
        "\tFuzzy\tUnscharf\tfuzzy\r\n"
        "\tOdd flag\tKomisch\tsparkle\r\n"
        "# a comment\r\n"
        "\r\n"
        "only two\tcolumns\r\n"
        "bad id here\tX\tY\r\n"
        "\tPress {A}!\tDrücke {B}!\r\n"
        "\tPress START.\tDrücke Start.\r\n"
        "\tToo squeezed\tZu eng\tsqueeze=0.5\r\n"
        "\tBad token\t{NOPE}\r\n"
        "\tBad bytes\tKaputt \xC3\r\n"
        "\tYes\tJa\r\n"
        "\tYes\tJawohl\r\n"
        "dng101b1/mswk1\tHello.\tNochmal\r\n"
        "\t{BR}\t{BR}\r\n"
        "\t\tLeer\r\n"
        "\tSlash/Semi;\tSchräg/Semi;\r\n";
    const auto parsed = GameTextCatalog::parse(tsv);
    const auto& catalog = *parsed.catalog;
    check(parsed.revision == "abc123", "The revision comment is read");
    check(parsed.accepted == 9, "Nine entries are accepted: " + std::to_string(parsed.accepted));
    check(parsed.rejected == 10, "Ten entries are rejected: " + std::to_string(parsed.rejected));
    check(parsed.skipped == 3, "Untranslated, fuzzy and agreeing duplicates are skipped: " + std::to_string(parsed.skipped));
    check(parsed.rows == parsed.accepted + parsed.rejected + parsed.skipped, "Every row is counted once");
    auto issue_at = [&](std::size_t line, const char* text) {
        for (const auto& issue : parsed.issues)
            if (issue.line == line && issue.message.find(text) != std::string::npos) return true;
        return false;
    };
    check(issue_at(14, "fewer than three columns"), "A short row is reported with its line");
    check(issue_at(15, "an id that is neither"), "A bad id is reported");
    check(issue_at(16, "tokens differ"), "Other button tokens are reported");
    check(issue_at(17, "START"), "A lost START is reported");
    check(issue_at(18, "squeeze must lie between"), "A squeeze below 0.90 is reported");
    check(issue_at(19, "unknown token"), "An unknown token in a translation is reported");
    check(issue_at(20, "invalid UTF-8"), "Bad bytes in a translation are reported");
    check(issue_at(22, "another translation"), "A second, different translation of the same English text is reported");
    check(issue_at(23, "a second entry"), "A second entry for the same pack id is reported");
    check(issue_at(25, "empty English"), "An empty English text is reported");
    check(issue_at(11, "unknown flag") && catalog.find(u"Odd flag"), "An unknown flag is a warning, the entry stays");
    check(issue_at(10, "fuzzy") && !catalog.find(u"Fuzzy"), "A fuzzy entry keeps the English text");
    check(!catalog.find(u"Not yet"), "An empty translation keeps the English text");
    check(catalog.find(u"Yes") && catalog.find(u"Yes")->text == u"Ja", "The first of two different translations stays");
    check(catalog.find(u"Press ≪.") && catalog.find(u"Press ≪.")->text == u"Drücke ≪.", "'*' ids and tokens work");
    check(catalog.find(u"Sword Mods:") && catalog.find(u"Sword Mods:")->id == "db:weapon:3:TXT", "A database id is a content entry");
    check(catalog.find(u"Wide text") && std::fabs(catalog.find(u"Wide text")->squeeze - 0.92f) < 1e-6f, "The squeeze flag is read");
    check(catalog.find(u"\r\n") != nullptr, "A text of only a line break is an entry");
    const auto* slash = catalog.find(u"Slashあ" u"Semiい");
    check(slash && slash->text == u"Schrägあ" u"Semiい", "'/' and ';' become the US font's U+3042 / U+3044 on both sides");
    check(catalog.find_id("dng101b1/mswk1") && catalog.find_id("dng101b1/mswk1")->text == u"Hallo (Dungeon).", "Pack ids are kept apart");

    // Never throws, whatever the bytes.
    std::string noise;
    for (int i = 0; i < 4096; ++i) noise.push_back(char((i * 7919 + 13) & 0xFF));
    const auto junk = GameTextCatalog::parse(noise);
    check(junk.catalog != nullptr, "Random bytes give an (empty or partial) catalog");
    check(GameTextCatalog::parse("").catalog->size() == 0, "An empty file is an empty catalog");
}

void test_resolve() {
    const auto parsed = GameTextCatalog::parse(
        "\tHello.\tHallo.\n"
        "stag0001/mswk7\tHello.\tGuten Tag.\n"
        "\t∧Wisdom Chip α\t∧Weisheits-Chip α\n"
        "\tSword Mods:\tSchwert-Mods:\n"
        "\tLevel 2\tStufe 2\n"
        "\tShort\tKurz\tsqueeze=0.95\n");
    const auto& catalog = *parsed.catalog;
    check(parsed.accepted == 6, "The resolve fixture loads");
    auto text = [&](std::u16string_view english, std::string_view id = {}) {
        const auto resolved = catalog.resolve(english, id);
        return resolved ? resolved->text : std::u16string(u"<none>");
    };
    check(text(u"Hello.") == u"Hallo.", "A content entry applies without an id");
    check(text(u"Hello.", "stag0001/mswk7") == u"Guten Tag." && catalog.resolve(u"Hello.", "stag0001/mswk7")->by_id,
          "The id override applies to its pack string");
    check(text(u"Hello.", "stag0002/mswk7") == u"Hallo.", "Another id falls back to the content entry");
    check(text(u"Goodbye.", "stag0001/mswk7") == u"<none>", "An id whose English text differs does not apply");
    check(text(u"⊆∧Wisdom Chip α") == u"⊆∧Weisheits-Chip α", "An equipped marker before a NAME is kept");
    check(text(u"赫∧Wisdom Chip α") == u"赫∧Weisheits-Chip α", "The other equip marker is kept too");
    check(text(u"Sword Mods:3") == u"Schwert-Mods:3", "The rank digit after a TXT is kept");
    check(text(u"Level 2") == u"Stufe 2", "A whole text with a digit is found as it is");
    check(text(u"Level 23") == u"<none>", "Only one trailing digit is a rank digit");
    check(text(u"⊆") == u"<none>" && text(u"7") == u"<none>", "A lone marker or digit is nothing to translate");
    check(text(u"") == u"<none>", "The empty text resolves to nothing");
    check(std::fabs(catalog.resolve(u"⊆Short")->squeeze - 0.95f) < 1e-6f, "A compound keeps its core's squeeze");
    check(text(u"Unknown") == u"<none>", "An unknown text stays English (nullopt)");
}

void test_language_ini() {
    using dw4::i18n::parse_language_ini;
    std::string error;
    const auto info = parse_language_ini(
        "[language]\ncode=de\nname=Deutsch (Fan-Übersetzung)\nrevision=0123abcd\ntoken_format=1\n[files]\ngame=game.tsv\ntextures=tex.tsv\n", "folder",
        &error);
    check(info && info->code == "de" && info->name == "Deutsch (Fan-Übersetzung)" && info->revision == "0123abcd" && info->textures_file == "tex.tsv",
          "A language.ini is read: " + error);
    const auto folder_code = parse_language_ini("[language]\nname=Test\n", "qps", &error);
    check(folder_code && folder_code->code == "qps", "Without a code the folder names the language");
    check(!parse_language_ini("[language]\ncode=EN-GB\n", "x", &error), "An uppercase code is rejected");
    check(!parse_language_ini("[language]\ncode=en\n", "en", &error), "English needs no pack");
    check(!parse_language_ini("[language]\ncode=de\ntoken_format=2\n", "de", &error), "An unknown token format is rejected");
    check(!parse_language_ini("[language]\ncode=de\n[files]\ngame=../../secret.tsv\n", "de", &error), "A game file outside the folder is rejected");
    check(!parse_language_ini("[language]\ncode=de\n[files]\ntextures=C:\\x.tsv\n", "de", &error), "An absolute path is rejected");
    check(!parse_language_ini("[language]\ncode=de\nname=\xFF\n", "de", &error), "A language.ini that is not UTF-8 is rejected");
    check(!parse_language_ini("", "x", &error), "A one-letter folder without a code is rejected");

    // Discovery below a lang/ folder.
    const auto root = std::filesystem::temp_directory_path() / "dw4_i18n_tests_lang";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "de");
    std::filesystem::create_directories(root / "zz");
    std::filesystem::create_directories(root / "fr");
    std::filesystem::create_directories(root / "de2");
    std::ofstream(root / "de" / "language.ini") << "[language]\ncode=de\nname=Deutsch\n";
    std::ofstream(root / "fr" / "language.ini") << "[language]\ncode=fr\nname=Français\n";
    std::ofstream(root / "de2" / "language.ini") << "[language]\ncode=de\nname=Doppelt\n";
    std::ofstream(root / "stray.txt") << "not a folder";
    std::vector<std::string> problems;
    const auto found = dw4::i18n::discover_languages(root, &problems);
    check(found.size() == 2 && found[0].code == "de" && found[1].code == "fr", "Two languages are found, sorted by code");
    check(found[0].folder.filename() == "de", "The folder is recorded");
    check(problems.size() == 2, "The folder without language.ini and the second 'de' are reported: " + std::to_string(problems.size()));
    check(dw4::i18n::discover_languages(root / "missing").empty(), "A missing lang/ folder lists nothing");
    std::filesystem::remove_all(root, ec);
}

void test_texture_rules() {
    using dw4::i18n::parse_texture_rules;
    const auto rules = parse_texture_rules(
        "# usa hash\timage\tsource\n"
        "dce3cec48abddbd1\tstmojipnl01.png\tdisc:ger_pack:a54ea4388dccd54a\n"
        "82504A8EC6521575\tstcngmoji.png\trender:travel_dialog\n"
        "8328a86358ddcecb\tbit.png\tdisc:ger_pack:8891780d65461abd\n"
        "1234\tshort.png\tdisc:ger_pack:8891780d65461abd\n"
        "dce3cec48abddbd1\tagain.png\tdisc:ger_pack:a54ea4388dccd54a\n"
        "0000000000000001\tx.png\turl:http://example\n"
        "0000000000000002\tx.png\tdisc:../pack:8891780d65461abd\n"
        "0000000000000003\tx.png\trender:Bad Recipe\n"
        "0000000000000004\tonly two columns\n"
        "0000000000000005\tx.png\tdisc:ger_pack:xyz\n");
    check(rules.rules.size() == 3, "Three rules are read: " + std::to_string(rules.rules.size()));
    check(rules.problems.size() == 7, "Seven lines are rejected: " + std::to_string(rules.problems.size()));
    const auto& first = rules.rules[0];
    check(first.usa_hash == 0xdce3cec48abddbd1ull && first.kind == dw4::i18n::TextureRule::Kind::disc && first.pack == "ger_pack" &&
              first.source_hash == 0xa54ea4388dccd54aull,
          "A disc rule names the pack and the source hash");
    check(rules.rules[1].usa_hash == 0x82504a8ec6521575ull && rules.rules[1].kind == dw4::i18n::TextureRule::Kind::render &&
              rules.rules[1].recipe == "travel_dialog",
          "A render rule names its recipe (hashes in any case)");
    check(rules.problems[0].starts_with("line 5:"), "Problems name their line");
    check(dw4::i18n::parse_hash16("ffffffffffffffff") == 0xffffffffffffffffull && !dw4::i18n::parse_hash16("fffffffffffffffg"),
          "parse_hash16 reads 16 hex digits only");
}

void test_sha256() {
    check(dw4::i18n::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of 'abc'");
    check(dw4::i18n::sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of the empty text");
    check(dw4::i18n::sha256_hex(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb",
          "SHA-256 across a block boundary");
}

// A synthetic font: 'A'..'Z', 'a'..'z', ' ', ',', '-', ”, ’, ー, Ä, ä in a
// 64 x 64 atlas, every glyph 0.1 wide (advance 0.12), cells 4 x 8 texels.
dw4::engine::FontImage synthetic_font() {
    dw4::engine::FontImage font;
    font.name = "NORMAL";
    font.line_height = 0.28f;
    int cell = 0;
    auto add = [&](char32_t code) {
        dw4::engine::Glyph g;
        g.advance = 0.12f;
        g.width = 0.1f;
        g.height = 0.27f;
        const int x = (cell % 8) * 4, y = (cell / 8) * 8;
        g.u0 = float(x) / 64.0f, g.u1 = float(x + 4) / 64.0f;
        g.v0 = float(y) / 64.0f, g.v1 = float(y + 8) / 64.0f;
        font.glyphs[std::uint32_t(code)] = g;
        ++cell;
    };
    for (char32_t c : {U'A', U'B', U'C', U' ', U',', U'-', U'”', U'’', U'ー', U'Ä', U'ä'}) add(c);
    return font;
}

void test_fonts() {
    using namespace dw4::engine;
    const FontImage font = synthetic_font();
    const auto display = make_display_font(font);
    check(display->glyphs.contains(u'„') && display->glyphs.contains(u'“') && display->glyphs.contains(u'‘'), "The composed glyphs are added");
    check(display->glyphs.at(u'‚').u0 == font.glyphs.at(u',').u0 && display->glyphs.at(u'–').u0 == font.glyphs.at(u'-').u0 &&
              display->glyphs.at(u'—').u0 == font.glyphs.at(u'ー').u0,
          "‚ – — are aliases of their sources' cells");
    check(std::fabs(display->glyphs.at(u'„').u0 - 258.0f / 512.0f) < 1e-6f, "A composed cell lies in the free area");
    check(std::fabs(display->glyphs.at(u'Ä').y_offset - 0.01f) < 1e-6f && display->glyphs.at(u'A').y_offset == 0.0f,
          "Umlauts are raised by one texel, the base letters stay");
    check(!font.glyphs.contains(u'„'), "The original font keeps its glyphs");
    check(display->binding == nullptr, "The display font binds no texture of its own");

    // English text lays out the same with the display font.
    const auto a = layout_text(font, u"AB C,\r\nCBA", 2, 2.0f);
    const auto b = layout_text(*display, u"AB C,\r\nCBA", 2, 2.0f);
    check(a.positions.size() == b.positions.size() && a.texcoords == b.texcoords, "English quads are unchanged by the display font");
    bool same = true;
    for (std::size_t i = 0; i < a.positions.size(); ++i)
        same = same && a.positions[i].x == b.positions[i].x && a.positions[i].y == b.positions[i].y;
    check(same, "English positions are unchanged by the display font");

    // Measured widths and the squeeze.
    std::vector<float> widths;
    (void)layout_text(font, u"AAA\r\nA", 0, 2.0f, 1.0f, &widths);
    check(widths.size() == 2 && std::fabs(widths[0] - 0.34f) < 1e-5f && std::fabs(widths[1] - 0.1f) < 1e-5f,
          "line_widths measures each line's right edge");
    const auto squeezed = layout_text(font, u"AAA", 1, 2.0f, 0.9f, &widths);
    check(std::fabs(widths[0] - 0.306f) < 1e-5f, "A squeezed line is narrower");
    float right = 0;
    for (const auto& p : squeezed.positions) right = std::max(right, p.x);
    check(std::fabs(right - 2.0f) < 1e-5f, "A squeezed right-aligned line still ends at the box's right edge");
    const auto plain = layout_text(font, u"AAA", 1, 2.0f);
    const auto unit = layout_text(font, u"AAA", 1, 2.0f, 1.0f);
    bool unchanged = plain.positions.size() == unit.positions.size();
    for (std::size_t i = 0; unchanged && i < plain.positions.size(); ++i) unchanged = plain.positions[i].x == unit.positions[i].x;
    check(unchanged, "A squeeze of 1 leaves the original arithmetic");

    // Painting the composed cells into a 512 x 512 atlas (and a 4x one).
    for (const int size : {512, 2048}) {
        dw4::engine::FontImage big = synthetic_font();
        // Source cells of ” and ’ in the atlas's used part, ink in their top rows.
        auto& quote = big.glyphs.at(u'”');
        quote.u0 = 10.0f / 512, quote.u1 = 19.0f / 512, quote.v0 = 100.0f / 512, quote.v1 = 127.0f / 512;
        auto& apostrophe = big.glyphs.at(u'’');
        apostrophe.u0 = 30.0f / 512, apostrophe.u1 = 35.0f / 512, apostrophe.v0 = 100.0f / 512, apostrophe.v1 = 127.0f / 512;
        std::vector<std::uint8_t> atlas(std::size_t(size) * std::size_t(size) * 4, 0);
        const int scale = size / 512;
        auto texel = [&](int x, int y) { return atlas.data() + (std::size_t(y) * std::size_t(size) + std::size_t(x)) * 4; };
        // ”: ink in the cell's top 6 rows (v 121-126 in 512ths), leftmost column marked.
        for (int y = 121 * scale; y < 127 * scale; ++y)
            for (int x = 10 * scale; x < 19 * scale; ++x) texel(x, y)[3] = x < 12 * scale ? 255 : 128;
        paint_composed_glyphs(atlas, size, size, big);
        // „ (cell at 258/260): the ink moved 18 rows down: rows 260+3..260+8.
        auto alpha = [&](int x512, int y512) { return texel(x512 * scale, y512 * scale)[3]; };
        check(alpha(258, 263) == 255 && alpha(258, 268) == 255 && alpha(258, 281) == 0, "„ is ” moved 18 rows down (" + std::to_string(size) + ")");
        // “ (cell at 274/260): turned 180 degrees (rows 0..5 of the cell), moved up 18 rows: rows 278..283; the mark now on the right.
        check(alpha(274 + 8, 278) == 255 && alpha(274, 278) == 128 && alpha(274 + 8, 262) == 0, "“ is ” turned and moved back up (" + std::to_string(size) + ")");
        check(alpha(258, 300) == 0 && alpha(300, 300) == 0, "Nothing else is painted (" + std::to_string(size) + ")");
    }

    // Text drawn into a picture (the travel dialog's look).
    std::vector<std::uint8_t> atlas(64 * 64 * 4, 0);
    for (std::size_t i = 3; i < atlas.size(); i += 4) atlas[i] = 255;
    for (std::size_t i = 0; i < atlas.size(); i += 4) atlas[i] = atlas[i + 1] = atlas[i + 2] = 255;
    std::vector<std::uint8_t> picture(64 * 32 * 4, 0);
    // (Synthetic scale: 4 texels per 0.1 font units -> 40 texels per unit.)
    render_text_rgba(picture, 64, 32, atlas, 64, 64, font, u"AB", 32, 0, 32);
    int white = 0, black = 0, left = 64, right_x = -1;
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) {
            const auto* p = picture.data() + (std::size_t(y) * 64 + std::size_t(x)) * 4;
            if (p[3] == 255 && p[0] == 255) ++white;
            if (p[3] > 0 && p[0] == 0) ++black;
            if (p[3] == 255 && p[0] == 255) left = std::min(left, x), right_x = std::max(right_x, x);
        }
    check(white > 0 && black > 0, "Glyphs are drawn white with a black outline");
    check(std::abs((left + right_x) / 2 - 32) <= 1, "The text is centred on center_x");
}
} // namespace

int main() {
    try {
        test_utf8();
        test_tokens();
        test_ids();
        test_parse();
        test_resolve();
        test_language_ini();
        test_texture_rules();
        test_sha256();
        test_fonts();
    } catch (const std::exception& error) {
        std::cerr << "i18n test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
    std::cout << "i18n tests passed (" << checks << " checks)\n";
    return 0;
}
