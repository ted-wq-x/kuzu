#include "planner/operator/logical_dummy_sink.h"

namespace kuzu {
namespace planner {

void LogicalDummySink::computeFactorizedSchema() {
    copyChildSchema(0);
}

void LogicalDummySink::computeFlatSchema() {
    copyChildSchema(0);
}

} // namespace planner
} // namespace kuzu
