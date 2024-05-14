#include "function/algorithm/algorithm_function_collection.h"
#include "function/algorithm_function.h"
#include "processor/result/factorized_table.h"
#include "graph/graph.h"

using namespace kuzu::processor;
using namespace kuzu::common;
using namespace kuzu::binder;
using namespace kuzu::storage;

namespace kuzu {
namespace function {

struct PageRankExtraBindData final : public AlgoFuncExtraBindData {
    double dampingFactor = 0.85;
    int64_t maxIteration = 10;
    double delta = 0.0001; // detect convergence

    PageRankExtraBindData() = default;

    std::unique_ptr<AlgoFuncExtraBindData> copy() const override {
        return std::make_unique<PageRankExtraBindData>();
    }
};

static AlgoFuncBindData bindFunc(const expression_vector& params) {
    auto bindData = AlgoFuncBindData();
    bindData.columnNames.push_back("node_id");
    bindData.columnNames.push_back("rank");
    bindData.columnTypes.push_back(*LogicalType::INTERNAL_ID());
    bindData.columnTypes.push_back(*LogicalType::DOUBLE());
    bindData.extraData = std::make_unique<PageRankExtraBindData>();
    return bindData;
}

struct PageRankLocalState : public AlgoFuncLocalState {
    std::unique_ptr<ValueVector> nodeIDVector;
    std::unique_ptr<ValueVector> rankVector;
    std::vector<ValueVector*> vectors;

    explicit PageRankLocalState(MemoryManager* mm) {
        nodeIDVector = std::make_unique<ValueVector>(*LogicalType::INTERNAL_ID(), mm);
        rankVector = std::make_unique<ValueVector>(*LogicalType::DOUBLE(), mm);
        nodeIDVector->state = DataChunkState::getSingleValueDataChunkState();
        rankVector->state = DataChunkState::getSingleValueDataChunkState();
        vectors.push_back(nodeIDVector.get());
        vectors.push_back(rankVector.get());
    }

    void materialize(graph::Graph* graph, const std::vector<double>& ranks, FactorizedTable& table) const {
        for (auto offset = 0u; offset < graph->getNumNodes(); ++offset) {
            nodeIDVector->setValue<nodeID_t>(0, {offset, graph->getNodeTableID()});
            rankVector->setValue<double>(0, ranks[offset]);
            table.append(vectors);
        }
    }
};

static std::unique_ptr<AlgoFuncLocalState> initLocalStateFunc(storage::MemoryManager* mm) {
    return std::make_unique<PageRankLocalState>(mm);
}

static void execFunc(const AlgoFuncInput& input, FactorizedTable& output) {
    auto extraData = input.bindData.extraData->ptrCast<PageRankExtraBindData>();
    auto graph = input.graph;
    auto localState = input.localState->ptrCast<PageRankLocalState>();
    // Initialize state.
    std::vector<double> ranks;
    ranks.resize(graph->getNumNodes());
    for (auto offset = 0u; offset < graph->getNumNodes(); ++offset) {
        ranks[offset] = (double )1 / graph->getNumNodes();
    }
    auto dampingValue = (1 - extraData->dampingFactor) / graph->getNumNodes();
    // Compute page rank.
    for (auto i = 0u; i < extraData->maxIteration; ++i) {
        auto change = 0.0;
        for (auto offset = 0u; offset < graph->getNumNodes(); ++offset) {
            auto rank = 0.0;
            auto numNbr = graph->scanNbrFwd(offset);
            auto nbrs = graph->getNbrs();
            for (auto& nbr : nbrs) {
                auto numNbrOfNbr = graph->scanNbrFwd(nbr.offset);
                if (numNbrOfNbr == 0) {
                    numNbrOfNbr = graph->getNumNodes();
                }
                rank += extraData->dampingFactor * (ranks[nbr.offset] / numNbrOfNbr);
            }
            rank += dampingValue;
            change += abs(ranks[offset] - rank);
            ranks[offset] = rank;
        }
        if (change < extraData->delta) {
            break ;
        }
    }
    // Materialize result.
    localState->materialize(graph, ranks, output);
}


function_set PageRankFunction::getFunctionSet() {
    function_set result;
    auto function = std::make_unique<AlgorithmFunction>(name, std::vector<LogicalTypeID>{LogicalTypeID::ANY});
    function->bindFunc = bindFunc;
    function->initLocalStateFunc = initLocalStateFunc;
    function->execFunc = execFunc;
    result.push_back(std::move(function));
    return result;
}

} // namespace function
} // namespace kuzu
