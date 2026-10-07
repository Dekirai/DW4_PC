#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace dw4::engine {

// Alchemy stores matrices row-major and multiplies row vectors (v' = v * M),
// with the translation in elements 12..14. The same 16 floats are a valid
// column-major matrix for column-vector shaders, so no transpose is needed
// when uploading to OpenGL.
struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 identity() { return {}; }
    float& operator()(int row, int column) { return m[static_cast<std::size_t>(row * 4 + column)]; }
    float operator()(int row, int column) const { return m[static_cast<std::size_t>(row * 4 + column)]; }
};

struct Vec3f {
    float x{}, y{}, z{};
};
struct Vec4f {
    float x{}, y{}, z{}, w{};
};

// Row-vector product: applying a then b equals v * (a * b).
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a(i, k) * b(k, j);
            r(i, j) = s;
        }
    return r;
}

inline Vec3f transform_point(const Vec3f& v, const Mat4& m) {
    return {v.x * m(0, 0) + v.y * m(1, 0) + v.z * m(2, 0) + m(3, 0),
            v.x * m(0, 1) + v.y * m(1, 1) + v.z * m(2, 1) + m(3, 1),
            v.x * m(0, 2) + v.y * m(1, 2) + v.z * m(2, 2) + m(3, 2)};
}

inline Mat4 translation(float x, float y, float z) {
    Mat4 r;
    r(3, 0) = x;
    r(3, 1) = y;
    r(3, 2) = z;
    return r;
}

inline Mat4 scaling(float x, float y, float z) {
    Mat4 r;
    r(0, 0) = x;
    r(1, 1) = y;
    r(2, 2) = z;
    return r;
}

// General 4x4 inverse (row-major storage, layout independent).
bool invert(const Mat4& source, Mat4& result);

// Quaternion (x, y, z, w) to rotation matrix in Alchemy row-vector layout.
Mat4 quaternion_matrix(float x, float y, float z, float w);

// Right-handed perspective in OpenGL clip space, row-vector layout.
Mat4 perspective(float fov_y_radians, float aspect, float near_plane, float far_plane);

// The same from a horizontal field of view, as Alchemy's setPerspective
// (801DB6E4) builds it when the vertical one is not set: the width keeps
// tan(fov_x / 2), the height follows as tan(fov_y / 2) = tan(fov_x / 2) / aspect.
Mat4 perspective_horizontal(float fov_x_radians, float aspect, float near_plane, float far_plane);

// Right-handed look-at view matrix in row-vector layout.
Mat4 look_at(const Vec3f& eye, const Vec3f& target, const Vec3f& up);

inline float length(const Vec3f& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
inline Vec3f operator-(const Vec3f& a, const Vec3f& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3f operator+(const Vec3f& a, const Vec3f& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3f operator*(const Vec3f& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(const Vec3f& a, const Vec3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3f cross(const Vec3f& a, const Vec3f& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3f normalize(const Vec3f& v) {
    const float l = length(v);
    return l > 0 ? v * (1.0f / l) : v;
}

} // namespace dw4::engine
