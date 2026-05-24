#ifndef PRIMITIVE_H
#define PRIMITIVE_H

#include <string>
#include <algorithm>
#include <cmath>
#include <Eigen/Dense>

enum class PrimitiveType : int { NONE = 0, SPHERE = 1, CUBE = 2, CYLINDER = 4 };

// Compute the projective matrix M that maps a cone (r1 bottom, r2 top) to
// the unit cylinder (x^2+z^2=1, y in [-0.5, 0.5]).
// For pure cylinders (r1==r2) this is an affine scale; for cones it's projective.
inline Eigen::Matrix4f computeConeToUnitCylinderMatrix(float r1, float r2)
{
  // Clamp near-zero radii to avoid singularity
  float rmax = std::max(r1, r2);
  float eps = 0.001f * rmax;
  r1 = std::max(r1, eps);
  r2 = std::max(r2, eps);

  float alpha = (r1 + r2) * 0.5f;
  float beta = r2 - r1;
  float inv_r1 = 1.0f / r1;
  float c = (r1 + r2) / (2.0f * r1);
  float d = (r2 - r1) / (4.0f * r1);

  Eigen::Matrix4f M = Eigen::Matrix4f::Zero();
  M(0, 0) = inv_r1;
  M(1, 1) = c;
  M(1, 3) = d;
  M(2, 2) = inv_r1;
  M(3, 1) = beta * inv_r1;
  M(3, 3) = alpha * inv_r1;

  return M;
}

struct alignas(16) Primitive {
  int type;
  int defaultColor;
  int _pad0;
  int _pad1;
  Eigen::Vector4f color;
  Eigen::Matrix4f inv_transform;

  Primitive(PrimitiveType t, const Eigen::Vector4f& c, const Eigen::Matrix4f trans,
            float bottom_r = 1.0f, float top_r = 1.0f, bool isDefaultColor = true)
  {
    type = static_cast<int>(t);
    defaultColor = isDefaultColor ? 1 : 0;
    _pad0 = 0;
    _pad1 = 0;
    color = c;

    if (t == PrimitiveType::CYLINDER) {
      // Compose projective cone-to-unit-cylinder matrix with affine inverse
      Eigen::Matrix4f M = computeConeToUnitCylinderMatrix(bottom_r, top_r);
      inv_transform = M * trans.inverse();
    } else {
      inv_transform = trans.inverse();
    }
  }

  Primitive() : type(0), defaultColor(1), _pad0(0), _pad1(0), color(Eigen::Vector4f::Zero()), inv_transform(Eigen::Matrix4f::Identity()) {}
};

inline std::string printPrimitive(const PrimitiveType& type)
{
  switch (type) {
  case PrimitiveType::NONE:     return "None";
  case PrimitiveType::SPHERE:   return "Sphere";
  case PrimitiveType::CUBE:     return "Cube";
  case PrimitiveType::CYLINDER: return "Cylinder";
  default:                      return "Unknown";
  }
}

#endif  // PRIMITIVE_H
