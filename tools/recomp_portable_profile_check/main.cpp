// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 DW4_PC contributors
#include "portable_profile.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using moderngekko::frontend::MigrateLegacyRuntimeConfig;

namespace {
unsigned checks = 0, failures = 0, skipped = 0;
void Check(bool passed, const char* description) {
  ++checks;
  if (!passed) { ++failures; std::cerr << "FAIL: " << description << '\n'; }
}
void Write(const fs::path& path, const std::string& contents) {
  if (fs::exists(path)) throw std::runtime_error("Refusing to overwrite an existing fixture.");
  std::ofstream stream(path, std::ios::binary);
  stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  stream.close();
  if (!stream) throw std::runtime_error("Cannot write fixture.");
}
std::string Read(const fs::path& path) {
  if (fs::file_size(path) > 4u * 1024u * 1024u + 1u)
    throw std::runtime_error("Fixture unexpectedly exceeds its allocation bound.");
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
bool NoTemporary(const fs::path& directory) {
  for (const auto& entry : fs::directory_iterator(directory)) {
    if (entry.path().filename().wstring().starts_with(L".Runtime.ini.migrate-")) return false;
  }
  return true;
}
fs::path MakeCase(const fs::path& root, const wchar_t* name) {
  const fs::path result = root / name;
  if (!fs::create_directory(result)) throw std::runtime_error("Fixture directory already exists.");
  fs::create_directory(result / "Config");
  return result;
}
void ExpectFailure(const fs::path& root, const char* description) {
  std::string error;
  Check(!MigrateLegacyRuntimeConfig(root, &error), description);
  Check(!error.empty(), "Failure returns an actionable error.");
  Check(!fs::exists(root / "Config" / "Runtime.ini"), "Failure did not create a partial Runtime.ini.");
  Check(NoTemporary(root / "Config"), "Failure removed its own temporary file.");
}

void Cases(const fs::path& root) {
  std::string error = "stale error";
  const fs::path missing = root / "not-created";
  Check(MigrateLegacyRuntimeConfig(missing, &error), "Missing profile is a successful no-op.");
  Check(error.empty() && !fs::exists(missing), "Missing profile is not created and old errors are cleared.");
  const fs::path empty = MakeCase(root, L"empty");
  Check(MigrateLegacyRuntimeConfig(empty, nullptr), "Missing legacy is a successful no-op with null error.");
  Check(fs::is_empty(empty / "Config"), "Missing legacy leaves Config empty.");
  Check(!MigrateLegacyRuntimeConfig({}, &error) && !error.empty(), "Empty user directory is rejected.");

  const fs::path existing = MakeCase(root, L"existing");
  const std::string current("current\0configuration\r\n", 23);
  Write(existing / "Config/Runtime.ini", current);
  fs::create_directory(existing / "Config/Dolphin.ini");
  fs::create_directory(existing / "Saves");
  Write(existing / "Saves/card.raw", "SAVE SENTINEL");
  const auto current_time = fs::last_write_time(existing / "Config/Runtime.ini");
  Check(MigrateLegacyRuntimeConfig(existing, &error), "Existing Runtime.ini takes precedence over malformed legacy path.");
  Check(Read(existing / "Config/Runtime.ini") == current, "Existing current bytes are unchanged.");
  Check(fs::last_write_time(existing / "Config/Runtime.ini") == current_time, "Existing current modification time is unchanged.");
  Check(fs::is_directory(existing / "Config/Dolphin.ini") && Read(existing / "Saves/card.raw") == "SAVE SENTINEL",
        "Existing current configuration preserves legacy and saves.");

  const fs::path valid = MakeCase(root, L"valid");
  const std::string ini = "[Core]\r\nCPUThread = True\r\n[Display]\r\nFullscreen = False\r\n";
  Write(valid / "Config/Dolphin.ini", ini);
  fs::create_directory(valid / "GC");
  Write(valid / "GC/MemoryCardA.USA.raw", "MEMORY CARD SENTINEL");
  const auto legacy_time = fs::last_write_time(valid / "Config/Dolphin.ini");
  Check(MigrateLegacyRuntimeConfig(valid, &error), "Valid legacy configuration migrates.");
  if (!error.empty()) std::cerr << "Migration diagnostic: " << error << '\n';
  Check(error.empty() && Read(valid / "Config/Runtime.ini") == ini, "Migration preserves exact bytes.");
  Check(Read(valid / "Config/Dolphin.ini") == ini && fs::last_write_time(valid / "Config/Dolphin.ini") == legacy_time,
        "Migration preserves legacy bytes and modification time.");
  Check(Read(valid / "GC/MemoryCardA.USA.raw") == "MEMORY CARD SENTINEL", "Migration preserves memory-card data.");
  Check(NoTemporary(valid / "Config"), "Successful migration leaves no temporary files.");
  Check(MigrateLegacyRuntimeConfig(valid, &error), "Repeated migration is a successful no-op.");

  const fs::path malformed = MakeCase(root, L"malformed-text");
  const std::string malformed_bytes("not[INI\0\xff\r\n", 11);
  Write(malformed / "Config/Dolphin.ini", malformed_bytes);
  Check(MigrateLegacyRuntimeConfig(malformed, &error), "Malformed INI bytes are copied without destructive parsing.");
  Check(Read(malformed / "Config/Dolphin.ini") == malformed_bytes && Read(malformed / "Config/Runtime.ini") == malformed_bytes,
        "Malformed legacy remains intact for the normal loader to diagnose.");
  const fs::path zero = MakeCase(root, L"zero-byte");
  Write(zero / "Config/Dolphin.ini", "");
  Check(MigrateLegacyRuntimeConfig(zero, &error) && fs::file_size(zero / "Config/Runtime.ini") == 0,
        "Empty legacy produces a complete zero-byte current file.");

  const fs::path unicode = MakeCase(root, L"profile-\u65e5\u672c\u8a9e-\u00e4\u00f6\u00fc");
  Write(unicode / "Config/Dolphin.ini", ini);
  Check(MigrateLegacyRuntimeConfig(unicode, &error) && Read(unicode / "Config/Runtime.ini") == ini,
        "Unicode user-directory paths migrate correctly.");

  const fs::path maximum = MakeCase(root, L"maximum-size");
  const std::string maximum_bytes(4u * 1024u * 1024u, 'M');
  Write(maximum / "Config/Dolphin.ini", maximum_bytes);
  Check(MigrateLegacyRuntimeConfig(maximum, &error) && Read(maximum / "Config/Runtime.ini") == maximum_bytes,
        "Exactly 4 MiB is accepted without truncation.");
  const fs::path oversized = MakeCase(root, L"oversized");
  Write(oversized / "Config/Dolphin.ini", std::string(maximum_bytes.size() + 1, 'O'));
  ExpectFailure(oversized, "Legacy larger than 4 MiB is rejected.");
  Check(fs::file_size(oversized / "Config/Dolphin.ini") == maximum_bytes.size() + 1, "Oversized legacy is unchanged.");

  const fs::path source_directory = MakeCase(root, L"source-directory");
  fs::create_directory(source_directory / "Config/Dolphin.ini");
  ExpectFailure(source_directory, "Legacy directory is rejected.");
  const fs::path destination_directory = MakeCase(root, L"destination-directory");
  Write(destination_directory / "Config/Dolphin.ini", ini);
  fs::create_directory(destination_directory / "Config/Runtime.ini");
  Check(!MigrateLegacyRuntimeConfig(destination_directory, &error) && !error.empty(), "Destination directory is rejected.");
  Check(fs::is_directory(destination_directory / "Config/Runtime.ini") && Read(destination_directory / "Config/Dolphin.ini") == ini,
        "Invalid destination and legacy are preserved.");

  const fs::path bad_config = root / "config-file";
  fs::create_directory(bad_config);
  Write(bad_config / "Config", "not a directory");
  Check(!MigrateLegacyRuntimeConfig(bad_config, &error) && !error.empty(), "Config as a file is rejected.");
  Check(Read(bad_config / "Config") == "not a directory", "Invalid Config entry is preserved.");

  const fs::path busy = MakeCase(root, L"busy-source");
  Write(busy / "Config/Dolphin.ini", ini);
  const HANDLE held = CreateFileW((busy / "Config/Dolphin.ini").c_str(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (held == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open source sharing fixture.");
  ExpectFailure(busy, "An actively writable legacy file is rejected instead of copying an unstable source.");
  CloseHandle(held);
  Check(MigrateLegacyRuntimeConfig(busy, &error), "Migration succeeds after the writer closes.");

  const fs::path stale = MakeCase(root, L"stale-temporary");
  Write(stale / "Config/Dolphin.ini", ini);
  Write(stale / "Config/.Runtime.ini.migrate-stale.tmp", "STALE USER CONTENT");
  Check(MigrateLegacyRuntimeConfig(stale, &error), "A stale temporary name does not block migration.");
  Check(Read(stale / "Config/.Runtime.ini.migrate-stale.tmp") == "STALE USER CONTENT", "Unowned stale files are never removed.");

  const fs::path linked = MakeCase(root, L"source-symlink");
  const fs::path outside = root / "outside-target.ini";
  Write(outside, "OUTSIDE TARGET");
  if (CreateSymbolicLinkW((linked / "Config/Dolphin.ini").c_str(), outside.c_str(),
                         SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
    ExpectFailure(linked, "Legacy symbolic link is rejected without following its target.");
    Check(Read(outside) == "OUTSIDE TARGET", "Symlink target is unchanged.");
    const fs::path destination_link = MakeCase(root, L"destination-symlink");
    Write(destination_link / "Config/Dolphin.ini", ini);
    if (!CreateSymbolicLinkW((destination_link / "Config/Runtime.ini").c_str(), outside.c_str(),
                            SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE))
      throw std::runtime_error("Cannot create destination symlink fixture.");
    Check(!MigrateLegacyRuntimeConfig(destination_link, &error) && !error.empty(), "Destination symlink is rejected.");
    Check(fs::is_symlink(fs::symlink_status(destination_link / "Config/Runtime.ini")) && Read(outside) == "OUTSIDE TARGET",
          "Destination symlink and target are preserved.");
  } else {
    ++skipped;
    std::cout << "SKIP: symbolic-link fixtures require Windows Developer Mode or symlink privilege.\n";
  }

  unsigned external_winners = 0;
  for (unsigned trial = 0; trial < 16 && external_winners == 0; ++trial) {
    const fs::path race = MakeCase(root, (L"race-" + std::to_wstring(trial)).c_str());
    Write(race / "Config/Dolphin.ini", maximum_bytes);
    std::atomic<bool> finished{false}, winner{false};
    std::thread writer([&] {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      while (!finished.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline) {
        if (!NoTemporary(race / "Config")) {
          const HANDLE file = CreateFileW((race / "Config/Runtime.ini").c_str(), GENERIC_WRITE,
              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
          if (file != INVALID_HANDLE_VALUE) {
            constexpr char sentinel[] = "RACING DESTINATION WINS";
            DWORD written = 0;
            const bool wrote = WriteFile(file, sentinel, sizeof(sentinel) - 1, &written, nullptr) && written == sizeof(sentinel) - 1;
            CloseHandle(file);
            winner.store(wrote, std::memory_order_release);
          }
          return;
        }
        std::this_thread::yield();
      }
    });
    const bool migrated = MigrateLegacyRuntimeConfig(race, &error);
    finished.store(true, std::memory_order_release);
    writer.join();
    Check(migrated, "Concurrent destination creation completes without destructive replacement.");
    if (winner.load(std::memory_order_acquire)) {
      ++external_winners;
      Check(Read(race / "Config/Runtime.ini") == "RACING DESTINATION WINS", "A destination created during the copy wins byte-for-byte.");
    } else Check(Read(race / "Config/Runtime.ini") == maximum_bytes, "Migration winning a race publishes complete bytes.");
    Check(NoTemporary(race / "Config"), "Destination race cleans the losing migration temporary file.");
    Check(Read(race / "Config/Dolphin.ini") == maximum_bytes, "Destination race preserves legacy source.");
  }
  Check(external_winners != 0, "At least one actual destination race exercised no-replace publication.");
}
}

int main(int argc, char** argv) {
  if (argc != 3) { std::cerr << "Usage: recomp-portable-profile-check.exe fixtures-directory report.json\n"; return 2; }
  try {
    const fs::path root = fs::absolute(fs::path(argv[1]));
    if (!fs::create_directory(root)) throw std::runtime_error("Refusing to reuse an existing fixture root.");
    Cases(root);
    std::ofstream report(argv[2], std::ios::binary);
    report << "{\n  \"passed\": " << (failures == 0 ? "true" : "false")
           << ",\n  \"checks\": " << checks << ",\n  \"failures\": " << failures
           << ",\n  \"skipped\": " << skipped << ",\n  \"game_executed\": false\n}\n";
    report.close();
    if (!report) throw std::runtime_error("Cannot write evidence report.");
    std::cout << "Portable profile migration: " << checks << " checks, " << failures << " failures, " << skipped << " skipped.\n";
    return failures == 0 ? 0 : 1;
  } catch (const std::exception& exception) {
    std::cerr << "Checker failed: " << exception.what() << '\n';
    return 2;
  }
}
