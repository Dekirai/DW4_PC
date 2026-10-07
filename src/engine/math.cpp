#include "dw4/engine/math.hpp"

namespace dw4::engine {

bool invert(const Mat4& source, Mat4& result) {
    const auto& m = source.m;
    std::array<float, 16> inv{};
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (det == 0.0f || !std::isfinite(det)) return false;
    const float scale = 1.0f / det;
    for (std::size_t i = 0; i < 16; ++i) result.m[i] = inv[i] * scale;
    return true;
}

Mat4 quaternion_matrix(float x, float y, float z, float w) {
    Mat4 r;
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xy = x * y, xz = x * z, yz = y * z;
    const float wx = w * x, wy = w * y, wz = w * z;
    // Row-vector convention: rows are the rotated basis vectors.
    r(0, 0) = 1 - 2 * (yy + zz);
    r(0, 1) = 2 * (xy + wz);
    r(0, 2) = 2 * (xz - wy);
    r(1, 0) = 2 * (xy - wz);
    r(1, 1) = 1 - 2 * (xx + zz);
    r(1, 2) = 2 * (yz + wx);
    r(2, 0) = 2 * (xz + wy);
    r(2, 1) = 2 * (yz - wx);
    r(2, 2) = 1 - 2 * (xx + yy);
    return r;
}

Mat4 perspective(float fov_y, float aspect, float n, float f) {
    Mat4 r;
    const float t = 1.0f / std::tan(fov_y * 0.5f);
    r.m = {};
    r(0, 0) = t / aspect;
    r(1, 1) = t;
    r(2, 2) = (f + n) / (n - f);
    r(2, 3) = -1.0f;
    r(3, 2) = 2.0f * f * n / (n - f);
    return r;
}

Mat4 perspective_horizontal(float fov_x, float aspect, float n, float f) {
    return perspective(2.0f * std::atan(std::tan(fov_x * 0.5f) / aspect), aspect, n, f);
}

Mat4 look_at(const Vec3f& eye, const Vec3f& target, const Vec3f& up) {
    const Vec3f zaxis = normalize(eye - target);
    const Vec3f xaxis = normalize(cross(up, zaxis));
    const Vec3f yaxis = cross(zaxis, xaxis);
    Mat4 r;
    r(0, 0) = xaxis.x; r(1, 0) = xaxis.y; r(2, 0) = xaxis.z;
    r(0, 1) = yaxis.x; r(1, 1) = yaxis.y; r(2, 1) = yaxis.z;
    r(0, 2) = zaxis.x; r(1, 2) = zaxis.y; r(2, 2) = zaxis.z;
    r(3, 0) = -dot(xaxis, eye);
    r(3, 1) = -dot(yaxis, eye);
    r(3, 2) = -dot(zaxis, eye);
    return r;
}

} // namespace dw4::engine
