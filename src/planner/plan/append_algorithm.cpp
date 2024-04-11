#include "binder/query/reading_clause/bound_algorithm_call.h"
#include "planner/operator/logical_algorithm_call.h"
#include "planner/planner.h"

using namespace kuzu::binder;

namespace kuzu {
namespace planner {

std::shared_ptr<LogicalOperator> Planner::getAlgorithm(const BoundReadingClause& readingClause) {
    auto& call = readingClause.constCast<BoundAlgorithmCall>();
    return std::make_shared<LogicalAlgorithmCall>(call.getFunc(), call.getBindData(),
        call.getGraphExpr(), call.getOutExprs());
}

} // namespace planner
} // namespace kuzu
