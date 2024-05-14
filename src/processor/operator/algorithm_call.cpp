#include "processor/operator/algorithm_call.h"

#include "binder/expression/graph_expression.h"
#include "graph/on_disk_graph.h"

using namespace kuzu::binder;
using namespace kuzu::graph;

namespace kuzu {
namespace processor {

void AlgorithmCall::initLocalStateInternal(ResultSet*, ExecutionContext* context) {
    algoFuncInput.graph = sharedState->graph.get();
    algoFuncInput.bindData = info.bindData.copy();
    algoFuncInput.localState =
        info.function.initLocalStateFunc(context->clientContext->getMemoryManager());
}

void AlgorithmCall::initGlobalStateInternal(ExecutionContext* context) {
    auto graphExpr_ = info.graphExpr->constPtrCast<GraphExpression>();
    sharedState->graph =
        std::make_unique<OnDiskGraph>(context->clientContext, graphExpr_->getNodeName(), graphExpr_->getRelName());
}

void AlgorithmCall::executeInternal(ExecutionContext*) {
    info.function.execFunc(algoFuncInput, *sharedState->fTable);
}

} // namespace processor
} // namespace kuzu
