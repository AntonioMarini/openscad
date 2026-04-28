#pragma once

#include <Eigen/Core>
#include <cstdint>

// A node in the product BVH.
// Internal nodes (is_leaf==0): bounds_inv covers the union of all product bounds in the subtree.
// Leaf nodes    (is_leaf==1): bounds_inv is the product's own bounds; cmd_start/cmd_count point into
//   product_commands[].
// Preorder flat layout: skip skip_children nodes (including self) on bounds miss.
// std430: 8 x uint32 (32 B) + mat4 (64 B) = 96 B.
struct alignas(16) ProductBVHNode {
  uint32_t is_leaf;        // 0 = internal union node, 1 = leaf product
  uint32_t skip_children;  // preorder skip count (includes self)
  uint32_t bounds_skip;    // 1 = degenerate bounds, skip test
  uint32_t bounds_type;    // 0 = AABB (col(0).xyz=min, col(1).xyz=max), 1 = OBB (mat4 inv)
  uint32_t cmd_start;      // leaf only: first index into product_commands[]
  uint32_t cmd_count;      // leaf only: number of commands in this product
  uint32_t _pad[2];
  Eigen::Matrix4f bounds_inv; // bounds data (OBB inv-transform or AABB packed)
};

// A node in a product's preorder command tree.
// Only PRIMITIVE (0) and OPERATION (1) types — no UNION ops in products.
// std430: 8 x uint32 (32 B) + mat4 (64 B) = 96 B.
struct alignas(16) ProductCommand {
  uint32_t type;           // 0 = PRIMITIVE, 1 = OPERATION
  uint32_t id;             // index into primitives[] or operations[]
  uint32_t skip_children;  // preorder skip count for this subtree
  uint32_t bounds_skip;    // 1 = degenerate, skip test entirely
  uint32_t bounds_type;    // 0 = AABB, 1 = OBB
  uint32_t duplicate_id;   // 0 = unique, >0 = shared subtree (cache key)
  uint32_t _pad[2];
  Eigen::Matrix4f bounds_inv; // col(0).xyz=min, col(1).xyz=max when bounds_type==0
};
