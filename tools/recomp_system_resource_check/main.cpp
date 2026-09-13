// Copyright 2026 DW4_PC contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Common/SystemResource.h"

namespace fs = std::filesystem;
static std::string system_root;
static int checks = 0;
static void Check(bool value, const char* message)
{
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}

// File-system and logging boundaries only. The resource resolver, INI parser and conversions are
// actual production sources; this keeps the test independent of a running game core.
namespace Common::Log
{
void GenericLogFmtImpl(LogLevel, LogType, const char*, int, fmt::string_view format,
                       const fmt::format_args& args)
{
  std::cerr << fmt::vformat(format, args) << '\n';
}
}

namespace File
{
const std::string& GetSysDirectory() { return system_root; }
bool ReadFileToString(const std::string& filename, std::string& output)
{
  std::ifstream input(StringToPath(filename), std::ios::binary);
  if (!input)
    return false;
  output.assign(std::istreambuf_iterator<char>(input), {});
  return !input.bad();
}
std::string GetTempFilenameForAtomicWrite(std::string path) { return path + ".tmp"; }
bool RenameSync(const std::string& source, const std::string& target)
{
  std::error_code error;
  fs::rename(StringToPath(source), StringToPath(target), error);
  return !error;
}
}

static void Write(const fs::path& path, const std::string& bytes)
{
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary);
  file.write(bytes.data(), bytes.size());
  Check(static_cast<bool>(file), "write synthetic fixture");
}

int main(int argc, char** argv)
{
  try
  {
    Check(argc == 3, "expected scratch directory and source Sys directory");
    const fs::path scratch = StringToPath(argv[1]);
    Check(!fs::exists(scratch), "scratch directory must be new");
    fs::create_directories(scratch);
    system_root = (scratch / "system").generic_string() + "/";
    const std::array<const char*, 7> names{
        "GC/dsp_coef.bin", "GC/dsp_rom.bin", "GC/font_japanese.bin", "GC/font_western.bin",
        "Resources/OSD_Font.ttf", "GameSettings/GDJEB2.ini", "codehandler.bin"};
    for (const char* name : names)
    {
      const auto bytes = File::ReadSystemResource(name);
#ifdef TEST_EMBEDDED
      std::string original;
      Check(File::ReadFileToString((StringToPath(argv[2]) / name).generic_string(), original),
            "read source resource");
      Check(bytes.size() == original.size(), "compiled resource size matches source");
      Check(std::equal(bytes.begin(), bytes.end(),
                       reinterpret_cast<const u8*>(original.data())),
            "compiled resource bytes match source");
      Check(File::ReadSystemResourceFromPath(system_root + "/" + name).data() == bytes.data(),
            "duplicate root separator resolves immutable storage");
      std::string windows = system_root + name;
      std::ranges::replace(windows, '/', '\\');
      Check(File::ReadSystemResourceFromPath(windows).data() == bytes.data(),
            "Windows separators resolve immutable storage");
#else
      Check(bytes.empty(), "generic build has no embedded bytes");
#endif
    }
    Check(File::ReadSystemResource("../GC/dsp_coef.bin").empty(), "reject relative traversal");
    Check(File::ReadSystemResource("GC/../GC/dsp_coef.bin").empty(), "reject internal traversal");
    Check(File::ReadSystemResource("/GC/dsp_coef.bin").empty(), "reject absolute resource name");
    Check(File::ReadSystemResource("Wii/setting.txt").empty(), "reject unlisted resource");
    Check(File::ReadSystemResourceFromPath(system_root + "../GC/dsp_coef.bin").empty(),
          "reject traversal beneath root");
    Check(File::ReadSystemResourceFromPath(system_root + "../user/GameSettings/GDJEB2.ini").empty(),
          "user path never resolves default");
    Check(File::ReadSystemResourceFromPath(system_root.substr(0, system_root.size() - 1) +
                                          "-other/GC/dsp_coef.bin").empty(),
          "reject lookalike root prefix");
    std::string contents;
#ifdef TEST_EMBEDDED
    Check(File::ReadSystemResourceFromPath(system_root + "GC//dsp_coef.bin").size() == 4096,
          "duplicate separators inside relative resource path resolve");
    Check(File::ReadSystemResourceFromPath(system_root + "GC//../GC/dsp_coef.bin").empty(),
          "duplicate separators do not permit traversal");
    Check(File::ReadFileToStringOrSystemResource(system_root + "GC/dsp_coef.bin", contents),
          "DSP fallback reads embedded coefficients");
    Check(contents.size() == 4096, "DSP coefficient length preserved");
    Common::IniFile embedded_ini;
    Check(embedded_ini.Load(system_root + "GameSettings/GDJEB2.ini"), "default INI embedded load");
    Check(embedded_ini.Exists("ActionReplay"), "actual bundled INI raw section retained");
#else
    Check(!File::ReadFileToStringOrSystemResource(system_root + "GC/dsp_coef.bin", contents),
          "generic missing resource fails");
#endif
    Write(StringToPath(system_root) / "GC/dsp_coef.bin", "user override");
    Check(File::ReadFileToStringOrSystemResource(system_root + "GC/dsp_coef.bin", contents),
          "file override is readable");
    Check(contents == "user override", "existing file takes precedence");
    const std::string synthetic = "[Core]\nCPUThread = False\nSpeed = 3\n";
    Write(StringToPath(system_root) / "GameSettings/GDJEB2.ini", synthetic);
    Common::IniFile override_ini;
    Check(override_ini.Load(system_root + "GameSettings/GDJEB2.ini"), "file INI override loads");
    Check(override_ini.Exists("Core", "Speed") && !override_ini.Exists("ActionReplay"),
          "INI file overrides embedded source without unintended merge");
    const auto user_ini_path = (scratch / "user/GameSettings/GDJEB2.ini").generic_string();
    Check(!override_ini.Load(user_ini_path, true) && override_ini.Exists("Core", "Speed"),
          "missing user INI never resolves embedded defaults and retains merge data");
    Write(StringToPath(user_ini_path), "[Core]\nSpeed = 4\n");
    Check(override_ini.Load(user_ini_path, true), "user INI can merge after system defaults");
    int user_speed = 0;
    Check(override_ini.GetIfExists("Core", "Speed", &user_speed) && user_speed == 4,
          "user configuration has final precedence");

    for (const std::string& newline : {std::string("\n"), std::string("\r\n")})
    {
      const std::string ini_text = "\xef\xbb\xbf[Core]" + newline + "CPUThread = True" + newline +
          "Speed = 1" + newline + "[ActionReplay]" + newline + "$Synthetic code" + newline +
          "01234567 89ABCDEF" + newline;
      const fs::path ini_path = scratch / "parser.ini";
      Write(ini_path, ini_text);
      Common::IniFile file_ini, memory_ini;
      std::istringstream input(ini_text);
      Check(file_ini.Load(ini_path.generic_string()) && memory_ini.Load(input), "file/memory parse");
      std::vector<std::string> file_lines, memory_lines;
      Check(file_ini.GetLines("ActionReplay", &file_lines) &&
            memory_ini.GetLines("ActionReplay", &memory_lines), "raw lines exist");
      Check(file_lines == memory_lines && memory_lines ==
            std::vector<std::string>{"$Synthetic code", "01234567 89ABCDEF"}, "raw lines identical");
      std::istringstream user("[Core]\nCPUThread = False\n");
      Check(memory_ini.Load(user, true), "keep current data loads");
      bool cpu_thread = true;
      int speed = 0;
      Check(memory_ini.GetIfExists("Core", "CPUThread", &cpu_thread) && !cpu_thread &&
            memory_ini.GetIfExists("Core", "Speed", &speed) && speed == 1, "layer precedence");
      std::istringstream failed;
      failed.setstate(std::ios::failbit);
      Check(!memory_ini.Load(failed, true) && memory_ini.Exists("Core"), "failed merge retains data");
      Check(!memory_ini.Load(failed, false) && !memory_ini.Exists("Core"), "failed reset clears data");
    }
    const std::string trailing_cr = "[ActionReplay]\n$Last line\r";
    Write(scratch / "trailing-cr.ini", trailing_cr);
    Common::IniFile cr_file, cr_memory;
    std::istringstream cr_input(trailing_cr);
    Check(cr_file.Load((scratch / "trailing-cr.ini").generic_string()) && cr_memory.Load(cr_input),
          "lone trailing CR parses");
    std::vector<std::string> cr_file_lines, cr_memory_lines;
    Check(cr_file.GetLines("ActionReplay", &cr_file_lines) &&
          cr_memory.GetLines("ActionReplay", &cr_memory_lines) && cr_file_lines == cr_memory_lines &&
          cr_memory_lines == std::vector<std::string>{"$Last line"}, "lone CR file/memory equivalence");
    Common::IniFile malformed;
    std::istringstream malformed_input("ignored = 7\n[Core]\nBrokenInt = no-number\n"
                                      "[Unterminated\nSpeed = 1\nSpeed = 2\n");
    Check(malformed.Load(malformed_input), "tolerated malformed lines preserve parser behavior");
    int parsed_number = 123;
    Check(!malformed.GetIfExists("Core", "BrokenInt", &parsed_number, 71) && parsed_number == 71,
          "malformed integer fails conversion and supplies the requested fallback");
    Check(malformed.GetIfExists("Core", "Speed", &parsed_number) && parsed_number == 2 &&
          !malformed.Exists("Unterminated") && !malformed.Exists("Core", "ignored"),
          "duplicate keys use final value; incomplete sections and pre-section keys ignored");
    std::cout << "PASS " << checks << " resource and actual INI parser checks\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
