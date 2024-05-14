#include "function/algorithm/algorithm_function_collection.h"
#include "function/algorithm_function.h"
#include "processor/result/factorized_table.h"
#include "graph/graph.h"
#include "function/algorithm/frontier.h"
#include "binder/expression/literal_expression.h"

using namespace kuzu::processor;
using namespace kuzu::common;
using namespace kuzu::binder;
using namespace kuzu::storage;

namespace kuzu {
namespace function {

struct ShortestPathExtraBindData final : public AlgoFuncExtraBindData {
    uint8_t upperBound;

    explicit ShortestPathExtraBindData(uint8_t upperBound) : upperBound{upperBound} {}

    std::unique_ptr<AlgoFuncExtraBindData> copy() const override {
        return std::make_unique<ShortestPathExtraBindData>(upperBound);
    }
};

static AlgoFuncBindData bindFunc(const expression_vector& params) {
    auto bindData = AlgoFuncBindData();
    bindData.columnNames.push_back("src");
    bindData.columnNames.push_back("dst");
    bindData.columnNames.push_back("length");
    bindData.columnTypes.push_back(*LogicalType::INTERNAL_ID());
    bindData.columnTypes.push_back(*LogicalType::INTERNAL_ID());
    bindData.columnTypes.push_back(*LogicalType::INT64());
    auto upperBound = params[1]->constCast<LiteralExpression>().getValue().getValue<int64_t>();
    bindData.extraData = std::make_unique<ShortestPathExtraBindData>(upperBound);
    return bindData;
}

struct SourceState {
    nodeID_t sourceNodeID;
    // Frontiers.
    Frontier currentFrontier;
    Frontier nextFrontier;
    // Visited state.
    common::offset_t numVisited = 0;
    std::vector<int64_t> visitedArray;

    explicit SourceState(nodeID_t sourceNodeID, common::offset_t numNodes) : sourceNodeID{sourceNodeID} {
        visitedArray.resize(numNodes);
        for (auto i = 0u; i < numNodes; ++i) {
            visitedArray[i] = -1;
        }
        currentFrontier = Frontier();
        currentFrontier.addNode(sourceNodeID, 1 /* multiplicity */);
        nextFrontier = Frontier();
    }

    bool allVisited() const {
        return numVisited == visitedArray.size();
    }
    bool visited(offset_t offset) const {
        return visitedArray[offset] != -1;
    }
    void markVisited(nodeID_t nodeID, int64_t length) {
        numVisited++;
        visitedArray[nodeID.offset] = length;
    }
    int64_t getLength(offset_t offset) const {
        return visitedArray[offset];
    }

    void initNextFrontier() {
        currentFrontier = std::move(nextFrontier);
        nextFrontier = Frontier();
    }
};

struct ShortestPathLocalState : public AlgoFuncLocalState {
    std::unique_ptr<ValueVector> srcNodeIDVector;
    std::unique_ptr<ValueVector> dstNodeIDVector;
    std::unique_ptr<ValueVector> lengthVector;
    std::vector<ValueVector*> vectors;

    explicit ShortestPathLocalState(MemoryManager* mm) {
        srcNodeIDVector = std::make_unique<ValueVector>(*LogicalType::INTERNAL_ID(), mm);
        dstNodeIDVector = std::make_unique<ValueVector>(*LogicalType::INTERNAL_ID(), mm);
        lengthVector = std::make_unique<ValueVector>(*LogicalType::INT64(), mm);
        srcNodeIDVector->state = DataChunkState::getSingleValueDataChunkState();
        dstNodeIDVector->state = DataChunkState::getSingleValueDataChunkState();
        lengthVector->state = DataChunkState::getSingleValueDataChunkState();
        vectors.push_back(srcNodeIDVector.get());
        vectors.push_back(dstNodeIDVector.get());
        vectors.push_back(lengthVector.get());
    }

    void materialize(graph::Graph* graph, const SourceState& sourceState,
        FactorizedTable& table) const {
        srcNodeIDVector->setValue<nodeID_t>(0, sourceState.sourceNodeID);
        for (auto offset = 0u; offset < graph->getNumNodes(); ++offset) {
            if (!sourceState.visited(offset)) {
                continue;
            }
            dstNodeIDVector->setValue<nodeID_t>(0, {offset, graph->getNodeTableID()});
            lengthVector->setValue<int64_t>(0, sourceState.getLength(offset));
            table.append(vectors);
        }
    }
};

static std::unique_ptr<AlgoFuncLocalState> initLocalStateFunc(storage::MemoryManager* mm) {
    return std::make_unique<ShortestPathLocalState>(mm);
}

static void execFunc(const AlgoFuncInput& input, FactorizedTable& output) {
    auto extraData = input.bindData.extraData->ptrCast<ShortestPathExtraBindData>();
    auto graph = input.graph;
    auto localState = input.localState->ptrCast<ShortestPathLocalState>();
    for (auto offset = 0u; offset < graph->getNumNodes(); ++offset) {
        auto sourceNodeID = nodeID_t {offset, graph->getNodeTableID()};
        auto sourceState = SourceState(sourceNodeID, graph->getNumNodes());
        // Start recursive computation for current source node ID.
        // TODO(Xiyang): we should do currentLevel < upperBound
        for (auto currentLevel = 0; currentLevel <= extraData->upperBound; ++currentLevel) {
            if (sourceState.allVisited()) {
                continue ;
            }
            // Compute next frontier.
            for (auto currentNodeID : sourceState.currentFrontier.getNodeIDs()) {
                if (sourceState.visited(currentNodeID.offset)) {
                    continue;
                }
                sourceState.markVisited(currentNodeID, currentLevel);
                auto numNbr = graph->scanNbrFwd(currentNodeID.offset);
                for (auto i = 0u; i < numNbr; ++i) {
                    auto nbrID = graph->getNbr(i);
                    sourceState.nextFrontier.addNode(nbrID, 1);
                }
            }
            sourceState.initNextFrontier();
        };
        localState->materialize(graph, sourceState, output);
    }
}


function_set ShortestPathFunction::getFunctionSet() {
    function_set result;
    auto function = std::make_unique<AlgorithmFunction>(name, std::vector<LogicalTypeID>{LogicalTypeID::ANY, LogicalTypeID::INT64});
    function->bindFunc = bindFunc;
    function->initLocalStateFunc = initLocalStateFunc;
    function->execFunc = execFunc;
    result.push_back(std::move(function));
    return result;
}

} // namespace function
} // namespace kuzu
