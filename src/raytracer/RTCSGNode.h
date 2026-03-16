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

// FLATTENER CLASS
class CSGTree
{
public:
  std::shared_ptr<RTCSGNode> root;

  CSGTree(std::shared_ptr<RTCSGNode> root_node) : root(root_node) {};

  std::map<RTCSGNode *, unsigned int> node_count;
  std::map<RTCSGNode *, unsigned int> node_duplicate_id;
  std::map<RTCSGNode *, unsigned int> node_first_cmd_id;  // cmd index of first emission
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

    if (is_shared) {
      auto dup_it = node_duplicate_id.find(node.get());
      if (dup_it != node_duplicate_id.end()) {
        // Already fully emitted — push a lightweight cache reference id.
        CSGCommand cache_ref_cmd(CSGCommandType::CACHED_REF, 0);
        cache_ref_cmd.skip_children = 1;
        cache_ref_cmd.duplicate_id = dup_it->second;
        auto cache_ref_cmd_id = (unsigned int)commands.size();
        commands.push_back(cache_ref_cmd);
        obbs.push_back(obbs[node_first_cmd_id[node.get()]]);  // reuse original OBB
        return cache_ref_cmd_id;
      }

      // First occurrence: reserve duplicate_id now so recursive children
      // can already reference it if needed.
      node_duplicate_id[node.get()] = next_duplicate_id++;
    }

    auto cmd_id = (unsigned int)commands.size();

    if (node->is_leaf()) {
      Primitive p(node->primitive, node->color, node->transform, node->r1, node->r2);
      auto prim_id = (unsigned int)primitives.size();
      primitives.push_back(p);

      CSGCommand cmd(CSGCommandType::PRIMITIVE, prim_id);
      cmd.skip_children = 1;
      if (is_shared) cmd.duplicate_id = node_duplicate_id[node.get()];

      commands.push_back(cmd);
      obbs.push_back(OBB::buildPrimitiveOBB(*node));
    } else {
      Operation op(node->op, 0, 0);
      auto op_id = (unsigned int)operations.size();
      operations.push_back(op);

      CSGCommand cmd(CSGCommandType::OPERATION, op_id);
      cmd.skip_children = 0;
      if (is_shared) cmd.duplicate_id = node_duplicate_id[node.get()];

      commands.push_back(cmd);
      obbs.emplace_back();

      unsigned int left_id = flatten_tree(node->left, primitives, operations, commands, obbs);
      unsigned int right_id = flatten_tree(node->right, primitives, operations, commands, obbs);

      commands[cmd_id].skip_children = (unsigned int)commands.size() - cmd_id;
      obbs[cmd_id] = OBB::buildOperationOBB(node->op, obbs[left_id], obbs[right_id]);
      operations[op_id].left_id = left_id;
      operations[op_id].right_id = right_id;
    }

    if (is_shared) node_first_cmd_id[node.get()] = cmd_id;

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
