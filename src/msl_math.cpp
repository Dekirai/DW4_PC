#include "dw4/msl_math.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

// Built with /fp:strict (no contraction): every operation below rounds where
// the original's instruction rounds; std::fma marks the fused ones.
namespace dw4::msl {

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

} // namespace dw4::msl
