#pragma once

#include "function/algorithm_function.h"
#include "planner/operator/logical_operator.h"

namespace kuzu {
namespace planner {

class LogicalAlgorithmCall final : public LogicalOperator {
    static constexpr LogicalOperatorType operatorType_ = LogicalOperatorType::ALGORITHM;

public:
    LogicalAlgorithmCall(function::AlgorithmFunction algoFunc, function::AlgoFuncBindData bindData,
        std::shared_ptr<binder::Expression> graphExpr, binder::expression_vector outExprs)
        : LogicalOperator{operatorType_}, algoFunc{std::move(algoFunc)},
          bindData{std::move(bindData)}, graphExpr{std::move(graphExpr)},
          outExprs{std::move(outExprs)} {}

    void computeFlatSchema() override;
    void computeFactorizedSchema() override;

    function::AlgorithmFunction getFunction() const { return algoFunc; }
    function::AlgoFuncBindData getBindData() const { return bindData.copy(); }
    std::shared_ptr<binder::Expression> getGraphExpr() const { return graphExpr; }
    binder::expression_vector getOutExprs() const { return outExprs; }

    std::string getExpressionsForPrinting() const override { return algoFunc.name; }

    std::unique_ptr<LogicalOperator> copy() override {
        return std::make_unique<LogicalAlgorithmCall>(algoFunc, bindData.copy(), graphExpr,
            outExprs);
    }

private:
    // Algorithm function.
    function::AlgorithmFunction algoFunc;
    // Algorithm bind data.
    function::AlgoFuncBindData bindData;
    // Input graph.
    std::shared_ptr<binder::Expression> graphExpr;
    // Algorithm output expressions.
    binder::expression_vector outExprs;
};

} // namespace planner
} // namespace kuzu
