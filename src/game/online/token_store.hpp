#pragma once

// The remembered login (docs/SEAMLESS.md 10): the server's session token,
// kept with DPAPI in user scope (only this Windows account can read it back)
// with the server and the account as extra entropy, in saves/online/ next to
// the program. A token of another server or account never decrypts.
//
// File: "DW4TOKEN" | u32 version 1 | u32 length | DPAPI blob.
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dw4::game::online {

// The file for one account (account names are ASCII lower case on the
// server; anything else is folded to '_').
std::filesystem::path token_file(const std::filesystem::path& folder, const std::string& account);

bool save_token(const std::filesystem::path& folder, const std::string& server, const std::string& account, const std::string& token);
std::optional<std::string> load_token(const std::filesystem::path& folder, const std::string& server, const std::string& account);
void forget_token(const std::filesystem::path& folder, const std::string& account);

// The protection itself (tests): DPAPI user scope with the entropy.
std::optional<std::vector<unsigned char>> protect(const std::string& secret, const std::string& entropy);
std::optional<std::string> unprotect(const std::vector<unsigned char>& blob, const std::string& entropy);

} // namespace dw4::game::online
