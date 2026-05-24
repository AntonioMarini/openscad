#ifndef OPENSCAD_CSGTREE_H
#define OPENSCAD_CSGTREE_H

#include <vector>
#include <memory>
#include <algorithm>
#include <cfloat>
#include <iostream>
#include <map>
#include <string>

#include "RTCSGNode.h"
#include "RTBounds.h"
#include "Primitive.h"
#include "Operation.h"
#include "DNFData.h"

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

  // DNF caching state
  std::map<RTCSGNode *, unsigned int> dnf_node_count;
  std::map<RTCSGNode *, unsigned int> dnf_node_duplicate_id;
  unsigned int dnf_next_duplicate_id = 1;

  void countNodes(std::shared_ptr<RTCSGNode> node)
  {
    if (!node) return;
    node_count[node.get()]++;
    if (node_count[node.get()] > 1) return;
    countNodes(node->left);
    countNodes(node->right);
  }

  void countDNFNodes(const std::vector<std::shared_ptr<RTCSGNode>>& products)
  {
    for (const auto& prod : products) {
      countDNFNodesRec(prod);
    }
  }

  void countDNFNodesRec(const std::shared_ptr<RTCSGNode>& node)
  {
    if (!node) return;
    dnf_node_count[node.get()]++;
    if (dnf_node_count[node.get()] > 1) return;
    countDNFNodesRec(node->left);
    countDNFNodesRec(node->right);
  }

  // Pretty-print the tree with box-drawing characters
  static void print_tree(const std::shared_ptr<RTCSGNode>& node, const std::string& prefix = "",
                         bool is_left = true, bool is_root = true)
  {
    if (!node) return;

    std::string connector = is_root ? "" : (is_left ? "├── " : "└── ");
    std::string child_prefix = is_root ? "" : (is_left ? "│   " : "    ");

    std::cout << prefix << connector;

    if (node->is_leaf()) {
      std::cout << printPrimitive(node->primitive) << " col=(" << node->color.x() << ", "
                << node->color.y() << ", " << node->color.z() << ")\n";
    } else {
      const char *op_str = (node->op == OperationType::UNION)          ? "UNION"
                           : (node->op == OperationType::INTERSECTION) ? "INTERSECT"
                           : (node->op == OperationType::DIFFERENCE)   ? "DIFFERENCE"
                                                                       : "???";
      std::cout << op_str << "\n";
      print_tree(node->left, prefix + child_prefix, true, false);
      print_tree(node->right, prefix + child_prefix, false, false);
    }
  }

  static void print_tree_labeled(const std::string& label, const std::shared_ptr<RTCSGNode>& node)
  {
    std::cout << "\n===== [RT] " << label << " =====\n";
    print_tree(node);
    std::cout << "===== end =====\n" << std::endl;
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
  uint32_t flatten_product(const std::shared_ptr<RTCSGNode>& node, std::vector<Primitive>& primitives,
                           std::vector<Operation>& operations, std::vector<ProductCommand>& commands,
                           std::map<RTCSGNode *, uint32_t>& prim_cache, RTBounds& out_bounds)
  {
    auto cmd_id = (uint32_t)commands.size();
    ProductCommand cmd{};

    // Determine duplicate_id for shared subtrees
    uint32_t dup_id = 0;
    bool is_shared = dnf_node_count[node.get()] > 1;
    if (is_shared) {
      auto it = dnf_node_duplicate_id.find(node.get());
      if (it != dnf_node_duplicate_id.end()) {
        dup_id = it->second;
      } else {
        dup_id = dnf_next_duplicate_id++;
        dnf_node_duplicate_id[node.get()] = dup_id;
      }
    }

    if (node->is_leaf()) {
      auto it = prim_cache.find(node.get());
      uint32_t prim_id;
      if (it != prim_cache.end()) {
        prim_id = it->second;
      } else {
        prim_id = (uint32_t)primitives.size();
        primitives.emplace_back(node->primitive, node->color, node->transform, node->r1, node->r2,
                                node->isDefaultColor);
        prim_cache[node.get()] = prim_id;
      }
      out_bounds = RTBounds::buildPrimitiveBounds(*node);
      cmd.type = 0;  // PRIMITIVE
      cmd.id = prim_id;
      cmd.skip_children = 1;
      cmd.duplicate_id = dup_id;
      cmd.bounds_skip = out_bounds.skip ? 1u : 0u;
      cmd.bounds_type = out_bounds.skip ? 1u : out_bounds.bounds_type;
      cmd.bounds_inv = out_bounds.skip ? Eigen::Matrix4f::Zero() : out_bounds.inv_transform;
      commands.push_back(cmd);
    } else {
      auto op_id = (uint32_t)operations.size();
      operations.emplace_back(node->op, 0u, 0u);

      cmd.type = 1;  // OPERATION
      cmd.id = op_id;
      cmd.skip_children = 0;  // back-filled after recursion
      cmd.duplicate_id = dup_id;
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
                                  std::vector<ProductBVHNode>& bvh_nodes, bool useKD = true)
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

    int mid = (begin + end) / 2;

    if (useKD) {
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

      std::nth_element(products.begin() + begin, products.begin() + mid, products.begin() + end,
                       [axis](const FlatProduct& a, const FlatProduct& b) {
                         return a.centroid[axis] < b.centroid[axis];
                       });
    }
    // Naive: just split at midpoint without spatial sorting

    RTBounds left_bounds = buildProductBVH(products, begin, mid, bvh_nodes, useKD);
    RTBounds right_bounds = buildProductBVH(products, mid, end, bvh_nodes, useKD);

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

  // Simulate eval_product's stack usage (no bounds culling = worst case) to find the
  // minimum MAX_PRODUCT_STACK needed for a given product's command slice.
  static uint32_t compute_eval_stack_depth(const std::vector<ProductCommand>& cmds, uint32_t start,
                                           uint32_t count)
  {
    int res_sp = 0;
    uint32_t max_res_sp = 0, max_op_sp = 0;
    std::vector<int> op_recv;
    op_recv.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
      if (cmds[start + i].type == 1u) {  // CMD_TYPE_OPERATION
        op_recv.push_back(0);
        max_op_sp = std::max(max_op_sp, (uint32_t)op_recv.size());
      } else {  // CMD_TYPE_PRIMITIVE
        res_sp++;
        max_res_sp = std::max(max_res_sp, (uint32_t)res_sp);
        while (!op_recv.empty()) {
          op_recv.back()++;
          if (op_recv.back() < 2) break;
          op_recv.pop_back();
          res_sp--;  // two consumed, one produced
        }
      }
    }
    return std::max({max_res_sp, max_op_sp, 4u});
  }

  // Flatten the entire tree into DNF (sum-of-products) with a KD BVH over the products.
  uint32_t flatten_to_dnf(const std::shared_ptr<RTCSGNode>& node, std::vector<Primitive>& primitives,
                          std::vector<Operation>& operations,
                          std::vector<ProductCommand>& product_commands,
                          std::vector<ProductBVHNode>& bvh_nodes, bool useKDProductBVH = true)
  {
    std::vector<std::shared_ptr<RTCSGNode>> products;
    collect_products(node, products);
    if (products.empty()) return 4u;

    countDNFNodes(products);

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

    // Step 1b: prune products with degenerate (empty) bounds.
    // After DNF distribution, many products are geometrically empty
    // (e.g., intersection of non-overlapping objects). Remove them to shrink the BVH.
    size_t before_prune = flat.size();
    flat.erase(std::remove_if(flat.begin(), flat.end(),
                              [](const FlatProduct& fp) { return fp.bounds.skip != 0; }),
               flat.end());
    if (before_prune != flat.size()) {
      std::cout << "[RT/DNF] Pruned " << (before_prune - flat.size()) << " / " << before_prune
                << " empty products (" << flat.size() << " remaining)" << std::endl;
    }
    if (flat.empty()) return 4u;

    uint32_t max_stack = 4u;
    for (const auto& fp : flat)
      max_stack =
        std::max(max_stack, compute_eval_stack_depth(product_commands, fp.cmd_start, fp.cmd_count));

    // Step 2: build BVH over flat products (KD-sorted or naive midpoint)
    buildProductBVH(flat, 0, (int)flat.size(), bvh_nodes, useKDProductBVH);

    std::cout << "[RT/DNF] " << flat.size() << " products, BVH nodes: " << bvh_nodes.size()
              << ", max_stack: " << max_stack << std::endl;

    return max_stack;
  }
};
#endif  // OPENSCAD_CSGTREE_H
