#pragma once

#include "binder/query/reading_clause/bound_reading_clause.h"
#include "function/algorithm_function.h"

namespace kuzu {
namespace binder {

class BoundAlgorithmCall : public BoundReadingClause {
    static constexpr common::ClauseType clauseType_ = common::ClauseType::ALGORITHM_CALL;

public:
    BoundAlgorithmCall(function::AlgorithmFunction algoFunc, function::AlgoFuncBindData bindData,
        std::shared_ptr<Expression> graphExpr, expression_vector nodeInputs,
        expression_vector outExprs)
        : BoundReadingClause{clauseType_}, algoFunc{std::move(algoFunc)},
          bindData{std::move(bindData)}, graphExpr{std::move(graphExpr)},
          nodeInputs{std::move(nodeInputs)}, outExprs{std::move(outExprs)} {}

    function::AlgorithmFunction getFunc() const { return algoFunc; }
    function::AlgoFuncBindData getBindData() const { return bindData.copy(); }
    std::shared_ptr<Expression> getGraphExpr() const { return graphExpr; }
    expression_vector getNodeInputs() const { return nodeInputs; }
    expression_vector getOutExprs() const { return outExprs; }

private:
    // Algorithm function.
    function::AlgorithmFunction algoFunc;
    // Algorithm bind data.
    function::AlgoFuncBindData bindData;
    // Input graph.
    std::shared_ptr<binder::Expression> graphExpr;
    // Additional inputs to algorithm other than the graph. This is limited to nodes for now.
    binder::expression_vector nodeInputs;
    // Algorithm output expressions.
    expression_vector outExprs;
};

} // namespace binder
} // namespace kuzu
