#include "common/exception/binder.h"
#include "common/vector/value_vector.h"
#include "function/algorithm/algorithm_function_collection.h"
#include "function/algorithm_function.h"
#include "graph/graph.h"
#include "processor/result/factorized_table.h"

using namespace kuzu::common;
using namespace kuzu::processor;

namespace kuzu {
namespace function {

static AlgoFuncBindData bindFunc(const binder::expression_vector& params) {
    auto bindData = AlgoFuncBindData();
    KU_ASSERT(params.size() == 1);
    bindData.columnNames.push_back("DEGREE");
    bindData.columnTypes.push_back(*LogicalType::DOUBLE());
    return bindData;
}

struct DemoAvgDegreeFuncLocalState : public AlgoFuncLocalState {
    ValueVector resultVector;
    std::vector<ValueVector*> vectors;

    DemoAvgDegreeFuncLocalState(const LogicalType& type, storage::MemoryManager* mm)
        : resultVector{type, mm} {
        resultVector.state = DataChunkState::getSingleValueDataChunkState();
        vectors.push_back(&resultVector);
    }
};

static std::unique_ptr<AlgoFuncLocalState> initLocalStateFunc(storage::MemoryManager* mm) {
    return std::make_unique<DemoAvgDegreeFuncLocalState>(*LogicalType::DOUBLE(), mm);
}

static void execFunc(const AlgoFuncInput& input, FactorizedTable& fTable) {
    auto localState = input.localState->ptrCast<DemoAvgDegreeFuncLocalState>();
    auto graph = input.graph;
    auto numNodes = graph->getNumNodes();
    auto numEdges = graph->getNumEdges();
    auto degree = (double)numEdges / (double)numNodes;
    localState->resultVector.setValue<double>(0, degree);
    fTable.append(localState->vectors);
}

function_set DemoAvgDegreeFunction::getFunctionSet() {
    function_set result;
    auto function =
        std::make_unique<AlgorithmFunction>(name, std::vector<LogicalTypeID>{LogicalTypeID::ANY});
    function->bindFunc = bindFunc;
    function->initLocalStateFunc = initLocalStateFunc;
    function->execFunc = execFunc;
    result.push_back(std::move(function));
    return result;
}

} // namespace function
} // namespace kuzu
