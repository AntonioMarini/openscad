#ifndef PRIMITIVE_H
#define PRIMITIVE_H

#include <string>
#include <Eigen/Dense>

enum class PrimitiveType : int { NONE = 0, SPHERE = 1, CUBE = 2, CYLINDER = 4 };

struct alignas(16) Primitive {
  int type;
  float r1;
  float r2;
  int defaultColor;
  Eigen::Vector4f color;
  Eigen::Matrix4f inv_transform;

  Primitive(PrimitiveType t, const Eigen::Vector4f& c, const Eigen::Matrix4f trans,
            float bottom_r = 1.0f, float top_r = 1.0f, bool isDefaultColor = true)
  {
    type = static_cast<int>(t);
    r1 = bottom_r;
    r2 = top_r;
    defaultColor = isDefaultColor ? 1 : 0;
    color = c;
    inv_transform = trans.inverse();
  }

  Primitive() : type(0), r1(1.0f), r2(1.0f), defaultColor(1), color(Eigen::Vector4f::Zero()), inv_transform(Eigen::Matrix4f::Identity()) {}
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
