#include "function/algorithm/algorithm_function_collection.h"
#include "function/algorithm_function.h"
#include "processor/result/factorized_table.h"
#include "graph/graph.h"
#include "function/algorithm/frontier.h"
#include "binder/expression/expression_util.h"
#include "binder/expression/literal_expression.h"
#include "common/exception/binder.h"

using namespace kuzu::processor;
using namespace kuzu::common;
using namespace kuzu::binder;
using namespace kuzu::storage;

namespace kuzu {
namespace function {

struct VariableLengthPathExtraBindData final : public AlgoFuncExtraBindData {
    uint8_t lowerBound;
    uint8_t upperBound;

    VariableLengthPathExtraBindData(uint8_t lowerBound, uint8_t upperBound)
        : lowerBound{lowerBound}, upperBound{upperBound} {}

    std::unique_ptr<AlgoFuncExtraBindData> copy() const override {
        return std::make_unique<VariableLengthPathExtraBindData>(lowerBound, upperBound);
    }
};

static AlgoFuncBindData bindFunc(const expression_vector& params) {
    auto bindData = AlgoFuncBindData();
    bindData.columnNames.push_back("src");
    bindData.columnNames.push_back("dst");
    bindData.columnNames.push_back("length");
    bindData.columnNames.push_back("num_path");
    bindData.columnTypes.push_back(*LogicalType::INTERNAL_ID());
    bindData.columnTypes.push_back(*LogicalType::INTERNAL_ID());
    bindData.columnTypes.push_back(*LogicalType::INT64());
    bindData.columnTypes.push_back(*LogicalType::INT64());
    KU_ASSERT(params.size() == 3);
    for (auto i = 1u; i < 3u; ++i) {
        ExpressionUtil::validateExpressionType(*params[i], ExpressionType::LITERAL);
        ExpressionUtil::validateDataType(*params[i], *LogicalType::INT64());
    }
    auto lowerBound = params[1]->constCast<LiteralExpression>().getValue().getValue<int64_t>();
    if (lowerBound <= 0) {
        throw BinderException("Lower bound must be greater than 0.");
    }
    auto upperBound = params[2]->constCast<LiteralExpression>().getValue().getValue<int64_t>();
    bindData.extraData = std::make_unique<VariableLengthPathExtraBindData>(lowerBound, upperBound);
    return bindData;
}

struct SourceState {
    nodeID_t sourceNodeID;
    // Frontiers
    Frontier currentFrontier;
    Frontier nextFrontier;

    explicit SourceState(nodeID_t sourceNodeID) : sourceNodeID{sourceNodeID} {
        currentFrontier = Frontier();
        currentFrontier.addNode(sourceNodeID, 1 /* multiplicity */);
        nextFrontier = Frontier();
    }

    void initNextFrontier() {
        currentFrontier = std::move(nextFrontier);
        nextFrontier = Frontier();
    }
};


struct VariableLengthPathLocalState : public AlgoFuncLocalState {
    std::unique_ptr<ValueVector> srcNodeIDVector;
    std::unique_ptr<ValueVector> dstNodeIDVector;
    std::unique_ptr<ValueVector> lengthVector;
    std::unique_ptr<ValueVector> numPathVector;
    std::vector<ValueVector*> vectors;

    explicit VariableLengthPathLocalState(MemoryManager* mm) {
        srcNodeIDVector = std::make_unique<ValueVector>(*LogicalType::INTERNAL_ID(), mm);
        dstNodeIDVector = std::make_unique<ValueVector>(*LogicalType::INTERNAL_ID(), mm);
        lengthVector = std::make_unique<ValueVector>(*LogicalType::INT64(), mm);
        numPathVector = std::make_unique<ValueVector>(*LogicalType::INT64(), mm);
        srcNodeIDVector->state = DataChunkState::getSingleValueDataChunkState();
        dstNodeIDVector->state = DataChunkState::getSingleValueDataChunkState();
        lengthVector->state = DataChunkState::getSingleValueDataChunkState();
        numPathVector->state = DataChunkState::getSingleValueDataChunkState();
        vectors.push_back(srcNodeIDVector.get());
        vectors.push_back(dstNodeIDVector.get());
        vectors.push_back(lengthVector.get());
        vectors.push_back(numPathVector.get());
    }

    void materialize(const SourceState& sourceState, uint8_t length, FactorizedTable& table) const {
        srcNodeIDVector->setValue<nodeID_t>(0, sourceState.sourceNodeID);
        for (auto dstNodeID : sourceState.nextFrontier.getNodeIDs()) {
            dstNodeIDVector->setValue<nodeID_t>(0, dstNodeID);
            auto numPath = sourceState.nextFrontier.getMultiplicity(dstNodeID);
            lengthVector->setValue<int64_t>(0, length);
            numPathVector->setValue<int64_t>(0, numPath);
            table.append(vectors);
        }
    }
};

static std::unique_ptr<AlgoFuncLocalState> initLocalStateFunc(storage::MemoryManager* mm) {
    return std::make_unique<VariableLengthPathLocalState>(mm);
}

static void execFunc(const AlgoFuncInput& input, FactorizedTable& output) {
    auto extraData = input.bindData.extraData->ptrCast<VariableLengthPathExtraBindData>();
    auto graph = input.graph;
    auto localState = input.localState->ptrCast<VariableLengthPathLocalState>();
    for (auto offset = 0u; offset < graph->getNumNodes(); ++offset) {
        auto sourceNodeID = nodeID_t {offset, graph->getNodeTableID()};
        auto sourceState = SourceState(sourceNodeID);
        // Start recursive computation for current source node ID.
        for (auto currentLevel = 1; currentLevel <= extraData->upperBound; ++currentLevel) {
            // Compute next frontier.
            for (auto currentNodeID : sourceState.currentFrontier.getNodeIDs()) {
                auto currentMultiplicity = sourceState.currentFrontier.getMultiplicity(currentNodeID);
                auto numNbr = graph->scanNbrFwd(currentNodeID.offset);
                for (auto i = 0u; i < numNbr; ++i) {
                    auto nbrID = graph->getNbr(i);
                    sourceState.nextFrontier.addNode(nbrID, currentMultiplicity);
                }
            }
            if (currentLevel >= extraData->lowerBound) {
                localState->materialize(sourceState, currentLevel, output);
            }
            sourceState.initNextFrontier();
        };
    }
}


function_set VariableLengthPathFunction::getFunctionSet() {
    function_set result;
    auto function = std::make_unique<AlgorithmFunction>(name, std::vector<LogicalTypeID>{LogicalTypeID::ANY, LogicalTypeID::INT64, LogicalTypeID::INT64});
    function->bindFunc = bindFunc;
    function->initLocalStateFunc = initLocalStateFunc;
    function->execFunc = execFunc;
    result.push_back(std::move(function));
    return result;
}


} // namespace function
} // namespace kuzu
