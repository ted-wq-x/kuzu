#include "binder/query/updating_clause/bound_set_clause.h"
#include "planner/logical_plan/logical_operator/logical_set.h"
#include "planner/query_planner.h"

namespace kuzu {
namespace planner {

void QueryPlanner::appendSetNodeProperty(
    const std::vector<std::unique_ptr<binder::BoundSetNodeProperty>>& setNodeProperties,
    LogicalPlan& plan) {
    std::vector<std::shared_ptr<NodeExpression>> nodes;
    std::vector<expression_pair> setItems;
    for (auto& setNodeProperty : setNodeProperties) {
        nodes.push_back(setNodeProperty->getNode());
        setItems.push_back(setNodeProperty->getSetItem());
    }
    for (auto i = 0u; i < setItems.size(); ++i) {
        auto lhsNodeID = nodes[i]->getInternalIDProperty();
        auto rhs = setItems[i].second;
        // flatten rhs
        auto rhsDependentGroupsPos = plan.getSchema()->getDependentGroupsPos(rhs);
        auto rhsGroupsPosToFlatten = factorization::FlattenAllButOne::getGroupsPosToFlatten(
            rhsDependentGroupsPos, plan.getSchema());
        appendFlattens(rhsGroupsPosToFlatten, plan);
        // flatten lhs if needed
        auto lhsGroupPos = plan.getSchema()->getGroupPos(*lhsNodeID);
        auto rhsLeadingGroupPos =
            SchemaUtils::getLeadingGroupPos(rhsDependentGroupsPos, *plan.getSchema());
        if (lhsGroupPos != rhsLeadingGroupPos) {
            appendFlattenIfNecessary(lhsGroupPos, plan);
        }
    }
    auto setNodeProperty = std::make_shared<LogicalSetNodeProperty>(
        std::move(nodes), std::move(setItems), plan.getLastOperator());
    setNodeProperty->computeFactorizedSchema();
    plan.setLastOperator(setNodeProperty);
}

void QueryPlanner::appendSetRelProperty(
    const std::vector<std::unique_ptr<binder::BoundSetRelProperty>>& setRelProperties,
    LogicalPlan& plan) {
    std::vector<std::shared_ptr<RelExpression>> rels;
    std::vector<expression_pair> setItems;
    for (auto& setRelProperty : setRelProperties) {
        rels.push_back(setRelProperty->getRel());
        setItems.push_back(setRelProperty->getSetItem());
    }
    auto setRelProperty = std::make_shared<LogicalSetRelProperty>(
        std::move(rels), std::move(setItems), plan.getLastOperator());
    for (auto i = 0u; i < setRelProperty->getNumRels(); ++i) {
        appendFlattens(setRelProperty->getGroupsPosToFlatten(i), plan);
        setRelProperty->setChild(0, plan.getLastOperator());
    }
    setRelProperty->computeFactorizedSchema();
    plan.setLastOperator(setRelProperty);
}

} // namespace planner
} // namespace kuzu