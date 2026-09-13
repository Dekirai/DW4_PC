#include "module_source_identity.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;
namespace Source = moderngekko::port;

namespace
{
unsigned checks = 0;
unsigned skipped = 0;

void Check(bool condition, const std::string& message)
{
  ++checks;
  if (!condition)
    throw std::runtime_error(message);
}

template <typename Function>
void CheckRejected(Function&& function, const std::string& message)
{
  bool rejected = false;
  try
  {
    function();
  }
  catch (const std::runtime_error&)
  {
    rejected = true;
  }
  Check(rejected, message);
}

void WriteFile(const fs::path& path, std::string_view content)
{
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file.write(content.data(), static_cast<std::streamsize>(content.size()));
  file.close();
  if (!file)
    throw std::runtime_error("Could not write synthetic fixture: " + path.string());
}

std::string Content(std::string_view relative)
{
  return "fixture file: " + std::string(relative) + "\n";
}

void Populate(const fs::path& root, bool reverse)
{
  std::vector<std::string> paths;
  for (const auto& relative : Source::REQUIRED_MODULE_SOURCE_FILES)
    paths.emplace_back(relative);
  if (reverse)
    std::reverse(paths.begin(), paths.end());
  for (const auto& relative : paths)
    WriteFile(root / relative, Content(relative));
}

bool SameIdentity(const Source::ModuleSourceIdentity& lhs,
                  const Source::ModuleSourceIdentity& rhs)
{
  return lhs.sha256 == rhs.sha256 && lhs.files == rhs.files &&
         lhs.Manifest() == rhs.Manifest();
}

bool IsSha256(std::string_view value)
{
  return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}

void CheckAddedSource(const fs::path& root, const Source::ModuleSourceIdentity& baseline,
                      const std::string& relative)
{
  const auto file = root / fs::path(std::u8string(relative.begin(), relative.end()));
  WriteFile(file, "synthetic additional source\n");
  const auto added = Source::CollectModuleSourceIdentity(root);
  Check(added.sha256 != baseline.sha256, "New source was not hashed: " + relative);
  Check(added.files.size() == baseline.files.size() + 1,
        "New source was not listed: " + relative);
  Check(std::any_of(added.files.begin(), added.files.end(), [&](const auto& entry) {
          return entry.first == relative;
        }), "New source path is not UTF-8: " + relative);
  WriteFile(file, "synthetic additional source changed\n");
  const auto changed = Source::CollectModuleSourceIdentity(root);
  Check(changed.sha256 != added.sha256, "Additional source edit was ignored: " + relative);
  fs::remove(file);
  Check(SameIdentity(Source::CollectModuleSourceIdentity(root), baseline),
        "Removing an optional source did not restore the identity: " + relative);
}

void CheckRecords(const fs::path& fixture_parent,
                  const Source::ModuleSourceIdentity& baseline)
{
  const auto record = fixture_parent / "source.record";
  const auto manifest = baseline.Manifest();
  const std::string cache_key = "module_cache_key=synthetic-key\n";
  const auto marker = cache_key + manifest;
  const auto source_receipt = manifest + "module_sha256=" + std::string(64, 'a') + "\n";
  const auto receipt = cache_key + source_receipt;
  Check(!Source::ModuleSourceRecordMatches(record, marker), "Missing record matched");
  WriteFile(record, marker);
  Check(Source::ModuleSourceRecordMatches(record, marker), "Current source marker did not match");
  Check(!Source::ModuleSourceRecordMatches(record, receipt), "Marker matched a completed receipt");
  WriteFile(record, receipt);
  Check(Source::ModuleSourceRecordMatches(record, receipt), "Current completed receipt did not match");

  auto stale = baseline;
  stale.sha256 = std::string(64, 'b');
  WriteFile(record, cache_key + stale.Manifest());
  Check(!Source::ModuleSourceRecordMatches(record, marker), "Stale source marker matched");
  WriteFile(record, "module_cache_key=stale-key\n" + manifest);
  Check(!Source::ModuleSourceRecordMatches(record, marker), "Stale cache key marker matched");
  WriteFile(record, marker + "module_sha256=" + std::string(64, 'b') + "\n");
  Check(!Source::ModuleSourceRecordMatches(record, receipt), "Altered binary digest matched");

  const std::string metadata = "configuration=Release\ncompiler=synthetic\n";
  WriteFile(record, metadata + source_receipt);
  Check(Source::ModuleSourceRecordMatches(record, source_receipt, true),
        "Manifest with preceding metadata did not match its receipt suffix");
  Check(!Source::ModuleSourceRecordMatches(record, source_receipt),
        "Exact record match accepted preceding metadata");
  WriteFile(record, metadata + source_receipt + "junk\n");
  Check(!Source::ModuleSourceRecordMatches(record, source_receipt, true),
        "Receipt suffix match accepted trailing junk");
  WriteFile(record, receipt + "junk\n");
  Check(!Source::ModuleSourceRecordMatches(record, receipt),
        "Exact record match accepted trailing junk");
  WriteFile(record, receipt.substr(0, receipt.size() - 1));
  Check(!Source::ModuleSourceRecordMatches(record, receipt), "Truncated receipt matched");
  WriteFile(record, "");
  Check(!Source::ModuleSourceRecordMatches(record, receipt), "Empty receipt matched");
  fs::resize_file(record, 4 * 1024 * 1024 + 1);
  Check(!Source::ModuleSourceRecordMatches(record, receipt, true), "Oversized receipt matched");
  fs::remove(record);
  fs::create_directory(record);
  Check(!Source::ModuleSourceRecordMatches(record, receipt), "Directory matched a receipt");
  fs::remove(record);

  const auto actual_record = fixture_parent / "actual.record";
  WriteFile(actual_record, receipt);
  std::error_code error;
  fs::create_symlink(actual_record, record, error);
  if (error)
  {
    ++skipped;
    std::cout << "SKIP: record symlink fixture unavailable; OS error " << error.value() << '\n';
  }
  else
  {
    Check(!Source::ModuleSourceRecordMatches(record, receipt), "Symlink matched a receipt");
    fs::remove(record);
  }
}

void CheckSymlinks(const fs::path& root, const fs::path& fixture_parent,
                   const Source::ModuleSourceIdentity& baseline)
{
  const auto external = fixture_parent / "external.c";
  WriteFile(external, "synthetic external source\n");
  const auto file_link = root / "vendor/dolphin/GXRuntime/src/core/symlink_source.c";
  std::error_code error;
  fs::create_symlink(external, file_link, error);
  if (error)
  {
    ++skipped;
    std::cout << "SKIP: file symlink fixture unavailable; OS error " << error.value() << '\n';
  }
  else
  {
    CheckRejected([&] { Source::CollectModuleSourceIdentity(root); },
                  "Source file symlink was accepted");
    fs::remove(file_link);
    fs::create_symlink(fixture_parent / "absent.c", file_link);
    CheckRejected([&] { Source::CollectModuleSourceIdentity(root); },
                  "Dangling source file symlink was accepted");
    fs::remove(file_link);
  }

  const auto external_directory = fixture_parent / "external-directory";
  WriteFile(external_directory / "source.c", "synthetic external directory source\n");
  const auto directory_link = root / "vendor/dolphin/GXRuntime/src/core/symlink_directory";
  fs::create_directory_symlink(external_directory, directory_link, error);
  if (error)
  {
    ++skipped;
    std::cout << "SKIP: directory symlink fixture unavailable; OS error " << error.value() << '\n';
  }
  else
  {
    CheckRejected([&] { Source::CollectModuleSourceIdentity(root); },
                  "Source directory symlink was accepted");
    fs::remove(directory_link);
  }
  Check(SameIdentity(Source::CollectModuleSourceIdentity(root), baseline),
        "Symlink fixtures changed regular source identity");
}

void RunChecks(const fs::path& fixture_parent)
{
  const auto root = fixture_parent / "first";
  const auto relocated = fixture_parent / "relocated with spaces";
  Populate(root, false);
  Populate(relocated, true);
  const auto baseline = Source::CollectModuleSourceIdentity(root);
  Check(IsSha256(baseline.sha256), "Aggregate identity is not lowercase SHA-256");
  Check(baseline.files.size() == Source::REQUIRED_MODULE_SOURCE_FILES.size(),
        "Required-only fixture has the wrong file count");
  Check(std::is_sorted(baseline.files.begin(), baseline.files.end()),
        "Source manifest is not sorted");
  Check(std::adjacent_find(baseline.files.begin(), baseline.files.end(),
                          [](const auto& lhs, const auto& rhs) {
                            return lhs.first == rhs.first;
                          }) == baseline.files.end(),
        "Source manifest contains duplicate paths");
  Check(!baseline.Manifest().empty(), "Source manifest is empty");
  Check(baseline.Manifest().find(root.generic_string()) == std::string::npos,
        "Source manifest contains an absolute fixture path");
  Check(SameIdentity(Source::CollectModuleSourceIdentity(root), baseline),
        "Unchanged sources produced a different identity");
  Check(SameIdentity(Source::CollectModuleSourceIdentity(relocated), baseline),
        "Root relocation or creation order changed the identity");

  for (const auto& [relative, hash] : baseline.files)
  {
    Check(!fs::path(relative).is_absolute() && relative.find('\\') == std::string::npos,
          "Source entry is not a normalized relative path: " + relative);
    Check(IsSha256(hash), "Source entry has an invalid digest: " + relative);
    Check(baseline.Manifest().find(relative) != std::string::npos &&
              baseline.Manifest().find(hash) != std::string::npos,
          "Source manifest omits a path or digest: " + relative);
  }

  for (const auto& required : Source::REQUIRED_MODULE_SOURCE_FILES)
  {
    const std::string relative(required);
    const auto file = root / relative;
    auto changed_content = Content(relative);
    changed_content.front() = 'F';
    const auto timestamp = fs::last_write_time(file);
    WriteFile(file, changed_content);
    fs::last_write_time(file, timestamp);
    const auto changed = Source::CollectModuleSourceIdentity(root);
    Check(changed.sha256 != baseline.sha256,
          "Same-size, same-timestamp required source edit was ignored: " + relative);
    Check(changed.files.size() == baseline.files.size(),
          "Editing a required source changed the file count: " + relative);
    WriteFile(file, Content(relative));
    Check(SameIdentity(Source::CollectModuleSourceIdentity(root), baseline),
          "Restoring required source bytes did not restore identity: " + relative);
    fs::remove(file);
    CheckRejected([&] { Source::CollectModuleSourceIdentity(root); },
                  "Missing required source was accepted: " + relative);
    WriteFile(file, Content(relative));
  }

  for (const std::string relative : {
           "vendor/dolphin/GXRuntime/src/core/new_cpu.c",
           "vendor/dolphin/GXRuntime/include/cpu/new_types.h",
           "vendor/dolphin/module-template/cmake/new_options.cmake",
           "vendor/dolphin/GXRuntime/src/core/nonascii-\xc3\xbc.c",
           "vendor/dolphin/GXRuntime/src/core/\xe6\xba\x90/helper.c"})
    CheckAddedSource(root, baseline, relative);

  for (const std::string relative : {
           "README.md", "vendor/dolphin/GXRuntime/src/core/README.md",
           "vendor/dolphin/module-template/README.md",
           "vendor/dolphin/GXRuntime/build/generated.c",
           "vendor/dolphin/GXRuntime/src/core/build/generated.c",
           "vendor/dolphin/module-template/build/module_export.c",
           "vendor/dolphin/module-template/generated/generated.c",
           "vendor/dolphin/module-template/notes.\xe8\xaf\xb4\xe6\x98\x8e"})
  {
    const auto file = root / fs::path(std::u8string(relative.begin(), relative.end()));
    WriteFile(file, "ignored fixture\n");
    Check(SameIdentity(Source::CollectModuleSourceIdentity(root), baseline),
          "Documentation or build output changed identity: " + relative);
    WriteFile(file, "changed ignored fixture\n");
    Check(SameIdentity(Source::CollectModuleSourceIdentity(root), baseline),
          "Documentation or build output edit changed identity: " + relative);
  }

  CheckRejected([&] { Source::CollectModuleSourceIdentity(fixture_parent / "missing"); },
                "Missing source root was accepted");
  const auto first = root / std::string(Source::REQUIRED_MODULE_SOURCE_FILES.front());
  fs::remove(first);
  fs::create_directory(first);
  CheckRejected([&] { Source::CollectModuleSourceIdentity(root); },
                "Required source replaced by a directory was accepted");
  fs::remove(first);
  WriteFile(first, Content(std::string(Source::REQUIRED_MODULE_SOURCE_FILES.front())));

#ifdef _WIN32
  const HANDLE locked_file = CreateFileW(first.c_str(), GENERIC_READ, 0, nullptr,
                                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (locked_file == INVALID_HANDLE_VALUE)
    throw std::runtime_error("Could not exclusively lock synthetic source for read-failure check");
  bool rejected = false;
  try
  {
    Source::CollectModuleSourceIdentity(root);
  }
  catch (const std::runtime_error&)
  {
    rejected = true;
  }
  CloseHandle(locked_file);
  Check(rejected, "Unreadable required source was accepted");
#else
  ++skipped;
  std::cout << "SKIP: exclusive file-lock read-failure check requires Windows\n";
#endif

  CheckSymlinks(root, fixture_parent, baseline);
  CheckRecords(fixture_parent, baseline);
  Check(SameIdentity(Source::CollectModuleSourceIdentity(root), baseline),
        "Checks failed to restore the original source identity");
}
}  // namespace

int main(int argc, char** argv)
{
  if (argc > 2)
  {
    std::cerr << "Usage: recomp-module-source-check.exe [runtime-source-root]\n";
    return 2;
  }
  try
  {
    // Keep synthetic inputs next to this standalone checker in the ignored build directory.
    const auto output_root = fs::absolute(fs::path(argv[0])).parent_path();
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto fixtures = output_root / ("fixtures-" + std::to_string(token));
    if (!fs::create_directory(fixtures))
      throw std::runtime_error("Synthetic fixture directory already exists");
    RunChecks(fixtures);
    if (argc == 2)
    {
      const auto identity = Source::CollectModuleSourceIdentity(fs::path(argv[1]));
      std::cout << "SOURCE: " << identity.sha256 << "; " << identity.files.size()
                << " files\n";
    }
    std::cout << "PASS: " << checks << " module source checks; " << skipped
              << " skipped; no game, export or full build launched.\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
