#include "token_store.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dpapi.h>
#endif

#include <cstring>
#include <fstream>
#include <iterator>

namespace dw4::game::online {

namespace {
constexpr char kMagic[8] = {'D', 'W', '4', 'T', 'O', 'K', 'E', 'N'};
std::string entropy_of(const std::string& server, const std::string& account) { return "dw4online|" + server + "|" + account; }
} // namespace

std::filesystem::path token_file(const std::filesystem::path& folder, const std::string& account) {
    std::string name;
    for (const char c : account) name += (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ? c : '_';
    if (name.empty()) name = "_";
    return folder / (name + ".token");
}

#ifdef _WIN32
std::optional<std::vector<unsigned char>> protect(const std::string& secret, const std::string& entropy) {
    DATA_BLOB in{DWORD(secret.size()), reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()))};
    DATA_BLOB extra{DWORD(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"DW4_PC online token", &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::vector<unsigned char> blob(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return blob;
}

std::optional<std::string> unprotect(const std::vector<unsigned char>& blob, const std::string& entropy) {
    DATA_BLOB in{DWORD(blob.size()), const_cast<BYTE*>(blob.data())};
    DATA_BLOB extra{DWORD(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.data()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::string secret(reinterpret_cast<const char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return secret;
}
#else
std::optional<std::vector<unsigned char>> protect(const std::string&, const std::string&) { return std::nullopt; }
std::optional<std::string> unprotect(const std::vector<unsigned char>&, const std::string&) { return std::nullopt; }
#endif

bool save_token(const std::filesystem::path& folder, const std::string& server, const std::string& account, const std::string& token) {
    const auto blob = protect(token, entropy_of(server, account));
    if (!blob) return false;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const auto path = token_file(folder, account);
    const auto temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        const std::uint32_t version = 1, length = std::uint32_t(blob->size());
        out.write(kMagic, 8);
        out.write(reinterpret_cast<const char*>(&version), 4);
        out.write(reinterpret_cast<const char*>(&length), 4);
        out.write(reinterpret_cast<const char*>(blob->data()), std::streamsize(blob->size()));
        if (!out) return false;
    }
    std::filesystem::rename(temp, path, error);
    return !error;
}

std::optional<std::string> load_token(const std::filesystem::path& folder, const std::string& server, const std::string& account) {
    std::ifstream in(token_file(folder, account), std::ios::binary);
    if (!in) return std::nullopt;
    const std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (bytes.size() < 16 || bytes.size() > 65536 || std::memcmp(bytes.data(), kMagic, 8) != 0) return std::nullopt;
    std::uint32_t version = 0, length = 0;
    std::memcpy(&version, bytes.data() + 8, 4);
    std::memcpy(&length, bytes.data() + 12, 4);
    if (version != 1 || length != bytes.size() - 16) return std::nullopt;
    return unprotect(std::vector<unsigned char>(bytes.begin() + 16, bytes.end()), entropy_of(server, account));
}

void forget_token(const std::filesystem::path& folder, const std::string& account) {
    std::error_code error;
    std::filesystem::remove(token_file(folder, account), error);
}

} // namespace dw4::game::online
