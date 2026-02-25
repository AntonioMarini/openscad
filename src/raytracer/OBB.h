#ifndef OBB_H
#define OBB_H

#include <Eigen/Dense>
#include <vector>

enum class PrimitiveType : int;
enum class OperationType : int;
struct RTCSGNode;

struct alignas(16) OBB {
    Eigen::Matrix4f inv_transform;
    unsigned int skip;
    unsigned int _pad[3];

    OBB() : inv_transform(Eigen::Matrix4f::Identity()){
        _pad[0] = _pad[1] = _pad[2] = 0;
    }

    float getVolume() const;
    bool containsPoint(const Eigen::Vector3f&) const;
    std::vector<Eigen::Vector3f> getCorners() const;

    static OBB buildPrimitiveOBB(const RTCSGNode& node);
    static OBB buildOperationOBB(OperationType optype, const OBB& leftOBB, const OBB& rightOBB);
    static Eigen::Vector3f half_sizes(PrimitiveType ptype);
};

#endif