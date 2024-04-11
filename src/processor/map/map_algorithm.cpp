#include "planner/operator/logical_algorithm_call.h"
#include "processor/operator/algorithm_call.h"
#include "processor/plan_mapper.h"

using namespace kuzu::common;
using namespace kuzu::planner;

namespace kuzu {
namespace processor {

std::unique_ptr<PhysicalOperator> PlanMapper::mapAlgorithm(LogicalOperator* logicalOperator) {
    auto call = logicalOperator->constPtrCast<LogicalAlgorithmCall>();
    auto exprs = call->getOutExprs();
    auto outSchema = call->getSchema();
    // Create operator info.
    auto info = AlgorithmCallInfo(call->getFunction(), call->getBindData(), call->getGraphExpr());
    // Create shared state.
    auto tableSchema = std::make_unique<FactorizedTableSchema>();
    for (auto& e : exprs) {
        auto dataPos = getDataPos(*e, *outSchema);
        auto columnSchema = ColumnSchema(false /* isUnFlat */, dataPos.dataChunkPos,
            LogicalTypeUtils::getRowLayoutSize(e->getDataType()));
        tableSchema->appendColumn(std::move(columnSchema));
    }
    auto table =
        std::make_shared<FactorizedTable>(clientContext->getMemoryManager(), tableSchema->copy());
    auto sharedState = std::make_shared<AlgorithmCallSharedState>(table);
    auto algorithm = std::make_unique<AlgorithmCall>(std::make_unique<ResultSetDescriptor>(),
        std::move(info), sharedState, getOperatorID(), call->getExpressionsForPrinting());
    return createFTableScanAligned(call->getOutExprs(), outSchema, table, DEFAULT_VECTOR_CAPACITY,
        std::move(algorithm));
}

} // namespace processor
} // namespace kuzu
