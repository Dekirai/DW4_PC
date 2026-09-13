// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include "core/cpu.h"
#include <immintrin.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

extern "C" {
bool baseline_ppc_psq_store(CPUState*, u8, u32, bool, u8, bool, u32);
bool current_ppc_psq_store(CPUState*, u8, u32, bool, u8, bool, u32);
bool current_inline_store(CPUState*, u8, u32, bool, u8, bool, u32);
void baseline_set_journal(PPCMemWriteJournal, void*);
void current_set_journal(PPCMemWriteJournal, void*);
void timing_inline(CPUState*, const f64*, u32);
void timing_outline(CPUState*, const f64*, u32);
}

namespace {
constexpr unsigned sticky_mask = 0x3f;
constexpr unsigned environment_mask = 0x6000 | 0x8000 | 0x40;
constexpr unsigned exception_masks = 0x1f80;
constexpr u32 memory_size = 128;
constexpr u8 source_register = 7;
constexpr u8 gqr_index = 0xfc; // Exercises the real index mask, selecting GQR4.
constexpr u32 instruction_address = 0x81234564;
using Store = bool (*)(CPUState*, u8, u32, bool, u8, bool, u32);

struct Event {
    u32 kind, address, size, reserve_valid, gqr, fpscr, mxcsr;
    u64 value, lane_one;
};
struct Fixture {
    CPUState cpu;
    std::array<u8, memory_size> ram, exram;
    std::array<Event, 4> trace;
    u32 trace_size;
    bool overflow;
    bool mutate;
    u64 mutation_bits;
};
struct Case {
    u8 type;
    s32 scale;
    bool w, indexed, lsqe;
    u32 profile, reserve_mode, initial_csr;
    u64 first, second, mutation;
    bool mutate;
};
struct Outcome { bool success; u32 csr; };
struct Counters {
    unsigned long long cases = 0, comparisons = 0, failures = 0;
    unsigned long long illegal = 0, invalid = 0, mutations = 0, trace_events = 0;
    unsigned long long clean_cases = 0, sticky_cases = 0, address_cases = 0;
};

void record(Fixture& fixture, u32 kind, u32 address, u64 value, u32 size) {
    if (fixture.trace_size >= fixture.trace.size()) { fixture.overflow = true; return; }
    Event& event = fixture.trace[fixture.trace_size++];
    event.kind = kind;
    event.address = address;
    event.size = size;
    event.value = value;
    event.reserve_valid = fixture.cpu.reserve_valid;
    event.gqr = fixture.cpu.gqr[gqr_index & 7];
    event.fpscr = fixture.cpu.fpscr;
    event.lane_one = f64_bits(fixture.cpu.ps1[source_register]);
    event.mxcsr = _mm_getcsr();
    if (fixture.mutate && fixture.trace_size == 1) {
        // Both callback kinds change the live second lane and GQR. The second
        // store must read this lane after the first callback, using old GQR.
        std::memcpy(&fixture.cpu.ps1[source_register], &fixture.mutation_bits, sizeof(f64));
        fixture.cpu.gqr[gqr_index & 7] = 0xe5313e05u;
        fixture.cpu.fpscr ^= 0x01020304u;
    }
}
void external_write(CPUState* cpu, u32 address, u64 value, u8 size) {
    auto& fixture = *static_cast<Fixture*>(cpu->external_user_data);
    record(fixture, 2, address, value, size);
}
void journal(u32 offset, u32 size, void* user) {
    auto& fixture = *static_cast<Fixture*>(user);
    if (offset > memory_size || size > memory_size - offset || size > 8) {
        fixture.overflow = true;
        return;
    }
    u64 preimage = 0;
    for (u32 i = 0; i < size; ++i) preimage = (preimage << 8) | fixture.ram[offset + i];
    record(fixture, 1, offset, preimage, size);
}

constexpr u32 addresses[] = {
    0x80000020, 0xc0000020, 0x8000001f, 0x8000007f,
    0x90000020, 0xd0000020, 0xe0000103, 0xffffffff,
    0x7fffffff, 0x8fffffff, 0xe0000103, 0x80000020,
    0x90000020, 0xbfffffff
};
constexpr u32 profile_count = static_cast<u32>(std::size(addresses));

CPUState seed_cpu() {
    CPUState cpu{};
    for (u32 i = 0; i < 32; ++i) {
        cpu.gpr[i] = 0x10203040u ^ (i * 0x01010101u);
        cpu.fpr[i] = static_cast<f64>(i) + 0.25;
        cpu.ps1[i] = static_cast<f64>(i) + 0.75;
    }
    for (u32 i = 0; i < 16; ++i) cpu.sr[i] = 0x90807060u + i;
    for (u32 i = 0; i < 8; ++i) cpu.gqr[i] = 0x43210000u + i;
    for (u32 i = 0; i < 512; ++i) {
        cpu.locked_cache_tag[i] = 0xabc00000u + 32u * i;
        cpu.locked_cache_valid[i] = (i & 1) != 0;
    }
    cpu.pc = 0x87654320; cpu.lr = 0x81230100; cpu.ctr = 0x1234;
    cpu.cr = 0x67ab1234; cpu.xer = 0xe0000005; cpu.fpscr = 0x5aa55aa5;
    cpu.msr = 0xffffffff; cpu.srr0 = 0x11112222; cpu.srr1 = 0x33334444;
    cpu.dar = 0x55556666; cpu.dsisr = 0x77778888; cpu.ear = 0x9999aaaa;
    cpu.hid2 = 0x0f3f5f7f; cpu.timebase = 0x123456789abcdef0ULL;
    cpu.exception = PPC_EXC_DSI; cpu.program_exception = PPC_PROGRAM_PRIV;
    cpu.tlb_last_vps = 13; cpu.tlb_last_index = 17; cpu.tlb_invalidate_count = 23;
    cpu.external_addr = 0xc1234567; cpu.external_value = 0x89abcdef;
    cpu.external_rid = 12; cpu.external_read_count = 21; cpu.external_write_count = 37;
    cpu.downcount = -123456789;
    return cpu;
}

void prepare(Fixture& fixture, const CPUState& seed, const Case& test) {
    std::memcpy(&fixture.cpu, &seed, sizeof(seed));
    fixture.ram.fill(0xa5); fixture.exram.fill(0x5a);
    std::memset(fixture.trace.data(), 0, sizeof(fixture.trace));
    fixture.trace_size = 0; fixture.overflow = false;
    fixture.mutate = test.mutate; fixture.mutation_bits = test.mutation;
    fixture.cpu.ram = fixture.ram.data(); fixture.cpu.ram_size = memory_size;
    fixture.cpu.exram = test.profile == 12 ? nullptr : fixture.exram.data();
    fixture.cpu.exram_size = memory_size;
    fixture.cpu.external_write = test.profile == 10 ? nullptr : external_write;
    fixture.cpu.external_user_data = &fixture;
    fixture.cpu.gqr[gqr_index & 7] = 0xa56ac0f8u | test.type
        | ((static_cast<u32>(test.scale) & 63u) << 8);
    // Remove store-scale bits from the sentinel before setting the exact scale.
    fixture.cpu.gqr[gqr_index & 7] &= ~0x3f00u;
    fixture.cpu.gqr[gqr_index & 7] |= (static_cast<u32>(test.scale) & 63u) << 8;
    fixture.cpu.hid2 = (fixture.cpu.hid2 & ~PPC_HID2_LSQE) | (test.lsqe ? PPC_HID2_LSQE : 0);
    fixture.cpu.reserve_valid = test.reserve_mode != 3;
    fixture.cpu.reserve_addr = test.reserve_mode == 0 ? addresses[test.profile]
        : test.reserve_mode == 1 ? addresses[test.profile] + 1u
        : 0x82345660u;
    std::memcpy(&fixture.cpu.fpr[source_register], &test.first, sizeof(f64));
    std::memcpy(&fixture.cpu.ps1[source_register], &test.second, sizeof(f64));
}

Outcome execute(Fixture& fixture, const Case& test, Store store, bool baseline) {
    const auto callback = test.profile == 11 ? nullptr : journal;
    if (baseline) baseline_set_journal(callback, &fixture);
    else current_set_journal(callback, &fixture);
    _mm_setcsr(test.initial_csr);
    const bool success = store(&fixture.cpu, source_register, addresses[test.profile],
                              test.w, gqr_index, test.indexed, instruction_address);
    return {success, _mm_getcsr()};
}

bool equal_cpu(const CPUState& first, const CPUState& second) {
    CPUState a, b;
    std::memcpy(&a, &first, sizeof(a)); std::memcpy(&b, &second, sizeof(b));
    // Only allocation identity differs. Presence, sizes and all CPU bytes,
    // including callback function pointers and FPSCR, remain compared.
    a.ram = b.ram = nullptr;
    if ((a.exram == nullptr) != (b.exram == nullptr)) return false;
    a.exram = b.exram = nullptr;
    a.external_user_data = b.external_user_data = nullptr;
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

bool check(Counters& counts, const CPUState& seed, const Case& test) {
    Fixture reference, outline, inlined;
    prepare(reference, seed, test); prepare(outline, seed, test); prepare(inlined, seed, test);
    const Outcome expected = execute(reference, test, baseline_ppc_psq_store, true);
    const Outcome actual_outline = execute(outline, test, current_ppc_psq_store, false);
    const Outcome actual_inline = execute(inlined, test, current_inline_store, false);
    ++counts.cases;
    counts.illegal += !test.indexed && !test.lsqe;
    counts.invalid += test.type >= 1 && test.type <= 3;
    counts.mutations += reference.mutate && reference.trace_size != 0;
    counts.trace_events += reference.trace_size;
    bool all_passed = true;
    const auto compare = [&](const Fixture& actual, Outcome outcome, const char* label) {
        ++counts.comparisons;
        const bool cpu_ok = equal_cpu(reference.cpu, actual.cpu);
        const bool memory_ok = reference.ram == actual.ram && reference.exram == actual.exram;
        const bool trace_ok = reference.trace_size == actual.trace_size && !reference.overflow
            && !actual.overflow && std::memcmp(reference.trace.data(), actual.trace.data(), sizeof(reference.trace)) == 0;
        const bool passed = expected.success == outcome.success && expected.csr == outcome.csr
            && cpu_ok && memory_ok && trace_ok;
        if (!passed) {
            ++counts.failures; all_passed = false;
            if (counts.failures <= 12) std::fprintf(stderr,
                "%s mismatch type=%u scale=%d W=%u indexed=%u LSQE=%u profile=%u reserve=%u CSR=%04x lanes=%016llx,%016llx mutate=%u; cpu=%u memory=%u trace=%u result=%u/%u CSR=%04x/%04x\n",
                label, test.type, test.scale, test.w, test.indexed, test.lsqe, test.profile,
                test.reserve_mode, test.initial_csr, test.first, test.second, test.mutate,
                cpu_ok, memory_ok, trace_ok, expected.success, outcome.success, expected.csr, outcome.csr);
        }
    };
    compare(outline, actual_outline, "out-of-line");
    compare(inlined, actual_inline, "inline");
    return all_passed;
}

std::vector<u64> corpus(u8 type, s32 scale) {
    std::vector<u64> values;
    constexpr u64 raw[] = {
        0, 1, 0x000fffffffffffffULL, 0x0010000000000000ULL,
        0x7fefffffffffffffULL, 0x7ff0000000000000ULL,
        0x7ff8000000000000ULL, 0x7ff8000000000123ULL,
        0x7fffffffffffffffULL, 0x7ff0000000000001ULL, 0x7ff4000000000000ULL
    };
    for (u64 value : raw) { values.push_back(value); values.push_back(value | 0x8000000000000000ULL); }
    const auto neighbors = [&](f64 value) {
        values.push_back(f64_bits(value));
        values.push_back(f64_bits(std::nextafter(value, -std::numeric_limits<f64>::infinity())));
        values.push_back(f64_bits(std::nextafter(value, std::numeric_limits<f64>::infinity())));
    };
    for (u32 bits : {1u, 0x007fffffu, 0x00800000u, 0x3f000000u, 0x3f800000u, 0x7f7fffffu}) {
        neighbors(static_cast<f64>(f32_value(bits)));
        neighbors(-static_cast<f64>(f32_value(bits)));
    }
    const f64 low = type == 6 ? -128.0 : type == 7 ? -32768.0 : 0.0;
    const f64 high = type == 5 ? 65535.0 : type == 6 ? 127.0 : type == 7 ? 32767.0 : 255.0;
    for (f64 boundary : {low, high, -1.5, -0.5, 0.5, 1.5}) {
        neighbors(std::ldexp(boundary, -scale));
        const f32 value = static_cast<f32>(boundary);
        neighbors(std::ldexp(static_cast<f64>(std::nextafter(value, -std::numeric_limits<f32>::infinity())), -scale));
        neighbors(std::ldexp(static_cast<f64>(std::nextafter(value, std::numeric_limits<f32>::infinity())), -scale));
    }
    u64 state = 0xf18034546782abcdULL ^ static_cast<u64>(scale + 32) ^ (static_cast<u64>(type) << 32);
    for (u32 i = 0; i < 12; ++i) {
        state ^= state >> 12; state ^= state << 25; state ^= state >> 27;
        values.push_back(state * 0x2545f4914f6cdd1dULL);
    }
    return values;
}

bool order_oracle(const CPUState& seed, unsigned csr) {
    Case test{4, 0, false, false, true, 0, 0, csr,
              f64_bits(12.75), f64_bits(17.75), f64_bits(91.75), true};
    for (u32 profile : {0u, 6u, 7u}) {
        test.profile = profile;
        for (Store store : {baseline_ppc_psq_store, current_ppc_psq_store, current_inline_store}) {
            Fixture fixture;
            prepare(fixture, seed, test);
            const Outcome result = execute(fixture, test, store, store == baseline_ppc_psq_store);
            if (!result.success || fixture.trace_size != 2 || fixture.overflow) return false;
            const u32 first_address = profile == 0 ? 0x20u : addresses[profile];
            if (fixture.trace[0].address != first_address || fixture.trace[1].address != first_address + 1u
                || fixture.trace[0].size != 1 || fixture.trace[1].size != 1
                || fixture.trace[0].lane_one != test.second || fixture.trace[1].lane_one != test.mutation
                || fixture.trace[1].gqr != 0xe5313e05u) return false;
            if (profile == 0) {
                if (fixture.ram[32] != 12 || fixture.ram[33] != 91 || fixture.cpu.reserve_valid
                    || fixture.trace[0].reserve_valid || fixture.trace[0].value != 0xa5
                    || fixture.trace[1].value != 0xa5) return false;
            } else if (fixture.trace[0].value != 12 || fixture.trace[1].value != 91) return false;
        }
    }
    return true;
}

struct Timing {
    double inline_ns, outline_ns, inline_min_ns, inline_max_ns, outline_min_ns, outline_max_ns;
    u64 checksum;
    bool passed;
    bool performed;
    bool concurrent_game;
};
volatile u64 timing_sink = 0;
bool game_is_running() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true; // Defer on unknown contention.
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    bool found = false;
    if (!Process32FirstW(snapshot, &process)) found = true;
    else do {
        if (_wcsicmp(process.szExeFile, L"DW4_PC.exe") == 0) { found = true; break; }
    } while (Process32NextW(snapshot, &process));
    CloseHandle(snapshot);
    return found;
}
Timing time_helpers(unsigned csr) {
    if (game_is_running()) return {0, 0, 0, 0, 0, 0, 0, true, false, true};
    CPUState inlined{}, outline{};
    std::array<u8, 8192> inline_ram{}, outline_ram{};
    std::array<f64, 256> inputs{};
    for (u32 i = 0; i < inputs.size(); ++i) inputs[i] = static_cast<f64>(i) + 0.375;
    for (CPUState* cpu : {&inlined, &outline}) {
        cpu->ram_size = 8192; cpu->gqr[4] = 0x00040004; cpu->hid2 = PPC_HID2_LSQE;
    }
    inlined.ram = inline_ram.data(); outline.ram = outline_ram.data();
    current_set_journal(nullptr, nullptr);
    _mm_setcsr(csr);
    timing_inline(&inlined, inputs.data(), 8192);
    timing_outline(&outline, inputs.data(), 8192);
    constexpr u32 iterations = 2000000;
    std::array<double, 9> inline_times{}, outline_times{};
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency)) return {};
    const auto measure = [&](bool use_inline) {
        LARGE_INTEGER start{}, end{};
        QueryPerformanceCounter(&start);
        if (use_inline) timing_inline(&inlined, inputs.data(), iterations);
        else timing_outline(&outline, inputs.data(), iterations);
        QueryPerformanceCounter(&end);
        u64 checksum = 0;
        const auto& memory = use_inline ? inline_ram : outline_ram;
        for (u8 byte : memory) checksum = (checksum * 131u) + byte;
        timing_sink = checksum;
        return static_cast<double>(end.QuadPart - start.QuadPart) * 1e9
            / static_cast<double>(frequency.QuadPart) / iterations;
    };
    for (u32 trial = 0; trial < inline_times.size(); ++trial) {
        if (trial & 1) { outline_times[trial] = measure(false); inline_times[trial] = measure(true); }
        else { inline_times[trial] = measure(true); outline_times[trial] = measure(false); }
    }
    std::sort(inline_times.begin(), inline_times.end());
    std::sort(outline_times.begin(), outline_times.end());
    return {inline_times[4], outline_times[4], inline_times.front(), inline_times.back(),
            outline_times.front(), outline_times.back(), timing_sink,
            inline_ram == outline_ram && equal_cpu(inlined, outline), true, game_is_running()};
}
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "Usage: recomp-psq-store-check.exe report.json\n"); return 2; }
    const unsigned original_csr = _mm_getcsr();
    const unsigned clean = (original_csr | exception_masks) & ~(sticky_mask | environment_mask);
    _mm_setcsr(clean);
    const CPUState seed = seed_cpu();
    Counters counts;
    const bool oracle_passed = order_oracle(seed, clean);
    if (!oracle_passed) { std::fprintf(stderr, "Independent callback ordering oracle failed.\n"); ++counts.failures; }
    for (u8 type = 0; type < 8; ++type) {
        for (s32 scale = -32; scale < 32; ++scale) {
            _mm_setcsr(clean);
            const auto values = corpus(type, scale);
            for (u32 environment = 0; environment < 16; ++environment) {
                const unsigned csr = clean | ((environment & 3u) << 13)
                    | ((environment & 4u) ? 0x8000u : 0) | ((environment & 8u) ? 0x40u : 0);
                for (u32 flags = 0; flags < 8; ++flags) {
                    for (u32 i = 0; i < values.size(); ++i) {
                        const u32 selection = i + static_cast<u32>(scale + 32) + type * 5u;
                        Case test{type, scale, (flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0,
                                  selection % profile_count, (selection / profile_count) % 4, csr,
                                  values[i], values[(i + 17) % values.size()],
                                  values[(i + 31) % values.size()], (i & 1) != 0};
                        check(counts, seed, test); ++counts.clean_cases;
                    }
                }
            }
        }
        std::printf("Completed type %u; %llu cases, %llu failures\n", type, counts.cases, counts.failures);
        std::fflush(stdout);
    }
    // Explicitly cross every address, reservation, mutation and control profile
    // with every store format/scale/enable state, beyond the rotating corpus.
    for (u8 type = 0; type < 8; ++type) for (s32 scale = -32; scale < 32; ++scale)
        for (u32 flags = 0; flags < 8; ++flags) for (u32 profile = 0; profile < profile_count; ++profile)
            for (u32 reserve = 0; reserve < 4; ++reserve) for (u32 mutation = 0; mutation < 2; ++mutation) {
                const Case test{type, scale, (flags & 1) != 0, (flags & 2) != 0, (flags & 4) != 0,
                    profile, reserve, clean, f64_bits(12.75), f64_bits(17.75), f64_bits(91.75), mutation != 0};
                check(counts, seed, test); ++counts.address_cases;
            }
    // Every combination of preexisting MXCSR status bits, including values
    // whose conversions generate new exceptions. Masks remain enabled.
    for (u32 status = 0; status < 64; ++status) for (u32 environment = 0; environment < 16; ++environment)
        for (u8 type = 0; type < 8; ++type) for (u32 flags = 0; flags < 8; ++flags) {
            const unsigned csr = clean | status | ((environment & 3u) << 13)
                | ((environment & 4u) ? 0x8000u : 0) | ((environment & 8u) ? 0x40u : 0);
            const Case test{type, static_cast<s32>(status) - 32, (flags & 1) != 0,
                (flags & 2) != 0, (flags & 4) != 0, status % profile_count, status % 4,
                csr, 0x7ff0000000000001ULL, 0x0000000000000001ULL, f64_bits(91.75), (status & 1) != 0};
            check(counts, seed, test); ++counts.sticky_cases;
        }
    _mm_setcsr(clean);
    const Timing timing = time_helpers(clean);
    if (!timing.passed) ++counts.failures;
    baseline_set_journal(nullptr, nullptr); current_set_journal(nullptr, nullptr);
    _mm_setcsr(original_csr);
    const unsigned restored_csr = _mm_getcsr();
    FILE* report = nullptr;
    if (fopen_s(&report, argv[1], "wb") != 0 || !report) {
        std::fprintf(stderr, "Cannot write report.\n"); return 2;
    }
    std::fprintf(report,
        "{\n  \"passed\": %s,\n  \"cases\": %llu,\n  \"comparisons\": %llu,\n  \"failures\": %llu,\n"
        "  \"clean_status_corpus_cases\": %llu,\n  \"address_reservation_cases\": %llu,\n  \"preexisting_status_cases\": %llu,\n"
        "  \"illegal_enable_cases\": %llu,\n  \"invalid_type_cases\": %llu,\n  \"callback_mutation_cases\": %llu,\n  \"baseline_trace_events\": %llu,\n"
        "  \"types\": 8,\n  \"scales\": 64,\n  \"w_indexed_lsqe_combinations\": 8,\n  \"rounding_ftz_daz_combinations\": 16,\n"
        "  \"address_profiles\": %u,\n  \"reservation_profiles\": 4,\n  \"initial_mxcsr_status_combinations\": 64,\n"
        "  \"cpu_state_bytes\": %zu,\n  \"memory_bytes_per_fixture\": %u,\n  \"ordered_callback_oracle_passed\": %s,\n"
        "  \"original_mxcsr\": %u,\n  \"restored_mxcsr\": %u,\n"
        "  \"synthetic_timing\": {\"performed\": %s, \"concurrent_game_detected_or_unknown\": %s, \"paired_stores_per_trial\": 2000000, \"alternating_trials\": 9, \"inline_median_ns_per_pair\": %.6f, \"outline_median_ns_per_pair\": %.6f, \"inline_min_ns_per_pair\": %.6f, \"inline_max_ns_per_pair\": %.6f, \"outline_min_ns_per_pair\": %.6f, \"outline_max_ns_per_pair\": %.6f, \"outline_over_inline_ratio\": %.6f, \"checksum\": \"%016llx\", \"state_memory_equal\": %s, \"game_fps_measurement\": false}\n}\n",
        counts.failures == 0 ? "true" : "false", counts.cases, counts.comparisons, counts.failures,
        counts.clean_cases, counts.address_cases, counts.sticky_cases, counts.illegal, counts.invalid,
        counts.mutations, counts.trace_events, profile_count, sizeof(CPUState), memory_size * 2,
        oracle_passed ? "true" : "false", original_csr, restored_csr,
        timing.performed ? "true" : "false", timing.concurrent_game ? "true" : "false",
        timing.inline_ns, timing.outline_ns, timing.inline_min_ns, timing.inline_max_ns,
        timing.outline_min_ns, timing.outline_max_ns, timing.performed ? timing.outline_ns / timing.inline_ns : 0,
        timing.checksum, !timing.performed ? "null" : timing.passed ? "true" : "false");
    std::fclose(report);
    std::printf("Paired-store differential: %llu cases, %llu comparisons, %llu failures.\n",
                counts.cases, counts.comparisons, counts.failures);
    if (timing.performed) std::printf("Synthetic actual-helper timing: inline %.3f ns/pair, outline %.3f ns/pair (%.3fx); not game FPS%s.\n",
                timing.inline_ns, timing.outline_ns, timing.outline_ns / timing.inline_ns,
                timing.concurrent_game ? "; contended, not acceptance evidence" : "");
    else std::printf("Synthetic timing deferred: a DW4_PC process is running or process inspection failed.\n");
    return counts.failures == 0 && restored_csr == original_csr ? 0 : 1;
}
