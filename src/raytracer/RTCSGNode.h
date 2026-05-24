//
// Created by apollyon-black on 2/17/26.
//

#ifndef OPENSCAD_RTCSGNODE_H
#define OPENSCAD_RTCSGNODE_H

#include <memory>

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

};

#endif  // OPENSCAD_RTCSGNODE_H
