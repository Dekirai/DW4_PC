// The online CPU gate: the CRT's FMA3 choice computed from CPUID words, step
// for step as __acrt_initialize_fma3 (docs/SEAMLESS.md 6.12), and on this
// machine the same value the static CRT holds.
#include "online/cpu_gate.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::game::online;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

constexpr std::uint32_t kFma = 1u << 12, kOsxsave = 1u << 27, kAvx = 1u << 28, kAvx2 = 1u << 5;

void computed_values() {
    CpuFeatureWords modern{kFma | kOsxsave | kAvx, 0x7, 0x10, kAvx2};
    check(crt_fma3_value(modern) == 3, "FMA3 + AVX2 with the OS's YMM state: 3");
    auto no_avx2 = modern;
    no_avx2.leaf7_ebx = 0;
    check(crt_fma3_value(no_avx2) == 1, "FMA3 without AVX2 (Piledriver): 1, the mixed set");
    auto old_leaf = modern;
    old_leaf.max_leaf = 6;
    check(crt_fma3_value(old_leaf) == 1, "no leaf 7: 1");
    auto no_fma = modern;
    no_fma.leaf1_ecx &= ~kFma;
    check(crt_fma3_value(no_fma) == 0, "no FMA: 0");
    auto no_avx = modern;
    no_avx.leaf1_ecx &= ~kAvx;
    check(crt_fma3_value(no_avx) == 0, "no AVX: 0");
    auto no_osxsave = modern;
    no_osxsave.leaf1_ecx &= ~kOsxsave;
    check(crt_fma3_value(no_osxsave) == 0, "no OSXSAVE: 0");
    auto no_ymm = modern;
    no_ymm.xcr0 = 0x3; // the OS saves only x87 / XMM
    check(crt_fma3_value(no_ymm) == 0, "the OS does not save YMM: 0");
    auto other_bits = modern;
    other_bits.leaf1_ecx |= 0x00f00000;
    other_bits.leaf7_ebx |= 0xffff0000;
    other_bits.xcr0 |= 0xe0;
    check(crt_fma3_value(other_bits) == 3, "other feature bits do not matter");
}

void this_machine() {
    const int computed = crt_fma3_value(read_cpu_features());
    const int crt = crt_math_path();
    std::cout << "this machine: computed " << computed << ", the CRT's choice " << crt << '\n';
    check(computed == crt, "the replication gives the CRT's own value on this machine");
    check((crt == 3) == !online_cpu_refusal().has_value(), "the gate wants exactly 3");
    if (crt != 3) check(*online_cpu_refusal() == kCpuRefusal, "refusal text");
}
} // namespace

int main() {
    try {
        computed_values();
        this_machine();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << checks << " checks passed\n";
    return 0;
}
