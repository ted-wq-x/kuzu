#pragma once

#include "expression.h"

namespace kuzu {
namespace binder {

// Represent on disk graph
class GraphExpression : public Expression {
    static constexpr common::ExpressionType exprType = common::ExpressionType::GRAPH;

public:
    GraphExpression(std::string uniqueName, std::vector<std::string> tableNames)
        : Expression{exprType, *common::LogicalType::ANY(), std::move(uniqueName)},
          tableNames{std::move(tableNames)} {}

    std::vector<std::string> getTableNames() const { return tableNames; }

protected:
    std::string toStringInternal() const override { return alias.empty() ? uniqueName : alias; }

private:
    // NOTE: I'm still debating if we should use table id here.
    std::vector<std::string> tableNames;
};

} // namespace binder
} // namespace kuzu
