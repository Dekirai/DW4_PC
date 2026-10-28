// Gekko arithmetic, the MSL math library and the camera / ray-test routines
// against bit patterns of the original: Dolphin logs of the stag0204 cannon
// scene (OSReport hooks, round 42: G 0x802F43B4, E 0x802F440C, V 0x8034C690,
// M 0x8034C6B8, B 0x802F32C8, T 0x80123984, U 0x80123A54) and, for the MSL
// functions, the DOL's own instructions run with Dolphin's double arithmetic.
#include "dw4/engine/gekko_math.hpp"
#include "dw4/motion.hpp"
#include "dw4/msl_math.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace dw4::engine;
int checks{};
void check(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
float f(std::uint32_t bits) { return std::bit_cast<float>(bits); }
std::uint32_t b(float value) { return std::bit_cast<std::uint32_t>(value); }
double d(std::uint64_t bits) { return std::bit_cast<double>(bits); }
std::uint64_t b(double value) { return std::bit_cast<std::uint64_t>(value); }
Vec3f v3(const std::array<std::uint32_t, 3>& w) { return {f(w[0]), f(w[1]), f(w[2])}; }
bool same(const Vec3f& v, const std::array<std::uint32_t, 3>& w) { return b(v.x) == w[0] && b(v.y) == w[1] && b(v.z) == w[2]; }
// The 12 logged words of a matrix: rows 0..3, columns 0..2 (column 3 is 0, 0, 0, 1).
Mat4 m12(const std::array<std::uint32_t, 12>& w) {
    Mat4 m;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 3; ++column) m(row, column) = f(w[std::size_t(row * 3 + column)]);
    return m;
}
bool same(const Mat4& m, const std::array<std::uint32_t, 12>& w) {
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 3; ++column)
            if (b(m(row, column)) != w[std::size_t(row * 3 + column)]) return false;
    return true;
}

void single_rounding() {
    // fmadds rounds the exact a * c + b once: the camera translation z of
    // n 5488 (eye ty -2.4e-14 times R12, then 140 * R22 and the pivot z fused
    // on) is 0x43CB5975; a double FMA rounded again to single gives ...76.
    const float s = gekko::muls(f(0xa8da827a), f(0x3d8b43b4));
    float v = gekko::madds(0.0f, f(0x3f7d1c78), s);
    v = gekko::madds(f(0x430c0000), f(0xbe08afb0), v);
    v = gekko::madds(1.0f, f(0x43d4b178), v);
    check(b(v) == 0x43cb5975, "fmadds rounds once (stag0204 camera translation n 5488)");
    float twice = float(std::fma(0.0, double(f(0x3f7d1c78)), double(s)));
    twice = float(std::fma(double(f(0x430c0000)), double(f(0xbe08afb0)), double(twice)));
    twice = float(std::fma(1.0, double(f(0x43d4b178)), double(twice)));
    check(b(twice) == 0x43cb5976, "a double fma then frsp would round twice");
    // frsqrte from the hardware table (Dolphin's model).
    check(b(gekko::frsqrte(1.0)) == 0x3feffe8000000000ull && gekko::frsqrte(1.0) == 0.99981689453125, "frsqrte(1)");
    check(b(gekko::frsqrte(0.5)) == b(1.413970947265625), "frsqrte(0.5)");
    check(b(gekko::frsqrte(4.0)) == b(0.499908447265625), "frsqrte(4)");
    for (const float x : {1.0f, 2.0f, 0.75f, 1234.5f, 1e-3f})
        check(gekko::sqrt_newton(x) == std::sqrt(x), "frsqrte + three Newton steps give the float square root");
}

void msl() {
    struct Case {
        const char* name;
        std::uint64_t x, y, expected;
    };
    // Each differs from the correctly rounded result in the last bit: the
    // DOL's fdlibm with CodeWarrior's fused multiply-adds.
    static constexpr Case cases[] = {
        {"sin", 0xbffc953c3d459e20ull, 0, 0xbfef424617db6638ull},
        {"sin", 0xbffd4985eaecdd54ull, 0, 0xbfeeed654853226eull},
        {"sin", 0xc00313d86316fe00ull, 0, 0xbfe5f939b56fca73ull},
        {"sin", 0x4011d39efece06c0ull, 0, 0xbfeef5977d042062ull},
        {"cos", 0x4010c0e8570c7230ull, 0, 0xbfe002dd77f0f653ull},
        {"cos", 0xc010bebffc434335ull, 0, 0xbfe011cd119e69d6ull},
        {"cos", 0x40124766b2ce2848ull, 0, 0xbfc232e252d28fafull},
        {"cos", 0xc010820064628558ull, 0, 0xbfe1ae955f83a2e6ull},
        {"asin", 0xbfdd9ca3b9cf781cull, 0, 0xbfdec910dc76991aull},
        {"asin", 0x3fe00c06cab4c846ull, 0, 0x3fe0cf370b8eb0e0ull},
        {"asin", 0x3fefec3766f5b3d6ull, 0, 0x3ff80542af14cf21ull},
        {"asin", 0xbfef5498b3b33f9eull, 0, 0xbff5da9e51a562b7ull},
        {"acos", 0x3fe0efd938a6ad74ull, 0, 0x3ff035703f386603ull},
        {"acos", 0xbfebca807925e464ull, 0, 0x4004fbb4825cb926ull},
        {"acos", 0x3fe4cdc99602ebdaull, 0, 0x3feb9e21e08f12c7ull},
        {"acos", 0x3fe078a66d2ba9ecull, 0, 0x3ff07b50a3deade1ull},
        {"atan2", 0x3ff4b692511e43e8ull, 0xbfe956de6226e834ull, 0x4000f54192b7a5c6ull},
        {"atan2", 0xbffb3578976d64caull, 0x4001633260f78becull, 0xbfe53ef11c05ea1eull},
        {"atan2", 0x3ff86dfe8c455448ull, 0x3ff84977c743a1c0ull, 0x3fe939f974604698ull},
        {"atan2", 0xbfcd2b92024cfb60ull, 0x40027c6c242dec0aull, 0xbfb92a7cf5fc4260ull},
    };
    for (const auto& c : cases) {
        const std::string name = c.name;
        const double x = d(c.x), y = d(c.y);
        const double r = name == "sin" ? gekko::sin(x) : name == "cos" ? gekko::cos(x) : name == "asin" ? gekko::asin(x)
                         : name == "acos" ? gekko::acos(x) : gekko::atan2(x, y);
        check(b(r) == c.expected, "MSL " + name);
    }
    check(gekko::atan2(0.0, -1.0) == 3.1415926535897931160 && gekko::acos(1.0) == 0.0 && gekko::cos(0.0) == 1.0, "MSL special values");
}

void camera() {
    // B n 5487 (the box camera's live eye / target after the blend) and the
    // players' centre: the path pivot -> target -> eye through 80128774 gives
    // the camera matrix G of n 5488.
    Mat4 pivot, target, eye;
    pivot(3, 0) = f(0x43faaa54); pivot(3, 1) = f(0x42700000); pivot(3, 2) = f(0x43d4b178);
    target = m12({0xbe18aec8, 0x3c6d3100, 0x3f7d1c78, 0x3f086647, 0x3f57ef7c, 0x3d8b43b4, 0xbf553f2c, 0x3f0974b8, 0xbe08afb0,
                  0x00000000, 0xc111f977, 0x00000000});
    eye(3, 1) = f(0xa8da827a); eye(3, 2) = f(0x430c0000);
    Mat4 world;
    for (const Mat4* node : {&pivot, &target, &eye}) gekko::premultiply(world, *node);
    const std::array<std::uint32_t, 12> g{0xbe18aec8, 0x3c6d3100, 0x3f7d1c78, 0x3f086647, 0x3f57ef7c, 0x3d8b43b4,
                                          0xbf553f2c, 0x3f0974b8, 0xbe08afb0, 0x43c05b0e, 0x42fc187a, 0x43cb5975};
    check(same(world, g), "80128774 path product (G n 5488)");
    // 8012947C / 80129134: the Euler round trip (E n 5488).
    float x = 0, y = 0, z = 0;
    gekko::matrix_euler(world, x, y, z);
    check(b(x) == 0x402ae84b && b(y) == 0xbfb5d07d && b(z) == 0x4042de0d, "8012947C Euler angles (E n 5488)");
    const Vec3f t{world(3, 0), world(3, 1), world(3, 2)};
    gekko::euler_matrix(world, x, y, z);
    world(3, 0) = t.x; world(3, 1) = t.y; world(3, 2) = t.z;
    const std::array<std::uint32_t, 12> e{0xbe18aebc, 0x3c6d30dc, 0x3f7d1c76, 0x3f086643, 0x3f57ef7f, 0x3d8b43b4,
                                          0xbf553f2e, 0x3f0974b4, 0xbe08afa2, 0x43c05b0e, 0x42fc187a, 0x43cb5975};
    check(same(world, e), "80129134 rebuilt matrix (E n 5488)");
    // 8012A860: the published view matrix (V n 5488).
    Mat4 view;
    gekko::view_inverse(world, view);
    const std::array<std::uint32_t, 12> v{0xbe18aebc, 0x3f086643, 0xbf553f2e, 0x3c6d30dc, 0x3f57ef7f, 0x3f0974b4,
                                          0x3f7d1c76, 0x3d8b43b4, 0xbe08afa2, 0xc3ad4940, 0xc3a97a3b, 0x439988fc};
    check(same(view, v), "8012A860 view matrix (V n 5488)");
    // 8012A92C + 8012D434: the player input's forward direction (V n 5488:
    // not the camera's row 2 - the general inverse rounds).
    Mat4 inverse;
    check(gekko::invert(view, inverse), "8012A92C inverts");
    const Vec3f forward = gekko::rotate({0, 0, 1}, inverse);
    check(same(forward, {0xbf553f2c, 0x3f0974b3, 0xbe08afa1}), "8012A92C forward direction (V n 5488)");
    // 8034C67C..8034C6B4: the wanted yaw (M n 5488).
    const float sx = f(0xbf260dd6), sy = f(0x3ec8a60e);
    const float yaw = gekko::adds(float(gekko::atan2(forward.x, forward.z)), float(gekko::atan2(sx, -sy)));
    check(b(yaw) == 0xc0760380, "wanted yaw (M n 5488)");
    // A singular matrix leaves the result alone.
    Mat4 zero, untouched;
    zero.m.fill(0.0f);
    check(!gekko::invert(zero, untouched) && untouched.m == Mat4{}.m, "8012A92C singular");
}

void ray_test() {
    // T / U n 5300..5303: vertices a, b, c, the cross product (b - a) x
    // (c - a) of 8012CB74, the unit normal and its fused dot with the
    // segment direction.
    struct Case {
        std::array<std::uint32_t, 9> vertices;
        std::array<std::uint32_t, 3> cross, normal, direction;
        std::uint32_t facing;
    };
    static constexpr Case cases[] = {
        {{0x4240e178, 0xc2480000, 0xc200a260, 0x4240e178, 0x43480000, 0xc200a260, 0x4200f8f0, 0xc2480000, 0xc240a7a4},
         {0xc57a1492, 0x00000000, 0x4579a453}, {0xbf352d98, 0x00000000, 0x3f34dc46}, {0x3f74987f, 0x00000000, 0xbe971d7d}, 0xbf627cc3},
        {{0x4135b760, 0x43480000, 0xc2635870, 0xc1341f00, 0x43480000, 0xc2636cbc, 0xc1341f00, 0xc2480000, 0xc2636cbc},
         {0xc09e91c0, 0x31900000, 0x45b0adad}, {0xba65c2b5, 0x2b50a673, 0x3f7ffffa}, {0x3f74987f, 0x00000000, 0xbe971d7d}, 0xbe978b3d},
        {{0xc240a7a0, 0xc2480000, 0xc200f8ec, 0xc240a7a0, 0x43480000, 0xc200f8ec, 0xc2635870, 0xc2480000, 0xc135b760},
         {0x45a32ba3, 0x80000000, 0x450782ac}, {0x3f6c6d57, 0x80000000, 0x3ec45952}, {0x3f74987f, 0x00000000, 0xbe971d7d}, 0x3f44eb3a},
        {{0xc2636cc0, 0x43480000, 0x41341f10, 0xc240e178, 0x43480000, 0x4200a264, 0xc240e178, 0xc2480000, 0x4200a264},
         {0x45a34a00, 0xb6f46000, 0xc506f011}, {0x3f6c9956, 0xb0b10b66, 0xbec384e5}, {0x3f74987f, 0x00000000, 0xbe971d7d}, 0x3f7ee990},
        {{0xc135b760, 0xc2480000, 0x42635874, 0xc135b760, 0x43480000, 0x42635874, 0x41341f20, 0xc2480000, 0x42636cc0},
         {0x409e91c0, 0x00000000, 0xc5b0adbc}, {0x3a65c2a1, 0x00000000, 0xbf7ffffa}, {0x3f74987f, 0x00000000, 0xbe971d7d}, 0x3e978b3d},
        {{0x4200a268, 0x43480000, 0x4240e178, 0x4240a7a8, 0x43480000, 0x4200f8e8, 0x4240a7a8, 0xc2480000, 0x4200f8e8},
         {0xc579a472, 0xb69e8000, 0xc57a1482}, {0xbf34dc56, 0xb065a8ee, 0xbf352d86}, {0x3f74987f, 0x00000000, 0xbe971d7d}, 0xbeeea8c5},
        {{0x42636cc0, 0xc2480000, 0xc1341f30, 0x42636cc0, 0x43480000, 0xc1341f30, 0x4240e178, 0xc2480000, 0xc200a260},
         {0xc5a349e9, 0x00000000, 0x4506f011}, {0xbf6c9950, 0x00000000, 0x3ec384fc}, {0x3f74987f, 0x00000000, 0xbe971d7d}, 0xbf7ee98f},
    };
    for (const auto& c : cases) {
        const Vec3f a = v3({c.vertices[0], c.vertices[1], c.vertices[2]});
        const Vec3f bb = v3({c.vertices[3], c.vertices[4], c.vertices[5]});
        const Vec3f cc = v3({c.vertices[6], c.vertices[7], c.vertices[8]});
        const Vec3f e1{gekko::subs(bb.x, a.x), gekko::subs(bb.y, a.y), gekko::subs(bb.z, a.z)};
        const Vec3f e2{gekko::subs(cc.x, a.x), gekko::subs(cc.y, a.y), gekko::subs(cc.z, a.z)};
        check(same(gekko::cross(e1, e2), c.cross), "8012CB74 fused cross product");
        const Vec3f n = gekko::triangle_normal(a, bb, cc);
        check(same(n, c.normal), "801238E8 face normal");
        check(b(gekko::dot(n, v3(c.direction))) == c.facing, "801238E8 fused facing dot product");
    }
    // 801F6050's vertex space for a moving child (the stag0203 car turned by
    // pi, its matrix keeping sin(pi) = 8.74e-8): Y n 4400, 801252D0 with the
    // inverse, and H n 4401, the local hit back through 8012D498.
    {
        Mat4 inverse, top;
        const std::uint32_t inv[16] = {0xbf800000, 0x80000000, 0xb3bbbd2e, 0x80000000, 0x80000000, 0x3f800000, 0x80000000, 0x00000000,
                                       0x33bbbd2e, 0x80000000, 0xbf800000, 0x80000000, 0xc50fc000, 0xc1200000, 0x44a27ffe, 0x3f800000};
        const std::uint32_t t16[16] = {0xbf800000, 0x00000000, 0x33bbbd2e, 0x00000000, 0x00000000, 0x3f800000, 0x00000000, 0x00000000,
                                       0xb3bbbd2e, 0x00000000, 0xbf800000, 0x00000000, 0xc50fc000, 0x41200000, 0x44a28000, 0x3f800000};
        for (int i = 0; i < 16; ++i) {
            inverse.m[std::size_t(i)] = f(inv[i]);
            top.m[std::size_t(i)] = f(t16[i]);
        }
        Vec3f origin{f(0xc50fc000), f(0x41600000), f(0x44a05000)}, direction{f(0x3f350492), 0.0f, f(0xbf350555)};
        float length = f(0x41280001);
        gekko::transform_segment(origin, direction, length, inverse);
        check(same(origin, {0x00000000, 0x40800000, 0x418c0000}) && same(direction, {0xbf350492, 0x00000000, 0x3f350555}) &&
                  b(length) == 0x41280001,
              "801252D0 segment in the child's space (Y n 4400)");
        check(same(gekko::transform_point(v3({0xc0400000, 0x00000000, 0x41680000}), top), {0xc50f9000, 0x41200000, 0x44a0b000}),
              "8012D498 local hit back to the world (H n 4401)");
        Mat4 inverted;
        check(gekko::invert(top, inverted) && inverted.m == inverse.m, "8012A92C of the child's matrix");
    }
    // The CHECKWALL side axis: the direction through the Euler (0, pi/2, 0)
    // matrix keeps cos(pi/2) = -4.371139e-8 (802FE974): (0.3346, 0, 0.9423)
    // is not simply (0.9423, 0, -0.3346).
    Mat4 quarter;
    gekko::euler_matrix(quarter, 0.0f, 1.5707964f, 0.0f);
    check(b(quarter.m[0]) == 0xb33bbd2e && b(quarter.m[8]) == 0x3f800000 && b(quarter.m[2]) == 0xbf800000, "Euler (0, pi/2, 0)");
    const Vec3f direction{f(0x3eab5942), 0.0f, f(0x3f713d00)};
    const Vec3f side = gekko::rotate(direction, quarter);
    // z = -x + z * cos(pi/2) rounded once: one bit beyond -x (0xBEAB5942).
    check(b(side.x) == b(direction.z) && b(side.y) == 0 && b(side.z) == 0xbeab5943, "the quarter-turn side axis rounds");
}

void ride() {
    // 8030CEB0 on the moving stag0203 train (Dolphin n 4629, hooks A
    // 0x8030CF60, B 0x8030CF6C, R 0x8030CFA0): last frame's matrix of the car
    // with its translation cleared, inverted in place by 8012A92C; the rider's
    // matrix (its offset as the translation) times that, times the car's
    // current matrix, both through the paired-single 80128690. (The port's
    // plain inverse and products matched 1 of 60 inverses and 21 of 61
    // results of this ride.)
    Mat4 last = m12({0xbf800000, 0x329e31ac, 0x33bade71, 0xb2b23f97, 0x3f800001, 0x311bd90f, 0xb3bb8019, 0x30f997f8, 0xbf800001,
                     0xc50fc000, 0x411fffc4, 0x449718bf});
    last(3, 0) = last(3, 1) = last(3, 2) = 0.0f;
    check(gekko::invert(last, last), "8012A92C inverts the car's rotation");
    check(same(last, {0xbf800000, 0x329e31ab, 0xb3bade6f, 0xb2b23f96, 0x3f7ffffe, 0x311bd905, 0x33bb8017, 0x30f997e6, 0xbf7ffffe, 0x00000000,
                      0x80000000, 0x80000000}),
          "8012A92C in place (A n 4629)");
    const Mat4 rider = m12({0xbf7ffffe, 0x329e31ac, 0xafdec077, 0xb2b23f94, 0x3f800000, 0x311bd909, 0x2ef45000, 0x30f99814, 0xbf800001,
                            0x00000000, 0x00000000, 0xc18c0000});
    const Mat4 car = m12({0xbf800001, 0x311d9300, 0x33bf0082, 0x32195c0c, 0x3f800001, 0x30a6c16b, 0xb3b80dba, 0xb14d0104, 0xbf800001,
                          0xc50fc001, 0x4120002c, 0x4496d129});
    check(same(gekko::multiply(gekko::multiply(rider, last), car),
               {0xbf800000, 0x311d9304, 0x30d0d462, 0x32195c0f, 0x3f800000, 0x30a6c160, 0x30ebdcc0, 0xb14d00f0, 0xbf800001, 0xc50fc001,
                0x4120002c, 0x4494a129}),
          "80128690 twice: the rider on the car (R n 4629)");
}

void turn_basis() {
    // The model turn basis 801292D4 (dw4::original_model_turn_basis) calls
    // the MSL sin / cos (800A2AE4 / 800A257C) like gekko::euler_matrix: at
    // 0.100430205 the MSL sin is 0x1.9aabc3b509d5ap-4 (a host libm may
    // return the double below), at 0.100845315 the MSL cos is
    // 0x1.fd6613fed7618p-1. (Over every float angle up to 2^19 pi / 2 the
    // two agree once rounded to single, round 44.)
    check(b(dw4::msl::sin(double(f(0x3dcdae5a)))) == 0x3FB9AABC3B509D5Aull, "MSL sin 0.100430205");
    check(b(dw4::msl::cos(double(f(0x3dce87fd)))) == 0x3FEFD6613FED7618ull, "MSL cos 0.100845315");
    check(gekko::sin(0.3) == dw4::msl::sin(0.3) && gekko::atan2(0.3, -2.0) == dw4::msl::atan2(0.3, -2.0), "gekko forwards to the MSL");
    std::array<float, 16> m{};
    dw4::original_model_turn_basis(m, {0.0f, f(0x3dcdae5a), 0.0f});
    check(b(m[2]) == 0xbdcd55e2u && b(m[8]) == 0x3dcd55e2u, "801292D4: -sin y and sin y from the MSL (frsp)");
    dw4::original_model_turn_basis(m, {0.0f, f(0x3dce87fd), 0.0f});
    check(b(m[0]) == 0x3f7eb30au && b(m[10]) == 0x3f7eb30au, "801292D4: cos y from the MSL (frsp)");
    Mat4 e;
    gekko::euler_matrix(e, 0.0f, f(0x3dcdae5a), 0.0f);
    dw4::original_model_turn_basis(m, {0.0f, f(0x3dcdae5a), 0.0f});
    check(std::memcmp(e.m.data(), m.data(), sizeof m) == 0, "The core's turn basis and gekko::euler_matrix agree");
}
} // namespace

int main() {
    try {
        single_rounding();
        msl();
        camera();
        ray_test();
        ride();
        turn_basis();
        std::cout << checks << " gekko math checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Gekko math test failure: " << error.what() << '\n';
        return 1;
    }
}
