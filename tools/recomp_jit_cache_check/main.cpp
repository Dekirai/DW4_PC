// Asset-free fixtures for methods extracted verbatim from pinned and current
// JitCache.cpp by scripts/test-recomp-jit-cache.ps1. No game data or runtime.
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <vector>

using u32 = std::uint32_t;
using u64 = std::uint64_t;
#include "mmu_constants.inc"

namespace
{
u64 checks = 0, cases = 0, original_erase_attempts = 0, patched_erase_attempts = 0;
void Check(bool condition, const char* message)
{
  ++checks;
  if (!condition)
    throw std::runtime_error(message);
}

struct MetadataSet
{
  std::unordered_set<u32> values;
  u64 attempts = 0;
  std::vector<u32> removed;
  bool empty() const { return values.empty(); }
  void erase(u32 address)
  {
    ++attempts;
    if (values.erase(address) != 0)
      removed.push_back(address);
  }
};
struct Metadata
{
  MetadataSet fifoWriteAddresses, pairedQuantizeAddresses, noSpeculativeConstantsAddresses;
  std::array<MetadataSet*, 3> All()
  {
    return {&fifoWriteAddresses, &pairedQuantizeAddresses, &noSpeculativeConstantsAddresses};
  }
};
using Event = std::tuple<char, u32, u32>;
struct ValidBlocks
{
  bool initially_valid = false;
  std::unordered_set<u32> cleared;
  std::vector<Event> events;
  bool Test(u32 block)
  {
    events.emplace_back('T', block, 0);
    return initially_valid && !cleared.contains(block);
  }
  void Clear(u32 block)
  {
    events.emplace_back('C', block, 0);
    cleared.insert(block);
  }
};
struct Translation
{
  bool valid, translated, from_bat;
  u32 address;
};
// Deterministic MMU dependency, not a replacement implementation of translation.
struct FixtureMMU
{
  bool use_bat = false, translate = false, invalid_odd_pages = false;
  u32 physical_xor = 0;
  std::vector<u32> queries;
  Translation JitCache_TranslateAddress(u32 address)
  {
    queries.push_back(address);
    return {!(invalid_odd_pages && ((address >> 12) & 1u)), translate, use_bat,
            address ^ physical_xor};
  }
};
struct FixtureJit
{
  Metadata js;
  FixtureMMU m_mmu;
};
enum class Mutation { None, Publish, Clear };
struct Fixture
{
  FixtureJit m_jit;
  ValidBlocks valid_block;
  std::vector<Event> events;
  Mutation mutation = Mutation::None;
  u32 publish_address = 0;
  // The dependency can publish metadata, proving the guard reads AFTER this call.
  void ErasePhysicalRange(u32 address, u32 length)
  {
    events.emplace_back('E', address, length);
    if (mutation == Mutation::Publish)
      m_jit.js.fifoWriteAddresses.values.insert(publish_address);
    else if (mutation == Mutation::Clear)
      for (auto* set : m_jit.js.All())
        set->values.clear();
  }
};
struct OriginalCache : Fixture
{
  void InvalidateICache(u32 address, u32 length, bool forced);
  void InvalidateICacheInternal(u32 physical, u32 address, u32 length, bool forced);
};
struct PatchedCache : Fixture
{
  void InvalidateICache(u32 address, u32 length, bool forced);
  void InvalidateICacheInternal(u32 physical, u32 address, u32 length, bool forced);
};
#include "original_methods.inc"
#include "patched_methods.inc"

struct Case
{
  u32 address = 0x80001000, physical = 0x1000, length = 64;
  unsigned populated_mask = 0;
  bool forced = false, valid = true, outer = false, bat = false, translated = true;
  bool invalid_odd_pages = false;
  Mutation mutation = Mutation::None;
};
template <typename Cache>
void Initialize(Cache& cache, const Case& c)
{
  cache.valid_block.initially_valid = c.valid;
  cache.m_jit.m_mmu.use_bat = c.bat;
  cache.m_jit.m_mmu.translate = c.translated;
  cache.m_jit.m_mmu.invalid_odd_pages = c.invalid_odd_pages;
  cache.m_jit.m_mmu.physical_xor = c.translated ? 0x80000000u : 0u;
  cache.mutation = c.mutation;
  cache.publish_address = c.address;
  const auto sets = cache.m_jit.js.All();
  for (unsigned index = 0; index < sets.size(); ++index)
  {
    if ((c.populated_mask & (1u << index)) == 0)
      continue;
    for (u32 offset : {0u, 1u, 4u, 28u, 32u, 36u, 60u, 4096u, 131072u})
      sets[index]->values.insert(c.address + offset);
    sets[index]->values.insert(c.address + c.length);
    sets[index]->values.insert(c.address - 4);
    sets[index]->values.insert(0);
    sets[index]->values.insert(0xfffffffcu);
  }
}
struct Result
{
  u64 original_attempts = 0, patched_attempts = 0;
  std::array<std::unordered_set<u32>, 3> remaining;
  std::vector<Event> erased, bit_events;
  std::vector<u32> translations;
};
Result Run(const Case& c)
{
  OriginalCache original;
  PatchedCache patched;
  Initialize(original, c);
  Initialize(patched, c);
  if (c.outer)
  {
    original.InvalidateICache(c.address, c.length, c.forced);
    patched.InvalidateICache(c.address, c.length, c.forced);
  }
  else
  {
    original.InvalidateICacheInternal(c.physical, c.address, c.length, c.forced);
    patched.InvalidateICacheInternal(c.physical, c.address, c.length, c.forced);
  }
  ++cases;
  Check(original.events == patched.events, "Block-erasure calls or order changed.");
  Check(original.valid_block.events == patched.valid_block.events,
        "Valid-block test/clear order changed.");
  Check(original.valid_block.cleared == patched.valid_block.cleared,
        "Resulting valid-block state changed.");
  Check(original.m_jit.m_mmu.queries == patched.m_jit.m_mmu.queries,
        "MMU translation calls or order changed.");
  Result result;
  const auto before = original.m_jit.js.All();
  const auto after = patched.m_jit.js.All();
  for (unsigned index = 0; index < before.size(); ++index)
  {
    Check(before[index]->values == after[index]->values, "Metadata contents changed.");
    Check(before[index]->removed == after[index]->removed,
          "Successful metadata-erasure order changed.");
    Check(after[index]->attempts <= before[index]->attempts, "Metadata work increased.");
    result.original_attempts += before[index]->attempts;
    result.patched_attempts += after[index]->attempts;
    result.remaining[index] = after[index]->values;
  }
  result.erased = patched.events;
  result.bit_events = patched.valid_block.events;
  result.translations = patched.m_jit.m_mmu.queries;
  original_erase_attempts += result.original_attempts;
  patched_erase_attempts += result.patched_attempts;
  return result;
}

void BoundaryChecks()
{
  Case c;
  auto result = Run(c);
  Check(result.original_attempts == 48 && result.patched_attempts == 0,
        "Empty two-line range must avoid 48 metadata erase attempts.");
  Check(result.erased == std::vector<Event>{{'E', 0x1000, 64}},
        "Empty metadata must not bypass block invalidation.");
  Check(result.bit_events.size() == 2, "Empty metadata must still clear both valid bits.");
  c.length = 32;
  c.valid = false;
  result = Run(c);
  Check(result.erased.empty() && result.original_attempts == 0,
        "Original invalid single-line short circuit changed.");
  c.valid = true;
  result = Run(c);
  Check(result.erased.size() == 1 && result.original_attempts == 24 &&
            result.patched_attempts == 0, "Valid empty single-line block erasure changed.");
  c.populated_mask = 7;
  c.forced = true;
  result = Run(c);
  Check(result.erased.size() == 1 && result.original_attempts == 0 &&
            result.remaining[0].contains(c.address), "Forced metadata retention changed.");
  c.forced = false;
  result = Run(c);
  Check(!result.remaining[0].contains(c.address) &&
            result.remaining[0].contains(c.address + 1) &&
            result.remaining[0].contains(c.address + c.length),
        "Metadata stride or exclusive range end changed.");
  c.populated_mask = 0;
  c.mutation = Mutation::Publish;
  result = Run(c);
  Check(result.patched_attempts == 24 && !result.remaining[0].contains(c.address),
        "Guard must read metadata after block destruction publishes it.");
  c.populated_mask = 7;
  c.mutation = Mutation::Clear;
  result = Run(c);
  Check(result.original_attempts == 24 && result.patched_attempts == 0,
        "Guard must observe metadata cleared during block destruction.");
  c = {};
  c.outer = true;
  c.address = 0xffffffe0;
  c.length = 64;
  c.populated_mask = 7;
  result = Run(c);
  Check(result.translations == std::vector<u32>{0xffffffe0u, 0u},
        "Wrapped invalidation must preserve both MMU page translations.");
  Check(result.remaining[0].contains(0xffffffe0u) && !result.remaining[0].contains(0),
        "Original wrapped metadata-range behavior changed.");
  c.address = 0x80000fe0;
  result = Run(c);
  Check(result.translations == std::vector<u32>{0x80000fe0u, 0x80001000u},
        "Hardware-page range splitting changed.");
  c.bat = true;
  result = Run(c);
  Check(result.translations == std::vector<u32>{0x80000fe0u}, "BAT range splitting changed.");
  c.address = 0x8001ffe0;
  result = Run(c);
  Check(result.translations == std::vector<u32>{0x8001ffe0u, 0x80020000u},
        "Cross-BAT range splitting changed.");
  c.bat = false;
  c.address = 0x80000fe0;
  c.invalid_odd_pages = true;
  result = Run(c);
  Check(result.erased.size() == 1, "Invalid translated page must not be erased.");
}

void Corpus()
{
  constexpr std::array<u32, 13> lengths{0, 1, 3, 4, 28, 31, 32, 33, 36, 64, 4096, 8192, 131104};
  for (u32 length : lengths)
    for (unsigned mask = 0; mask < 8; ++mask)
      for (unsigned flags = 0; flags < 16; ++flags)
      {
        Case c;
        c.length = length;
        c.populated_mask = mask;
        c.forced = (flags & 1) != 0;
        c.valid = (flags & 2) != 0;
        c.outer = (flags & 4) != 0;
        if ((flags & 8) != 0) { ++c.address; ++c.physical; }
        Run(c);
      }
  // Aligned lengths include wrapping, without entering the upstream uint32 loop's
  // pathological nonterminating unaligned end near UINT32_MAX.
  for (u32 address : {0u, 0xff0u, 0x1fff0u, 0x80000ff0u, 0x8001fff0u, 0xfffffff0u, 0xfffffffcu})
    for (u32 length : {0u, 4u, 16u, 32u, 64u, 4096u, 8192u})
      for (unsigned flags = 0; flags < 32; ++flags)
      {
        Case c;
        c.address = address;
        c.physical = address ^ 0x80000000u;
        c.length = length;
        c.populated_mask = flags % 8;
        c.outer = true;
        c.bat = (flags & 8) != 0;
        c.invalid_odd_pages = (flags & 16) != 0;
        c.translated = (flags & 4) == 0;
        Run(c);
      }
  for (Mutation mutation : {Mutation::Publish, Mutation::Clear})
    for (unsigned mask = 0; mask < 8; ++mask)
      for (bool forced : {false, true})
      {
        Case c;
        c.mutation = mutation;
        c.populated_mask = mask;
        c.forced = forced;
        Run(c);
      }
}
}  // namespace

int wmain(int argc, wchar_t** argv)
{
  try
  {
    if (argc != 2) throw std::runtime_error("Expected the JSON report path.");
    BoundaryChecks();
    Corpus();
    std::ofstream report{std::filesystem::path(argv[1])};
    if (!report) throw std::runtime_error("Could not create the report.");
    report << "{\n  \"passed\": true,\n  \"checks\": " << checks
           << ",\n  \"cases\": " << cases
           << ",\n  \"original_metadata_erase_attempts\": " << original_erase_attempts
           << ",\n  \"patched_metadata_erase_attempts\": " << patched_erase_attempts
           << ",\n  \"game_data_used\": false,\n  \"runtime_launched\": false\n}\n";
    report.close();
    if (!report) throw std::runtime_error("Could not write the report.");
    std::cout << "JIT metadata invalidation: " << checks << " checks / " << cases
              << " source-method cases passed; erase attempts " << original_erase_attempts
              << " -> " << patched_erase_attempts << ".\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "JIT metadata invalidation check failed after " << checks << " checks: "
              << error.what() << '\n';
    return 1;
  }
}
