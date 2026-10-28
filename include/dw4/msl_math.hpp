#pragma once

// The MSL math library as CodeWarrior compiled it into the DOL (fdlibm 5.3,
// multiply-adds fused): the double results the original's trigonometry
// returns, bit for bit, for code that must follow the original's rounding
// (the engine reaches them through dw4::engine::gekko as well).
//   sin 800A2AE4, cos 800A257C, asin 800A2C54 (__ieee754_asin 8009F678),
//   acos 800A2C34 (__ieee754_acos 8009F43C), atan 800A21F8,
//   atan2 800A2C74 (__ieee754_atan2 8009F8B0).
// The argument reduction (__ieee754_rem_pio2 800A0C5C) is followed up to
// |x| <= 2^19 pi / 2; the table reduction beyond (__kernel_rem_pio2
// 800A10F0, never reached by the game's angles) falls back to the host
// library.
namespace dw4::msl {

double sin(double x);
double cos(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);

} // namespace dw4::msl
