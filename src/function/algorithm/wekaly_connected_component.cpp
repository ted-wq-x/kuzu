#include "function/algorithm/algorithm_function_collection.h"
#include "function/algorithm_function.h"
#include "graph/graph.h"
#include "processor/result/factorized_table.h"

using namespace kuzu::binder;
using namespace kuzu::common;
using namespace kuzu::processor;
using namespace kuzu::storage;

namespace kuzu {
namespace function {

static AlgoFuncBindData bindFunc(const expression_vector&) {
    auto bindData = AlgoFuncBindData();
    bindData.columnNames.push_back("node_id");
    bindData.columnNames.push_back("group_id");
    bindData.columnTypes.push_back(*LogicalType::INTERNAL_ID());
    bindData.columnTypes.push_back(*LogicalType::INT64());
    return bindData;
}

struct WeaklyConnectedComponentLocalState : public AlgoFuncLocalState {
    std::unique_ptr<ValueVector> nodeIDVector;
    std::unique_ptr<ValueVector> groupVector;
    std::vector<ValueVector*> vectors;

    explicit WeaklyConnectedComponentLocalState(MemoryManager* mm) {
        nodeIDVector = std::make_unique<ValueVector>(*LogicalType::INTERNAL_ID(), mm);
        groupVector = std::make_unique<ValueVector>(*LogicalType::INT64(), mm);
        nodeIDVector->state = DataChunkState::getSingleValueDataChunkState();
        groupVector->state = DataChunkState::getSingleValueDataChunkState();
        vectors.push_back(nodeIDVector.get());
        vectors.push_back(groupVector.get());
    }

    void materialize(graph::Graph* graph, const std::vector<int64_t>& groupArray, FactorizedTable& table) const {
        for (auto offset = 0u; offset < graph->getNumNodes(); ++offset) {
            nodeIDVector->setValue<nodeID_t>(0, {offset, graph->getNodeTableID()});
            groupVector->setValue<int64_t>(0, groupArray[offset]);
            table.append(vectors);
        }
    }
};

static std::unique_ptr<AlgoFuncLocalState> initLocalStateFunc(storage::MemoryManager* mm) {
    return std::make_unique<WeaklyConnectedComponentLocalState>(mm);
}

// DFS style
static void findConnectedComponent(graph::Graph* graph, std::vector<bool>& visitedArray, std::vector<int64_t>& groupArray, common::offset_t offset, int64_t groupID) {
    visitedArray[offset] = true;
    groupArray[offset] = groupID;
    auto numNbr = graph->scanNbrFwd(offset);
    auto nbrs = graph->getNbrs();
    for (auto nbrID : nbrs) {
        if (visitedArray[nbrID.offset]) {
            continue;
        }
        findConnectedComponent(graph, visitedArray, groupArray, nbrID.offset, groupID);
    }
}

static void execFunc(const AlgoFuncInput& input, FactorizedTable& output) {
    auto graph = input.graph;
    auto localState = input.localState->ptrCast<WeaklyConnectedComponentLocalState>();
    // Initialize state.
    std::vector<bool> visitedArray;
    std::vector<int64_t> groupArray;
    visitedArray.resize(graph->getNumNodes());
    groupArray.resize(graph->getNumNodes());
    for (auto offset = 0u; offset < graph->getNumNodes(); ++offset) {
        visitedArray[offset] = false;
        groupArray[offset] = -1;
    }
    // Compute weakly connected component.
    auto groupID = 0;
    for (auto offset = 0u; offset < graph->getNumNodes(); ++offset) {
        if (visitedArray[offset]) {
            continue;
        }
        findConnectedComponent(graph, visitedArray, groupArray, offset, groupID++);
    }
    // Materialize result.
    localState->materialize(graph, groupArray, output);
}

function_set WeaklyConnectedComponentFunction::getFunctionSet() {
    function_set result;
    auto function = std::make_unique<AlgorithmFunction>(name, std::vector<LogicalTypeID>{LogicalTypeID::ANY});
    function->bindFunc = bindFunc;
    function->initLocalStateFunc = initLocalStateFunc;
    function->execFunc = execFunc;
    result.push_back(std::move(function));
    return result;
}

}
}
