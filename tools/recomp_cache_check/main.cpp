// SPDX-License-Identifier: GPL-2.0-or-later
// Isolated synthetic equivalence checks; no game runtime or device is started.
#include "Core/PowerPC/StaticRecomp/StaticRecompDw4Cache.h"

#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{
using namespace StaticRecompDw4Cache;
unsigned checks = 0;
u64 original_instructions = 0;
u64 batched_lines = 0;
unsigned timing_boundaries = 0;
unsigned equivalence_cases = 0;
unsigned syscall_exits = 0;
constexpr u32 EE_RI_MASK = PPC_MSR_EE | PPC_MSR_RI;
constexpr u32 EeRiBits(unsigned combination)
{
  return ((combination & 1) ? PPC_MSR_EE : 0u) | ((combination & 2) ? PPC_MSR_RI : 0u);
}
void Check(bool condition, const char* message)
{
  ++checks;
  if (!condition)
    throw std::runtime_error(message);
}

struct Timing
{
  s32 downcount = 0;
  u64 remainder = 0;
  u64 native_cycles = 0;
  u64 hook_cycles = 0;
  u64 hooks = 0;
  u64 system_calls = 0;
  std::vector<u32> journal;
};
Timing* current_timing = nullptr;
}

#include "interpreter_fixture.hpp"

// Actual GXRuntime exception implementation and system-call helper, extracted
// unchanged from the module source. The reference below models SC independently.
extern "C" {
#include "generated_exceptions.inc"
}

// The emitted stub calls this boundary. Its independently modeled chassis
// behavior is intentionally separate from the raw-instruction reference below.
extern "C" void ppc_fallback_instruction(CPUState* cpu, u32 raw, u32 cia)
{
  Check((raw == 0x7c00186cu && cia == 0x8025d3a0u) ||
            (raw == 0x7c0018acu && cia == 0x8025d314u) ||
            (raw == 0x7c001bacu && cia == 0x8025d2e8u),
        "The emitted fallback is not an expected cache instruction.");
  Check(current_timing != nullptr, "Missing synthetic fallback context.");
  current_timing->journal.push_back(cpu->gpr[3] & ~31u);
  current_timing->downcount -= 5;
  current_timing->hook_cycles += 5;
  ++current_timing->hooks;
  cpu->pc = cia + 4;
}

#define DOLRECOMP_C_LOOP_CYCLE_BUDGET 256
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4102)  // Extracted fall-through labels are unchanged.
#endif
#include "generated_cache.inc"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include "generated_code_ranges.inc"

namespace
{
void Store(CPUState& cpu, u32 address, u32 value)
{
  auto* p = cpu.ram + address - 0x80000000u;
  for (unsigned i = 0; i != 4; ++i)
    p[i] = static_cast<u8>(value >> ((3 - i) * 8));
}

// Decode the fetched original addi/bdnz/cache/blr/sc, without calling Prepare,
// Commit, the generated C, or the generated stub. Dispatch boundaries and the
// split hook/native accounting reproduce the existing chassis, not a claim
// that all seven cycles currently advance its native timebase accumulator.
void Original(CPUState& cpu, Timing& timing)
{
  for (;;)
  {
    const u32 cia = cpu.pc;
    const u32 instruction = ReadWord(cpu, cia);
    ++original_instructions;
    if ((instruction >> 26) == 14)
    {
      const u32 rd = (instruction >> 21) & 31;
      const u32 ra = (instruction >> 16) & 31;
      const auto immediate = static_cast<s16>(instruction & 0xffff);
      cpu.gpr[rd] = (ra ? cpu.gpr[ra] : 0u) + static_cast<u32>(static_cast<s32>(immediate));
      --cpu.downcount;
      cpu.pc += 4;
    }
    else if ((instruction >> 26) == 16)
    {
      Check(((instruction >> 21) & 31) == 16 && (instruction & 3) == 0,
            "Reference only accepts relative bdnz without link.");
      --cpu.ctr;
      --cpu.downcount;
      const auto displacement = static_cast<s16>(instruction & 0xfffc);
      cpu.pc = cpu.ctr ? cia + static_cast<u32>(static_cast<s32>(displacement)) : cia + 4;
      if (cpu.ctr && cpu.downcount <= -DOLRECOMP_C_LOOP_CYCLE_BUDGET)
        return;
    }
    else if (instruction == 0x7c00186cu || instruction == 0x7c0018acu ||
             instruction == 0x7c001bacu)
    {
      const auto effects = InterpreterFixture::Execute(cpu, instruction, false);
      Check(!effects.privileged_exception && effects.flushed.empty() &&
                effects.invalidated.empty() && effects.stored.empty(),
            "Original Interpreter took an unsupported cache path.");
      timing.journal.insert(timing.journal.end(), effects.jit_lines.begin(), effects.jit_lines.end());
      timing.downcount -= 5;
      timing.hook_cycles += 5;
      ++timing.hooks;
      cpu.pc = cia + 4;
      return;
    }
    else if (instruction == 0x4e800020u)
    {
      --cpu.downcount;
      cpu.pc = cpu.lr & ~3u;
      return;
    }
    else if (instruction == 0x44000002u)
    {
      // SC saves its continuation and takes the low vector in this guarded
      // profile. This deliberately does not call the generated helper.
      const u32 old_msr = cpu.msr;
      cpu.srr0 = cia + 4;
      cpu.srr1 = old_msr & 0x87c0ffffu;
      cpu.exception |= 0x00000008u;
      cpu.msr &= ~0x0004ef37u;
      cpu.pc = 0x00000c00u;
      cpu.downcount -= 2;
      ++timing.system_calls;
      return;
    }
    else
      throw std::runtime_error("Unexpected original cache-loop instruction.");
  }
}

void Flush(CPUState& cpu, Timing& timing, s64 charge)
{
  const u64 cycles = static_cast<u64>(charge > 0 ? charge : 1);
  cpu.downcount = 0;
  timing.downcount -= static_cast<s32>(cycles);
  timing.native_cycles += cycles;
  cpu.timebase += (timing.remainder + cycles) / 12;
  timing.remainder = (timing.remainder + cycles) % 12;
}

struct Translation
{
  bool valid = true;
  bool translated = true;
  bool from_bat = true;
  u32 address = 0;
};
Translation Identity(u32 address)
{
  return {true, true, true, address - 0x80000000u};
}

void Compare(const CPUState& expected, const CPUState& actual,
             const Timing& et, const Timing& at)
{
  Check(std::memcmp(&expected, &actual, sizeof(expected)) == 0,
        "Cache batching or emitted C changed CPU state.");
  Check(et.downcount == at.downcount && et.remainder == at.remainder &&
            et.native_cycles == at.native_cycles && et.hook_cycles == at.hook_cycles &&
            et.system_calls == at.system_calls,
        "Cache batching or emitted C changed the split timing endpoint.");
  Check(et.journal == at.journal, "Ordered cache-line invalidation journal differs.");
}

void RunSlice(CPUState& cpu, Timing& timing, Environment env, unsigned mode)
{
  const u32 loop_pc = env.dispatch_address;
  while (timing.downcount > 0 && (cpu.pc == loop_pc || cpu.pc == loop_pc - 4))
  {
    env.dispatch_address = cpu.pc;
    env.dispatch_r3 = cpu.gpr[3];
    env.dispatch_ctr = cpu.ctr;
    const u64 hooks = timing.hooks;
    if (mode == 0)
      Original(cpu, timing);
    else
    {
      current_timing = &timing;
      GeneratedCache(&cpu);
      current_timing = nullptr;
      if (cpu.exception & PPC_EXC_SYSTEM_CALL)
        ++timing.system_calls;
    }
    s64 charge = -cpu.downcount;
    if (mode == 2)
    {
      env.dispatch_charge = charge;
      env.downcount = timing.downcount;
      env.completed_hooks = timing.hooks - hooks;
      const auto batch = Prepare(cpu, env);
      if (HasIdentityMapping(batch, Identity))
      {
        // A range call must invalidate exactly this ordered set of lines.
        // This is a journal model, not a test of live JIT metadata internals.
        for (u32 line = 0; line != batch.lines; ++line)
          timing.journal.push_back(batch.address + 32 * line);
        Commit(cpu, timing.downcount, charge, batch);
        timing.hook_cycles += 5 * batch.lines;
        batched_lines += batch.lines;
      }
    }
    Flush(cpu, timing, charge);
  }
}
}

int main(int argc, char** argv)
try
{
  using namespace StaticRecompDw4Cache;
  if (argc != 3)
    throw std::runtime_error("Usage: recomp-cache-check report.json original-cache.bin");
  std::ifstream input(argv[2], std::ios::binary);
  const std::vector<u8> original((std::istreambuf_iterator<char>(input)), {});
  Check(original.size() == CHUNK_END - CHUNK_START, "Original chunk length differs.");
  u64 hash = 0xcbf29ce484222325ull;
  for (const u8 value : original)
    hash = (hash ^ value) * 0x100000001b3ull;
  Check(hash == CHUNK_HASH, "Policy FNV hash differs from the original DOL chunk.");
  std::vector<u8> ram(GC_MAIN_RAM_SIZE);
  std::copy(original.begin(), original.end(), ram.begin() + CHUNK_START - 0x80000000u);
  constexpr std::array<Loop, 3> expected_loops{{
      {0x8025d3a4u, 0x7c00186cu}, {0x8025d318u, 0x7c0018acu},
      {0x8025d2ecu, 0x7c001bacu}}};
  Check(LOOPS.size() == expected_loops.size(), "Unexpected supported cache-loop count.");
  for (const u32 pc : {0u, 0x8025d3a0u, 0x8025d314u, 0x8025d2e8u, 0x8025d348u,
                       0x8025d378u, 0x8025d3a5u, 0xffffffffu})
    Check(FindLoop(pc) == nullptr, "Unverified cache-loop entry was recognized.");
  constexpr unsigned trials = 1536;
  for (const auto& loop : expected_loops)
  {
    const u32 LOOP_PC = loop.pc;
    const bool syscall_terminal = LOOP_PC == 0x8025d318u;
    Check(FindLoop(LOOP_PC) && FindLoop(LOOP_PC)->instruction == loop.instruction,
          "Required exact cache-loop profile is absent.");
    CPUState cpu{};
    cpu.ram = ram.data();
    cpu.ram_size = static_cast<u32>(ram.size());
    cpu.pc = LOOP_PC;
    cpu.lr = 0x80480003u;  // Return outside the extracted chunk, preserving LR low bits.
    cpu.msr = PPC_MSR_EE | PPC_MSR_IR | PPC_MSR_DR;
    cpu.gpr[3] = 0x81000020u;
    cpu.ctr = 1024;
    cpu.downcount = -2;
    cpu.cr = 0xdeadbeefu;
    cpu.xer = 0x80000000u;
    StaticRecompRange chunk{CHUNK_START, CHUNK_END};
    StaticRecompModuleDesc module{};
    std::memcpy(module.game_id, "GDJEB2", 7);
    module.entry_point = 0x80005240u;
    module.chunk_ranges = &chunk;
    module.num_chunk_ranges = 1;
    module.chunk_hashes = &hash;
    module.code_ranges = s_code_ranges;
    module.num_code_ranges = static_cast<u32>(std::size(s_code_ranges));
    Environment env;
    env.enabled = env.module_active = env.running = env.chunk_verified = true;
    env.dispatch_address = LOOP_PC;
    env.dispatch_r3 = cpu.gpr[3] - 32;
    env.dispatch_ctr = cpu.ctr + 1;
    env.completed_hooks = 1;
    env.dispatch_charge = 2;
    env.downcount = 10000;
    env.ppc_msr = cpu.msr;
    env.game_id = "GDJEB2";
    env.module = &module;
    Check(ReadWord(cpu, LOOP_PC - 4) == loop.instruction &&
              ReadWord(cpu, LOOP_PC) == 0x38630020u &&
              ReadWord(cpu, LOOP_PC + 4) == 0x4200fff8u &&
              ReadWord(cpu, LOOP_PC + 8) == (syscall_terminal ? 0x44000002u : 0x4e800020u),
          "Original cache loop opcodes differ.");
    {
      const auto regular = InterpreterFixture::Execute(cpu, loop.instruction, false);
      Check(regular.jit_lines == std::vector<u32>{cpu.gpr[3] & ~31u} &&
                !regular.privileged_exception && regular.flushed.empty() &&
                regular.invalidated.empty() && regular.stored.empty(),
            "Actual Interpreter dcache-off behavior differs from the batch contract.");
      const auto cached = InterpreterFixture::Execute(cpu, loop.instruction, true);
      Check(cached.jit_lines.empty() && !cached.privileged_exception &&
                cached.flushed.size() == (loop.instruction == 0x7c0018acu ? 1u : 0u) &&
                cached.invalidated.size() == (loop.instruction == 0x7c001bacu ? 1u : 0u) &&
                cached.stored.size() == (loop.instruction == 0x7c00186cu ? 1u : 0u),
            "Actual Interpreter dcache-on behavior was not exercised.");
      auto privileged = cpu;
      privileged.msr |= PPC_MSR_PR;
      for (const bool dcache : {false, true})
      {
        const auto trapped = InterpreterFixture::Execute(privileged, 0x7c001bacu, dcache);
        Check(trapped.privileged_exception && trapped.jit_lines.empty() &&
                  trapped.invalidated.empty(), "Actual dcbi privilege trap was bypassed.");
      }
    }
    const CPUState saved = cpu;
    const auto initial = Prepare(cpu, env);
    Check(initial.address == 0x81000040u && initial.lines == 256, "Wrong next line or batch cap.");
    Check(std::memcmp(&cpu, &saved, sizeof(cpu)) == 0, "Prepare modified architectural state.");
    {
      auto c = cpu;
      c.gpr[3] |= 17;
      const auto before = c;
      s32 ppc_downcount = 100;
      s64 native_charge = 2;
      Commit(c, ppc_downcount, native_charge, {initial.address, 3});
      Check(ppc_downcount == 85 && native_charge == 8 && c.downcount == -2 &&
                c.gpr[3] == before.gpr[3] + 96 && c.ctr == before.ctr - 3 &&
                c.timebase == before.timebase && c.pc == before.pc,
            "Commit merged hook/native costs, lost low bits, or prematurely changed timebase.");
      c = cpu;
      c.ctr = 1;
      c.downcount = 0;
      Timing terminal;
      terminal.downcount = 100;
      auto expected = c;
      Timing expected_terminal = terminal;
      Original(expected, expected_terminal);
      current_timing = &terminal;
      GeneratedCache(&c);
      current_timing = nullptr;
      terminal.system_calls = (c.exception & PPC_EXC_SYSTEM_CALL) ? 1 : 0;
      Compare(expected, c, expected_terminal, terminal);
      Check(c.downcount == (syscall_terminal ? -4 : -3) && terminal.downcount == 100 &&
                terminal.journal.empty() && c.ctr == 0 &&
                c.pc == (syscall_terminal ? 0xc00u : (c.lr & ~3u)) &&
                (!syscall_terminal || (c.srr0 == 0x8025d324u &&
                                        c.exception == PPC_EXC_SYSTEM_CALL)),
            "The final real return/system call changed CPU state or its native cycle charge.");
    }

    const auto reject_env = [&](auto edit) {
      auto e = env;
      edit(e);
      Check(Prepare(cpu, e).lines == 0, "Environment guard accepted an unsupported profile.");
    };
    reject_env([](auto& e) { e.enabled = false; });
    reject_env([](auto& e) { e.module_active = false; });
    reject_env([](auto& e) { e.running = false; });
    reject_env([](auto& e) { e.lockstep = true; });
    reject_env([](auto& e) { e.chunk_verified = false; });
    reject_env([](auto& e) { e.chunk_has_hooks = true; });
    reject_env([](auto& e) { e.forced_fallback = true; });
    reject_env([](auto& e) { e.dcache_enabled = true; });
    reject_env([&](auto& e) { e.generic_idle_pc = LOOP_PC; });
    reject_env([](auto& e) { e.generic_idle_pc = 4; });
    reject_env([](auto& e) { e.game_id = "GDJPB2"; });
    reject_env([](auto& e) { e.game_id = {}; });
    reject_env([&](auto& e) { e.dispatch_address = LOOP_PC - 4; });
    for (const auto& other : expected_loops)
      if (other.pc != LOOP_PC)
        reject_env([&](auto& e) { e.dispatch_address = other.pc; });
    reject_env([](auto& e) { ++e.dispatch_r3; });
    reject_env([](auto& e) { ++e.dispatch_ctr; });
    reject_env([](auto& e) { e.dispatch_ctr = 1; });
    reject_env([](auto& e) { e.dispatch_charge = 0; });
    reject_env([](auto& e) { e.dispatch_charge = 3; });
    reject_env([](auto& e) { e.completed_hooks = 0; });
    reject_env([](auto& e) { e.completed_hooks = 2; });
    for (unsigned bit = 0; bit != 32; ++bit)
      if (((1u << bit) & EE_RI_MASK) == 0)
        reject_env([bit](auto& e) { e.ppc_msr ^= 1u << bit; });
    reject_env([](auto& e) { e.module = nullptr; });
    reject_env([](auto& e) { e.chunk_index = 1; });
    for (unsigned bit = 0; bit != 32; ++bit)
      reject_env([bit](auto& e) { e.pending_exceptions = 1u << bit; });
    for (const s32 value : {std::numeric_limits<s32>::min(), -1, 0, 2, 8, 9})
      reject_env([value](auto& e) { e.downcount = value; });
    const auto reject_cpu = [&](auto edit) {
      auto c = cpu;
      edit(c);
      Check(Prepare(c, env).lines == 0, "CPU guard accepted an unsupported state.");
    };
    reject_cpu([](auto& c) { ++c.pc; });
    reject_cpu([](auto& c) { c.downcount = 0; });
    reject_cpu([](auto& c) { c.ctr = 1; });
    reject_cpu([](auto& c) { c.exception = 1; });
    reject_cpu([](auto& c) { c.program_exception = 1; });
    reject_cpu([](auto& c) { c.ram = nullptr; });
    reject_cpu([](auto& c) { c.ram_size = CHUNK_END - 0x80000000u - 1; });
    for (const u32 bit : {PPC_MSR_IR, PPC_MSR_DR})
    {
      auto c = cpu;
      auto e = env;
      c.msr &= ~bit;
      e.ppc_msr = c.msr;
      Check(Prepare(c, e).lines == 0, "Missing required MSR bit accepted.");
    }
    for (const u32 bit : {PPC_MSR_POW, PPC_MSR_ILE, PPC_MSR_PR, PPC_MSR_SE,
                         PPC_MSR_BE, PPC_MSR_IP, PPC_MSR_PM, PPC_MSR_LE})
    {
      auto c = cpu;
      auto e = env;
      c.msr |= bit;
      e.ppc_msr = c.msr;
      Check(Prepare(c, e).lines == 0, "Excluded MSR bit accepted.");
    }
    // Every independent guest/chassis EE/RI state is admissible, but no
    // pending exception becomes admissible when interrupts are disabled.
    for (unsigned guest_bits = 0; guest_bits != 4; ++guest_bits)
    {
      for (unsigned ppc_bits = 0; ppc_bits != 4; ++ppc_bits)
      {
        auto c = cpu;
        auto e = env;
        c.msr = (c.msr & ~EE_RI_MASK) | EeRiBits(guest_bits);
        e.ppc_msr = (e.ppc_msr & ~EE_RI_MASK) | EeRiBits(ppc_bits);
        const auto before = c;
        const auto b = Prepare(c, e);
        Check(b.address == initial.address && b.lines == initial.lines &&
                  std::memcmp(&before, &c, sizeof(c)) == 0,
              "EE/RI-only mismatch changed batch acceptance or guest state.");
        for (unsigned bit = 0; bit != 32; ++bit)
        {
          e.pending_exceptions = 1u << bit;
          Check(Prepare(c, e).lines == 0,
                "Pending exception accepted for an independent EE/RI state.");
        }
      }
    }
    const auto reject_module = [&](auto edit) {
      auto m = module;
      auto e = env;
      edit(m);
      e.module = &m;
      Check(Prepare(cpu, e).lines == 0, "Module identity guard failed.");
    };
    reject_module([](auto& m) { m.game_id[5] = '3'; });
    reject_module([](auto& m) { m.game_id[6] = 'X'; });
    reject_module([](auto& m) { ++m.entry_point; });
    reject_module([](auto& m) { m.num_rel_modules = 1; });
    reject_module([](auto& m) { m.chunk_ranges = nullptr; });
    reject_module([](auto& m) { m.chunk_hashes = nullptr; });
    reject_module([](auto& m) { m.num_chunk_ranges = 0; });
    reject_module([](auto& m) { m.code_ranges = nullptr; });
    reject_module([](auto& m) { m.num_code_ranges = 0; });
    hash ^= 1;
    Check(Prepare(cpu, env).lines == 0, "Changed chunk hash accepted.");
    hash ^= 1;
    ++chunk.start;
    Check(Prepare(cpu, env).lines == 0, "Changed chunk start accepted.");
    --chunk.start;
    ++chunk.end;
    Check(Prepare(cpu, env).lines == 0, "Changed chunk end accepted.");
    --chunk.end;
    for (const u32 address : {LOOP_PC - 4, LOOP_PC, LOOP_PC + 4})
    {
      const u32 word = ReadWord(cpu, address);
      Store(cpu, address, word ^ 1);
      Check(Prepare(cpu, env).lines == 0, "Changed live instruction accepted.");
      Store(cpu, address, word);
    }
    const auto at_address = [&](u32 r3, u32 ctr = 1024) {
      auto c = cpu;
      auto e = env;
      c.gpr[3] = r3;
      c.ctr = ctr;
      e.dispatch_r3 = r3 - 32;
      e.dispatch_ctr = ctr + 1;
      return Prepare(c, e);
    };
    for (const u32 r3 : {0u, 0x7fffffc0u, 0x81800000u, 0xc1000020u, 0xffffffe0u})
      Check(at_address(r3).lines == 0, "Noncanonical or wrapped MEM1 range accepted.");
    for (u32 low = 0; low != 32; ++low)
    {
      const auto b = at_address(0x81000020u + low);
      Check(b.address == 0x81000040u && b.lines == 256, "Unaligned r3 selected the wrong line.");
      Check(at_address(0x8101ffc0u + low).lines == 1, "Batch crossed a 128 KiB BAT index.");
      const auto end = at_address(0x817fffc0u + low, 2);
      Check(end.address == 0x817fffe0u && end.lines == 1, "Last valid MEM1 line was lost.");
    }
    Check(at_address(0x817fffe0u).lines == 0, "MEM1 endpoint overflow accepted.");
    Check(at_address(0x81000020u, 2).lines == 1, "Final CTR=1 dispatch was consumed.");
    Check(at_address(0x81000020u, 1).lines == 0, "Terminal branch was batched.");
    for (const auto& code : s_code_ranges)
      Check(at_address(code.start - 32).lines == 0, "A real module code range was batched.");
    {
      auto c = cpu;
      c.ram_size = 0x01000050u;
      Check(Prepare(c, env).lines == 0, "Partial backing RAM accepted.");
      c = cpu;
      auto e = env;
      c.gpr[3] = 0;
      e.dispatch_r3 = 0xffffffe0u;
      Check(Prepare(c, e).lines == 0, "Dispatch r3 wrapped through 32 bits.");
    }
    for (const StaticRecompRange bad : {StaticRecompRange{0x81000041u, 0x81000042u},
                                       StaticRecompRange{0x81000000u, 0x81000041u},
                                       StaticRecompRange{0x81000040u, 0x81000040u},
                                       StaticRecompRange{0x81000041u, 0x81000040u}})
    {
      auto m = module;
      auto e = env;
      m.code_ranges = &bad;
      m.num_code_ranges = 1;
      e.module = &m;
      Check(Prepare(cpu, e).lines == 0, "Unaligned overlap or invalid code range accepted.");
    }
    for (s32 count = 10; count != 80; ++count)
    {
      auto e = env;
      e.downcount = count;
      const auto b = Prepare(cpu, e);
      Check(b.lines == static_cast<u32>((count - 3) / 7) && count - 2 - 7 * b.lines > 0,
            "Batch did not leave a positive timing slice for a real dispatch.");
    }
    {
      auto e = env;
      e.downcount = std::numeric_limits<s32>::max();
      Check(Prepare(cpu, e).lines == 256, "Maximum signed slice overflowed the batch cap.");
    }
    unsigned translations = 0;
    Check(!HasIdentityMapping({}, [&](u32 a) { ++translations; return Identity(a); }) &&
              translations == 0, "Empty batch called translation.");
    for (unsigned endpoint = 0; endpoint != 2; ++endpoint)
      for (unsigned failure = 0; failure != 4; ++failure)
      {
        unsigned calls = 0;
        Check(!HasIdentityMapping(initial, [&](u32 address) {
          auto result = Identity(address);
          if (calls++ == endpoint)
          {
            if (failure == 0) result.valid = false;
            if (failure == 1) result.translated = false;
            if (failure == 2) result.from_bat = false;
            if (failure == 3) ++result.address;
          }
          return result;
        }), "Translation guard accepted a nonidentity endpoint.");
      }
    std::vector<u32> translated;
    Check(HasIdentityMapping(initial, [&](u32 a) { translated.push_back(a); return Identity(a); }) &&
              translated == std::vector<u32>{initial.address, initial.address + initial.lines * 32 - 1},
          "Translation did not inspect both exact batch endpoints.");

    // Compare each timing-event boundary, including seven-cycle overshoot and
    // the real three-cycle return/four-cycle SC, all low address bits, all twelve
    // TB residues, wraparound TB, direct cache entry, and BAT-boundary batches.
    for (unsigned msr_case = 0; msr_case != 16; ++msr_case)
    {
      const u64 previous_batched_lines = batched_lines;
      for (unsigned trial = 0; trial != trials; ++trial)
      {
        ++equivalence_cases;
        auto reference = cpu;
        reference.downcount = 0;
        reference.pc = trial % 4 == 0 ? LOOP_PC - 4 : LOOP_PC;
        reference.gpr[3] = (trial & 1 ? 0x8101ff00u : 0x81000000u) + trial % 32;
        reference.ctr = 1 + (trial * 37u) % 1024;
        reference.lr = 0x80480000u + trial % 4;
        reference.timebase = trial & 2 ? std::numeric_limits<u64>::max() - 8 : trial * 11u;
        reference.gpr[0] = 0xdead0000u ^ trial;
        reference.cr ^= trial;
        constexpr std::array<u32, 6> extra_msr{PPC_MSR_FP, PPC_MSR_ME, PPC_MSR_FE0,
                                             PPC_MSR_FE1, PPC_MSR_RI, 0x00800000u};
        for (unsigned bit = 0; bit != extra_msr.size(); ++bit)
          if (trial & (1u << bit)) reference.msr |= extra_msr[bit];
        auto trial_env = env;
        reference.msr = (reference.msr & ~EE_RI_MASK) | EeRiBits(msr_case & 3);
        trial_env.ppc_msr = (reference.msr & ~EE_RI_MASK) | EeRiBits(msr_case >> 2);
        auto emitted = reference;
        auto optimized = reference;
        Timing rt, nt, ot;
        rt.remainder = nt.remainder = ot.remainder = trial % 12;
        unsigned events = 0;
        do
        {
          const s32 slice = static_cast<s32>(1 + ((trial * 7919u + events * 13u) % 2000));
          rt.downcount = nt.downcount = ot.downcount = slice;
          RunSlice(reference, rt, trial_env, 0);
          RunSlice(emitted, nt, trial_env, 1);
          RunSlice(optimized, ot, trial_env, 2);
          Compare(reference, emitted, rt, nt);
          Compare(reference, optimized, rt, ot);
          ++timing_boundaries;
          Check(++events < 8192, "Synthetic timing test did not terminate.");
        } while (reference.pc == LOOP_PC || reference.pc == LOOP_PC - 4);
        Check(reference.pc == (syscall_terminal ? 0xc00u : (reference.lr & ~3u)) &&
                  reference.ctr == 0 && rt.system_calls == (syscall_terminal ? 1u : 0u),
              "Final real return/system call did not execute exactly once.");
        if (syscall_terminal)
        {
          Check(reference.srr0 == 0x8025d324u && reference.exception == PPC_EXC_SYSTEM_CALL,
                "Dcbf terminal system call lost its continuation or exception.");
          ++syscall_exits;
        }
      }
        Check(batched_lines - previous_batched_lines > 100000,
            "Each loop and independent EE/RI state must exercise substantial accepted batching.");
    }
  }
  Check(std::equal(original.begin(), original.end(), ram.begin() + CHUNK_START - 0x80000000u),
        "Policy or generated execution modified original code memory.");
  std::ofstream report(argv[1]);
  Check(static_cast<bool>(report), "Could not create report.");
  report << "{\"passed\":true,\"checks\":" << checks << ",\"equivalence_cases\":" << equivalence_cases
         << ",\"timing_boundaries\":" << timing_boundaries << ",\"original_instructions\":"
         << original_instructions << ",\"batched_lines\":" << batched_lines
         << ",\"loop_profiles\":3,\"syscall_exits\":" << syscall_exits
         << ",\"guest_ppc_ee_ri_combinations\":16"
         << ",\"original_chunk_fnv\":\"350B8C917F12EA10\",\"hook_cycles_per_line\":5,"
            "\"native_cycles_per_line\":2,\"tb_ratio\":12,\"runtime_launched\":false,"
            "\"scope\":\"Actual generated loops, Interpreter cache functions and GXRuntime SC helper; synthetic CPU/timing/cache-line journal equivalence. Live JIT metadata and audible output are not exercised.\"}\n";
  report.close();
  Check(static_cast<bool>(report), "Could not finish report.");
  std::cout << "DW4 cache policy: " << checks << " checks, " << equivalence_cases << " cases, "
            << timing_boundaries << " timing boundaries, zero differences.\n";
  return 0;
}
catch (const std::exception& error)
{
  std::cerr << error.what() << '\n';
  return 1;
}
