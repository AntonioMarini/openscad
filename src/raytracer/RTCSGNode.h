//
// Created by apollyon-black on 2/17/26.
//

#ifndef OPENSCAD_RTCSGNODE_H
#define OPENSCAD_RTCSGNODE_H

#include <vector>
#include <memory>

#include "OBB.h"
#include "Primitive.h"
#include "Operation.h"
#include "CSGCommand.h"
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
                            std::vector<OBB>& obbs)
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
          obbs.push_back(obbs[node_first_cmd_id[node.get()]]);
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
      obbs.push_back(OBB::buildPrimitiveOBB(*node));
    } else {
      Operation op(node->op, 0, 0);
      auto op_id = (unsigned int)operations.size();
      operations.push_back(op);

      // 2nd+ occurrences use CACHED_OPERATION so the shader tries cache first,
      // but falls back to children on miss (real eviction support)
      CSGCommandType cmd_type = is_second_occurrence ? CSGCommandType::CACHED_OPERATION : CSGCommandType::OPERATION;
      CSGCommand cmd(cmd_type, op_id);
      cmd.skip_children = 0;
      if (is_second_occurrence)
        cmd.duplicate_id = second_dup_id;
      else if (is_shared)
        cmd.duplicate_id = node_duplicate_id[node.get()];

      commands.push_back(cmd);
      obbs.emplace_back();

      unsigned int left_id = flatten_tree(node->left, primitives, operations, commands, obbs);
      unsigned int right_id = flatten_tree(node->right, primitives, operations, commands, obbs);

      commands[cmd_id].skip_children = (unsigned int)commands.size() - cmd_id;
      obbs[cmd_id] = OBB::buildOperationOBB(node->op, obbs[left_id], obbs[right_id]);
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
      // print_matrix(node->transform);
    } else {
      std::cout << "Operation: " << static_cast<int>(node->op) << "\n";
      print_tree(node->left, depth + 1);
      print_tree(node->right, depth + 1);
    }
  }
};
#endif  // !CSG_TREE_H
