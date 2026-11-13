#include "dw4/i18n/language_pack.hpp"

#include "dw4/i18n/game_text.hpp"
#include "dw4/ini.hpp"

#include <algorithm>
#include <fstream>

namespace dw4::i18n {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

bool valid_code(std::string_view code) {
    if (code.size() < 2 || code.size() > 8) return false;
    return std::all_of(code.begin(), code.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

// A file name inside the pack folder (no folders, no parent references, no
// control characters; UTF-8 beyond ASCII is fine: it is used as a UTF-8 path).
bool plain_file_name(std::string_view name) {
    if (name.empty() || name.size() > 64 || name == "." || name == "..") return false;
    return std::none_of(name.begin(), name.end(), [](char c) {
        const auto byte = static_cast<unsigned char>(c);
        return c == '/' || c == '\\' || c == ':' || byte < 0x20 || byte == 0x7f;
    });
}

} // namespace

std::optional<std::uint64_t> parse_hash16(std::string_view text) {
    text = trim(text);
    if (text.size() != 16) return std::nullopt;
    std::uint64_t value = 0;
    for (const char c : text) {
        int digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return std::nullopt;
        value = (value << 4) | std::uint64_t(digit);
    }
    return value;
}

std::optional<LanguageInfo> parse_language_ini(std::string_view text, std::string_view folder_name, std::string* error) {
    auto fail = [&](std::string message) -> std::optional<LanguageInfo> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    if (!utf8_to_utf16(text)) return fail("language.ini is not UTF-8");
    const auto ini = IniFile::parse(text);
    LanguageInfo info;
    info.code = ini.get_or("language", "code", folder_name);
    if (!valid_code(info.code)) return fail("the language code '" + info.code + "' is not 2 to 8 lowercase letters, digits or '-'");
    if (info.code == "en") return fail("English is the game's own text and needs no pack");
    info.name = ini.get_or("language", "name", info.code);
    if (info.name.empty() || info.name.size() > 64) info.name = info.code;
    info.revision = ini.get_or("language", "revision", "");
    info.token_format = ini.get_int("language", "token_format", 1);
    if (info.token_format != kTokenFormat)
        return fail("token format " + std::to_string(info.token_format) + " (this build reads " + std::to_string(kTokenFormat) + ")");
    for (auto [key, target] : {std::pair<const char*, std::string*>{"game", &info.game_file}, {"textures", &info.textures_file}, {"ui", &info.ui_file}}) {
        if (const auto value = ini.get("files", key)) {
            if (!value->empty() && !plain_file_name(*value)) return fail(std::string("[files] ") + key + " must be a file name in the pack folder");
            *target = *value;
        }
    }
    return info;
}

std::filesystem::path path_from_utf8(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::vector<LanguageInfo> discover_languages(const std::filesystem::path& lang_root, std::vector<std::string>* problems) {
    constexpr std::size_t kMaxFolders = 256;
    constexpr std::uintmax_t kMaxIni = 64 * 1024;
    std::vector<LanguageInfo> found;
    auto report = [&](const std::string& message) {
        if (problems) problems->push_back(message);
    };
    std::vector<std::filesystem::path> folders;
    try {
        std::error_code error;
        if (!std::filesystem::is_directory(lang_root, error)) return found;
        std::filesystem::directory_iterator entry(lang_root, error);
        for (const std::filesystem::directory_iterator end; !error && entry != end; entry.increment(error)) {
            std::error_code kind;
            if (!entry->is_directory(kind)) continue;
            if (folders.size() == kMaxFolders) {
                report("more than " + std::to_string(kMaxFolders) + " folders: the rest are left out");
                break;
            }
            folders.push_back(entry->path());
        }
    } catch (const std::exception&) {
        report("the folder cannot be listed");
    }
    // (By folder name: the first of two packs for one code is the same on every run.)
    std::sort(folders.begin(), folders.end());
    for (const auto& folder : folders) {
        // Each folder on its own: one that cannot be read (a name outside
        // UTF-8, an ini that vanished) is reported and the rest still load.
        std::string name = "(a folder whose name is not valid UTF-16)";
        try {
            name = path_to_utf8(folder.filename());
            const auto ini_path = folder / "language.ini";
            std::error_code error;
            if (!std::filesystem::is_regular_file(ini_path, error)) {
                report(name + ": no language.ini");
                continue;
            }
            const auto size = std::filesystem::file_size(ini_path, error);
            if (error) {
                report(name + ": language.ini cannot be read");
                continue;
            }
            if (size > kMaxIni) {
                report(name + ": language.ini is larger than 64 KB");
                continue;
            }
            std::string text(std::size_t(size), '\0');
            std::ifstream in(ini_path, std::ios::binary);
            if (!in || !in.read(text.data(), std::streamsize(size))) {
                report(name + ": language.ini cannot be read");
                continue;
            }
            std::string why;
            auto info = parse_language_ini(text, name, &why);
            if (!info) {
                report(name + ": " + why);
                continue;
            }
            if (std::any_of(found.begin(), found.end(), [&](const LanguageInfo& other) { return other.code == info->code; })) {
                report(name + ": a second pack for '" + info->code + "'");
                continue;
            }
            info->folder = folder;
            found.push_back(std::move(*info));
        } catch (const std::exception&) {
            // (MSVC's message is in the ANSI code page: not shown.)
            report(name + ": skipped, its name or language.ini cannot be read");
        }
    }
    std::sort(found.begin(), found.end(), [](const LanguageInfo& a, const LanguageInfo& b) { return a.code < b.code; });
    return found;
}

TextureRules parse_texture_rules(std::string_view text) {
    TextureRules result;
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);
    std::size_t number = 0;
    for (std::string_view rest = text; !rest.empty();) {
        const auto end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        ++number;
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        auto problem = [&](const std::string& message) { result.problems.push_back("line " + std::to_string(number) + ": " + message); };
        std::vector<std::string_view> columns;
        for (std::string_view row = line;;) {
            const auto tab = row.find('\t');
            columns.push_back(trim(row.substr(0, tab)));
            if (tab == std::string_view::npos) break;
            row.remove_prefix(tab + 1);
        }
        if (columns.size() < 3) {
            problem("needs <usa hash> <TAB> <image name> <TAB> <source>");
            continue;
        }
        TextureRule rule;
        const auto hash = parse_hash16(columns[0]);
        if (!hash) {
            problem("the USA hash is not 16 hex digits");
            continue;
        }
        rule.usa_hash = *hash;
        rule.image_name = std::string(columns[1]);
        if (rule.image_name.empty() || rule.image_name.size() > 64) {
            problem("an image name of 1 to 64 characters");
            continue;
        }
        const auto source = columns[2];
        if (source.starts_with("disc:")) {
            const auto body = source.substr(5);
            const auto colon = body.find(':');
            const auto pack = colon == std::string_view::npos ? std::string_view{} : body.substr(0, colon);
            const auto source_hash = colon == std::string_view::npos ? std::nullopt : parse_hash16(body.substr(colon + 1));
            const bool pack_ok = pack.size() >= 4 && pack.size() <= 16 && pack.ends_with("_pack") &&
                                 std::all_of(pack.begin(), pack.end(), [](char c) { return (c >= 'a' && c <= 'z') || c == '_'; });
            if (!pack_ok || !source_hash) {
                problem("disc sources read disc:<name>_pack:<16 hex digits>");
                continue;
            }
            rule.kind = TextureRule::Kind::disc;
            rule.pack = std::string(pack);
            rule.source_hash = *source_hash;
        } else if (source.starts_with("render:")) {
            rule.kind = TextureRule::Kind::render;
            rule.recipe = std::string(source.substr(7));
            if (rule.recipe.empty() || !std::all_of(rule.recipe.begin(), rule.recipe.end(), [](char c) {
                    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
                })) {
                problem("render sources read render:<recipe> (lowercase letters, digits, '_')");
                continue;
            }
        } else {
            problem("the source is neither disc:... nor render:...");
            continue;
        }
        if (std::any_of(result.rules.begin(), result.rules.end(), [&](const TextureRule& other) { return other.usa_hash == rule.usa_hash; })) {
            problem("a second rule for the same USA hash");
            continue;
        }
        result.rules.push_back(std::move(rule));
    }
    return result;
}

} // namespace dw4::i18n
