// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Only the services touched by the extracted, unchanged Interpreter cache
// functions are replaced. No game, live MMU, JIT or device is initialized.
namespace InterpreterFixture
{
using UGeckoInstruction = u32;
enum class ProgramExceptionCause { PrivilegedInstruction };
struct Effects
{
  std::vector<u32> jit_lines;
  std::vector<u32> flushed;
  std::vector<u32> invalidated;
  std::vector<u32> stored;
  bool privileged_exception = false;
};
struct State
{
  const CPUState& cpu;
  bool m_enable_dcache;
  struct { bool PR; } msr;
  Effects& effects;
};
inline u32 Helper_Get_EA_X(const State& state, UGeckoInstruction instruction)
{
  const u32 ra = (instruction >> 16) & 31;
  const u32 rb = (instruction >> 11) & 31;
  return (ra ? state.cpu.gpr[ra] : 0u) + state.cpu.gpr[rb];
}
inline void GenerateProgramException(State& state, ProgramExceptionCause)
{
  state.effects.privileged_exception = true;
}
struct System
{
  Effects& effects;
  System& GetJitInterface() { return *this; }
  void InvalidateICacheLine(u32 address) { effects.jit_lines.push_back(address & ~31u); }
};
struct MMU
{
  Effects& effects;
  void FlushDCacheLine(u32 address) { effects.flushed.push_back(address); }
  void InvalidateDCacheLine(u32 address) { effects.invalidated.push_back(address); }
  void StoreDCacheLine(u32 address) { effects.stored.push_back(address); }
};
struct Interpreter
{
  State m_ppc_state;
  System m_system;
  MMU m_mmu;
  static void dcbf(Interpreter&, UGeckoInstruction);
  static void dcbi(Interpreter&, UGeckoInstruction);
  static void dcbst(Interpreter&, UGeckoInstruction);
};

#include "generated_interpreter.inc"

inline Effects Execute(const CPUState& cpu, u32 instruction, bool dcache_enabled)
{
  Effects effects;
  Interpreter interpreter{{cpu, dcache_enabled, {(cpu.msr & PPC_MSR_PR) != 0}, effects},
                          {effects}, {effects}};
  switch (instruction)
  {
  case 0x7c00186cu: Interpreter::dcbst(interpreter, instruction); break;
  case 0x7c0018acu: Interpreter::dcbf(interpreter, instruction); break;
  case 0x7c001bacu: Interpreter::dcbi(interpreter, instruction); break;
  default: throw std::runtime_error("Unsupported original cache instruction.");
  }
  return effects;
}
}  // namespace InterpreterFixture
