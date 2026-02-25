//
// Created by apollyon-black on 2/17/26.
//

#include "RTCSGTreeVisitor.h"
#include "RTCSGNode.h"
#include "core/ColorNode.h"

#include "core/CsgOpNode.h"
#include "core/TransformNode.h"
#include "core/primitives.h"

static float deg2rad(float deg) { return deg * (3.14159265359f / 180.0f); }

static OperationType mapOperator(OpenSCADOperator op) {
  switch (op) {
  case OpenSCADOperator::UNION:        return OperationType::UNION;
  case OpenSCADOperator::INTERSECTION: return OperationType::INTERSECTION;
  case OpenSCADOperator::DIFFERENCE:   return OperationType::DIFFERENCE;
  default:                             return OperationType::NONE;
  }
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

// binarize the children
void RTCSGTreeVisitor::applyToChildren(State& state, const AbstractNode& node, OperationType op) {
  const auto& vc = this->visitedchildren[node.index()];

  if (vc.empty()) {
    this->stored_term[node.index()] = nullptr;
    return;
  }

  std::shared_ptr<RTCSGNode> t1 = nullptr;

  for (const auto& chnode : vc) {
    auto it = this->stored_term.find(chnode->index());
    std::shared_ptr<RTCSGNode> t2 = (it != stored_term.end()) ? it->second : nullptr;
    this->stored_term.erase(chnode->index());

    if (t2 && !t1) {
      t1 = t2;  // first valid child
    } else if (t2 && t1) {
      // Binarize: combine t1 and t2
      auto combined = std::make_shared<RTCSGNode>(op, t1, t2);
      t1 = combined;
    }
  }

  this->stored_term[node.index()] = t1;
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
    state.setMatrix(state.matrix() * node.matrix); // build up the transform matrix with previous nodes one
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
    const OperationType rtOp = mapOperator(node.type); // translate openscad op type to rt op type
    if (rtOp == OperationType::NONE) {
      // Unsupported op (minkowski, hull, etc.), skip for now, and treat like union instead
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
        Eigen::Vector3f col(0.8f, 0.8f, 0.8f);  // default
        if (state.color().isValid()) {
            auto stateColor = state.color();
            col = Eigen::Vector3f(static_cast<float>(stateColor.r()), static_cast<float>(stateColor.g()), static_cast<float>(stateColor.b()));
        }

        if (auto* cube = dynamic_cast<const CubeNode*>(&node)) {
            rtNode = std::make_shared<RTCSGNode>(PrimitiveType::CUBE);
            rtNode->color = col;

            // Local transform: scale by size
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

            // OpenSCAD cylinders are along Z, your raytracer expects Y-axis
            // Rotate 90° around X to match your convention
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