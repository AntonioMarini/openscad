//
// Created by apollyon-black on 2/16/26.
//

#ifndef OPENSCAD_RTCSGTREEVISITOR_H
#define OPENSCAD_RTCSGTREEVISITOR_H

#include "core/NodeVisitor.h"
#include "RTCSGNode.h"

#include <list>
#include <map>
#include <memory>
#include <vector>

/***
 * Used for traversing an abstract tree and provide a RTCSGNode struct reasy for raytracer
 */
class RTCSGTreeVisitor : public NodeVisitor  // nota: public!
{
public:
  Response visit(State& state, const AbstractNode& node) override;
  Response visit(State& state, const LeafNode& node) override;
  Response visit(State& state, const CsgOpNode& node) override;
  Response visit(State& state, const TransformNode& node) override;
  Response visit(State& state, const ColorNode& node) override;

  // Binarize balanced methods for lists of childrens
  std::shared_ptr<RTCSGNode> binarizeNaive(std::vector<std::shared_ptr<RTCSGNode>>& children,
                                           OperationType op);
  std::shared_ptr<RTCSGNode> binarizeKD(std::vector<std::shared_ptr<RTCSGNode>>& children,
                                        OperationType op);

  // Entry point — same pattern as CSGTreeEvaluator::buildCSGTree
  std::shared_ptr<RTCSGNode> buildRTTree(const AbstractNode& node);

  // Method used for distributing operations other than unions. Leaving all the unions on top of the
  // tree.
  std::shared_ptr<RTCSGNode> distributeOperation(std::shared_ptr<RTCSGNode> node);

  // Getter for the result
  std::shared_ptr<RTCSGNode> getRootNode() const { return rootNode; }

  Eigen::Vector3f getCentroid(const std::shared_ptr<RTCSGNode>& node);

  Eigen::Vector3f defaultColor = Eigen::Vector3f(1.0f, 1.0f, 1.0f);
  void setDefaultColor(const Eigen::Vector3f& col) { defaultColor = col; }

  bool useKDBinarization = true;
  void setBinarizationMethod(int m) { useKDBinarization = (m != 0); }

  ~RTCSGTreeVisitor() override = default;

private:
  void addToParent(const State& state, const AbstractNode& node);
  void applyToChildren(State& state, const AbstractNode& node, OperationType op);

  // Same mechanism as CSGTreeEvaluator
  using ChildList = std::list<std::shared_ptr<const AbstractNode>>;
  std::map<int, ChildList> visitedchildren;
  std::map<int, std::shared_ptr<RTCSGNode>> stored_term;

  std::shared_ptr<RTCSGNode> rootNode;
};

#endif  // OPENSCAD_RTCSGTREEVISITOR_H
