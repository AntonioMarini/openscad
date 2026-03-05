//
// Created by apollyon-black on 2/17/26.
//

#include "RTCSGTreeVisitor.h"
#include "RTCSGNode.h"
#include "core/ColorNode.h"

#include "core/CsgOpNode.h"
#include "core/TransformNode.h"
#include "core/primitives.h"

#include <algorithm>

static float deg2rad(float deg) { return deg * (3.14159265359f / 180.0f); }

static OperationType mapOperator(OpenSCADOperator op) {
  switch (op) {
  case OpenSCADOperator::UNION:        return OperationType::UNION;
  case OpenSCADOperator::INTERSECTION: return OperationType::INTERSECTION;
  case OpenSCADOperator::DIFFERENCE:   return OperationType::DIFFERENCE;
  default:                             return OperationType::NONE;
  }
}

// ---- Naive balanced binarization ----
// Splits the children list in half recursively without any spatial ordering.
// Produces a balanced binary tree (log2 depth) instead of a linear chain.

std::shared_ptr<RTCSGNode> RTCSGTreeVisitor::binarizeNaive(
    std::vector<std::shared_ptr<RTCSGNode>>& children, OperationType op)
{
    if (children.empty()) return nullptr;
    if (children.size() == 1) return children[0];
    if (children.size() == 2) {
        return std::make_shared<RTCSGNode>(op, children[0], children[1]);
    }

    size_t mid = children.size() / 2;
    std::vector<std::shared_ptr<RTCSGNode>> left(children.begin(), children.begin() + mid);
    std::vector<std::shared_ptr<RTCSGNode>> right(children.begin() + mid, children.end());

    auto leftNode = binarizeNaive(left, op);
    auto rightNode = binarizeNaive(right, op);

    return std::make_shared<RTCSGNode>(op, leftNode, rightNode);
}

// ---- Entry point ----

std::shared_ptr<RTCSGNode> RTCSGTreeVisitor::buildRTTree(const AbstractNode& node) {
  this->traverse(node);
  rootNode = stored_term[node.index()];
  return rootNode;
}

void RTCSGTreeVisitor::addToParent(const State& state, const AbstractNode& node) {
  this->visitedchildren.erase(node.index());
  if (state.parent()) {
    this->visitedchildren[state.parent()->index()].push_back(node.shared_from_this());
  }
}

// Binarize children: uses naive balanced split for commutative ops
// (union, intersection), and keeps first child fixed for difference.
void RTCSGTreeVisitor::applyToChildren(State& state, const AbstractNode& node, OperationType op) {
  const auto& vc = this->visitedchildren[node.index()];

  if (vc.empty()) {
    this->stored_term[node.index()] = nullptr;
    return;
  }

  // Collect all valid RTCSGNode children
  std::vector<std::shared_ptr<RTCSGNode>> validChildren;
  for (const auto& chnode : vc) {
    auto it = this->stored_term.find(chnode->index());
    std::shared_ptr<RTCSGNode> t = (it != stored_term.end()) ? it->second : nullptr;
    this->stored_term.erase(chnode->index());
    if (t) validChildren.push_back(t);
  }

  if (validChildren.empty()) {
    this->stored_term[node.index()] = nullptr;
    return;
  }

  if (validChildren.size() == 1) {
    this->stored_term[node.index()] = validChildren[0];
    return;
  }

  if (op == OperationType::DIFFERENCE) {
    // Difference is NOT commutative: first child is the base,
    // the rest are subtracted.
    auto base = validChildren[0];

    if (validChildren.size() == 2) {
      this->stored_term[node.index()] = std::make_shared<RTCSGNode>(op, base, validChildren[1]);
      return;
    }

    // The subtracted children (index 1..N-1) are combined as a union,
    // then subtracted from the base.
    std::vector<std::shared_ptr<RTCSGNode>> subtracted(
        validChildren.begin() + 1, validChildren.end());
    auto subtractedTree = binarizeNaive(subtracted, OperationType::UNION);

    this->stored_term[node.index()] = std::make_shared<RTCSGNode>(op, base, subtractedTree);
  } else {
    // Union and Intersection are commutative: naive balanced binarization
    this->stored_term[node.index()] = binarizeNaive(validChildren, op);
  }
}

// ---- AbstractNode (fallback): treat as union of children ----
Response RTCSGTreeVisitor::visit(State& state, const AbstractNode& node) {
  if (state.isPostfix()) {
    applyToChildren(state, node, OperationType::UNION);
    addToParent(state, node);
  }
  return Response::ContinueTraversal;
}

Response RTCSGTreeVisitor::visit(State& state, const ColorNode& node)
{
  if (state.isPrefix()) {
    if (!state.color().isValid()) state.setColor(node.color);
  }
  if (state.isPostfix()) {
    applyToChildren(state, node, OperationType::UNION);
    addToParent(state, node);
  }
  return Response::ContinueTraversal;
}

Response RTCSGTreeVisitor::visit(State& state, const TransformNode& node)
{
  if (state.isPrefix()) {
    state.setMatrix(state.matrix() * node.matrix);
  }
  if (state.isPostfix()) {
    applyToChildren(state, node, OperationType::UNION);
    addToParent(state, node);
  }
  return Response::ContinueTraversal;
}

Response RTCSGTreeVisitor::visit(State& state, const CsgOpNode& node)
{
  if (state.isPostfix()) {
    const OperationType rtOp = mapOperator(node.type);
    if (rtOp == OperationType::NONE) {
      applyToChildren(state, node, OperationType::UNION);
    } else {
      applyToChildren(state, node, rtOp);
    }
    addToParent(state, node);
  }
  return Response::ContinueTraversal;
}

Response RTCSGTreeVisitor::visit(State& state, const LeafNode& node) {
    if (state.isPostfix()) {
        std::shared_ptr<RTCSGNode> rtNode = nullptr;

        // Accumulated transform from State (double -> float)
        Eigen::Matrix4f worldMat = state.matrix().matrix().cast<float>();

        // Color from State
      Eigen::Vector3f col = defaultColor;
        if (state.color().isValid()) {
            auto stateColor = state.color();
            col = Eigen::Vector3f(static_cast<float>(stateColor.r()), static_cast<float>(stateColor.g()), static_cast<float>(stateColor.b()));
        }

        if (auto* cube = dynamic_cast<const CubeNode*>(&node)) {
            rtNode = std::make_shared<RTCSGNode>(PrimitiveType::CUBE);
            rtNode->color = col;

            Eigen::Vector3f size(cube->x, cube->y, cube->z);
            Eigen::Matrix4f local = Eigen::Matrix4f::Identity();
            local = (Eigen::Affine3f(local) * Eigen::Scaling(size)).matrix();

            if (!cube->center) {
                local = (Eigen::Affine3f(local) * Eigen::Translation3f(0.5f, 0.5f, 0.5f)).matrix();
            }

            rtNode->transform = worldMat * local;
        }
        else if (auto* sphere = dynamic_cast<const SphereNode*>(&node)) {
            rtNode = std::make_shared<RTCSGNode>(PrimitiveType::SPHERE);
            rtNode->color = col;

            float r = static_cast<float>(sphere->r);
            Eigen::Matrix4f local = Eigen::Matrix4f::Identity();
            local = (Eigen::Affine3f(local) * Eigen::Scaling(r, r, r)).matrix();

            rtNode->transform = worldMat * local;
        }
        else if (auto* cyl = dynamic_cast<const CylinderNode*>(&node)) {
            rtNode = std::make_shared<RTCSGNode>(PrimitiveType::CYLINDER);
            rtNode->color = col;

            float r1 = static_cast<float>(cyl->r1);
            float h = static_cast<float>(cyl->h);

            Eigen::Matrix4f local = Eigen::Matrix4f::Identity();
            local = (Eigen::Affine3f(local) * Eigen::Scaling(r1, h, r1)).matrix();

            if (!cyl->center) {
                local = (Eigen::Affine3f(local) * Eigen::Translation3f(0.0f, 0.5f, 0.0f)).matrix();
            }

            Eigen::AngleAxisf toYaxis(deg2rad(90.0f), Eigen::Vector3f::UnitX());
            local = (Eigen::Affine3f(toYaxis) * Eigen::Affine3f(local)).matrix();

            rtNode->transform = worldMat * local;
        }
        else {
           // Unsupported primitives (polyhedron, etc.)
        }

        this->stored_term[node.index()] = rtNode;
        addToParent(state, node);
    }
    return Response::ContinueTraversal;
}