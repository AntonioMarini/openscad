//
// Created by apollyon-black on 2/17/26.
//

#ifndef OPENSCAD_RTCSGNODE_H
#define OPENSCAD_RTCSGNODE_H

#include <vector>
#include <memory>
#include <algorithm>
#include <cfloat>

#include "RTBounds.h"
#include "Primitive.h"
#include "Operation.h"
#include "CSGCommand.h"
#include "DNFData.h"
#include <iostream>
#include <map>

struct RTCSGNode {
  std::shared_ptr<RTCSGNode> left = nullptr;
  std::shared_ptr<RTCSGNode> right = nullptr;

  OperationType op = OperationType::NONE;
  PrimitiveType primitive = PrimitiveType::NONE;

  Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
  Eigen::Vector4f color = Eigen::Vector4f(1.0f, 1.0f, 1.0f, 1.0f);
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

inline int countRTCSGNodes(const std::shared_ptr<RTCSGNode>& node)
{
  if (!node) return 0;
  return 1 + countRTCSGNodes(node->left) + countRTCSGNodes(node->right);
}

inline int treeDepth(const std::shared_ptr<RTCSGNode>& node)
{
  if (!node) return 0;
  return 1 + std::max(treeDepth(node->left), treeDepth(node->right));
}

// FLATTENER CLASS
class CSGTree
{
public:
  std::shared_ptr<RTCSGNode> root;

  CSGTree(std::shared_ptr<RTCSGNode> root_node) : root(root_node) {};

  std::map<RTCSGNode *, unsigned int> node_count;
  std::map<RTCSGNode *, unsigned int> node_duplicate_id;
  std::map<RTCSGNode *, unsigned int> node_first_cmd_id;
  std::map<RTCSGNode *, unsigned int> node_first_prim_id;
  unsigned int next_duplicate_id = 1;

  void countNodes(std::shared_ptr<RTCSGNode> node)
  {
    if (!node) return;
    node_count[node.get()]++;
    if (node_count[node.get()] > 1) return;
    countNodes(node->left);
    countNodes(node->right);
  }

  unsigned int flatten_tree(std::shared_ptr<RTCSGNode> node, std::vector<Primitive>& primitives,
                            std::vector<Operation>& operations, std::vector<CSGCommand>& commands,
                            std::vector<RTBounds>& bounds)
  {
    if (!node) return 0xFFFFFFFF;

    bool is_shared = node_count[node.get()] > 1;

    bool is_second_occurrence = false;
    unsigned int second_dup_id = 0;

    if (is_shared) {
      auto dup_it = node_duplicate_id.find(node.get());
      if (dup_it != node_duplicate_id.end()) {
        if (node->is_leaf()) {
          // Primitives can always recompute directly from primitives[] — keep lightweight stub
          CSGCommand cache_ref_cmd(CSGCommandType::CACHED_PRIMITIVE, node_first_prim_id[node.get()]);
          cache_ref_cmd.skip_children = 1;
          cache_ref_cmd.duplicate_id = dup_it->second;
          auto cache_ref_cmd_id = (unsigned int)commands.size();
          commands.push_back(cache_ref_cmd);
          bounds.push_back(bounds[node_first_cmd_id[node.get()]]);
          return cache_ref_cmd_id;
        }
        // Operation: fall through to emit full subtree so the shader can recompute on cache miss
        is_second_occurrence = true;
        second_dup_id = dup_it->second;
      } else {
        // First occurrence: reserve duplicate_id now so recursive children
        // can already reference it if needed.
        node_duplicate_id[node.get()] = next_duplicate_id++;
      }
    }

    auto cmd_id = (unsigned int)commands.size();

    if (node->is_leaf()) {
      Primitive p(node->primitive, node->color, node->transform, node->r1, node->r2);
      auto prim_id = (unsigned int)primitives.size();
      primitives.push_back(p);

      CSGCommand cmd(CSGCommandType::PRIMITIVE, prim_id);
      cmd.skip_children = 1;
      if (is_shared) {
        cmd.duplicate_id = node_duplicate_id[node.get()];
        node_first_prim_id[node.get()] = prim_id;
      }

      commands.push_back(cmd);
      bounds.push_back(RTBounds::buildPrimitiveBounds(*node));
    } else {
      Operation op(node->op, 0, 0);
      auto op_id = (unsigned int)operations.size();
      operations.push_back(op);

      // 2nd+ occurrences use CACHED_OPERATION so the shader tries cache first,
      // but falls back to children on miss (real eviction support)
      CSGCommandType cmd_type =
        is_second_occurrence ? CSGCommandType::CACHED_OPERATION : CSGCommandType::OPERATION;

      CSGCommand cmd(cmd_type, op_id);
      cmd.skip_children = 0;
      if (is_second_occurrence) cmd.duplicate_id = second_dup_id;
      else if (is_shared) cmd.duplicate_id = node_duplicate_id[node.get()];

      commands.push_back(cmd);
      bounds.emplace_back();

      unsigned int left_id = flatten_tree(node->left, primitives, operations, commands, bounds);
      unsigned int right_id = flatten_tree(node->right, primitives, operations, commands, bounds);

      commands[cmd_id].skip_children = (unsigned int)commands.size() - cmd_id;
      bounds[cmd_id] = RTBounds::buildOperationBounds(node->op, bounds[left_id], bounds[right_id]);
      operations[op_id].left_id = left_id;
      operations[op_id].right_id = right_id;
    }

    // Don't overwrite first_cmd_id on subsequent expansions
    if (is_shared && !is_second_occurrence) node_first_cmd_id[node.get()] = cmd_id;

    return cmd_id;
  }

  // helper function to print out matrix data to stdout for debugging
  void print_matrix(const Eigen::Matrix4f& mat)
  {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        std::cout << mat(i, j) << " ";
      }
      std::cout << std::endl;
    }
  }

  // debug function to print the tree
  void print_tree(const std::shared_ptr<RTCSGNode>& node, int depth = 0)
  {
    if (!node) return;
    for (int i = 0; i < depth; ++i) std::cout << "  ";
    if (node->is_leaf()) {
      std::cout << "Primitive: " << static_cast<int>(node->primitive) << " "
                << printPrimitive(node->primitive) << ", Color: (" << node->color.x() << ", "
                << node->color.y() << ", " << node->color.z() << ")\n";
    } else {
      std::cout << "Operation: " << static_cast<int>(node->op) << "\n";
      print_tree(node->left, depth + 1);
      print_tree(node->right, depth + 1);
    }
  }

  // --- DNF (Goldfeather) flatten ---

  // Collect products in the tree in a flat vector
  static void collect_products(const std::shared_ptr<RTCSGNode>& node,
                               std::vector<std::shared_ptr<RTCSGNode>>& products)
  {
    if (!node) return;
    if (node->op == OperationType::UNION) {
      collect_products(node->left, products);
      collect_products(node->right, products);
    } else {
      products.push_back(node);
    }
  }

  // Recursively flatten one product subtree into ProductCommand[].
  // Returns the cmd_id of the root command; sets out_bounds to the root bounds.
  // prim_cache is shared across all products to avoid duplicate Primitive entries.
  static uint32_t flatten_product(const std::shared_ptr<RTCSGNode>& node,
                                  std::vector<Primitive>& primitives, std::vector<Operation>& operations,
                                  std::vector<ProductCommand>& commands,
                                  std::map<RTCSGNode *, uint32_t>& prim_cache, RTBounds& out_bounds)
  {
    auto cmd_id = (uint32_t)commands.size();
    ProductCommand cmd{};

    if (node->is_leaf()) {
      auto it = prim_cache.find(node.get());
      uint32_t prim_id;
      if (it != prim_cache.end()) {
        prim_id = it->second;
      } else {
        prim_id = (uint32_t)primitives.size();
        primitives.emplace_back(node->primitive, node->color, node->transform, node->r1, node->r2);
        prim_cache[node.get()] = prim_id;
      }
      out_bounds = RTBounds::buildPrimitiveBounds(*node);
      cmd.type = 0;  // PRIMITIVE
      cmd.id = prim_id;
      cmd.skip_children = 1;
      cmd.bounds_skip = out_bounds.skip ? 1u : 0u;
      cmd.bounds_type = out_bounds.skip ? 1u : out_bounds.bounds_type;
      cmd.bounds_inv = out_bounds.skip ? Eigen::Matrix4f::Zero() : out_bounds.inv_transform;
      commands.push_back(cmd);
    } else {
      auto op_id = (uint32_t)operations.size();
      operations.emplace_back(node->op, 0u, 0u);

      cmd.type = 1;  // OPERATION
      cmd.id = op_id;
      cmd.skip_children = 0;    // back-filled after recursion
      commands.push_back(cmd);  // placeholder

      RTBounds left_bounds, right_bounds;
      uint32_t left_id =
        flatten_product(node->left, primitives, operations, commands, prim_cache, left_bounds);
      uint32_t right_id =
        flatten_product(node->right, primitives, operations, commands, prim_cache, right_bounds);

      operations[op_id].left_id = left_id;
      operations[op_id].right_id = right_id;
      commands[cmd_id].skip_children = (uint32_t)commands.size() - cmd_id;
      out_bounds = RTBounds::buildOperationBounds(node->op, left_bounds, right_bounds);
      commands[cmd_id].bounds_skip = out_bounds.skip ? 1u : 0u;
      commands[cmd_id].bounds_type = out_bounds.skip ? 1u : out_bounds.bounds_type;
      commands[cmd_id].bounds_inv = out_bounds.skip ? Eigen::Matrix4f::Zero() : out_bounds.inv_transform;
    }

    return cmd_id;
  }

  // Returns the world-space center of the bounds volume.
  static Eigen::Vector3f boundsCenter(const RTBounds& bb)
  {
    if (bb.bounds_type == 0) {  // AABB
      return (bb.inv_transform.col(0).head<3>() + bb.inv_transform.col(1).head<3>()) * 0.5f;
    }
    // OBB: center is the origin of the local frame in world coords
    return bb.inv_transform.inverse().col(3).head<3>();
  }

  // Temporary struct for BVH construction
  struct FlatProduct {
    uint32_t cmd_start;
    uint32_t cmd_count;
    RTBounds bounds;
    Eigen::Vector3f centroid;
  };

  // Recursive KD BVH builder over a range of FlatProducts.
  // Appends nodes to bvh_nodes in preorder (for traversing in gpu later).
  // Returns the combined bounds of the subtree (used by parent for union computation).
  static RTBounds buildProductBVH(std::vector<FlatProduct>& products, int begin, int end,
                                  std::vector<ProductBVHNode>& bvh_nodes)
  {
    auto curr_id = (uint32_t)bvh_nodes.size();
    bvh_nodes.emplace_back();  // placeholder, filled in below

    if (end - begin == 1) {  // LEAF
      const FlatProduct& fp = products[begin];
      ProductBVHNode& node = bvh_nodes[curr_id];

      // fill the placeholder data
      node.is_leaf = 1;
      node.skip_children = 1;
      node.bounds_skip = fp.bounds.skip;
      node.bounds_type = fp.bounds.skip ? 1u : fp.bounds.bounds_type;
      node.cmd_start = fp.cmd_start;
      node.cmd_count = fp.cmd_count;
      node.bounds_inv = fp.bounds.skip ? Eigen::Matrix4f::Zero() : fp.bounds.inv_transform;
      return fp.bounds;
    }

    // KD split: find the axis with the largest centroid extent
    Eigen::Vector3f cmin(FLT_MAX, FLT_MAX, FLT_MAX);
    Eigen::Vector3f cmax(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (int i = begin; i < end; ++i) {
      cmin = cmin.cwiseMin(products[i].centroid);
      cmax = cmax.cwiseMax(products[i].centroid);
    }
    Eigen::Vector3f extent = cmax - cmin;
    int axis = 0;
    if (extent[1] > extent[axis]) axis = 1;
    if (extent[2] > extent[axis]) axis = 2;

    int mid = (begin + end) / 2;
    std::nth_element(products.begin() + begin, products.begin() + mid, products.begin() + end,
                     [axis](const FlatProduct& a, const FlatProduct& b) {
                       return a.centroid[axis] < b.centroid[axis];
                     });

    RTBounds left_bounds = buildProductBVH(products, begin, mid, bvh_nodes);
    RTBounds right_bounds = buildProductBVH(products, mid, end, bvh_nodes);

    RTBounds combined = RTBounds::buildOperationBounds(OperationType::UNION, left_bounds, right_bounds);
    uint32_t subtree_count = (uint32_t)bvh_nodes.size() - curr_id;

    ProductBVHNode& node = bvh_nodes[curr_id];
    node.is_leaf = 0;
    node.skip_children = subtree_count;
    node.bounds_skip = combined.skip;
    node.bounds_type = combined.skip ? 1u : combined.bounds_type;
    node.cmd_start = 0;
    node.cmd_count = 0;
    node.bounds_inv = combined.skip ? Eigen::Matrix4f::Zero() : combined.inv_transform;

    return combined;
  }

  // Flatten the entire tree into DNF (sum-of-products) with a KD BVH over the products.
  uint32_t flatten_to_dnf(const std::shared_ptr<RTCSGNode>& node, std::vector<Primitive>& primitives,
                          std::vector<Operation>& operations,
                          std::vector<ProductCommand>& product_commands,
                          std::vector<ProductBVHNode>& bvh_nodes)
  {
    std::vector<std::shared_ptr<RTCSGNode>> products;
    collect_products(node, products);
    if (products.empty()) return 4u;

    std::map<RTCSGNode *, uint32_t> prim_cache;

    // Step 1: flatten each product into product_commands[], collecting its root bounds
    std::vector<FlatProduct> flat;
    flat.reserve(products.size());
    for (auto& prod_root : products) {
      auto cmd_start = (uint32_t)product_commands.size();
      RTBounds root_bounds;
      flatten_product(prod_root, primitives, operations, product_commands, prim_cache, root_bounds);
      FlatProduct fp;
      fp.cmd_start = cmd_start;
      fp.cmd_count = (uint32_t)product_commands.size() - cmd_start;
      fp.bounds = root_bounds;
      fp.centroid = boundsCenter(root_bounds);
      flat.push_back(std::move(fp));
    }

    uint32_t max_stack = 4u;
    for (const auto& fp : flat) max_stack = std::max(max_stack, fp.cmd_count);

    // Step 2: build KD BVH over flat products
    buildProductBVH(flat, 0, (int)flat.size(), bvh_nodes);

    return max_stack;
  }
};
#endif  // !CSG_TREE_H
