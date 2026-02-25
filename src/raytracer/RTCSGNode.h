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

struct RTCSGNode {
    std::shared_ptr<RTCSGNode> left = nullptr;
    std::shared_ptr<RTCSGNode> right = nullptr;

    OperationType op = OperationType::NONE;
    PrimitiveType primitive = PrimitiveType::NONE;

    Eigen::Matrix4f transform = Eigen::Matrix4f::Identity();
    Eigen::Vector3f color = Eigen::Vector3f(1.0f, 1.0f, 1.0f);

	Eigen::Vector3f min_bound = Eigen::Vector3f::Zero();
	Eigen::Vector3f max_bound = Eigen::Vector3f::Zero();

    bool is_leaf() const {
        return (left == nullptr || right == nullptr);
    }

    RTCSGNode(OperationType type, std::shared_ptr<RTCSGNode> l, std::shared_ptr<RTCSGNode> r)
        : op(type), left(l), right(r) {}

    RTCSGNode(PrimitiveType type, Eigen::Vector3f col = Eigen::Vector3f(1.0f, 1.0f, 1.0f))
        : primitive(type), color(col) {}

    void set_transform(const Eigen::Vector3f& pos, const Eigen::Vector3f& scale) {
        transform = Eigen::Matrix4f::Identity();
        transform(0,0) = scale.x();
        transform(1,1) = scale.y();
        transform(2,2) = scale.z();
        transform(0,3) = pos.x();
        transform(1,3) = pos.y();
        transform(2,3) = pos.z();
    }
};


inline void printRTCSGTree(const std::shared_ptr<RTCSGNode>& node, int depth = 0) {
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
    std::cout << indent << typeName
              << " color=(" << node->color.x() << ", " << node->color.y() << ", " << node->color.z() << ")"
              << std::endl;
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
class CSGTree {
public:
    std::shared_ptr<RTCSGNode> root;

    CSGTree(std::shared_ptr<RTCSGNode> root_node) : root(root_node) {};

    unsigned int flatten_tree(std::shared_ptr<RTCSGNode> node,
                        std::vector<Primitive>& primitives,
                        std::vector<Operation>& operations,
                        std::vector<CSGCommand>& commands)
    {
        if (!node) return 0xFFFFFFFF;

        if (node->is_leaf()) {
            Primitive p(node->primitive, node->color, node->transform);

            unsigned int prim_id = (unsigned int)primitives.size();
            primitives.push_back(p);

            CSGCommand cmd(CSGCommandType::PRIMITIVE, prim_id);
            cmd.obb = OBB::buildPrimitiveOBB(*node);

			// add skip child (just itself for leaf)
			cmd.skip_children = 1;

            unsigned int cmd_id = (unsigned int)commands.size();
            commands.push_back(cmd);

            return cmd_id;
        }

        unsigned int left_id = flatten_tree(node->left, primitives, operations, commands);
        unsigned int right_id = flatten_tree(node->right, primitives, operations, commands);

        Operation op(node->op, left_id, right_id);

        unsigned int op_id = (unsigned int)operations.size();
        operations.push_back(op);

        CSGCommand cmd(CSGCommandType::OPERATION, op_id);
        cmd.obb = OBB::buildOperationOBB(node->op, commands[left_id].obb, commands[right_id].obb);

		// add skip children (itself + left + right)
		cmd.skip_children = 1 + commands[left_id].skip_children + commands[right_id].skip_children;

        unsigned int cmd_id = (unsigned int)commands.size();
        commands.push_back(cmd);

        return cmd_id;
    }




	//helper function to print out matrix data to stdout for debugging
    void print_matrix(const Eigen::Matrix4f& mat) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                std::cout << mat(i, j) << " ";
            }
            std::cout << std::endl;
        }
	}

	// debug function to print the tree
    void print_tree(const std::shared_ptr<RTCSGNode>& node, int depth = 0) {
        if (!node) return;
        for (int i = 0; i < depth; ++i) std::cout << "  ";
        if (node->is_leaf()) {
			std::cout << "Primitive: " << static_cast<int>(node->primitive) << " " << printPrimitive(node->primitive)
				<< ", Color: (" << node->color.x() << ", " << node->color.y() << ", " << node->color.z() << ")\n";
			//print_matrix(node->transform);
        } else {
            std::cout << "Operation: " << static_cast<int>(node->op) << "\n";
            print_tree(node->left, depth + 1);
            print_tree(node->right, depth + 1);
        }
	}
};
#endif // !CSG_TREE_H


