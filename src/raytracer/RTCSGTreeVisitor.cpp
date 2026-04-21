#include "RTCSGTreeVisitor.h"
#include "RTCSGNode.h"
#include "core/ColorNode.h"

#include "core/CsgOpNode.h"
#include "core/TransformNode.h"
#include "core/primitives.h"
#include <algorithm>
#include <cfloat>
#include <memory>
#include <vector>

static float deg2rad(float deg) { return deg * (3.14159265359f / 180.0f); }

static OperationType mapOperator(OpenSCADOperator op)
{
  switch (op) {
  case OpenSCADOperator::UNION:        return OperationType::UNION;
  case OpenSCADOperator::INTERSECTION: return OperationType::INTERSECTION;
  case OpenSCADOperator::DIFFERENCE:   return OperationType::DIFFERENCE;
  default:                             return OperationType::NONE;
  }
}

// Splits the children list in half recursevely without any spatial grouping logic.
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

// Returns {sum_of_leaf_positions, leaf_count} so the caller can compute
// the true mean (= sum / count) weighted equally per leaf.
static std::pair<Eigen::Vector3f, int>
getCentroidWeighted(const std::shared_ptr<RTCSGNode>& node)
{
  if (!node) return {Eigen::Vector3f::Zero(), 0};
  if (node->is_leaf()) return {node->transform.col(3).head<3>(), 1};
  auto [ls, lc] = getCentroidWeighted(node->left);
  auto [rs, rc] = getCentroidWeighted(node->right);
  return {ls + rs, lc + rc};
}

Eigen::Vector3f RTCSGTreeVisitor::getCentroid(const std::shared_ptr<RTCSGNode>& node)
{
  auto [sum, count] = getCentroidWeighted(node);
  if (count == 0) return Eigen::Vector3f::Zero();
  return sum / static_cast<float>(count);
}

std::shared_ptr<RTCSGNode> RTCSGTreeVisitor::binarizeKD(
  std::vector<std::shared_ptr<RTCSGNode>>& children, OperationType op)
{
  if (children.size() == 1) return children[0];
  if (children.size() == 2) return std::make_shared<RTCSGNode>(op, children[0], children[1]);

  Eigen::Vector3f cmin(FLT_MAX, FLT_MAX, FLT_MAX);
  Eigen::Vector3f cmax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
  for (const auto& child : children) {
    Eigen::Vector3f c = getCentroid(child);
    cmin = cmin.cwiseMin(c);
    cmax = cmax.cwiseMax(c);
  }
  Eigen::Vector3f extent = cmax - cmin;
  int axis = 0;
  if (extent[1] > extent[axis]) axis = 1;
  if (extent[2] > extent[axis]) axis = 2;

  std::sort(children.begin(), children.end(),
            [&](const auto& a, const auto& b) { return getCentroid(a)[axis] < getCentroid(b)[axis]; });

  size_t mid = children.size() / 2;
  std::vector<std::shared_ptr<RTCSGNode>> left(children.begin(), children.begin() + mid);
  std::vector<std::shared_ptr<RTCSGNode>> right(children.begin() + mid, children.end());

  return std::make_shared<RTCSGNode>(op, binarizeKD(left, op), binarizeKD(right, op));
}

// ---- Entry point ----

std::shared_ptr<RTCSGNode> RTCSGTreeVisitor::buildRTTree(const AbstractNode& node)
{
  this->traverse(node);
  rootNode = stored_term[node.index()];
  return rootNode;
}

void RTCSGTreeVisitor::addToParent(const State& state, const AbstractNode& node)
{
  this->visitedchildren.erase(node.index());
  if (state.parent()) {
    this->visitedchildren[state.parent()->index()].push_back(node.shared_from_this());
  }
}

// Binarize children: uses naive balanced split for commutative ops
void RTCSGTreeVisitor::applyToChildren(State& state, const AbstractNode& node, OperationType op)
{
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
    // Difference is NOT commutative: first child is the base, rest are subtracted.
    auto result = validChildren[0];
    for (size_t i = 1; i < validChildren.size(); i++) {
      result = std::make_shared<RTCSGNode>(OperationType::DIFFERENCE, result, validChildren[i]);
    }
    this->stored_term[node.index()] = result;
  } else {
    // Union and Intersection are commutative -> balanced binarization (TODO: make binarization method
    // dynamic)
    this->stored_term[node.index()] =
      useKDBinarization ? binarizeKD(validChildren, op) : binarizeNaive(validChildren, op);
  }
}

// ---- AbstractNode (fallback): treat as union of children ----
Response RTCSGTreeVisitor::visit(State& state, const AbstractNode& node)
{
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

Response RTCSGTreeVisitor::visit(State& state, const LeafNode& node)
{
  if (state.isPostfix()) {
    std::shared_ptr<RTCSGNode> rtNode = nullptr;

    // Accumulated transform from State (double -> float)
    Eigen::Matrix4f worldMat = state.matrix().matrix().cast<float>();

    Eigen::Vector4f col(defaultColor.x(), defaultColor.y(), defaultColor.z(), 1.0f);
    if (state.color().isValid()) {
      auto stateColor = state.color();
      col = Eigen::Vector4f(static_cast<float>(stateColor.r()), static_cast<float>(stateColor.g()),
                            static_cast<float>(stateColor.b()), static_cast<float>(stateColor.a()));
    }

    if (auto *cube = dynamic_cast<const CubeNode *>(&node)) {
      rtNode = std::make_shared<RTCSGNode>(PrimitiveType::CUBE);
      rtNode->color = col;

      Eigen::Vector3f size(cube->x, cube->y, cube->z);
      Eigen::Matrix4f local = Eigen::Matrix4f::Identity();
      local = (Eigen::Affine3f(local) * Eigen::Scaling(size)).matrix();

      if (!cube->center) {
        local = (Eigen::Affine3f(local) * Eigen::Translation3f(0.5f, 0.5f, 0.5f)).matrix();
      }

      rtNode->transform = worldMat * local;
    } else if (auto *sphere = dynamic_cast<const SphereNode *>(&node)) {
      rtNode = std::make_shared<RTCSGNode>(PrimitiveType::SPHERE);
      rtNode->color = col;

      float r = static_cast<float>(sphere->r);
      Eigen::Matrix4f local = Eigen::Matrix4f::Identity();
      local = (Eigen::Affine3f(local) * Eigen::Scaling(r, r, r)).matrix();

      rtNode->transform = worldMat * local;
    } else if (auto *cyl = dynamic_cast<const CylinderNode *>(&node)) {
      float cyl_r1 = static_cast<float>(cyl->r1);
      float cyl_r2 = static_cast<float>(cyl->r2);
      float h = static_cast<float>(cyl->h);
      float rmax = std::max(cyl_r1, cyl_r2);

      if (rmax > 0.0f) {
        rtNode = std::make_shared<RTCSGNode>(PrimitiveType::CYLINDER);
        rtNode->color = col;
        rtNode->r1 = cyl_r1 / rmax;
        rtNode->r2 = cyl_r2 / rmax;

        Eigen::Matrix4f local = Eigen::Matrix4f::Identity();
        local = (Eigen::Affine3f(local) * Eigen::Scaling(rmax, h, rmax)).matrix();

        if (!cyl->center) {
          local = (Eigen::Affine3f(local) * Eigen::Translation3f(0.0f, 0.5f, 0.0f)).matrix();
        }

        Eigen::AngleAxisf toYaxis(deg2rad(90.0f), Eigen::Vector3f::UnitX());
        local = (Eigen::Affine3f(toYaxis) * Eigen::Affine3f(local)).matrix();

        rtNode->transform = worldMat * local;
      }
    } else {
      // Unsupported primitives (polyhedron, etc.)
    }

    this->stored_term[node.index()] = rtNode;
    addToParent(state, node);
  }
  return Response::ContinueTraversal;
}

std::shared_ptr<RTCSGNode> RTCSGTreeVisitor::distributeOperation(std::shared_ptr<RTCSGNode> node)
{
  if (node == nullptr) {
    return nullptr;
  }
  // base case
  if (node->is_leaf()) {
    return node;
  }

  // rec (postorder visit)
  node->left = distributeOperation(node->left);
  node->right = distributeOperation(node->right);

  // 1) (A U B) int. C -> (A int. C) U (B int. C)
  // 2) A int (B U C) -> (A int. B) U (A int. C)
  // 3) (A U B) \ C -> (A \ C) U (B \ C)
  // 4) A \ (B int. C) -> (A \ B) U (A \ C)
  // 5) A \ (B \ C) -> (A \ B) U (A int. C)
  // NOTE: A \ (B U C) is intentionally NOT transformed. The equivalent rewrite
  // (A\B)\C would destroy any balanced union structure on the right, turning a
  // shallow balanced tree into a deep chain and eliminating OBB culling benefits.

  bool changed = true;
  while (changed) {
    changed = false;

    if (node->op == OperationType::INTERSECTION && node->left->op == OperationType::UNION) {  // 1)

      std::shared_ptr<RTCSGNode> A = node->left->left;
      std::shared_ptr<RTCSGNode> B = node->left->right;
      std::shared_ptr<RTCSGNode> C = node->right;

      node->op = OperationType::UNION;
      node->left = std::make_shared<RTCSGNode>(OperationType::INTERSECTION, A, C);
      node->right = std::make_shared<RTCSGNode>(OperationType::INTERSECTION, B, C);
      changed = true;
    } else if (node->op == OperationType::INTERSECTION &&
               node->right->op == OperationType::UNION) {  // 2)

      std::shared_ptr<RTCSGNode> A = node->left;
      std::shared_ptr<RTCSGNode> B = node->right->left;
      std::shared_ptr<RTCSGNode> C = node->right->right;

      node->op = OperationType::UNION;
      node->left = std::make_shared<RTCSGNode>(OperationType::INTERSECTION, A, B);
      node->right = std::make_shared<RTCSGNode>(OperationType::INTERSECTION, A, C);
      changed = true;
    } else if (node->op == OperationType::DIFFERENCE && node->left->op == OperationType::UNION) {  // 3)

      std::shared_ptr<RTCSGNode> A = node->left->left;
      std::shared_ptr<RTCSGNode> B = node->left->right;
      std::shared_ptr<RTCSGNode> C = node->right;

      node->op = OperationType::UNION;
      node->left = std::make_shared<RTCSGNode>(OperationType::DIFFERENCE, A, C);
      node->right = std::make_shared<RTCSGNode>(OperationType::DIFFERENCE, B, C);
      changed = true;
    } else if (node->op == OperationType::DIFFERENCE &&
               node->right->op == OperationType::INTERSECTION) {  // 4)

      std::shared_ptr<RTCSGNode> A = node->left;
      std::shared_ptr<RTCSGNode> B = node->right->left;
      std::shared_ptr<RTCSGNode> C = node->right->right;

      node->op = OperationType::UNION;
      node->left = std::make_shared<RTCSGNode>(OperationType::DIFFERENCE, A, B);
      node->right = std::make_shared<RTCSGNode>(OperationType::DIFFERENCE, A, C);
      changed = true;
    } else if (node->op == OperationType::DIFFERENCE &&
               node->right->op == OperationType::DIFFERENCE) {  // 5)

      std::shared_ptr<RTCSGNode> A = node->left;
      std::shared_ptr<RTCSGNode> B = node->right->left;
      std::shared_ptr<RTCSGNode> C = node->right->right;

      node->op = OperationType::UNION;
      node->left = std::make_shared<RTCSGNode>(OperationType::DIFFERENCE, A, B);
      node->right = std::make_shared<RTCSGNode>(OperationType::INTERSECTION, A, C);
      changed = true;
    }

    if (changed) {  // there may be other new unions generated below -> should recurse again
      node->left = distributeOperation(node->left);
      node->right = distributeOperation(node->right);
    }
  }
  return node;
}
