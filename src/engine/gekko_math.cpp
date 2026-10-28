#include "dw4/engine/gekko_math.hpp"

#include <array>
#include <bit>
#include <limits>

// Built with /fp:strict (no contraction): every operation below rounds where
// the original's instruction rounds; std::fma marks the fused ones.
namespace dw4::engine::gekko {

namespace {
double fnmsub(double a, double c, double b) { return -std::fma(a, c, -b); }
} // namespace

// The Gekko's frsqrte table as Dolphin models it (Common/FloatUtils): base
// and step per 1/16 of the mantissa range, the first 16 entries for an odd
// biased exponent ([1, 2) and its powers of 4), the second 16 for an even one.
double frsqrte(double x) {
    struct Entry {
        std::int64_t base, step;
    };
    static constexpr std::array<Entry, 32> table{{
        {0x3ffa000, 0x7a4}, {0x3c29000, 0x700}, {0x38aa000, 0x670}, {0x3572000, 0x5f2},
        {0x3279000, 0x584}, {0x2fb7000, 0x524}, {0x2d26000, 0x4cc}, {0x2ac0000, 0x47e},
        {0x2881000, 0x43a}, {0x2665000, 0x3fa}, {0x2468000, 0x3c2}, {0x2287000, 0x38e},
        {0x20c1000, 0x35e}, {0x1f12000, 0x332}, {0x1d79000, 0x30a}, {0x1bf4000, 0x2e6},
        {0x1a7e800, 0x568}, {0x17cb800, 0x4f3}, {0x1552800, 0x48d}, {0x130c000, 0x435},
        {0x10f2000, 0x3e7}, {0x0eff000, 0x3a2}, {0x0d2e000, 0x365}, {0x0b7c000, 0x32e},
        {0x09e5000, 0x2fc}, {0x0867000, 0x2d0}, {0x06ff000, 0x2a8}, {0x05ab800, 0x283},
        {0x046a000, 0x261}, {0x0339800, 0x243}, {0x0218800, 0x226}, {0x0105800, 0x20b},
    }};
    const auto bits = std::bit_cast<std::int64_t>(x);
    std::int64_t mantissa = bits & ((std::int64_t(1) << 52) - 1);
    const std::int64_t sign = bits & std::int64_t(0x8000000000000000ull);
    std::int64_t exponent = bits & (std::int64_t(0x7FF) << 52);
    if (mantissa == 0 && exponent == 0) return sign ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    if (exponent == (std::int64_t(0x7FF) << 52)) {
        if (mantissa == 0) return sign ? std::numeric_limits<double>::quiet_NaN() : 0.0;
        return x;
    }
    if (sign) return std::numeric_limits<double>::quiet_NaN();
    if (exponent == 0) { // denormal: normalize
        do {
            exponent -= std::int64_t(1) << 52;
            mantissa <<= 1;
        } while (!(mantissa & (std::int64_t(1) << 52)));
        mantissa &= (std::int64_t(1) << 52) - 1;
        exponent += std::int64_t(1) << 52;
    }
    const std::int64_t odd = exponent & (std::int64_t(1) << 52);
    exponent = ((std::int64_t(0x3FF) << 52) - ((exponent - (std::int64_t(0x3FE) << 52)) / 2)) & (std::int64_t(0x7FF) << 52);
    const int i = int((odd | mantissa) >> 37);
    const Entry& entry = table[std::size_t((i / 2048) ^ 16)];
    return std::bit_cast<double>(exponent | ((entry.base - entry.step * (i % 2048)) << 26));
}

float sqrt_newton(float x) {
    if (!(x > 0.0f)) return x;
    const double v = x;
    double y = frsqrte(v);
    for (int step = 0; step < 3; ++step) {
        const double square = y * y; // fmul f0, f1, f1
        const double half = 0.5 * y; // fmul f1, f3, f1
        y = half * fnmsub(v, square, 3.0);
    }
    return float(v * y);
}

// ---------------------------------------------------------------------------
// Vector and matrix routines.
// ---------------------------------------------------------------------------
Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {msubs(a.y, b.z, muls(a.z, b.y)), msubs(a.z, b.x, muls(a.x, b.z)), msubs(a.x, b.y, muls(a.y, b.x))};
}

float dot(const Vec3f& a, const Vec3f& b) { return madds(a.z, b.z, madds(a.x, b.x, muls(a.y, b.y))); }

Vec3f triangle_normal(const Vec3f& a, const Vec3f& b, const Vec3f& c) {
    const Vec3f n = gekko::cross({subs(b.x, a.x), subs(b.y, a.y), subs(b.z, a.z)}, {subs(c.x, a.x), subs(c.y, a.y), subs(c.z, a.z)});
    float length = adds(muls(n.z, n.z), adds(muls(n.x, n.x), muls(n.y, n.y)));
    if (length > 0.0f) length = sqrt_newton(length);
    const float inverse = divs(1.0f, length);
    return {muls(n.x, inverse), muls(n.y, inverse), muls(n.z, inverse)};
}

Vec3f rotate(const Vec3f& v, const Mat4& m) {
    return {madds(v.z, m(2, 0), madds(v.x, m(0, 0), muls(v.y, m(1, 0)))),
            madds(v.z, m(2, 1), madds(v.x, m(0, 1), muls(v.y, m(1, 1)))),
            madds(v.z, m(2, 2), madds(v.x, m(0, 2), muls(v.y, m(1, 2))))};
}

Vec3f transform_point(const Vec3f& v, const Mat4& m) {
    Vec3f r{adds(m(3, 0), madds(v.z, m(2, 0), madds(v.x, m(0, 0), muls(v.y, m(1, 0))))),
            adds(m(3, 1), madds(v.z, m(2, 1), madds(v.x, m(0, 1), muls(v.y, m(1, 1))))),
            adds(m(3, 2), madds(v.z, m(2, 2), madds(v.x, m(0, 2), muls(v.y, m(1, 2)))))};
    float w = adds(m(3, 3), madds(v.z, m(2, 3), madds(v.x, m(0, 3), muls(v.y, m(1, 3)))));
    if (w == 1.0f) return r;
    // 0x8012D540..0x8012D564: the sign bit cleared, below 5e-7 (0x350637BD)
    // the epsilon itself (positive) is the divisor.
    if (std::fabs(w) < 5.0000001e-7f) w = 5.0000001e-7f; // a NaN stays
    const float scale = divs(1.0f, w);
    return {muls(r.x, scale), muls(r.y, scale), muls(r.z, scale)};
}

void transform_segment(Vec3f& origin, Vec3f& direction, float& length, const Mat4& inverse) {
    const Vec3f end{madds(length, direction.x, origin.x), madds(length, direction.y, origin.y),
                    madds(length, direction.z, origin.z)};
    origin = gekko::transform_point(origin, inverse);
    const Vec3f e = gekko::transform_point(end, inverse);
    direction = {subs(e.x, origin.x), subs(e.y, origin.y), subs(e.z, origin.z)};
    float l = adds(muls(direction.z, direction.z), adds(muls(direction.x, direction.x), muls(direction.y, direction.y)));
    if (l > 0.0f) l = sqrt_newton(l);
    const float scale = divs(1.0f, l);
    direction = {muls(direction.x, scale), muls(direction.y, scale), muls(direction.z, scale)};
    length = l;
}

void premultiply(Mat4& a, const Mat4& b) {
    // 0x801287FC..0x8012889C, per column k of a: row i of the result is
    // b(i,1) * a(1,k) rounded, then b(i,0) * a(0,k), b(i,2) * a(2,k) and
    // b(i,3) * a(3,k) fused onto it; 80128CF4 copies the result into a.
    Mat4 r;
    for (int k = 0; k < 4; ++k)
        for (int i = 0; i < 4; ++i) {
            float s = muls(b(i, 1), a(1, k));
            s = madds(b(i, 0), a(0, k), s);
            s = madds(b(i, 2), a(2, k), s);
            s = madds(b(i, 3), a(3, k), s);
            r(i, k) = s;
        }
    a = r;
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    // 0x80128690..0x80128770: per row i of a, the pairs (j = 0, 1) and
    // (j = 2, 3) of b's rows scaled by a(i,0) (ps_muls0), then a(i,1),
    // a(i,2) and a(i,3) fused onto them.
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = muls(b(0, j), a(i, 0));
            s = madds(b(1, j), a(i, 1), s);
            s = madds(b(2, j), a(i, 2), s);
            s = madds(b(3, j), a(i, 3), s);
            r(i, j) = s;
        }
    return r;
}

void matrix_euler(const Mat4& m, float& x, float& y, float& z) {
    // 0x801294A8..0x8012953C: the rows (row 0 in registers, rows 1 and 2 on
    // the stack) divided by m[15] unless it is 1.
    Vec3f r0{m.m[0], m.m[1], m.m[2]}, r1{m.m[4], m.m[5], m.m[6]}, r2{m.m[8], m.m[9], m.m[10]};
    if (m.m[15] != 1.0f) {
        const float inverse = divs(1.0f, m.m[15]);
        r1 = {muls(r1.x, inverse), muls(r1.y, inverse), muls(r1.z, inverse)};
        r2 = {muls(r2.x, inverse), muls(r2.y, inverse), muls(r2.z, inverse)};
        r0 = {muls(r0.x, inverse), muls(r0.y, inverse), muls(r0.z, inverse)};
    }
    // 0x80129540..0x801295D0: row 0 normalized (y * y, x and z fused onto it).
    float length = madds(r0.z, r0.z, madds(r0.x, r0.x, muls(r0.y, r0.y)));
    if (length > 0.0f) length = sqrt_newton(length);
    float inverse = divs(1.0f, length);
    r0 = {muls(r0.x, inverse), muls(r0.y, inverse), muls(r0.z, inverse)};
    // 0x801295D4..0x801295EC: row 1 minus its row 0 part (fused dot, fused moves).
    float d = -madds(r0.z, r1.z, madds(r0.x, r1.x, muls(r0.y, r1.y)));
    r1 = {madds(d, r0.x, r1.x), madds(d, r0.y, r1.y), madds(d, r0.z, r1.z)};
    // 0x801295F0..0x80129690: normalized with ((x*x + y*y) + z*z), plain sums.
    length = adds(muls(r1.z, r1.z), adds(muls(r1.x, r1.x), muls(r1.y, r1.y)));
    if (length > 0.0f) length = sqrt_newton(length);
    inverse = divs(1.0f, length);
    r1 = {muls(r1.x, inverse), muls(r1.y, inverse), muls(r1.z, inverse)};
    // 0x80129684..0x801296EC: row 2 minus its row 0 part, then its row 1 part.
    d = -madds(r0.z, r2.z, madds(r0.x, r2.x, muls(r0.y, r2.y)));
    r2 = {madds(d, r0.x, r2.x), madds(d, r0.y, r2.y), madds(d, r0.z, r2.z)};
    d = -madds(r1.z, r2.z, madds(r1.x, r2.x, muls(r1.y, r2.y)));
    r2 = {madds(d, r1.x, r2.x), madds(d, r1.y, r2.y), madds(d, r1.z, r2.z)};
    length = adds(muls(r2.z, r2.z), adds(muls(r2.x, r2.x), muls(r2.y, r2.y)));
    if (length > 0.0f) length = sqrt_newton(length);
    inverse = divs(1.0f, length);
    r2 = {muls(r2.x, inverse), muls(r2.y, inverse), muls(r2.z, inverse)};
    // 0x8012979C..0x80129814: a left-handed frame (row 0 . (row 1 x row 2) < 0)
    // has all three rows negated.
    const Vec3f c = gekko::cross(r1, r2);
    if (madds(r0.z, c.z, madds(r0.x, c.x, muls(r0.y, c.y))) < 0.0f) {
        r0 = {-r0.x, -r0.y, -r0.z};
        r1 = {-r1.x, -r1.y, -r1.z};
        r2 = {-r2.x, -r2.y, -r2.z};
    }
    // 0x80129818..0x80129858: y = asin(-row0.z) clamped to [-1, 1].
    const float s = -r0.z;
    const float above = s > -1.0f ? s : -1.0f;
    const float clamped = above >= 1.0f ? 1.0f : (s > -1.0f ? s : -1.0f);
    y = float(asin(clamped));
    // 0x80129860..0x801298BC: cos of the stored y decides the branch.
    if (float(cos(y)) != 0.0f) {
        x = float(atan2(r1.z, r2.z));
        z = float(atan2(r0.y, r0.x));
    } else {
        x = float(atan2(r1.x, r1.y));
        z = 0.0f;
    }
}

void euler_matrix(Mat4& m, float x, float y, float z) {
    // 801292D4: sines and cosines (frsp), an angle of 0 without the calls.
    float sx = 0.0f, cx = 1.0f, sy = 0.0f, cy = 1.0f, sz = 0.0f, cz = 1.0f, sxsy = 0.0f, cxsz = 0.0f;
    if (x != 0.0f) {
        sx = float(sin(x));
        cx = float(cos(x));
    }
    if (y != 0.0f) {
        sy = float(sin(y));
        cy = float(cos(y));
        sxsy = muls(sx, sy);
    }
    if (z != 0.0f) {
        sz = float(sin(z));
        cz = float(cos(z));
        cxsz = muls(cx, sz);
    }
    // 0x801293DC..0x8012942C.
    const float cxcz = muls(cx, cz), sxsz = muls(sx, sz), sxcz = muls(sx, cz);
    m.m[0] = muls(cy, cz);
    m.m[1] = muls(cy, sz);
    m.m[2] = -sy;
    m.m[4] = msubs(sxsy, cz, cxsz);
    m.m[5] = madds(sxsy, sz, cxcz);
    m.m[6] = muls(sx, cy);
    m.m[8] = madds(cxcz, sy, sxsz);
    m.m[9] = msubs(cxsz, sy, sxcz);
    m.m[10] = muls(cx, cy);
    // 80129134: the translation row and the fourth column from 804F8C30 /
    // 804F8AE0 (the homogeneous row and zero).
    m.m[12] = m.m[13] = m.m[14] = 0.0f;
    m.m[15] = 1.0f;
    m.m[3] = m.m[7] = m.m[11] = 0.0f;
}

void view_inverse(const Mat4& c, Mat4& v) {
    // 0x8012A860..0x8012A8C8: the transposed rotation, 0 / 1 in the fourth column.
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column) v(row, column) = c(column, row);
    v.m[3] = v.m[7] = v.m[11] = 0.0f;
    v.m[15] = 1.0f;
    // 0x8012A8CC..0x8012A91C: t_j = -(tz * v(2,j) + (tx * v(0,j) + ty * v(1,j))).
    const float tx = c.m[12], ty = c.m[13], tz = c.m[14];
    for (int column = 0; column < 3; ++column)
        v(3, column) = nmadds(tz, v(2, column), madds(tx, v(0, column), muls(ty, v(1, column))));
}

namespace {
// 8012AA04: the adjugate, instruction for instruction (fN the DOL's
// registers, tN its stack temporaries).
void adjugate(const float* in, float* out) {
    float f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23,
        f24, f25, f26, f27, f28, f29, f30, f31, t8, t10, t18, t20, t28, t30;
    f28 = in[11]; f21 = in[12]; f10 = in[13]; f31 = in[14]; f0 = muls(f21, f28); f9 = in[9]; f2 = muls(f10, f28);
    f27 = in[15]; f3 = muls(f31, f28); f13 = in[10]; f12 = in[6]; f16 = msubs(f9, f27, f2); f6 = in[8];
    f1 = muls(f21, f13); f11 = in[2]; f22 = msubs(f13, f27, f3); f2 = msubs(f6, f27, f0); f5 = in[4];
    f15 = msubs(f6, f31, f1); f8 = in[5]; f19 = muls(f21, f9); f0 = muls(f12, f2); f29 = in[7]; f3 = muls(f11, f2);
    f4 = in[0]; f14 = muls(f10, f13); f1 = msubs(f5, f22, f0); f0 = muls(f21, f29); f7 = in[1];
    f23 = msubs(f4, f22, f3); f30 = in[3]; f3 = muls(f21, f12); f21 = muls(f21, f8); f17 = muls(f12, f16);
    f0 = msubs(f5, f27, f0); t10 = f21; f14 = msubs(f9, f31, f14); f17 = msubs(f8, f22, f17); f21 = muls(f7, f0);
    f18 = madds(f29, f15, f1); f17 = madds(f29, f14, f17); t18 = f21; f21 = muls(f9, f12); f20 = muls(f8, f2);
    f25 = muls(f11, f16); out[0] = f17; f24 = muls(f7, f2); t20 = f21; f21 = muls(f8, f15);
    f22 = msubs(f7, f22, f25); f20 = msubs(f5, f16, f20); f18 = -f18; f1 = muls(f31, f29); f25 = muls(f7, f15);
    f19 = msubs(f6, f10, f19); out[4] = f18; f17 = msubs(f5, f14, f21); f22 = madds(f30, f14, f22); t8 = f1;
    f21 = madds(f29, f19, f20); f17 = madds(f12, f19, f17); f2 = muls(f10, f29); f16 = msubs(f4, f16, f24);
    out[8] = f21; f20 = -f17; f1 = msubs(f8, f27, f2); f18 = madds(f30, f19, f16); f16 = t8; f2 = muls(f9, f29);
    out[12] = f20; f17 = -f22; f21 = msubs(f12, f27, f16); f0 = muls(f11, f0); t30 = f2; f2 = msubs(f5, f31, f3);
    f3 = muls(f13, f29); out[1] = f17; f15 = madds(f30, f15, f23); f16 = muls(f11, f1); f26 = muls(f10, f12);
    t28 = f3; f3 = t30; f14 = msubs(f4, f14, f25); out[5] = f15; f18 = -f18; f0 = msubs(f4, f21, f0);
    f17 = msubs(f7, f21, f16); out[9] = f18; f18 = madds(f11, f19, f14); f16 = madds(f30, f2, f0); f0 = t28;
    f3 = msubs(f8, f28, f3); f15 = muls(f7, f2); f2 = t10; f20 = msubs(f8, f31, f26); out[13] = f18;
    f18 = msubs(f5, f10, f2); f2 = t18; f10 = msubs(f4, f20, f15); f17 = madds(f30, f20, f17);
    f15 = msubs(f4, f1, f2); f1 = t20; f10 = madds(f11, f18, f10); f0 = msubs(f12, f28, f0); out[2] = f17;
    f14 = muls(f11, f3); f16 = -f16; f1 = msubs(f8, f13, f1); f2 = msubs(f7, f0, f14); f14 = madds(f30, f18, f15);
    out[6] = f16; f10 = -f10; f2 = madds(f30, f1, f2); out[10] = f14; f2 = -f2; out[14] = f10; out[3] = f2;
    f10 = muls(f6, f29); f12 = muls(f6, f12); f2 = muls(f6, f8); f6 = msubs(f5, f28, f10); f8 = msubs(f5, f13, f12);
    f9 = msubs(f5, f9, f2); f5 = muls(f7, f6); f6 = muls(f11, f6); f2 = muls(f7, f8); f3 = msubs(f4, f3, f5);
    f5 = msubs(f4, f0, f6); f0 = msubs(f4, f1, f2); f1 = madds(f30, f9, f3); f2 = madds(f30, f8, f5);
    f0 = madds(f11, f9, f0); f1 = -f1; out[7] = f2; out[11] = f1; out[15] = f0;
    (void)f24;
}

// 8012AD50: the determinant, instruction for instruction.
float determinant(const float* in) {
    float f0, f1, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12, f13, f28, f29, f30, f31;
    f1 = in[11]; f5 = in[12]; f30 = in[13]; f29 = in[14]; f0 = muls(f5, f1); f13 = in[8]; f2 = muls(f30, f1);
    f7 = in[15]; f1 = muls(f29, f1); f6 = in[10]; f11 = in[9]; f4 = msubs(f13, f7, f0); f0 = in[6];
    f3 = muls(f5, f6); f10 = msubs(f11, f7, f2); f12 = in[4]; f7 = msubs(f6, f7, f1); f1 = muls(f0, f4);
    f31 = in[5]; f8 = msubs(f13, f29, f3); f6 = muls(f30, f6); f28 = in[7]; f3 = muls(f0, f10);
    f1 = msubs(f12, f7, f1); f2 = in[1]; f9 = muls(f5, f11); f5 = muls(f31, f4); f4 = in[0];
    f1 = madds(f28, f8, f1); f11 = msubs(f11, f29, f6); f6 = in[2]; f3 = msubs(f31, f7, f3); f7 = muls(f31, f8);
    f8 = in[3]; f9 = msubs(f13, f30, f9); f5 = msubs(f12, f10, f5); f7 = msubs(f12, f11, f7);
    f3 = madds(f28, f11, f3); f1 = muls(f2, f1); f2 = madds(f28, f9, f5); f5 = madds(f0, f9, f7);
    f0 = msubs(f4, f3, f1); f0 = madds(f6, f2, f0); f1 = nmsubs(f8, f5, f0);
    return f1;
}
} // namespace

bool invert(const Mat4& source, Mat4& result) {
    float adj[16];
    adjugate(source.m.data(), adj);
    const float det = determinant(source.m.data());
    // 0x8012A964..0x8012A988: |det| (sign bit cleared) against FLT_MIN.
    if (!(std::fabs(det) >= std::numeric_limits<float>::min())) return false;
    const float scale = divs(1.0f, det);
    for (std::size_t i = 0; i < 16; ++i) result.m[i] = muls(scale, adj[i]);
    return true;
}

} // namespace dw4::engine::gekko
