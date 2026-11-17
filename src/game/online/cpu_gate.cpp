#include "cpu_gate.hpp"

#include "http.hpp"

#include <cmath>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <immintrin.h>
#include <intrin.h>
#define DW4_HAVE_CPUID 1
#endif

#ifdef DW4_CRT_STATIC
// The static UCRT's dispatch word (fma3_available.obj). Read only.
extern "C" int __use_fma3_lib;
#endif

namespace dw4::game::online {

int crt_fma3_value(const CpuFeatureWords& words) noexcept {
    // fma3_available.obj: and ecx, 18001000h / cmp ecx, 18001000h.
    constexpr std::uint32_t kFmaOsxsaveAvx = (1u << 12) | (1u << 27) | (1u << 28);
    if ((words.leaf1_ecx & kFmaOsxsaveAvx) != kFmaOsxsaveAvx) return 0;
    // xgetbv(0): and al, 6 / cmp al, 6 (XMM and YMM state saved by the OS).
    if ((words.xcr0 & 6) != 6) return 0;
    // cpuid(0) max leaf < 7: jl -> 1.
    if (words.max_leaf < 7) return 1;
    // cpuid(7, 0): test bl, 20h (AVX2) -> 3, else 1.
    return (words.leaf7_ebx & (1u << 5)) ? 3 : 1;
}

CpuFeatureWords read_cpu_features() noexcept {
    CpuFeatureWords words;
#ifdef DW4_HAVE_CPUID
    int regs[4]{};
    __cpuid(regs, 0);
    words.max_leaf = std::uint32_t(regs[0]);
    __cpuid(regs, 1);
    words.leaf1_ecx = std::uint32_t(regs[2]);
    if (words.leaf1_ecx & (1u << 27)) words.xcr0 = _xgetbv(0);
    if (words.max_leaf >= 7) {
        __cpuidex(regs, 7, 0);
        words.leaf7_ebx = std::uint32_t(regs[1]);
    }
#endif
    return words;
}

int crt_math_path() noexcept {
#ifdef _MSC_VER
    if (_get_FMA3_enable() == 0) return 0;
#endif
#ifdef DW4_CRT_STATIC
    return __use_fma3_lib;
#else
    return crt_fma3_value(read_cpu_features());
#endif
}

std::optional<std::string> online_refusal_for(bool android, int crt_path) {
    if (android) return std::string(kAndroidRefusal);
    if (crt_path != 3) return std::string(kCpuRefusal);
    return std::nullopt;
}

std::optional<std::string> online_cpu_refusal() { return online_refusal_for(kAndroidBuild, kAndroidBuild ? 0 : crt_math_path()); }

std::optional<std::string> lan_refusal_for(bool android, bool peer_is_loopback) {
    if (android && !peer_is_loopback) return std::string(kAndroidLanRefusal);
    return std::nullopt;
}

std::optional<std::string> lan_refusal(bool peer_is_loopback) { return lan_refusal_for(kAndroidBuild, peer_is_loopback); }

std::optional<std::string> lan_game_problem_for(bool android, const std::string& guest_game) {
    if (guest_game == lan_game_for(android)) return std::nullopt;
    if (guest_game == lan_game_for(!android)) return std::string(android ? kAndroidLanRefusal : kPcLanAndroidRefusal);
    return std::string("Another game.");
}

std::optional<std::string> lan_game_problem(const std::string& guest_game) { return lan_game_problem_for(kAndroidBuild, guest_game); }

std::optional<std::string> cloud_refusal_for(bool android, bool https_transport) {
    if (android) return std::string(kAndroidCloudRefusal);
    if (!https_transport) return std::string(kNoTransportCloudRefusal);
    return std::nullopt;
}

std::optional<std::string> cloud_refusal() { return cloud_refusal_for(kAndroidBuild, kHttpsTransport); }

} // namespace dw4::game::online
