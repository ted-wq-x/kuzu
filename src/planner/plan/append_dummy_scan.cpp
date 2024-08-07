#include "planner/operator/scan/logical_dummy_scan.h"
#include "planner/planner.h"

namespace kuzu {
namespace planner {

void Planner::appendDummyScan(LogicalPlan& plan) {
    KU_ASSERT(plan.isEmpty());
    auto dummyScan = std::make_shared<LogicalDummyScan>();
    dummyScan->computeFactorizedSchema();
    plan.setLastOperator(std::move(dummyScan));
}

} // namespace planner
} // namespace kuzu
