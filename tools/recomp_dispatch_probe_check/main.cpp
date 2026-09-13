#include "Core/PowerPC/StaticRecomp/StaticRecompDispatchProbe.h"

#include <cstdio>
#include <cstdlib>
#include <limits>

namespace P = StaticRecompDispatchProbe;
static unsigned checks = 0;
static void Check(bool condition, const char* message)
{
  ++checks;
  if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
struct Fake
{
  P::u64 wall = 1000, cycles = 100, user = 10, kernel = 20;
  P::u64 wall_step = 0, cycle_step = 0;
  P::u32 thread = 7, cycle_thread = 7, cpu_thread = 7;
  unsigned calls = 0, wall_calls = 0, cycle_calls = 0, cpu_calls = 0;
  bool wall_ok = true, cycles_ok = true, cpu_ok = true;
};
struct Reader
{
  Fake* f;
  P::Counter Wall() const noexcept
  {
    ++f->calls; ++f->wall_calls;
    const auto value = f->wall; f->wall += f->wall_step;
    return {f->wall_ok, value, 0, f->wall_ok ? 0u : 91u};
  }
  P::u32 ThreadId() const noexcept { ++f->calls; return f->thread; }
  P::Counter Cycles() const noexcept
  {
    ++f->calls; ++f->cycle_calls;
    const auto value = f->cycles; f->cycles += f->cycle_step;
    return {f->cycles_ok, value, f->cycle_thread, f->cycles_ok ? 0u : 92u};
  }
  P::CpuTimes Cpu() const noexcept
  {
    ++f->calls; ++f->cpu_calls;
    return {f->cpu_ok, f->user, f->kernel, f->cpu_thread, f->cpu_ok ? 0u : 93u};
  }
};
using Probe = P::Probe<Reader, 4>;
static P::Config Config(P::u64 delay = 0, P::u32 stride = 1)
{
  return {true, delay, 1000, stride, 0x12345678u};
}
static const P::Entry* Site(const Probe& p, P::u32 key)
{
  for (const auto& e : p.GetReport().entries) if (e.used && e.pc == key) return &e;
  return nullptr;
}

int main()
{
  {
    Fake f; Probe p({}, Reader{&f});
    p.BeginWindow(); p.Poll(); Check(!p.BeginSample(0), "disabled selection");
    p.EndSample(); p.FinishWindow();
    Check(f.calls == 0 && p.GetReport().state == P::State::Disabled, "disabled path reads clocks");
    Check(p.Configure(Config()), "pre-window configuration rejected");
    p.BeginWindow(); Check(!p.Configure({}), "running configuration accepted");
  }
  for (const P::Config config : {P::Config{true, 0, 0, 1, 1},
       P::Config{true, 0, 31 * P::NS_PER_SECOND, 1, 1},
       P::Config{true, 91 * P::NS_PER_SECOND, 1, 1, 1},
       P::Config{true, 0, 1000, 0, 1}, P::Config{true, 0, 1000, (1u << 20) + 1, 1}})
  {
    Fake f; Probe p(config, Reader{&f}); p.BeginWindow(); p.Poll();
    Check(p.GetReport().state == P::State::Invalid && f.calls == 0, "invalid configuration used clocks");
  }
  {
    Fake f; Probe p(Config(100), Reader{&f}); p.BeginWindow();
    f.wall = 1099; p.Poll(); Check(!p.IsActive(), "early window");
    const auto before_reentry = f.calls; p.BeginWindow();
    Check(f.calls == before_reentry && p.GetReport().origin_ns == 1000, "Run reentry reset origin");
    f.wall = 1100; p.Poll();
    Check(p.IsActive() && p.GetReport().calibration.attempts == 16, "window/calibration did not start");
    Check(p.GetReport().calibration.zero_cycle_pairs == 16, "quantized calibration lost zeros");
    Check(p.BeginSample(0), "reserved zero key rejected");
    f.wall += 20; f.cycles += 50; p.EndSample();
    Check(Site(p, 0) && Site(p, 0)->samples == 1 && Site(p, 0)->wall_ns == 20 && Site(p, 0)->cycles == 50, "sample deltas/key");
    Check(p.BeginSample(0), "second sample absent"); p.EndSample();
    Check(Site(p, 0)->zero_cycle_samples == 1 && p.GetReport().zero_wall_samples == 1, "valid zero deltas discarded");
    f.wall = 2107; f.user += 100; f.kernel += 30; p.Poll();
    Check(p.GetReport().state == P::State::Completed && p.GetReport().cutoff_reached, "cutoff did not complete");
    Check(p.GetReport().cpu_available && p.GetReport().cpu_100ns == 130 && p.GetReport().cutoff_overshoot_ns == 7, "whole-window CPU/overshoot");
    const auto finished_calls = f.calls; p.Poll(); p.BeginWindow(); p.FinishWindow();
    Check(!p.BeginSample(0) && f.calls == finished_calls, "completed probe kept reading clocks");
  }
  {
    Fake f; Probe p(Config(), Reader{&f}); p.BeginWindow(); f.wall = 2000; p.Poll();
    Check(p.GetReport().window_missed && !p.GetReport().cpu_available && f.cpu_calls == 0, "late poll fabricated CPU window");
  }
  {
    Fake f; Probe p(Config(), Reader{&f}); p.BeginWindow(); p.Poll();
    Check(p.BeginSample(8), "cutoff sample absent"); f.wall = 2001; p.EndSample();
    Check(p.GetReport().crossing_cutoff == 1 && p.GetReport().accepted == 0 && !Site(p, 8), "cross-cutoff sample attributed");
  }
  {
    Fake f; Probe p(Config(), Reader{&f}); p.BeginWindow(); p.Poll();
    Check(p.BeginSample(1) && !p.BeginSample(2), "nested sample replaced outer sample");
    f.wall += 2; p.EndSample(); p.EndSample();
    Check(p.GetReport().nested_rejections == 1 && p.GetReport().unmatched_ends == 1 && Site(p, 1) && !Site(p, 2), "nested/end state");
    for (P::u32 key = 2; key <= 5; ++key) { Check(p.BeginSample(key), "table sample absent"); ++f.wall; p.EndSample(); }
    Check(p.GetReport().accepted == 5 && p.GetReport().table_overflow_samples == 1 && !Site(p, 5), "bounded table overflow");
    Check(p.BeginSample(1), "existing table key unavailable"); p.EndSample();
    Check(Site(p, 1)->samples == 2 && p.GetReport().table_overflow_samples == 1, "overflow blocked existing key");
  }
  {
    Fake f; Probe p(Config(), Reader{&f}); p.BeginWindow(); p.Poll();
    f.cycles_ok = false; Check(p.BeginSample(1), "cycle failure lost available wall interval"); f.wall += 2; p.EndSample();
    Check(Site(p, 1)->samples == 1 && Site(p, 1)->cycle_samples == 0 && Site(p, 1)->cycle_failures == 1 && p.GetReport().cycle_failures == 2, "cycle failure became zero CPU");
    f.cpu_ok = false; f.wall = 2000; p.Poll();
    Check(!p.GetReport().cpu_available && p.GetReport().cpu_failures == 1, "failed CPU read became valid");
  }
  {
    Fake f; Probe p(Config(), Reader{&f}); p.BeginWindow(); p.Poll();
    Check(p.BeginSample(1), "reset sample absent"); --f.cycles; p.EndSample();
    Check(p.GetReport().counter_resets == 1 && Site(p, 1)->cycle_samples == 0, "cycle reset wrapped");
    --f.user; f.wall = 2000; p.Poll();
    Check(!p.GetReport().cpu_available && p.GetReport().counter_resets == 2, "CPU reset wrapped");
  }
  {
    Fake f; Probe p(Config(), Reader{&f}); p.BeginWindow(); p.Poll();
    Check(p.BeginSample(1), "wall reset sample absent"); --f.wall; p.EndSample();
    Check(p.GetReport().state == P::State::Invalid && p.GetReport().accepted == 0, "backward wall time accepted");
  }
  for (const bool during_calibration : {false, true})
  {
    Fake f; Probe p(Config(), Reader{&f}); p.BeginWindow();
    if (during_calibration) f.cycle_thread = 9; else f.cpu_thread = 9;
    p.Poll();
    Check(p.GetReport().state == P::State::Invalid && !p.IsActive(), "Poll resurrected invalid owner");
  }
  {
    Fake f; Probe p(Config(), Reader{&f}); p.BeginWindow(); p.Poll();
    f.thread = 9; p.Poll();
    Check(p.GetReport().state == P::State::Invalid && p.GetReport().thread_mismatches == 1, "owner thread changed");
  }
  {
    Fake f; f.wall = (std::numeric_limits<P::u64>::max)() - 2;
    Probe p(Config(), Reader{&f}); p.BeginWindow();
    Check(p.GetReport().state == P::State::Invalid, "window addition overflow");
  }
  {
    Fake f; Probe p(Config(), Reader{&f}); p.BeginWindow(); p.Poll(); f.wall = 1300; p.FinishWindow();
    Check(p.GetReport().state == P::State::Completed && !p.GetReport().cutoff_reached && p.GetReport().observed_end_ns == 1300, "early finish claimed complete duration");
  }
  {
    Fake a, b; auto config = Config(0, 5); config.duration_ns = 1000000;
    Probe p(config, Reader{&a}), q(config, Reader{&b}); p.BeginWindow(); q.BeginWindow(); p.Poll(); q.Poll();
    unsigned last = 0, previous_gap = 0, distinct_gaps = 0, selected = 0;
    for (unsigned i = 1; i <= 300; ++i)
    {
      const bool x = p.BeginSample(0), y = q.BeginSample(0);
      Check(x == y, "seeded sampling was not reproducible");
      if (x)
      {
        const unsigned gap = i-last;
        Check(gap >= 1 && gap <= 9, "jitter gap outside bound");
        if (previous_gap && gap != previous_gap) ++distinct_gaps;
        previous_gap = gap; last = i; ++selected;
        ++a.wall; ++b.wall; p.EndSample(); q.EndSample();
      }
    }
    Check(selected > 20 && distinct_gaps > 0 && p.GetReport().opportunities == 300, "sampling reverted to periodic stride");
  }
#ifdef _WIN32
  // A few reads of this checker thread only. No workload, sleep, game or device.
  P::WindowsReader reader;
  const auto w0 = reader.Wall(), c0 = reader.Cycles(); const auto cpu = reader.Cpu();
  const auto c1 = reader.Cycles(), w1 = reader.Wall();
  Check(w0.valid && w1.valid && w1.value >= w0.value, "Windows steady clock unavailable");
  Check(c0.valid && c1.valid && c1.value >= c0.value && c0.thread_id == reader.ThreadId(), "owned-thread cycles unavailable");
  Check(cpu.valid && cpu.thread_id == reader.ThreadId(), "owned-thread CPU times unavailable");
#endif
  std::printf("PASS: %u dispatch probe checks; no game, audio, ETW or service launched.\n", checks);
}
