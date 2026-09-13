// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded policy/original-instruction test; this does not boot a game runtime.
#include "Core/PowerPC/StaticRecomp/StaticRecompDw4Idle.h"

#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#define DOLRECOMP_C_LOOP_CYCLE_BUDGET 256
#include "generated_idle.inc"

namespace
{
using namespace StaticRecompDw4Idle;
unsigned checks = 0;
u64 original_instructions = 0;
void Check(bool value, const char* message)
{
  ++checks;
  if (!value)
    throw std::runtime_error(message);
}

void Store(CPUState& cpu, u32 address, u32 value)
{
  auto* p = cpu.ram + address - 0x80000000u;
  for (unsigned i = 0; i != 4; ++i)
    p[i] = static_cast<u8>(value >> ((3 - i) * 8));
}

// Execute the actual three fetched original opcodes, one at a time. The budget
// boundary is the generated module's dispatch contract, not guest semantics.
void Original(CPUState& cpu)
{
  for (;;)
  {
    const u32 load = ReadWord(cpu, LOOP_PC);
    const u32 compare = ReadWord(cpu, LOOP_PC + 4);
    const u32 branch = ReadWord(cpu, LOOP_PC + 8);
    if (load != 0x800d1010u || compare != 0x28000000u || branch != 0x4182fff8u)
      throw std::runtime_error("Original instruction fixture changed.");
    cpu.gpr[(load >> 21) & 31] = ReadWord(cpu, cpu.gpr[(load >> 16) & 31] + (load & 0xffff));
    const u32 value = cpu.gpr[(compare >> 16) & 31];
    const u32 field = (value == 0 ? 2u : 4u) | (cpu.xer >> 31);
    cpu.cr = (cpu.cr & 0x0fffffffu) | (field << 28);
    cpu.downcount -= 3;
    original_instructions += 3;
    cpu.pc = (cpu.cr & 0x20000000u) ? LOOP_PC : LOOP_PC + 12;
    if (cpu.pc != LOOP_PC || cpu.downcount <= -256)
      return;
  }
}

struct Timing
{
  s32 downcount;
  u64 remainder;
  u64 cycles = 0;
};

void Charge(CPUState& cpu, Timing& timing, u64 extra = 0)
{
  const u64 cycles = static_cast<u64>(-cpu.downcount) + extra;
  cpu.downcount = 0;
  timing.downcount -= static_cast<s32>(cycles);
  timing.cycles += cycles;
  // Gekko's actual SystemTimers::TIMER_RATIO is twelve CPU cycles per TB tick.
  cpu.timebase += (timing.remainder + cycles) / 12;
  timing.remainder = (timing.remainder + cycles) % 12;
}
}

int main(int argc, char** argv)
try
{
  using namespace StaticRecompDw4Idle;
  std::vector<u8> ram(GC_MAIN_RAM_SIZE);
  CPUState cpu{};
  cpu.ram = ram.data();
  cpu.ram_size = static_cast<u32>(ram.size());
  cpu.pc = cpu.lr = LOOP_PC;
  cpu.msr = PPC_MSR_EE | PPC_MSR_IR | PPC_MSR_DR;
  cpu.gpr[13] = 0x80564f00u;
  cpu.gpr[30] = 0x80000000u;
  cpu.gpr[31] = 0x80514db8u;
  cpu.cr = 0x21234567u;
  cpu.downcount = -QUANTUM;
  Store(cpu, LOOP_PC, 0x800d1010u);
  Store(cpu, LOOP_PC + 4, 0x28000000u);
  Store(cpu, LOOP_PC + 8, 0x4182fff8u);
  if (argc < 3)
    throw std::runtime_error("A local original scheduler chunk fixture is required.");
  std::ifstream original_file(argv[2], std::ios::binary);
  const std::vector<u8> original_chunk((std::istreambuf_iterator<char>(original_file)), {});
  Check(original_chunk.size() == CHUNK_END - CHUNK_START, "Original scheduler chunk length differs.");
  u64 original_hash = 0xcbf29ce484222325ull;
  for (const u8 byte : original_chunk)
    original_hash = (original_hash ^ byte) * 0x100000001b3ull;
  Check(original_hash == CHUNK_HASH, "Policy chunk hash differs from original DOL bytes.");
  std::copy(original_chunk.begin(), original_chunk.end(), ram.begin() + CHUNK_START - 0x80000000u);
  Store(cpu, 0x800000d4u, 0x805154e8u);
  Store(cpu, 0x800000c0u, 0x005154e8u);
  StaticRecompRange range{CHUNK_START, CHUNK_END};
  u64 hash = CHUNK_HASH;
  StaticRecompModuleDesc module{};
  std::memcpy(module.game_id, "GDJEB2", 7);
  module.entry_point = 0x80005240u;
  module.chunk_ranges = &range;
  module.num_chunk_ranges = 1;
  module.chunk_hashes = &hash;
  Environment env;
  env.enabled = env.module_active = env.running = env.chunk_verified = true;
  env.dispatch_address = LOOP_PC;
  env.dispatch_charge = QUANTUM;
  env.downcount = 4096;
  env.game_id = "GDJEB2";
  env.module = &module;
  const CPUState saved = cpu;
  Check(ExtraCycles(cpu, env) == 14 * QUANTUM, "Expected bounded extra quanta.");
  Check(std::memcmp(&cpu, &saved, sizeof(cpu)) == 0, "Planning must not modify CPU state.");
  const auto reject_env = [&](auto edit) {
    auto other = env;
    edit(other);
    Check(ExtraCycles(cpu, other) == 0, "An environment guard accepted an unsupported profile.");
  };
  reject_env([](auto& e) { e.enabled = false; });
  reject_env([](auto& e) { e.module_active = false; });
  reject_env([](auto& e) { e.running = false; });
  reject_env([](auto& e) { e.lockstep = true; });
  reject_env([](auto& e) { e.chunk_verified = false; });
  reject_env([](auto& e) { e.chunk_has_hooks = true; });
  reject_env([](auto& e) { e.forced_fallback = true; });
  for (unsigned bit = 0; bit != 32; ++bit)
    reject_env([bit](auto& e) { e.pending_exceptions = 1u << bit; });
  reject_env([](auto& e) { e.generic_idle_pc = LOOP_PC; });
  reject_env([](auto& e) { e.game_id = "GDJPB2"; });
  reject_env([](auto& e) { e.game_id = {}; });
  reject_env([](auto& e) { e.dispatch_address = 0x8025f004; });
  reject_env([](auto& e) { e.dispatch_charge = 3; });
  reject_env([](auto& e) { e.module = nullptr; });
  reject_env([](auto& e) { e.chunk_index = 1; });
  for (const s32 remaining : {std::numeric_limits<s32>::min(), -1, 0, 258, 515, 516})
    reject_env([remaining](auto& e) { e.downcount = remaining; });

  const auto reject_cpu = [&](auto edit) {
    auto other = cpu;
    edit(other);
    Check(ExtraCycles(other, env) == 0, "A CPU guard accepted an unsupported state.");
  };
  for (const u32 bit : {PPC_MSR_EE, PPC_MSR_IR, PPC_MSR_DR})
    reject_cpu([bit](auto& c) { c.msr &= ~bit; });
  for (const u32 bit : {PPC_MSR_POW, PPC_MSR_ILE, PPC_MSR_PR, PPC_MSR_SE, PPC_MSR_BE,
                       PPC_MSR_IP, PPC_MSR_PM, PPC_MSR_LE})
    reject_cpu([bit](auto& c) { c.msr |= bit; });
  for (const unsigned reg : {0u, 13u, 30u, 31u})
    reject_cpu([reg](auto& c) { ++c.gpr[reg]; });
  reject_cpu([](auto& c) { ++c.lr; });
  reject_cpu([](auto& c) { ++c.pc; });
  reject_cpu([](auto& c) { c.cr ^= 0x10000000u; });
  reject_cpu([](auto& c) { c.downcount = 0; });
  reject_cpu([](auto& c) { c.exception = 1; });
  reject_cpu([](auto& c) { c.program_exception = 1; });
  reject_cpu([](auto& c) { c.ram = nullptr; });
  reject_cpu([](auto& c) { c.ram_size = 0x00565f13u; });
  for (const u32 address : {LOOP_PC, LOOP_PC + 4, LOOP_PC + 8, 0x80565f10u,
                            0x800000e4u, 0x800000d4u, 0x800000c0u})
  {
    const u32 word = ReadWord(cpu, address);
    Store(cpu, address, word ^ 1);
    Check(ExtraCycles(cpu, env) == 0, "Live code/scheduler mutation must reject.");
    Store(cpu, address, word);
  }
  const auto reject_module = [&](auto edit) {
    auto other = module;
    edit(other);
    auto e = env;
    e.module = &other;
    Check(ExtraCycles(cpu, e) == 0, "Module identity guard failed.");
  };
  reject_module([](auto& m) { m.game_id[5] = '3'; });
  reject_module([](auto& m) { ++m.entry_point; });
  reject_module([](auto& m) { m.num_rel_modules = 1; });
  reject_module([](auto& m) { m.chunk_ranges = nullptr; });
  reject_module([](auto& m) { m.chunk_hashes = nullptr; });
  hash ^= 1;
  Check(ExtraCycles(cpu, env) == 0, "Modified executable hash must reject.");
  hash = CHUNK_HASH;
  ++range.end;
  Check(ExtraCycles(cpu, env) == 0, "Different chunk layout must reject.");
  range.end = CHUNK_END;

  // A required GPU wait must finish before a fresh decision. Pending work,
  // invalidation, context changes or a shortened slice revoke the first plan.
  unsigned waits = 0;
  const auto synchronize_test = [&](auto mutate) {
    auto e = env;
    auto c = cpu;
    unsigned reads = 0;
    const auto extra = SynchronizedExtraCycles(true,
        [&]() { ++reads; return ExtraCycles(c, e); },
        [&]() { ++waits; mutate(c, e); });
    Check(extra == 0 && reads == 2, "Synchronization reused stale permission or charge.");
  };
  synchronize_test([](auto&, auto& e) { e.pending_exceptions = 1; });
  synchronize_test([](auto&, auto& e) { e.running = false; });
  synchronize_test([](auto&, auto& e) { e.chunk_verified = false; });
  synchronize_test([](auto&, auto& e) { e.chunk_has_hooks = true; });
  synchronize_test([](auto&, auto& e) { e.module = nullptr; });
  synchronize_test([](auto&, auto& e) { e.downcount = 516; });
  synchronize_test([](auto& c, auto&) { c.msr &= ~PPC_MSR_EE; });
  synchronize_test([](auto& c, auto&) { Store(c, 0x80565f10u, 1); });
  Store(cpu, 0x80565f10u, 0);
  synchronize_test([](auto& c, auto&) { Store(c, LOOP_PC, 0); });
  Store(cpu, LOOP_PC, 0x800d1010u);
  const unsigned prior_waits = waits;
  Check(SynchronizedExtraCycles(false, [&]() { return ExtraCycles(cpu, env); },
                                [&]() { ++waits; }) != 0 && waits == prior_waits,
        "Disabled synchronization unexpectedly waited.");
  auto disabled = env;
  disabled.enabled = false;
  Check(SynchronizedExtraCycles(true, [&]() { return ExtraCycles(cpu, disabled); },
                                [&]() { ++waits; }) == 0 && waits == prior_waits,
        "Rejected candidates must not wait for GPU work.");

  // Compare raw instruction execution, actual emitted C, and the optimized
  // charge path for nonmultiple slice lengths, both SO values, all TB residues,
  // and timebase wrap. Compare every byte of CPUState and the timing endpoint.
  for (unsigned trial = 0; trial != 2048; ++trial)
  {
    auto reference = cpu;
    reference.downcount = 0;
    reference.gpr[0] = trial ^ 0xfeed0000u;
    reference.xer = trial & 1 ? 0x80000000u : 0;
    reference.timebase = trial & 2 ? std::numeric_limits<u64>::max() - 64 : trial * 17u;
    auto native = reference;
    auto optimized = reference;
    const s32 slice = static_cast<s32>(1 + (trial * 7919u) % 65536u);
    Timing rt{slice, trial % 12u}, nt = rt, ot = rt;
    while (rt.downcount > 0)
    {
      Original(reference);
      Charge(reference, rt);
    }
    while (nt.downcount > 0)
    {
      loop_80261558(&native);
      Charge(native, nt);
    }
    while (ot.downcount > 0)
    {
      loop_80261558(&optimized);
      auto e = env;
      e.downcount = ot.downcount;
      Charge(optimized, ot, ExtraCycles(optimized, e));
    }
    Check(std::memcmp(&reference, &native, sizeof(cpu)) == 0, "Generated C differs from original instructions.");
    Check(std::memcmp(&reference, &optimized, sizeof(cpu)) == 0, "Elision changes architectural state.");
    Check(rt.downcount == ot.downcount && rt.remainder == ot.remainder && rt.cycles == ot.cycles,
          "Elision changes rounded timing boundary.");
    // The next timing event publishes a ready thread; all profiles run the
    // same real load and exit the loop after three cycles before disable-EE.
    Store(cpu, 0x80565f10u, 1u << (trial % 32));
    Original(reference);
    loop_80261558(&optimized);
    Check(std::memcmp(&reference, &optimized, sizeof(cpu)) == 0 && optimized.pc == LOOP_PC + 12,
          "Wakeup must leave the loop with the original prefix.");
    Check(ExtraCycles(optimized, env) == 0, "A nonempty queue must not elide.");
    Store(cpu, 0x80565f10u, 0);
  }
  auto large = env;
  large.downcount = std::numeric_limits<s32>::max();
  const u64 extra = ExtraCycles(cpu, large);
  Check(extra % 258 == 0 && extra + 258 < static_cast<u64>(large.downcount) &&
            static_cast<u64>(large.downcount) - extra - 258 <= 258,
        "Maximum signed downcount must leave one real quantum without overflow.");
  std::cout << "DW4 idle policy: " << checks << " checks, " << original_instructions
            << " original instructions, zero differences.\n";
  if (argc > 1)
  {
    std::ofstream report(argv[1]);
    report << "{\"passed\":true,\"checks\":" << checks << ",\"original_instructions\":"
           << original_instructions << ",\"equivalence_cases\":2048,\"runtime_launched\":false}\n";
  }
  return 0;
}
catch (const std::exception& error)
{
  std::cerr << error.what() << '\n';
  return 1;
}
