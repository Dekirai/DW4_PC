#include "language.hpp"

#include "gl_renderer.hpp"

#include "dw4/engine/engine.hpp"
#include "dw4/engine/managers.hpp"
#include "dw4/igb_document.hpp"
#include "dw4/textures.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>

namespace dw4::game {
namespace {

// A pack file's bytes (bounded: a catalog of the whole game is a few MB).
bool read_file(const std::filesystem::path& path, std::string& text, std::string& message) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        message = path.filename().string() + " cannot be read";
        return false;
    }
    if (size > 64u * 1024u * 1024u) {
        message = path.filename().string() + " is larger than 64 MB";
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    text.assign(std::size_t(size), '\0');
    if (!in.read(text.data(), std::streamsize(size))) {
        message = path.filename().string() + " cannot be read";
        return false;
    }
    return true;
}

std::string lower(std::string text) {
    for (auto& c : text) c = char(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

} // namespace

void Languages::discover(const std::filesystem::path& lang_root) {
    entries_.clear();
    packs_.clear();
    entries_.push_back({"en", "English", true});
    std::vector<std::string> problems;
    for (auto& info : i18n::discover_languages(lang_root, &problems)) {
        std::error_code error;
        const bool texts = std::filesystem::is_regular_file(info.folder / info.game_file, error);
        entries_.push_back({info.code, info.name, texts});
        Pack pack;
        pack.info = std::move(info);
        packs_.emplace(entries_.back().code, std::move(pack));
    }
    for (auto& problem : problems) note("lang/" + problem);
    selected_ = "en";
    ++generation_;
}

std::vector<std::string> Languages::take_notes() {
    auto notes = std::move(notes_);
    notes_.clear();
    return notes;
}

bool Languages::load(Pack& pack, std::string& message) {
    const std::string& code = pack.info.code;
    pack.catalog.reset();
    pack.rules = {};
    pack.textures.clear();
    pack.textures_built = false;
    std::error_code error;
    const auto game_path = pack.info.folder / pack.info.game_file;
    if (std::filesystem::is_regular_file(game_path, error)) {
        std::string text;
        if (!read_file(game_path, text, message)) return false;
        auto parsed = i18n::GameTextCatalog::parse(text);
        pack.catalog = parsed.catalog;
        note(code + ": " + pack.info.game_file + " " + std::to_string(parsed.accepted) + " texts, " + std::to_string(parsed.rejected) +
             " rejected (English), " + std::to_string(parsed.skipped) + " untranslated or repeated" +
             (parsed.revision.empty() ? std::string() : ", revision " + parsed.revision.substr(0, 16)));
        if (!pack.info.revision.empty() && !parsed.revision.empty() && parsed.revision != pack.info.revision)
            note(code + ": game.tsv was made for another English revision than language.ini names (each text is still checked)");
        std::size_t shown = 0;
        for (const auto& issue : parsed.issues) {
            if (!issue.rejected) continue;
            if (++shown > 20) {
                note(code + ": ... (" + std::to_string(parsed.rejected - 20) + " more rejected lines; the l10n lint lists them)");
                break;
            }
            note(code + ": " + pack.info.game_file + " line " + std::to_string(issue.line) + ": " + issue.message);
        }
    } else {
        note(code + ": no " + pack.info.game_file + " (the game text stays English, only the pictures change)");
    }
    if (!pack.info.textures_file.empty()) {
        const auto textures_path = pack.info.folder / pack.info.textures_file;
        if (std::filesystem::is_regular_file(textures_path, error)) {
            std::string text;
            if (!read_file(textures_path, text, message)) return false;
            pack.rules = i18n::parse_texture_rules(text);
            for (const auto& problem : pack.rules.problems) note(code + ": " + pack.info.textures_file + " " + problem);
        }
    }
    return true;
}

bool Languages::select(std::string_view code, std::string* message) {
    std::string why;
    if (code == "en") {
        if (selected_ != "en") ++generation_;
        selected_ = "en";
        return true;
    }
    const auto found = packs_.find(std::string(code));
    if (found == packs_.end()) {
        if (message) *message = "no language pack '" + std::string(code) + "' in lang/";
        return false;
    }
    if (!load(found->second, why)) {
        if (message) *message = std::string(code) + ": " + why;
        return false;
    }
    selected_ = found->first;
    ++generation_;
    return true;
}

void Languages::sync(engine::Engine& engine, GlRenderer* renderer) {
    Pack* pack = nullptr;
    if (selected_ != "en")
        if (const auto found = packs_.find(selected_); found != packs_.end()) pack = &found->second;
    const std::shared_ptr<const i18n::GameTextCatalog> catalog = pack ? pack->catalog : nullptr;
    if (engine.display_text() != catalog.get()) engine.set_display_text(catalog);
    if (!has_packs()) return; // (English only: the renderer stays exactly as it was)
    if (pack && !pack->textures_built) build_textures(*pack, engine);
    if (!renderer) return;
    if (const auto* text = engine.get<engine::TextCtlManager>())
        if (const auto* font = text->font("NORMAL")) renderer->set_font_atlas(*font);
    if (renderer != renderer_ || renderer_generation_ != generation_) {
        renderer->set_language_textures(pack && pack->textures_built ? pack->textures : LanguageTextures{});
        renderer_ = renderer;
        renderer_generation_ = generation_;
    }
}

void Languages::build_textures(Pack& pack, engine::Engine& engine) {
    const auto* text = engine.get<engine::TextCtlManager>();
    const engine::FontImage* font = text ? text->font("NORMAL") : nullptr;
    const bool font_ready = font && font->binding && font->binding->image;
    const bool renders = std::any_of(pack.rules.rules.begin(), pack.rules.rules.end(),
                                     [](const i18n::TextureRule& rule) { return rule.kind == i18n::TextureRule::Kind::render; });
    if (renders && !font_ready) return; // (again once the first stage loaded the font)
    pack.textures_built = true;
    ++generation_;
    const std::string& code = pack.info.code;

    // disc:<pack>:<hash>: the language's original pictures, read from the
    // player's own disc outside the engine's load queue (its timing stays).
    std::map<std::string, std::vector<const i18n::TextureRule*>> wanted;
    for (const auto& rule : pack.rules.rules)
        if (rule.kind == i18n::TextureRule::Kind::disc) wanted[rule.pack].push_back(&rule);
    for (auto& [pack_name, rules] : wanted) {
        const std::string suffix = "/separate/" + pack_name + ".igb";
        std::vector<std::string> candidates;
        for (const auto& resource : engine.resources().catalog().resources) {
            const auto path = lower(resource.logical_path);
            if (path.size() > suffix.size() && path.ends_with(suffix) && resource.location) candidates.push_back(resource.logical_path);
        }
        // (The first dungeon holds every HUD picture; the rest in a fixed order.)
        std::sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b) {
            const bool first_a = lower(a).starts_with("dng101b1/"), first_b = lower(b).starts_with("dng101b1/");
            return first_a != first_b ? first_a : a < b;
        });
        if (candidates.size() > 16) candidates.resize(16);
        for (const auto& path : candidates) {
            if (rules.empty()) break;
            try {
                const auto document = read_igb_document(engine.resources().read(path));
                for (const auto& info : list_igb_textures(document)) {
                    for (auto it = rules.begin(); it != rules.end();) {
                        if ((*it)->image_name != info.name) {
                            ++it;
                            continue;
                        }
                        const auto image = decode_igb_texture(document, info.reference_index);
                        const auto hash = texture_hash(image.rgba, int(image.width), int(image.height), image.block_compressed);
                        if (hash != (*it)->source_hash) {
                            ++it;
                            continue;
                        }
                        LanguageTexture picture;
                        picture.image.width = int(image.width);
                        picture.image.height = int(image.height);
                        picture.image.rgba = image.rgba;
                        picture.hash = hash;
                        picture.name = info.name;
                        pack.textures[(*it)->usa_hash] = std::move(picture);
                        it = rules.erase(it);
                    }
                }
            } catch (const std::exception& error) {
                note(code + ": " + path + ": " + error.what());
            }
        }
        for (const auto* rule : rules) note(code + ": " + rule->image_name + " (" + pack_name + ") was not found on the disc; it stays as it is");
    }

    // render:<recipe>: pictures drawn with the game font from the catalog.
    for (const auto& rule : pack.rules.rules) {
        if (rule.kind != i18n::TextureRule::Kind::render) continue;
        if (rule.recipe != "travel_dialog") {
            note(code + ": unknown picture recipe '" + rule.recipe + "' (" + rule.image_name + " stays as it is)");
            continue;
        }
        if (!pack.catalog) continue;
        try {
            auto atlas = decode_igb_texture(*font->binding->image.document(), font->binding->image.reference());
            const auto display = engine::make_display_font(*font);
            engine::paint_composed_glyphs(atlas.rgba, int(atlas.width), int(atlas.height), *display);
            std::string problem;
            TextureImage image = render_travel_dialog(*pack.catalog, *display, atlas.rgba, int(atlas.width), int(atlas.height), &problem);
            if (image.rgba.empty()) {
                note(code + ": " + rule.image_name + " stays as it is (" + problem + ")");
                continue;
            }
            LanguageTexture picture;
            picture.hash = TexturePack::hash(image.rgba.data(), image.rgba.size(), image.width, image.height);
            picture.image = std::move(image);
            picture.name = rule.image_name;
            pack.textures[rule.usa_hash] = std::move(picture);
        } catch (const std::exception& error) {
            note(code + ": " + rule.image_name + ": " + error.what());
        }
    }
    note(code + ": " + std::to_string(pack.textures.size()) + " of " + std::to_string(pack.rules.rules.size()) + " pictures ready");

    if (!dump_.empty()) {
        TexturePack dumper;
        dumper.configure({}, dump_);
        for (const auto& [usa, picture] : pack.textures)
            dumper.dump(code + "_" + picture.name, picture.image.rgba.data(), picture.image.width, picture.image.height, picture.hash);
    }
}

bool Languages::parse_schedule(std::string_view text, std::string* error) {
    while (!text.empty()) {
        const auto comma = text.find(',');
        const auto item = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        const auto colon = item.find(':');
        if (colon == std::string_view::npos || colon == 0 || colon + 1 >= item.size()) {
            if (error) *error = "expected FRAME:CODE, got '" + std::string(item) + "'";
            return false;
        }
        int frame = 0;
        for (const char c : item.substr(0, colon)) {
            if (c < '0' || c > '9' || frame > 100000000) {
                if (error) *error = "a frame is a number: '" + std::string(item) + "'";
                return false;
            }
            frame = frame * 10 + (c - '0');
        }
        schedule_.emplace_back(frame, std::string(item.substr(colon + 1)));
    }
    return true;
}

void Languages::switch_every(int frames, std::string other) {
    every_ = frames;
    every_other_ = std::move(other);
}

void Languages::at_frame(int frame) {
    auto change = [&](const std::string& code) {
        std::string message;
        if (select(code, &message)) note("now " + code + " (frame " + std::to_string(frame) + ")");
        else note(message);
    };
    for (const auto& [due, code] : schedule_)
        if (due == frame) change(code);
    if (every_ > 0 && frame > 0 && frame % every_ == 0) change(selected_ == "en" ? every_other_ : std::string("en"));
}

TextureImage render_travel_dialog(const i18n::GameTextCatalog& catalog, const engine::FontImage& font, const std::vector<std::uint8_t>& atlas,
                                  int atlas_width, int atlas_height, std::string* problem) {
    // stcngmoji.png (USA 82504a8ec6521575): rows run bottom-up, so the upper
    // line ("Go to the destination.") lies in rows 32-63, the lower one in
    // rows 0-31, both centred on x 128 (the travel dialog's two choices).
    struct Line {
        std::u16string_view english;
        int row0, row1;
    };
    static constexpr Line kLines[] = {{u"Go to the destination.", 32, 64}, {u"Stay here.", 0, 32}};
    TextureImage image;
    image.width = 256;
    image.height = 64;
    std::vector<std::uint8_t> rgba(std::size_t(image.width) * std::size_t(image.height) * 4, 0);
    for (const auto& line : kLines) {
        const auto resolved = catalog.resolve(line.english);
        if (!resolved) {
            if (problem) *problem = "the catalog has no translation of \"" + i18n::utf16_to_utf8(line.english) + "\"";
            return {};
        }
        for (const char16_t c : resolved->text)
            if (c == u'\r' || c == u'\n' || !font.glyphs.contains(std::uint32_t(c))) {
                if (problem) *problem = "the font cannot draw \"" + i18n::encode_tokens(resolved->text) + "\" on one line";
                return {};
            }
        engine::render_text_rgba(rgba, image.width, image.height, atlas, atlas_width, atlas_height, font, resolved->text, 128, line.row0,
                                 line.row1);
    }
    image.rgba = std::move(rgba);
    return image;
}

} // namespace dw4::game
