#pragma once

#include "planner/operator/extend/base_logical_extend.h"
#include "processor/data_pos.h"
#include "storage/predicate/column_predicate.h"

namespace kuzu {
namespace planner {

class LogicalExtend : public BaseLogicalExtend {
    static constexpr LogicalOperatorType type_ = LogicalOperatorType::EXTEND;

public:
    LogicalExtend(std::shared_ptr<binder::NodeExpression> boundNode,
        std::shared_ptr<binder::NodeExpression> nbrNode, std::shared_ptr<binder::RelExpression> rel,
        common::ExtendDirection direction, bool extendFromSource,
        binder::expression_vector properties, std::shared_ptr<LogicalOperator> child)
        : BaseLogicalExtend{type_, std::move(boundNode), std::move(nbrNode), std::move(rel),
              direction, extendFromSource, std::move(child)},
          properties{std::move(properties)} {}

    f_group_pos_set getGroupsPosToFlatten() override;

    void computeFactorizedSchema() override;
    void computeFlatSchema() override;

    binder::expression_vector getProperties() const { return properties; }
    void setPropertyPredicates(std::vector<storage::ColumnPredicateSet> predicates) {
        propertyPredicates = std::move(predicates);
    }
    const std::vector<storage::ColumnPredicateSet>& getPropertyPredicates() const {
        return propertyPredicates;
    }

    std::unique_ptr<LogicalOperator> copy() override;

    std::string getExpressionsForPrinting() const override{
        auto str = BaseLogicalExtend::getExpressionsForPrinting();
        auto outNodeVectorPos = processor::DataPos(getSchema()->getExpressionPos(*nbrNode->getInternalID()));
        std::vector<processor::DataPos> outVectorsPos;
        outVectorsPos.push_back(outNodeVectorPos);

        for (auto& expression : getProperties()) {
            outVectorsPos.emplace_back(getSchema()->getExpressionPos(*expression));
        }
        str += ",";
        for (const auto& item : outVectorsPos){
            str += "(" + std::to_string(item.dataChunkPos) + "_" + std::to_string(item.valueVectorPos) + ")";
        }
        return str;
    }

private:
    binder::expression_vector properties;
    std::vector<storage::ColumnPredicateSet> propertyPredicates;
};

} // namespace planner
} // namespace kuzu
