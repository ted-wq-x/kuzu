#include "planner/operator/logical_algorithm_call.h"

namespace kuzu {
namespace planner {

void LogicalAlgorithmCall::computeFlatSchema() {
    createEmptySchema();
    schema->createGroup();
    for (auto& e : outExprs) {
        schema->insertToGroupAndScope(e, 0);
    }
}

void LogicalAlgorithmCall::computeFactorizedSchema() {
    createEmptySchema();
    auto pos = schema->createGroup();
    for (auto& e : outExprs) {
        schema->insertToGroupAndScope(e, pos);
    }
}

} // namespace planner
} // namespace kuzu
