#pragma once

#include "binder/expression/expression.h"
#include "binder/expression/expression_util.h"
#include "planner/operator/logical_operator.h"

namespace kuzu {
namespace planner {

class LogicalProjection : public LogicalOperator {
public:
    explicit LogicalProjection(binder::expression_vector expressions,
        std::shared_ptr<LogicalOperator> child)
        : LogicalOperator{LogicalOperatorType::PROJECTION, std::move(child)},
          expressions{std::move(expressions)} {}

    void computeFactorizedSchema() override;
    void computeFlatSchema() override;

    inline std::string getExpressionsForPrinting() const override {
        std::string str = binder::ExpressionUtil::toString(expressions) + ",";
        for (const auto& item : expressions) {
            auto pair = getSchema()->getExpressionPos(*item);
            str += "(" + std::to_string(pair.first) + "_" + std::to_string(pair.second) + ")";
        }
        return str;
    }

    inline binder::expression_vector getExpressionsToProject() const { return expressions; }

    std::unordered_set<uint32_t> getDiscardedGroupsPos() const;

    std::unique_ptr<LogicalOperator> copy() override {
        return make_unique<LogicalProjection>(expressions, children[0]->copy());
    }

private:
    binder::expression_vector expressions;
};

} // namespace planner
} // namespace kuzu
