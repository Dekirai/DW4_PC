#include "dw4/collision_math.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace dw4 {
namespace {
[[noreturn]] void fail(const char* message) { throw CollisionMathError(message); }
constexpr float minimum_normal=std::numeric_limits<float>::min();

template<std::size_t N> void require_finite(const std::array<float,N>& values,const char* message) {
    for (const auto value:values) if (!std::isfinite(value)) fail(message);
}
void require_finite(const CollisionBox& box) {
    require_finite(box.minimum,"Collision box minimum contains a non-finite component.");
    require_finite(box.maximum,"Collision box maximum contains a non-finite component.");
}
float single(float value) {
    return std::abs(value)<minimum_normal ? std::copysign(0.0f,value) : value;
}

float product(float a,float b) {
    const float value=a*b;
    const float magnitude=std::abs(value);
    if (magnitude<minimum_normal || (magnitude==minimum_normal &&
        std::abs(static_cast<double>(a)*static_cast<double>(b))<minimum_normal))
        return std::copysign(0.0f,value);
    return value;
}
float sum(float a,float b) {
    const float value=a+b;
    return std::abs(value)<minimum_normal ? std::copysign(0.0f,value) : value;
}
float fused(float a,float b,float addend) {
    const float value=std::fma(a,b,addend);
    const float magnitude=std::abs(value);
    if (magnitude<minimum_normal) return std::copysign(0.0f,value);
    if (magnitude!=minimum_normal) return value;
    // A binary32 product is exact in binary64. TwoSum retains a deficit too
    // small for the double sum, so NI tininess is decided before rounding.
    const double multiplied=static_cast<double>(a)*static_cast<double>(b);
    const double added=multiplied+static_cast<double>(addend);
    const double virtual_addend=added-multiplied;
    const double error=(multiplied-(added-virtual_addend))+
        (static_cast<double>(addend)-virtual_addend);
    if (std::abs(added)<minimum_normal || (std::abs(added)==minimum_normal &&
        ((added>0 && error<0) || (added<0 && error>0))))
        return std::copysign(0.0f,value);
    return value;
}
float reciprocal(float denominator) {
    const float value=1.0f/denominator;
    // Exact 1/denominator is tiny iff |denominator| > 2^126. This also
    // retains the original signed zero result for infinite computed W.
    return std::abs(denominator)>0x1p126f ? std::copysign(0.0f,value) : value;
}
std::array<float,3> transform_offset(const AnimationMatrix& matrix,const std::array<float,3>& offset) {
    std::array<float,4> transformed{};
    for (std::size_t row=0;row<4;++row) {
        float value=product(offset[1],matrix[4+row]);
        value=fused(offset[0],matrix[row],value);
        value=fused(offset[2],matrix[8+row],value);
        transformed[row]=sum(matrix[12+row],value);
    }
    if (transformed[3]!=1.0f) {
        constexpr float epsilon=std::bit_cast<float>(std::uint32_t{0x350637bd});
        // 8012D548 clears W's sign only for the epsilon comparison; a small
        // magnitude replaces the actual denominator with positive epsilon.
        const float denominator=std::abs(transformed[3])<epsilon ? epsilon : transformed[3];
        const float inverse=reciprocal(denominator);
        for (std::size_t axis=0;axis<3;++axis) transformed[axis]=product(transformed[axis],inverse);
    }
    std::array<float,3> result{transformed[0],transformed[1],transformed[2]};
    for (const auto value:result) if (!std::isfinite(value)) fail("Hitbox offset transform is non-finite.");
    return result;
}
}

CollisionBox original_transform_box(const AnimationMatrix& matrix,const CollisionBox& box) {
    require_finite(matrix,"Collision transform matrix contains a non-finite component.");
    require_finite(box);
    // 80122D6C checks only X before all arithmetic, preserving the complete
    // input box when that axis is inverted.
    if (box.minimum[0]>box.maximum[0]) return box;
    std::array<std::array<float,3>,3> edges{};
    for (std::size_t axis=0;axis<3;++axis) {
        const float length=sum(box.maximum[axis],-box.minimum[axis]);
        for (std::size_t row=0;row<3;++row) edges[axis][row]=product(length,matrix[axis*4+row]);
    }
    std::array<std::array<float,3>,8> corners{};
    corners[0]=transform_offset(matrix,box.minimum);
    for (std::size_t row=0;row<3;++row) {
        corners[1][row]=sum(corners[0][row],edges[2][row]);
        corners[2][row]=sum(corners[0][row],edges[1][row]);
        corners[3][row]=sum(corners[0][row],edges[0][row]);
        corners[4][row]=sum(corners[2][row],edges[2][row]);
        corners[5][row]=sum(corners[3][row],edges[1][row]);
        corners[6][row]=sum(corners[3][row],edges[2][row]);
        corners[7][row]=sum(corners[5][row],edges[2][row]);
    }
    constexpr float largest=std::numeric_limits<float>::max();
    CollisionBox result{{largest,largest,largest},{-largest,-largest,-largest}};
    for (const auto& corner:corners) {
        for (std::size_t axis=0;axis<3;++axis) {
            // These negated comparisons reproduce the original branch order,
            // including unordered intermediate values before final validation.
            if (!(result.minimum[axis]<=corner[axis])) result.minimum[axis]=corner[axis];
            if (!(result.maximum[axis]>=corner[axis])) result.maximum[axis]=corner[axis];
        }
    }
    require_finite(result);
    return result;
}

std::array<float,3> original_box_center(const CollisionBox& box) {
    require_finite(box);
    std::array<float,3> result{};
    for (std::size_t axis=0;axis<3;++axis)
        result[axis]=fused(0.5f,box.minimum[axis],product(0.5f,box.maximum[axis]));
    require_finite(result,"Collision box center is non-finite.");
    return result;
}

CollisionBoxDistances original_box_distances(const CollisionBox& box) {
    const auto center=original_box_center(box);
    CollisionBoxDistances result;
    for (std::size_t axis=0;axis<3;++axis) {
        float minimum=sum(center[axis],-box.minimum[axis]);
        float maximum=sum(box.maximum[axis],-center[axis]);
        // Original compare/conditional-negate preserves a negative zero.
        if (minimum<0.0f) minimum=-minimum;
        if (maximum<0.0f) maximum=-maximum;
        result.minimum[axis]=minimum;result.maximum[axis]=maximum;
    }
    require_finite(result.minimum,"Collision minimum distance is non-finite.");
    require_finite(result.maximum,"Collision maximum distance is non-finite.");
    return result;
}

CollisionBox original_body_box(const AnimationMatrix& matrix,const CollisionBox& box,
    const CollisionBoxDistances& distances) {
    require_finite(distances.minimum,"Collision minimum distance contains a non-finite component.");
    require_finite(distances.maximum,"Collision maximum distance contains a non-finite component.");
    const auto center=original_box_center(original_transform_box(matrix,box));
    CollisionBox result;
    for (std::size_t axis=0;axis<3;++axis) {
        // 803081D4..8254 explicitly rounds loaded distances with frsp before
        // subtraction/addition; unlike lfs, this flushes subnormal inputs.
        result.minimum[axis]=sum(single(center[axis]),-single(distances.minimum[axis]));
        result.maximum[axis]=sum(single(center[axis]),single(distances.maximum[axis]));
    }
    require_finite(result);
    return result;
}

bool original_boxes_overlap(const CollisionBox& incoming,const CollisionBox& body) {
    require_finite(incoming);require_finite(body);
    for (std::size_t axis=0;axis<3;++axis) {
        if (!(incoming.minimum[axis]<=body.maximum[axis])) return false;
        if (!(body.minimum[axis]<=incoming.maximum[axis])) return false;
    }
    return true;
}

CollisionBox original_hitbox_box(const AnimationMatrix& matrix,const std::array<float,3>& offset,
    const std::array<float,3>& box_size) {
    for (const auto value:matrix) if (!std::isfinite(value)) fail("Hitbox source matrix contains a non-finite component.");
    for (const auto value:offset) if (!std::isfinite(value)) fail("Hitbox offset contains a non-finite component.");
    for (const auto value:box_size) if (!std::isfinite(value)) fail("Hitbox extent contains a non-finite component.");
    const auto point=transform_offset(matrix,offset);
    // 80122D04 receives identical min/max. Its edge differences are +0 and
    // all eight finite candidates compare equal. Strict min/max replacement
    // retains the first transformed point, including its signed-zero words.
    CollisionBox result;
    for (std::size_t axis=0;axis<3;++axis) {
        const float half_maximum=product(0.5f,point[axis]);
        const float center=fused(0.5f,point[axis],half_maximum);
        const float half_extent=product(box_size[axis],0.5f);
        result.minimum[axis]=sum(center,-half_extent);
        result.maximum[axis]=sum(half_extent,center);
        if (!std::isfinite(result.minimum[axis]) || !std::isfinite(result.maximum[axis]))
            fail("Hitbox world bounds are non-finite.");
    }
    return result;
}
} // namespace dw4
