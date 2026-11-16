#pragma once

// Online play needs every member's machine to run the same libm routines as
// the reference machine (docs/SEAMLESS.md 6.12). The static CRT (/MT, the
// Windows 10.0.26100 UCRT) picks its math routines once at start-up
// (__acrt_initialize_fma3 in fma3_available.obj, disassembled in round 52)
// and keeps the choice in __use_fma3_lib:
//
//   0  no FMA3: every routine takes its SSE2 path;
//   1  CPUID.1:ECX FMA (bit 12), OSXSAVE (27) and AVX (28) set and XGETBV(0)
//      has the XMM and YMM state bits (0x6), but no AVX2: the assembler
//      routines (sin, cos, tan, exp, log, fmod and their float versions,
//      powf) take their FMA3 path, the C routines (acos, asin, atan, atan2,
//      their float versions and pow) still their SSE2 path;
//   3  as 1 plus AVX2 (CPUID.(EAX=7,ECX=0):EBX bit 5, leaf 7 present): every
//      routine takes its FMA3 path (the C routines test (value & 3) == 3).
//
// Only 3 matches the reference machine, so the gate wants exactly 3 (and the
// switch still on: nothing may have called _set_FMA3_enable(0)).
#include <cstdint>
#include <optional>
#include <string>

namespace dw4::game::online {

// The CPUID / XGETBV words the CRT looks at.
struct CpuFeatureWords {
    std::uint32_t leaf1_ecx{};
    std::uint64_t xcr0{};        // XGETBV(0); only read when OSXSAVE is set
    std::uint32_t max_leaf{};    // CPUID.0:EAX
    std::uint32_t leaf7_ebx{};   // CPUID.(7,0):EBX; only read when max_leaf >= 7
};

// The value __acrt_initialize_fma3 stores in __use_fma3_lib for these words
// (0, 1 or 3), step for step as the CRT computes it.
int crt_fma3_value(const CpuFeatureWords& words) noexcept;

// This machine's words (cpuid / xgetbv).
CpuFeatureWords read_cpu_features() noexcept;

// The CRT's own choice as it runs in this process: __use_fma3_lib when the
// runtime is linked statically (DW4_CRT_STATIC), else the value computed from
// this machine's words; 0 when _get_FMA3_enable() reports the switch off.
int crt_math_path() noexcept;

// The refusal text for the online session start, or nothing when this
// machine may play online.
std::optional<std::string> online_cpu_refusal();

// Android: the simulation computes with the device's own libm, whose results
// differ from the PC's CRT (docs/ANDROID.md "Floating-point environment").
// The original's MSL routines on every platform come with their own protocol
// bump (feat-android-cdet); until then an Android build plays neither online
// nor LAN with another machine, where the session would part. The PC's rules
// stay as they were.
#ifdef __ANDROID__
inline constexpr bool kAndroidBuild = true;
#else
inline constexpr bool kAndroidBuild = false;
#endif

// online_cpu_refusal's rule for a platform and the CRT's math path (testable
// on every platform): Android always refused, the PC unless the path is 3.
std::optional<std::string> online_refusal_for(bool android, int crt_path);

// The LAN session's refusal for a peer (the joined host, or a host's guests:
// loopback when the host listens on loopback only), or nothing: Android only
// on this device (a test run's loopback session), the PC always.
std::optional<std::string> lan_refusal_for(bool android, bool peer_is_loopback);
std::optional<std::string> lan_refusal(bool peer_is_loopback);
// An IPv4 address (host byte order) in 127.0.0.0/8.
constexpr bool ipv4_loopback(std::uint32_t address) { return (address >> 24) == 127; }

// The texts the gate shows (also used by the tests).
inline constexpr const char* kCpuRefusal = "Online play needs a CPU with FMA3 (Intel 2013 / AMD 2015 or later).";
inline constexpr const char* kAndroidRefusal = "Online play on Android comes with a later update.";
inline constexpr const char* kAndroidLanRefusal = "LAN play with another device on Android comes with a later update.";

} // namespace dw4::game::online
