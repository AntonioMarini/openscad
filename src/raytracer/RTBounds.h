#ifndef RT_BOUNDS_H
#define RT_BOUNDS_H

#include <Eigen/Dense>
#include <vector>

enum class PrimitiveType : int;
enum class OperationType : int;
struct RTCSGNode;

struct alignas(16) RTBounds {
    // When bounds_type == 1 (OBB): standard inverse world-to-[-1,1]^3 transform.
    // When bounds_type == 0 (AABB): col(0).xyz = world-space min, col(1).xyz = world-space max;
    //   cols 2-3 and w components are unused (zero).
    Eigen::Matrix4f inv_transform;
    unsigned int skip;
    unsigned int bounds_type;  // 0 = AABB (slab test), 1 = OBB (matrix test)
    unsigned int _pad[2];

    RTBounds() : inv_transform(Eigen::Matrix4f::Identity()), skip(0), bounds_type(1)
    {
        _pad[0] = _pad[1] = 0;
    }

    float getVolume() const;
    bool containsPoint(const Eigen::Vector3f&) const;
    std::vector<Eigen::Vector3f> getCorners() const;

    // Return a copy with all geometry shifted by 'offset' (for numerical stability).
    RTBounds shifted(const Eigen::Vector3f& offset) const;

    static RTBounds buildPrimitiveBounds(const RTCSGNode& node);
    static RTBounds buildPrimitiveAABB(const RTCSGNode& node);
    static RTBounds buildOperationBounds(OperationType optype, const RTBounds& left,
                                         const RTBounds& right);
    static Eigen::Vector3f half_sizes(PrimitiveType ptype);

    // Use OBB only when its volume is strictly less than this fraction of the AABB volume.
    static constexpr float TIGHT_BOUNDS_THRESHOLD = 0.5f;

private:
    static RTBounds makeAABB(const Eigen::Vector3f& mn, const Eigen::Vector3f& mx);
};

#endif
