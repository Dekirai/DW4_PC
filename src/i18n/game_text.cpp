#include "dw4/i18n/game_text.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace dw4::i18n {
namespace {

struct Token {
    std::string_view name;
    char16_t code;
};
// Buttons (src/game/prompt_layout.cpp kButtonCodes), icons (docs/SEAMLESS.md
// "item icons"), the line break and the US font's slash / semicolon cells.
constexpr Token kGlyphTokens[] = {
    {"A", u'≪'},        {"B", u'≫'},       {"B2", u'∠'},       {"X", u'≒'},
    {"Y", u'⌒'},        {"START", u'⊥'},   {"L", u'√'},        {"R", u'亜'},
    {"Z", u'∵'},        {"DPAD", u'≡'},    {"STICK", u'∇'},    {"CSTICK", u'娃'},
    {"I_WEAPON", u'∈'}, {"I_ARMOR1", u'∋'}, {"I_ARMOR2", u'∨'}, {"I_CHIP", u'∧'},
    {"I_DATA", u'¶'},   {"I_FIRE", u'⊂'},  {"I_ICE", u'⊃'},    {"I_THUNDER", u'∪'},
    {"I_DARK", u'∩'},   {"I_EQUIPPED", u'⊆'},
};
constexpr char16_t kSlashCell = u'あ';
constexpr char16_t kSemicolonCell = u'い';

const Token* token_of(char16_t c) {
    for (const auto& token : kGlyphTokens)
        if (token.code == c) return &token;
    return nullptr;
}
const Token* token_named(std::string_view name) {
    for (const auto& token : kGlyphTokens)
        if (token.name == name) return &token;
    return nullptr;
}

void append_utf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(char(c));
    } else if (c < 0x800) {
        out.push_back(char(0xC0 | (c >> 6)));
        out.push_back(char(0x80 | (c & 0x3F)));
    } else {
        out.push_back(char(0xE0 | (c >> 12)));
        out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(char(0x80 | (c & 0x3F)));
    }
}

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

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}

// The letters and digits the prompt layout's START matcher treats as part of
// a word (src/game/prompt_layout.cpp prompt_glyphs: ASCII and full width).
bool word_char(char16_t c) {
    return (c >= u'0' && c <= u'9') || (c >= u'A' && c <= u'Z') || (c >= u'a' && c <= u'z') || (c >= 0xFF10 && c <= 0xFF19) ||
           (c >= 0xFF21 && c <= 0xFF3A) || (c >= 0xFF41 && c <= 0xFF5A);
}

} // namespace

std::string utf16_to_utf8(std::u16string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char16_t c : text) append_utf8(out, c);
    return out;
}

std::optional<std::u16string> utf8_to_utf16(std::string_view text) {
    std::u16string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto b0 = static_cast<unsigned char>(text[i]);
        char32_t c = 0;
        std::size_t length = 0;
        if (b0 < 0x80) {
            c = b0;
            length = 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            c = b0 & 0x1F;
            length = 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            c = b0 & 0x0F;
            length = 3;
        } else {
            return std::nullopt; // 4-byte forms lie above U+FFFF, the rest is invalid
        }
        if (i + length > text.size()) return std::nullopt;
        for (std::size_t k = 1; k < length; ++k) {
            const auto b = static_cast<unsigned char>(text[i + k]);
            if ((b & 0xC0) != 0x80) return std::nullopt;
            c = (c << 6) | (b & 0x3F);
        }
        if ((length == 2 && c < 0x80) || (length == 3 && c < 0x800)) return std::nullopt; // overlong
        if (c >= 0xD800 && c <= 0xDFFF) return std::nullopt;                              // surrogate
        out.push_back(char16_t(c));
        i += length;
    }
    return out;
}

std::string encode_tokens(std::u16string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char16_t c = text[i];
        if (c == u'\r' && i + 1 < text.size() && text[i + 1] == u'\n') {
            out += "{BR}";
            ++i;
        } else if (c == u'\r') {
            out += "{CR}";
        } else if (c == u'\n') {
            out += "{LF}";
        } else if (c == u'\t') {
            out += "{TAB}";
        } else if (c == kSlashCell) {
            out.push_back('/');
        } else if (c == kSemicolonCell) {
            out.push_back(';');
        } else if (const Token* token = token_of(c)) {
            out.push_back('{');
            out += token->name;
            out.push_back('}');
        } else {
            append_utf8(out, c);
        }
    }
    return out;
}

std::optional<std::u16string> decode_tokens(std::string_view text, std::string* error) {
    auto fail = [&](std::string message) -> std::optional<std::u16string> {
        if (error) *error = std::move(message);
        return std::nullopt;
    };
    const auto wide = utf8_to_utf16(text);
    if (!wide) return fail("invalid UTF-8");
    std::u16string out;
    out.reserve(wide->size());
    for (std::size_t i = 0; i < wide->size(); ++i) {
        const char16_t c = (*wide)[i];
        if (c == u'{') {
            const auto close = wide->find(u'}', i + 1);
            if (close == std::u16string::npos || close - i > 16) return fail("a '{' without its token");
            std::string name;
            for (std::size_t k = i + 1; k < close; ++k) {
                const char16_t n = (*wide)[k];
                if (!((n >= u'A' && n <= u'Z') || (n >= u'0' && n <= u'9') || n == u'_')) return fail("a '{' without its token");
                name.push_back(char(n));
            }
            if (name == "BR") out += u"\r\n";
            else if (name == "CR") out.push_back(u'\r');
            else if (name == "LF") out.push_back(u'\n');
            else if (name == "TAB") out.push_back(u'\t');
            else if (const Token* token = token_named(name)) out.push_back(token->code);
            else return fail("unknown token {" + name + "}");
            i = close;
        } else if (c == u'}') {
            return fail("a '}' without its token");
        } else if (c < 0x20 || c == 0x7F) {
            return fail("a control character (line breaks are {BR})");
        } else if (c == u'/') {
            out.push_back(kSlashCell);
        } else if (c == u';') {
            out.push_back(kSemicolonCell);
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::u16string glyph_codes(std::u16string_view text) {
    std::u16string codes;
    for (const char16_t c : text)
        if (token_of(c)) codes.push_back(c);
    return codes;
}

int start_words(std::u16string_view text) {
    int count = 0;
    for (const std::u16string_view word : {std::u16string_view(u"START"), std::u16string_view(u"ＳＴＡＲＴ")}) {
        for (std::size_t at = text.find(word); at != std::u16string_view::npos; at = text.find(word, at + word.size())) {
            const bool left = at == 0 || !word_char(text[at - 1]);
            const bool right = at + word.size() >= text.size() || !word_char(text[at + word.size()]);
            if (left && right) ++count;
        }
    }
    return count;
}

bool is_equip_marker(char16_t c) { return c == u'⊆' || c == u'赫'; }

IdKind id_kind(std::string_view id) {
    if (id.empty() || id == "*") return IdKind::content;
    auto name_char = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    if (id.starts_with("db:")) {
        const auto parts = split(id.substr(3), ':');
        if (parts.size() != 3 || parts[0].empty() || parts[1].empty() || parts[2].empty()) return IdKind::invalid;
        if (!std::all_of(parts[0].begin(), parts[0].end(), name_char)) return IdKind::invalid;
        if (!std::all_of(parts[1].begin(), parts[1].end(), [](char c) { return c >= '0' && c <= '9'; })) return IdKind::invalid;
        if (!std::all_of(parts[2].begin(), parts[2].end(), name_char)) return IdKind::invalid;
        return IdKind::database;
    }
    const auto parts = split(id, '/');
    if (parts.size() != 2 || parts[0].empty() || parts[1].empty()) return IdKind::invalid;
    for (const auto part : parts)
        if (!std::all_of(part.begin(), part.end(), name_char)) return IdKind::invalid;
    return IdKind::pack;
}

GameTextCatalog::Parsed GameTextCatalog::parse(std::string_view tsv) {
    Parsed result;
    auto catalog = std::make_shared<GameTextCatalog>();
    if (tsv.starts_with("\xEF\xBB\xBF")) tsv.remove_prefix(3); // UTF-8 BOM
    std::size_t number = 0;
    bool first_row = true;
    for (std::string_view rest = tsv; !rest.empty();) {
        const auto end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        ++number;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (trim(line).empty()) continue;
        if (line.front() == '#') {
            const auto body = trim(line.substr(1));
            if (body.starts_with("revision=")) result.revision = std::string(trim(body.substr(9)));
            continue;
        }
        const auto columns = split(line, '\t');
        if (first_row) {
            first_row = false;
            if (trim(columns[0]) == "id") continue; // header
        }
        ++result.rows;
        auto reject = [&](std::string message) {
            result.issues.push_back({number, std::move(message), true});
            ++result.rejected;
        };
        if (columns.size() < 3) {
            reject("fewer than three columns (id, English, translation)");
            continue;
        }
        TextEntry entry;
        entry.line = number;
        const auto id = trim(columns[0]);
        const IdKind kind = id_kind(id);
        if (kind == IdKind::invalid) {
            reject("an id that is neither empty, <stage>/<name> nor db:<table>:<row>:<column>");
            continue;
        }
        entry.id = kind == IdKind::content ? std::string() : std::string(id);
        std::string error;
        const auto english = decode_tokens(columns[1], &error);
        if (!english) {
            reject("English: " + error);
            continue;
        }
        if (english->empty()) {
            reject("an empty English text");
            continue;
        }
        if (trim(columns[2]).empty()) {
            // Not translated yet: the English text stays (no issue).
            ++result.skipped;
            continue;
        }
        const auto text = decode_tokens(columns[2], &error);
        if (!text) {
            reject("translation: " + error);
            continue;
        }
        if (glyph_codes(*english) != glyph_codes(*text)) {
            reject("the button / icon tokens differ from the English text's (same ones, same order)");
            continue;
        }
        if (start_words(*english) != start_words(*text)) {
            reject("the word START (the prompt picture) is not kept");
            continue;
        }
        bool fuzzy = false, bad_flag = false;
        if (columns.size() > 3) {
            for (const auto flag_text : split(columns[3], ',')) {
                const auto flag = trim(flag_text);
                if (flag.empty()) continue;
                if (flag == "fuzzy") {
                    fuzzy = true;
                } else if (flag.starts_with("squeeze=")) {
                    const std::string number_text(flag.substr(8));
                    char* end_ptr = nullptr;
                    const float value = std::strtof(number_text.c_str(), &end_ptr);
                    if (!end_ptr || *end_ptr || number_text.empty() || !(value >= kMinSqueeze - 1e-6f && value <= 1.0f)) {
                        reject("squeeze must lie between 0.90 and 1.00");
                        bad_flag = true;
                        break;
                    }
                    entry.squeeze = std::clamp(value, kMinSqueeze, 1.0f);
                } else {
                    result.issues.push_back({number, "unknown flag '" + std::string(flag) + "' (ignored)", false});
                }
            }
        }
        if (bad_flag) continue;
        if (fuzzy) {
            result.issues.push_back({number, "fuzzy: the English text stays", false});
            ++result.skipped;
            continue;
        }
        entry.english = std::move(*english);
        entry.text = std::move(*text);
        if (kind == IdKind::pack) {
            if (catalog->by_id_.contains(entry.id)) {
                reject("a second entry for " + entry.id);
                continue;
            }
            catalog->by_id_.emplace(entry.id, catalog->entries_.size());
        } else {
            if (const auto found = catalog->by_text_.find(entry.english); found != catalog->by_text_.end()) {
                const auto& first = catalog->entries_[found->second];
                if (first.text != entry.text || first.squeeze != entry.squeeze)
                    reject("the same English text as line " + std::to_string(first.line) +
                           " with another translation (the engine cannot tell them apart; use a <stage>/<name> id)");
                else
                    ++result.skipped; // a duplicate that agrees: nothing to add
                continue;
            }
            catalog->by_text_.emplace(entry.english, catalog->entries_.size());
        }
        catalog->entries_.push_back(std::move(entry));
        ++result.accepted;
    }
    result.catalog = std::move(catalog);
    return result;
}

const TextEntry* GameTextCatalog::find(std::u16string_view english) const {
    const auto found = by_text_.find(std::u16string(english));
    return found == by_text_.end() ? nullptr : &entries_[found->second];
}

const TextEntry* GameTextCatalog::find_id(std::string_view id) const {
    const auto found = by_id_.find(std::string(id));
    return found == by_id_.end() ? nullptr : &entries_[found->second];
}

std::optional<GameTextCatalog::Resolved> GameTextCatalog::resolve(std::u16string_view english, std::string_view id) const {
    if (english.empty()) return std::nullopt;
    if (!id.empty())
        if (const auto* entry = find_id(id); entry && entry->english == english) return Resolved{entry->text, entry->squeeze, true};
    if (const auto* entry = find(english)) return Resolved{entry->text, entry->squeeze, false};
    // subwnd_shop.cpp: an equip marker before a database NAME.
    if (english.size() > 1 && is_equip_marker(english.front()))
        if (auto core = resolve(english.substr(1))) {
            core->text.insert(core->text.begin(), english.front());
            core->by_id = false;
            return core;
        }
    // subwnd_shop.cpp / subwnd.cpp: the rank digit (one) after a database TXT.
    auto digit = [](char16_t c) { return c >= u'0' && c <= u'9'; };
    if (english.size() > 1 && digit(english.back()) && !digit(english[english.size() - 2]))
        if (const auto* entry = find(english.substr(0, english.size() - 1))) {
            Resolved core{entry->text, entry->squeeze, false};
            core.text.push_back(english.back());
            return core;
        }
    return std::nullopt;
}

// --------------------------------------------------------------------------
// SHA-256 (FIPS 180-4)
std::string sha256_hex(std::string_view data) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
        0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
        0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
        0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
        0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
        0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    std::string message(data);
    const std::uint64_t bits = std::uint64_t(data.size()) * 8;
    message.push_back(static_cast<char>(0x80 - 256)); // the padding's first byte, 0x80
    while (message.size() % 64 != 56) message.push_back('\0');
    for (int shift = 56; shift >= 0; shift -= 8) message.push_back(char((bits >> shift) & 0xFF));
    for (std::size_t block = 0; block < message.size(); block += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(static_cast<unsigned char>(message[block + i * 4])) << 24) |
                   (std::uint32_t(static_cast<unsigned char>(message[block + i * 4 + 1])) << 16) |
                   (std::uint32_t(static_cast<unsigned char>(message[block + i * 4 + 2])) << 8) |
                   std::uint32_t(static_cast<unsigned char>(message[block + i * 4 + 3]));
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = hh + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            hh = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += hh;
    }
    std::string hex;
    char digits[9];
    for (const auto word : h) {
        std::snprintf(digits, sizeof digits, "%08x", unsigned(word));
        hex += digits;
    }
    return hex;
}

} // namespace dw4::i18n
