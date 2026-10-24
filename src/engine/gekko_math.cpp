#include "dw4/engine/gekko_math.hpp"

#include <array>
#include <bit>
#include <limits>

// Built with /fp:strict (no contraction): every operation below rounds where
// the original's instruction rounds; std::fma marks the fused ones.
namespace dw4::engine::gekko {

namespace {
std::int32_t high_word(double x) { return std::int32_t(std::bit_cast<std::uint64_t>(x) >> 32); }
std::uint32_t low_word(double x) { return std::uint32_t(std::bit_cast<std::uint64_t>(x)); }
double with_words(std::uint32_t high, std::uint32_t low) {
    return std::bit_cast<double>((std::uint64_t(high) << 32) | low);
}
double fmadd(double a, double c, double b) { return std::fma(a, c, b); }
double fmsub(double a, double c, double b) { return std::fma(a, c, -b); }
double fnmsub(double a, double c, double b) { return -std::fma(a, c, -b); }
// fctiwz: truncation toward zero, saturated.
std::int32_t fctiwz(double x) {
    if (!(x == x)) return std::numeric_limits<std::int32_t>::min();
    if (x >= 2147483647.0) return std::numeric_limits<std::int32_t>::max();
    if (x <= -2147483648.0) return std::numeric_limits<std::int32_t>::min();
    return std::int32_t(x);
}
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
// MSL math library (fdlibm 5.3 as compiled into the DOL, fused multiply-adds
// where CodeWarrior made them).
// ---------------------------------------------------------------------------
namespace {
// __kernel_cos 800A0FFC.
double kernel_cos(double x, double y) {
    const std::int32_t ix = high_word(x) & 0x7fffffff;
    if (ix < 0x3e400000 && fctiwz(x) == 0) return 1.0;
    const double z = x * x;
    double r = fmadd(-1.13596475577881948265e-11, z, 2.08757232129817482790e-09);
    r = fmadd(z, r, -2.75573143513906633035e-07);
    r = fmadd(z, r, 2.48015872894767294178e-05);
    r = fmadd(z, r, -1.38888888888741095749e-03);
    r = fmadd(z, r, 4.16666666666666019037e-02);
    r = z * r;
    if (ix < 0x3FD33333) return 1.0 - fmsub(0.5, z, fmsub(z, r, x * y));
    const double qx = ix > 0x3fe90000 ? 0.28125 : with_words(std::uint32_t(ix - 0x00200000), 0);
    const double xy = x * y;
    const double hz = fmsub(0.5, z, qx);
    const double zr = fmsub(z, r, xy);
    const double a = 1.0 - qx;
    return a - (hz - zr);
}

// __kernel_sin 800A1F44.
double kernel_sin(double x, double y, int iy) {
    const std::int32_t ix = high_word(x) & 0x7fffffff;
    if (ix < 0x3e400000 && fctiwz(x) == 0) return x;
    const double z = x * x;
    double r = fmadd(1.58969099521155010221e-10, z, -2.50507602534068634195e-08);
    const double v = z * x;
    r = fmadd(z, r, 2.75573137070700676789e-06);
    r = fmadd(z, r, -1.98412698298579493134e-04);
    r = fmadd(z, r, 8.33333333332248946124e-03);
    if (iy == 0) return fmadd(v, fmadd(z, r, -1.66666666666666324348e-01), x);
    const double vr = v * r;
    double t = fmsub(0.5, y, vr);
    t = fmsub(z, t, y);
    t = fnmsub(-1.66666666666666324348e-01, v, t);
    return x - t;
}

// __ieee754_rem_pio2 800A0C5C: x - n pi/2 as y[0] + y[1], returns n.
int rem_pio2(double x, double& y0, double& y1) {
    constexpr double pio2_1 = 1.57079632673412561417e+00, pio2_1t = 6.07710050650619224932e-11;
    constexpr double pio2_2 = 6.07710050630396597660e-11, pio2_2t = 2.02226624879595063154e-21;
    constexpr double pio2_3 = 2.02226624871116645580e-21, pio2_3t = 8.47842766036889956997e-32;
    constexpr double invpio2 = 6.36619772367581382433e-01;
    static constexpr std::int32_t npio2_hw[32] = {
        0x3FF921FB, 0x400921FB, 0x4012D97C, 0x401921FB, 0x401F6A7A, 0x4022D97C, 0x4025FDBB, 0x402921FB,
        0x402C463A, 0x402F6A7A, 0x4031475C, 0x4032D97C, 0x40346B9C, 0x4035FDBB, 0x40378FDB, 0x403921FB,
        0x403AB41B, 0x403C463A, 0x403DD85A, 0x403F6A7A, 0x40407E4C, 0x4041475C, 0x4042106C, 0x4042D97C,
        0x4043A28C, 0x40446B9C, 0x404534AC, 0x4045FDBB, 0x4046C6CB, 0x40478FDB, 0x404858EB, 0x404921FB,
    };
    const std::int32_t hx = high_word(x), ix = hx & 0x7fffffff;
    if (ix <= 0x3fe921fb) {
        y0 = x;
        y1 = 0.0;
        return 0;
    }
    if (ix < 0x4002d97c) { // |x| < 3pi/4
        if (hx > 0) {
            double z = x - pio2_1;
            if (ix != 0x3ff921fb) {
                y0 = z - pio2_1t;
                y1 = (z - y0) - pio2_1t;
            } else {
                z = z - pio2_2;
                y0 = z - pio2_2t;
                y1 = (z - y0) - pio2_2t;
            }
            return 1;
        }
        double z = pio2_1 + x;
        if (ix != 0x3ff921fb) {
            y0 = pio2_1t + z;
            y1 = pio2_1t + (z - y0);
        } else {
            z = z + pio2_2;
            y0 = pio2_2t + z;
            y1 = pio2_2t + (z - y0);
        }
        return -1;
    }
    if (ix <= 0x413921fb) { // |x| <= 2^19 pi/2: medium size
        const double t = std::fabs(x);
        const int n = fctiwz(fmadd(invpio2, t, 0.5));
        const double fn = double(n);
        double r = fnmsub(pio2_1, fn, t);
        double w = pio2_1t * fn;
        if (n < 32 && ix != npio2_hw[n - 1]) {
            y0 = r - w;
        } else {
            const std::int32_t j = ix >> 20;
            y0 = r - w;
            std::int32_t i = j - ((high_word(y0) >> 20) & 0x7ff);
            if (i > 16) { // second iteration, good to 118 bits
                double tt = r;
                w = pio2_2 * fn;
                r = tt - w;
                w = fmsub(pio2_2t, fn, (tt - r) - w);
                y0 = r - w;
                i = j - ((high_word(y0) >> 20) & 0x7ff);
                if (i > 49) { // third iteration, 151 bits
                    tt = r;
                    w = pio2_3 * fn;
                    r = tt - w;
                    w = fmsub(pio2_3t, fn, (tt - r) - w);
                    y0 = r - w;
                }
            }
        }
        y1 = (r - y0) - w;
        if (hx < 0) {
            y0 = -y0;
            y1 = -y1;
            return -n;
        }
        return n;
    }
    if (ix >= 0x7ff00000) {
        y0 = y1 = x - x;
        return 0;
    }
    // __kernel_rem_pio2 800A10F0 (not transcribed): the host's reduction.
    const double q = std::nearbyint(x / 1.57079632679489661923);
    y0 = std::remainder(x, 1.57079632679489661923);
    y1 = 0.0;
    return int(std::fmod(q, 4.0));
}
} // namespace

double cos(double x) {
    const std::int32_t ix = high_word(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return kernel_cos(x, 0.0);
    if (ix >= 0x7ff00000) return x - x;
    double y0 = 0.0, y1 = 0.0;
    switch (rem_pio2(x, y0, y1) & 3) {
    case 0: return kernel_cos(y0, y1);
    case 1: return -kernel_sin(y0, y1, 1);
    case 2: return -kernel_cos(y0, y1);
    default: return kernel_sin(y0, y1, 1);
    }
}

double sin(double x) {
    const std::int32_t ix = high_word(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return kernel_sin(x, 0.0, 0);
    if (ix >= 0x7ff00000) return x - x;
    double y0 = 0.0, y1 = 0.0;
    switch (rem_pio2(x, y0, y1) & 3) {
    case 0: return kernel_sin(y0, y1, 1);
    case 1: return kernel_cos(y0, y1);
    case 2: return -kernel_sin(y0, y1, 1);
    default: return -kernel_cos(y0, y1);
    }
}

double asin(double x) {
    constexpr double pio2_hi = 1.57079632679489655800e+00, pio2_lo = 6.12323399573676603587e-17;
    constexpr double pio4_hi = 7.85398163397448278999e-01;
    constexpr double pS0 = 1.66666666666666657415e-01, pS1 = -3.25565818622400915405e-01,
                     pS2 = 2.01212532134862925881e-01, pS3 = -4.00555345006794114027e-02,
                     pS4 = 7.91534994289814532176e-04, pS5 = 3.47933107596021167570e-05;
    constexpr double qS1 = -2.40339491173441421878e+00, qS2 = 2.02094576023350569471e+00,
                     qS3 = -6.88283971605453293030e-01, qS4 = 7.70381505559019352791e-02;
    const std::int32_t hx = high_word(x), ix = hx & 0x7fffffff;
    if (ix >= 0x3ff00000) {
        if (((ix - 0x3ff00000) | std::int32_t(low_word(x))) == 0) return fmadd(pio2_hi, x, pio2_lo * x);
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (ix < 0x3fe00000) { // |x| < 0.5
        if (ix < 0x3e400000 && 1.0e300 + x > 1.0) return x;
        const double t = x * x;
        double p = fmadd(pS5, t, pS4);
        p = fmadd(t, p, pS3);
        double q = fmadd(qS4, t, qS3);
        p = fmadd(t, p, pS2);
        q = fmadd(t, q, qS2);
        p = fmadd(t, p, pS1);
        q = fmadd(t, q, qS1);
        p = fmadd(t, p, pS0);
        q = fmadd(t, q, 1.0);
        p = t * p;
        return fmadd(x, p / q, x);
    }
    // 1 > |x| >= 0.5
    const double w = 1.0 - std::fabs(x);
    const double t = 0.5 * w;
    double p = fmadd(pS5, t, pS4);
    double q = fmadd(qS4, t, qS3);
    p = fmadd(t, p, pS3);
    q = fmadd(t, q, qS2);
    p = fmadd(t, p, pS2);
    q = fmadd(t, q, qS1);
    p = fmadd(t, p, pS1);
    q = fmadd(t, q, 1.0);
    p = fmadd(t, p, pS0);
    p = t * p;
    const double s = std::sqrt(t); // __ieee754_sqrt 800A2D34: correctly rounded
    double result;
    if (ix >= 0x3FEF3333) { // |x| > 0.975
        const double ww = p / q;
        result = pio2_hi - fmsub(2.0, fmadd(s, ww, s), pio2_lo);
    } else {
        const double df = with_words(std::uint32_t(high_word(s)), 0);
        const double c = fnmsub(df, df, t) / (s + df);
        const double r = p / q;
        const double two_s = 2.0 * s;
        const double pp = fmsub(two_s, r, fnmsub(2.0, c, pio2_lo));
        const double qq = fnmsub(2.0, df, pio4_hi);
        result = pio4_hi - (pp - qq);
    }
    return hx > 0 ? result : -result;
}

double acos(double x) {
    constexpr double pi = 3.14159265358979311600e+00, pio2_hi = 1.57079632679489655800e+00,
                     pio2_lo = 6.12323399573676603587e-17;
    constexpr double pS0 = 1.66666666666666657415e-01, pS1 = -3.25565818622400915405e-01,
                     pS2 = 2.01212532134862925881e-01, pS3 = -4.00555345006794114027e-02,
                     pS4 = 7.91534994289814532176e-04, pS5 = 3.47933107596021167570e-05;
    constexpr double qS1 = -2.40339491173441421878e+00, qS2 = 2.02094576023350569471e+00,
                     qS3 = -6.88283971605453293030e-01, qS4 = 7.70381505559019352791e-02;
    const std::int32_t hx = high_word(x), ix = hx & 0x7fffffff;
    if (ix >= 0x3ff00000) { // |x| >= 1
        if (((ix - 0x3ff00000) | std::int32_t(low_word(x))) == 0) return hx > 0 ? 0.0 : pi;
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (ix < 0x3fe00000) { // |x| < 0.5
        if (ix <= 0x3c600000) return pio2_hi;
        const double z = x * x;
        double p = fmadd(pS5, z, pS4);
        p = fmadd(z, p, pS3);
        double q = fmadd(qS4, z, qS3);
        p = fmadd(z, p, pS2);
        q = fmadd(z, q, qS2);
        p = fmadd(z, p, pS1);
        q = fmadd(z, q, qS1);
        p = fmadd(z, p, pS0);
        q = fmadd(z, q, 1.0);
        p = z * p;
        const double r = p / q;
        return pio2_hi - (x - fnmsub(x, r, pio2_lo));
    }
    if (hx < 0) { // x < -0.5
        const double z = 0.5 * (1.0 + x);
        const double s = std::sqrt(z); // __ieee754_sqrt: correctly rounded
        double p = fmadd(pS5, z, pS4);
        p = fmadd(z, p, pS3);
        double q = fmadd(qS4, z, qS3);
        p = fmadd(z, p, pS2);
        q = fmadd(z, q, qS2);
        p = fmadd(z, p, pS1);
        q = fmadd(z, q, qS1);
        p = fmadd(z, p, pS0);
        q = fmadd(z, q, 1.0);
        p = z * p;
        const double r = p / q;
        const double w = fmsub(r, s, pio2_lo);
        return fnmsub(2.0, s + w, pi);
    }
    // x > 0.5
    const double z = 0.5 * (1.0 - x);
    const double s = std::sqrt(z);
    const double df = with_words(std::uint32_t(high_word(s)), 0);
    double p = fmadd(pS5, z, pS4);
    p = fmadd(z, p, pS3);
    const double c_top = fnmsub(df, df, z);
    p = fmadd(z, p, pS2);
    double q = fmadd(qS4, z, qS3);
    p = fmadd(z, p, pS1);
    q = fmadd(z, q, qS2);
    p = fmadd(z, p, pS0);
    q = fmadd(z, q, qS1);
    const double sum = s + df;
    p = z * p;
    q = fmadd(z, q, 1.0);
    const double c = c_top / sum;
    const double r = p / q;
    const double w = fmadd(r, s, c);
    return 2.0 * (df + w);
}

double atan(double x) {
    static constexpr double atanhi[4] = {4.63647609000806093515e-01, 7.85398163397448278999e-01,
                                         9.82793723247329054082e-01, 1.57079632679489655800e+00};
    static constexpr double atanlo[4] = {2.26987774529616870924e-17, 3.06161699786838301793e-17,
                                         1.39033110312309984516e-17, 6.12323399573676603587e-17};
    static constexpr double aT[11] = {3.33333333333329318027e-01, -1.99999999998764832476e-01,
                                      1.42857142725034663711e-01, -1.11111104054623557880e-01,
                                      9.09088713343650656196e-02, -7.69187620504482999495e-02,
                                      6.66107313738753120669e-02, -5.83357013379057348645e-02,
                                      4.97687799461593236017e-02, -3.65315727442169155270e-02,
                                      1.62858201153657823623e-02};
    const std::int32_t hx = high_word(x), ix = hx & 0x7fffffff;
    int id;
    if (ix >= 0x44100000) { // |x| >= 2^66
        if (ix > 0x7ff00000 || (ix == 0x7ff00000 && low_word(x) != 0)) return x + x;
        if (hx > 0) return atanhi[3] + atanlo[3];
        return -atanhi[3] - atanlo[3];
    }
    if (ix < 0x3fdc0000) { // |x| < 0.4375
        if (ix < 0x3e200000 && 1.0e300 + x > 1.0) return x;
        id = -1;
    } else {
        const double ax = std::fabs(x);
        if (ix < 0x3ff30000) {
            if (ix < 0x3fe60000) { // 7/16 <= |x| < 11/16
                id = 0;
                x = fmsub(2.0, ax, 1.0) / (2.0 + ax);
            } else { // 11/16 <= |x| < 19/16
                id = 1;
                x = (ax - 1.0) / (1.0 + ax);
            }
        } else if (ix < 0x40038000) { // |x| < 2.4375
            id = 2;
            x = (ax - 1.5) / fmadd(1.5, ax, 1.0);
        } else { // 2.4375 <= |x| < 2^66
            id = 3;
            x = -1.0 / ax;
        }
    }
    const double z = x * x;
    const double w = z * z;
    double s1 = fmadd(w, aT[10], aT[8]);
    double s2 = fmadd(w, aT[9], aT[7]);
    s1 = fmadd(w, s1, aT[6]);
    s2 = fmadd(w, s2, aT[5]);
    s1 = fmadd(w, s1, aT[4]);
    s2 = fmadd(w, s2, aT[3]);
    s1 = fmadd(w, s1, aT[2]);
    s2 = fmadd(w, s2, aT[1]);
    s1 = fmadd(w, s1, aT[0]);
    s2 = w * s2;
    s1 = z * s1;
    if (id < 0) return fnmsub(x, s1 + s2, x);
    const double result = atanhi[id] - (fmsub(x, s1 + s2, atanlo[id]) - x);
    return hx < 0 ? -result : result;
}

double atan2(double y, double x) {
    constexpr double pi = 3.1415926535897931160E+00, pi_lo = 1.2246467991473531772E-16;
    constexpr double pi_o_2 = 1.5707963267948965580E+00, pi_o_4 = 7.8539816339744827900E-01;
    constexpr double three_pi_o_4 = 2.356194490192344836998;
    const std::int32_t hx = high_word(x), hy = high_word(y);
    const std::uint32_t lx = low_word(x), ly = low_word(y);
    const std::int32_t ix = hx & 0x7fffffff, iy = hy & 0x7fffffff;
    if (std::uint32_t(ix | ((lx | (0u - lx)) >> 31)) > 0x7ff00000u ||
        std::uint32_t(iy | ((ly | (0u - ly)) >> 31)) > 0x7ff00000u)
        return x + y; // NaN
    if (((hx - 0x3ff00000) | std::int32_t(lx)) == 0) return atan(y); // x = 1.0
    const int m = ((hy >> 31) & 1) | ((hx >> 30) & 2);
    if ((iy | std::int32_t(ly)) == 0) { // y = 0
        switch (m) {
        case 0:
        case 1: return y;
        case 2: return pi;
        default: return -pi;
        }
    }
    if ((ix | std::int32_t(lx)) == 0) return hy < 0 ? -pi_o_2 : pi_o_2;
    if (ix == 0x7ff00000) {
        if (iy == 0x7ff00000) {
            switch (m) {
            case 0: return pi_o_4;
            case 1: return -pi_o_4;
            case 2: return three_pi_o_4;
            default: return -three_pi_o_4;
            }
        }
        switch (m) {
        case 0: return 0.0;
        case 1: return -0.0;
        case 2: return pi;
        default: return -pi;
        }
    }
    if (iy == 0x7ff00000) return hy < 0 ? -pi_o_2 : pi_o_2;
    const std::int32_t k = (iy - ix) >> 20;
    double z;
    if (k > 60) z = pi_o_2; // |y / x| > 2^60
    else if (hx < 0 && k < -60) z = 0.0;
    else z = atan(std::fabs(y / x));
    switch (m) {
    case 0: return z;
    case 1: return -z;
    case 2: return pi - (z - pi_lo);
    default: return (z - pi_lo) - pi;
    }
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
