#pragma once

#include "dw4/engine/math.hpp"

#include <cmath>
#include <cstdint>

// The original's floating point as the Gekko (and Dolphin, the reference)
// computes it, for code paths that must follow the original bit for bit:
// every instruction rounds its exact result once, the single-precision ones
// to single (fmuls / fadds / fsubs / fdivs of single operands give the same
// as the double result rounded to single; fmadds rounds the exact a * c + b
// once - a double fused multiply-add rounded again to single would differ
// when the double sum lands on a single tie, as the stag0204 camera
// translation at Dolphin n 5488 / 5566 showed), frsqrte comes from the
// hardware table.
// On top of these: the MSL math library as CodeWarrior compiled it (fdlibm
// with multiply-adds fused) and the matrix routines the camera and player
// input use. The routines keep the original operation order; see
// docs/research/CAMERA_MATH.md.
namespace dw4::engine::gekko {

// fmuls / fadds / fsubs / fdivs: the double result rounded to single.
inline float muls(float a, float c) { return float(double(a) * double(c)); }
inline float adds(float a, float b) { return float(double(a) + double(b)); }
inline float subs(float a, float b) { return float(double(a) - double(b)); }
inline float divs(float a, float b) { return float(double(a) / double(b)); }
// fmadds a * c + b, fmsubs a * c - b, fnmadds -(a * c + b), fnmsubs -(a * c - b),
// each rounded once (std::fma on floats is the correctly rounded fmaf).
inline float madds(float a, float c, float b) { return std::fma(a, c, b); }
inline float msubs(float a, float c, float b) { return std::fma(a, c, -b); }
inline float nmadds(float a, float c, float b) { return -std::fma(a, c, b); }
inline float nmsubs(float a, float c, float b) { return -std::fma(a, c, -b); }

// frsqrte: the estimate of 1 / sqrt(x) from the hardware table (12 bits).
double frsqrte(double x);
// The inlined float square root of 8012947C / 801238E8 / 802FDA0C: for a
// positive x, frsqrte and three Newton steps in double (fmul, fnmsub
// 3 - x y y), then x * y rounded to single. Other values are returned as
// they are (the callers skip the steps for x <= 0).
float sqrt_newton(float x);

// MSL (fdlibm compiled with fused multiply-adds): sin 800A2AE4, cos
// 800A257C, asin 800A2C54 (__ieee754_asin 8009F678), acos 800A2C34
// (__ieee754_acos 8009F43C), atan 800A21F8,
// atan2 800A2C74 (__ieee754_atan2 8009F8B0). The argument reduction
// (__ieee754_rem_pio2 800A0C5C) is followed up to |x| <= 2^19 pi / 2; the
// table reduction beyond (__kernel_rem_pio2 800A10F0, never reached by the
// game's angles) falls back to the host library.
double sin(double x);
double cos(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);

// 8012CB74: a x b, each component's second product rounded first (fmsubs).
Vec3f cross(const Vec3f& a, const Vec3f& b);
// The dot products of 801238E8 / 8012947C: y * y rounded, x * x and z * z
// fused onto it.
float dot(const Vec3f& a, const Vec3f& b);
// 801238E8 (0x80123910..0x80123A28): the face normal (b - a) x (c - a)
// through 8012CB74, its length from ((x*x + y*y) + z*z) plain (sqrt_newton
// when positive), each component times 1 / length.
Vec3f triangle_normal(const Vec3f& a, const Vec3f& b, const Vec3f& c);
// 8012D434: v * the upper 3x3 of m, the y term rounded, x and z fused onto it.
Vec3f rotate(const Vec3f& v, const Mat4& m);
// 8012D498: the point v through all of m with the homogeneous division: per
// axis the y product rounded, x and z fused onto it, the translation added;
// w likewise from the fourth column; unless w is 1 the point is divided by
// w, a w of magnitude below 5e-7 taken as +5e-7.
Vec3f transform_point(const Vec3f& v, const Mat4& m);
// 801252D0: a segment (origin, unit direction, length) into the space of
// `inverse`: the end point origin + length * direction (fused), both points
// through transform_point, the direction their difference times 1 / its
// length (no threshold), the length that length; in place.
void transform_segment(Vec3f& origin, Vec3f& direction, float& length, const Mat4& inverse);
// 80128774: a = b * a (row-vector convention: b applied first), each element
// the j = 1 product rounded, then j = 0, 2 and 3 fused onto it.
void premultiply(Mat4& a, const Mat4& b);
// 8012947C: Euler angles (x, y, z) of a matrix: the rows divided by m[15]
// when it is not 1, Gram-Schmidt with fused dot products, the rows negated
// when their determinant is negative, then y = asin(-row0.z) clamped to
// [-1, 1], x = atan2(row1.z, row2.z), z = atan2(row0.y, row0.x) (x =
// atan2(row1.x, row1.y), z = 0 when cos(y) is 0); each rounded to single.
void matrix_euler(const Mat4& m, float& x, float& y, float& z);
// 80129134 (801292D4): the rotation of Euler angles (x, y, z) into the nine
// rotation entries, sines and cosines rounded to single (an angle of 0 takes
// 0 and 1 without calling them); the translation row becomes (0, 0, 0, 1)
// and the fourth column 0 (804F8C30 / 804F8AE0).
void euler_matrix(Mat4& m, float x, float y, float z);
// 8012A860: the view matrix of a camera matrix: the 3x3 transposed and the
// translation -(t * R^T) per column (y product, x fused, z fused, negated).
void view_inverse(const Mat4& camera, Mat4& view);
// 8012A92C: the general inverse (adjugate 8012AA04, determinant 8012AD50,
// each entry times 1 / det). False (result untouched) when |det| < FLT_MIN.
bool invert(const Mat4& source, Mat4& result);

} // namespace dw4::engine::gekko
