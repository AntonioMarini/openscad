//
// Created by apollyon-black on 2/17/26.
//

#ifndef OPENSCAD_RTCSGNODE_H
#define OPENSCAD_RTCSGNODE_H

#include <memory>
#include <algorithm>
#include <iostream>
#include <string>

#include "Primitive.h"
#include "Operation.h"
#include <Eigen/Core>

struct RTCSGNode {
  std::shared_ptr<RTCSGNode> left = nullptr;
  std::shared_ptr<RTCSGNode> right = nullptr;

  OperationType op = OperationType::NONE;
  PrimitiveType primitive = PrimitiveType::NONE;

  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  Eigen::Vector4f color = Eigen::Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
  bool isDefaultColor = true;
  float r1 = 1.0f;
  float r2 = 1.0f;

  Eigen::Vector3f min_bound = Eigen::Vector3f::Zero();
  Eigen::Vector3f max_bound = Eigen::Vector3f::Zero();

  bool is_leaf() const { return (left == nullptr && right == nullptr); }

  RTCSGNode(OperationType type, std::shared_ptr<RTCSGNode> l, std::shared_ptr<RTCSGNode> r)
    : op(type), left(l), right(r)
  {
  }

  RTCSGNode(PrimitiveType type, Eigen::Vector4f col = Eigen::Vector4f(1.0f, 1.0f, 1.0f, 1.0f))
    : primitive(type), color(col)
  {
  }

  void set_transform(const Eigen::Vector3f& pos, const Eigen::Vector3f& scale)
  {
    transform = Eigen::Matrix4f::Identity();
    transform(0, 0) = scale.x();
    transform(1, 1) = scale.y();
    transform(2, 2) = scale.z();
    transform(0, 3) = pos.x();
    transform(1, 3) = pos.y();
    transform(2, 3) = pos.z();
  }
};

inline void printRTCSGTree(const std::shared_ptr<RTCSGNode>& node, int depth = 0)
{
  if (!node) return;

  std::string indent(depth * 2, ' ');

  if (node->is_leaf()) {
    std::string typeName;
    switch (node->primitive) {
    case PrimitiveType::SPHERE:   typeName = "Sphere"; break;
    case PrimitiveType::CUBE:     typeName = "Cube"; break;
    case PrimitiveType::CYLINDER: typeName = "Cylinder"; break;
    default:                      typeName = "Unknown"; break;
    }
    std::cout << indent << typeName << " color=(" << node->color.x() << ", " << node->color.y() << ", "
              << node->color.z() << ", " << node->color.w() << ")" << std::endl;
  } else {
    std::string opName;
    switch (node->op) {
    case OperationType::UNION:        opName = "Union"; break;
    case OperationType::INTERSECTION: opName = "Intersection"; break;
    case OperationType::DIFFERENCE:   opName = "Difference"; break;
    default:                          opName = "None"; break;
    }
    std::cout << indent << opName << std::endl;
    printRTCSGTree(node->left, depth + 1);
    printRTCSGTree(node->right, depth + 1);
  }
}

#endif  // OPENSCAD_RTCSGNODE_H
