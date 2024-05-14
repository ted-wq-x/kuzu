#include "common/vector/value_vector.h"
#include "function/algorithm/algorithm_function_collection.h"
#include "function/algorithm_function.h"
#include "graph/graph.h"
#include "processor/result/factorized_table.h"

using namespace kuzu::common;
using namespace kuzu::processor;
using namespace kuzu::binder;
using namespace kuzu::storage;

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
    std::unique_ptr<ValueVector> degreeVector;
    std::vector<ValueVector*> vectors;

    explicit DemoAvgDegreeFuncLocalState(MemoryManager* mm) {
        degreeVector = std::make_unique<ValueVector>(*LogicalType::DOUBLE(), mm);
        degreeVector->state = DataChunkState::getSingleValueDataChunkState();
        vectors.push_back(degreeVector.get());
    }

    void materializeResult(double degree, FactorizedTable& table) {
        degreeVector->setValue<double>(0, degree);
        table.append(vectors);
    }
};

static std::unique_ptr<AlgoFuncLocalState> initLocalStateFunc(MemoryManager* mm) {
    return std::make_unique<DemoAvgDegreeFuncLocalState>(mm);
}

static void execFunc(const AlgoFuncInput& input, FactorizedTable& fTable) {
    auto localState = input.localState->ptrCast<DemoAvgDegreeFuncLocalState>();
    auto graph = input.graph;
    auto numNodes = graph->getNumNodes();
    auto numEdges = graph->getNumEdges();
    auto degree = (double)numEdges / (double)numNodes;
    localState->materializeResult(degree, fTable);
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
