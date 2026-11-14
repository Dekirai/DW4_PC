// dw4_l10n: the translation tools (docs/L10N.md 4).
//
//   dw4_l10n export --research DIR --data GAME --out FILE
//       The English corpus as a translator's TSV (also a valid game.tsv with
//       empty translations): one row per unique English text with the box it
//       is shown in (font, max_width, max_lines, alignment). DIR is the text
//       inventory (work/l10n_research/text), GAME the extracted disc (read
//       only: the font metrics). The output is derived from the game: keep it
//       local (work/).
//   dw4_l10n lint --source FILE --catalog FILE --data GAME [--all] [--report FILE]
//       Checks a translated game.tsv against the exported source with the
//       engine's font metrics and layout: tokens, glyphs, each line's width
//       against its box, the line count, whether a squeeze is needed. Prints
//       the rows that are not ok (--all: every row) and a summary; exit code
//       1 when a row has an error or needs a squeeze flag it lacks.
//   dw4_l10n wrap --source FILE --catalog FILE --data GAME --out FILE
//       The catalog with the lint's suggested wraps put in: every translation
//       too wide or too long for its box that a greedy wrap makes fit (its own
//       {BR}s kept where possible). Translators write running text and let
//       this break it; the lint afterwards shows what is left.
//   dw4_l10n measure --data GAME --file FILE [--font NORMAL|MINIMUM]
//       The width of each line of every text in FILE (one text in the token
//       form per line), laid out as the engine lays a translation out: for
//       trying wordings against a box's budget (the export's max_width).
//   dw4_l10n revision --source FILE
//       The English corpus revision the source names (language.ini revision=).
#include "l10n.hpp"

#include "dw4/igb_document.hpp"
#include "dw4/resources.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int usage() {
    std::cerr << "usage: dw4_l10n export --research DIR --data GAME --out FILE\n"
                 "       dw4_l10n lint --source FILE --catalog FILE --data GAME [--all] [--report FILE]\n"
                 "       dw4_l10n wrap --source FILE --catalog FILE --data GAME --out FILE\n"
                 "       dw4_l10n measure --data GAME --file FILE [--font NORMAL|MINIMUM]\n"
                 "       dw4_l10n revision --source FILE\n";
    return 2;
}

bool read_file(const std::filesystem::path& path, std::string& text) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

bool write_file(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary);
    out << text;
    return bool(out);
}

// The game's fonts (bectext.igb) as translations are laid out.
struct GameFonts {
    std::shared_ptr<dw4::IgbDocument> document;
    std::map<std::string, std::unique_ptr<dw4::engine::FontImage>> originals;
    std::unique_ptr<dw4::engine::FontImage> normal, minimum;
    dw4::l10n::Fonts fonts() const { return {normal.get(), minimum.get()}; }
};
bool load_fonts(const std::filesystem::path& data, GameFonts& fonts) {
    try {
        const auto resources = dw4::MountedResources::open(data);
        fonts.document = std::make_shared<dw4::IgbDocument>(dw4::read_igb_document(resources.read("bectext.igb")));
        fonts.originals = dw4::engine::read_font_images(*fonts.document);
    } catch (const std::exception& error) {
        std::cerr << "the game's font (bectext.igb): " << error.what() << '\n';
        return false;
    }
    const auto normal = fonts.originals.find("NORMAL"), minimum = fonts.originals.find("MINIMUM");
    if (normal == fonts.originals.end() || minimum == fonts.originals.end()) {
        std::cerr << "bectext.igb has no NORMAL / MINIMUM font\n";
        return false;
    }
    fonts.normal = dw4::engine::make_display_font(*normal->second);
    fonts.minimum = dw4::engine::make_display_font(*minimum->second);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string command = argv[1];
    std::map<std::string, std::string> options;
    bool all = false;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--all") {
            all = true;
        } else if (arg.starts_with("--") && i + 1 < argc) {
            options[arg.substr(2)] = argv[++i];
        } else {
            return usage();
        }
    }
    auto option = [&](const char* name) -> std::string {
        const auto found = options.find(name);
        return found == options.end() ? std::string() : found->second;
    };

    if (command == "export") {
        if (option("research").empty() || option("data").empty() || option("out").empty()) return usage();
        GameFonts fonts;
        if (!load_fonts(std::filesystem::u8path(option("data")), fonts)) return 1;
        std::vector<std::string> problems;
        auto corpus = dw4::l10n::read_research(std::filesystem::u8path(option("research")), problems);
        for (const auto& problem : problems) std::cerr << "export: " << problem << '\n';
        if (corpus.texts.empty()) {
            std::cerr << "export: no English text found\n";
            return 1;
        }
        dw4::l10n::compute_budgets(corpus, fonts.fonts());
        if (!write_file(std::filesystem::u8path(option("out")), dw4::l10n::write_source_tsv(corpus, fonts.fonts()))) {
            std::cerr << "export: " << option("out") << " cannot be written\n";
            return 1;
        }
        std::cout << "export: " << corpus.texts.size() << " English texts in " << corpus.boxes.size() << " boxes, revision "
                  << dw4::l10n::corpus_revision(corpus) << " -> " << option("out") << '\n';
        return 0;
    }
    if (command == "lint" || command == "wrap") {
        if (option("source").empty() || option("catalog").empty() || option("data").empty()) return usage();
        if (command == "wrap" && option("out").empty()) return usage();
        std::string source_text, catalog_text;
        if (!read_file(std::filesystem::u8path(option("source")), source_text)) {
            std::cerr << command << ": " << option("source") << " cannot be read\n";
            return 1;
        }
        if (!read_file(std::filesystem::u8path(option("catalog")), catalog_text)) {
            std::cerr << command << ": " << option("catalog") << " cannot be read\n";
            return 1;
        }
        GameFonts fonts;
        if (!load_fonts(std::filesystem::u8path(option("data")), fonts)) return 1;
        std::vector<std::string> problems;
        const auto source = dw4::l10n::read_source_tsv(source_text, problems);
        for (const auto& problem : problems) std::cerr << command << ": " << problem << '\n';
        if (source.rows.empty()) return 1;
        const auto report = dw4::l10n::lint(source, catalog_text, fonts.fonts());
        if (command == "wrap") {
            std::size_t applied = 0;
            if (!write_file(std::filesystem::u8path(option("out")), dw4::l10n::apply_wraps(catalog_text, report, &applied))) {
                std::cerr << "wrap: " << option("out") << " cannot be written\n";
                return 1;
            }
            std::cout << "wrap: " << applied << " translations wrapped -> " << option("out") << '\n';
            return 0;
        }
        const auto text = dw4::l10n::format_report(report, all);
        if (!option("report").empty()) {
            if (!write_file(std::filesystem::u8path(option("report")), text)) {
                std::cerr << "lint: " << option("report") << " cannot be written\n";
                return 1;
            }
            // (The summary on the console too.)
            const auto summary = text.rfind("summary:");
            std::cout << (summary == std::string::npos ? text : text.substr(summary));
        } else {
            std::cout << text;
        }
        return report.failed() ? 1 : 0;
    }
    if (command == "measure") {
        std::string list;
        if (option("data").empty() || option("file").empty()) return usage();
        if (!read_file(std::filesystem::u8path(option("file")), list)) {
            std::cerr << "measure: " << option("file") << " cannot be read\n";
            return 1;
        }
        GameFonts fonts;
        if (!load_fonts(std::filesystem::u8path(option("data")), fonts)) return 1;
        const auto* font = fonts.fonts().of(option("font") == "MINIMUM" ? "MINIMUM" : "NORMAL");
        std::istringstream lines(list);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::string error;
            const auto text = dw4::i18n::decode_tokens(line, &error);
            if (!text) {
                std::cout << "?\t" << line << "\t" << error << '\n';
                continue;
            }
            std::string widths;
            char number[16];
            for (const float width : dw4::l10n::measure(*font, *text)) {
                std::snprintf(number, sizeof number, "%.3f", double(width));
                widths += (widths.empty() ? "" : " ") + std::string(number);
            }
            std::cout << widths << '\t' << line << '\n';
        }
        return 0;
    }
    if (command == "revision") {
        std::string source_text;
        if (option("source").empty() || !read_file(std::filesystem::u8path(option("source")), source_text)) return usage();
        std::vector<std::string> problems;
        const auto source = dw4::l10n::read_source_tsv(source_text, problems);
        std::cout << source.revision << '\n';
        return source.revision.empty() ? 1 : 0;
    }
    return usage();
}
