#include "function/table/sssp.h"

namespace kuzu {
namespace function {

struct CountUnionResult {
    uint64_t ans = 0;
    uint64_t nodeCount = 0;
    std::vector<std::vector<nodeID_t>> tempFrontier;
};

class CountScanState {
public:
    explicit CountScanState(SharedDataWithValue& sharedData, nodeID_t nodeID, std::string direction)
        : sharedData(sharedData), direction(direction) {

        nodeNumbers = 1;

        std::vector<nodeID_t> nextMission;
        nextMission.emplace_back(nodeID);
        nextFrontier.emplace_back(std::move(nextMission));

        globalBitSet = std::make_shared<InternalIDCountBitSet>(sharedData.context->getCatalog(),
            sharedData.context->getStorageManager(), sharedData.context->getTx());
        globalBitSet->markVisited(nodeID, 1);
    }

    void scanTask(std::shared_ptr<storage::RelTableScanState> readState,
        common::ValueVector& srcVector, std::shared_ptr<RelTableInfo> info,
        std::shared_ptr<InternalIDCountBitSet> threadBitSet, RelDataDirection relDataDirection,
        std::vector<internalID_t>& data, const processor::ResultSet& resultSet,
        evaluator::ExpressionEvaluator* relExpr) {
        auto relTable = info->relTable;
        readState->direction = relDataDirection;
        // 因为读取的都是相同nodeGroup里数据,故不需要每次都reset
        readState->dataScanState->resetState();
        auto dataChunkToSelect = resultSet.dataChunks[0];
        auto& selectVector = dataChunkToSelect->state->getSelVectorUnsafe();
        auto dstIdDataChunk = dataChunkToSelect->getValueVector(0);
        auto tx = sharedData.context->getTx();
        for (auto& currentNodeID : data) {
            srcVector.setValue<nodeID_t>(0, currentNodeID);
            relTable->initializeScanState(tx, *readState.get());
            auto count = globalBitSet->getNodeValueCount(currentNodeID);
            while (readState->hasMoreToRead(tx)) {
                relTable->scan(tx, *readState);

                if (relExpr) {
                    bool hasAtLeastOneSelectedValue = relExpr->select(selectVector);
                    if (!dataChunkToSelect->state->isFlat() &&
                        dataChunkToSelect->state->getSelVector().isUnfiltered()) {
                        dataChunkToSelect->state->getSelVectorUnsafe().setToFiltered();
                    }
                    if (!hasAtLeastOneSelectedValue) {
                        continue;
                    }
                }
                for (auto i = 0u; i < selectVector.getSelSize(); ++i) {
                    auto nbrID = dstIdDataChunk->getValue<nodeID_t>(i);
                    if (globalBitSet->isVisited(nbrID)) {
                        continue;
                    }
                    threadBitSet->markVisited(nbrID, count);
                }
            }
        }
    }

    void funcTask(uint32_t threadId) {
        auto rs = sharedData.createResultSet();
        auto relEvaluate = sharedData.initEvaluator(*rs.get());
        auto srcVector =
            common::ValueVector(LogicalType::INTERNAL_ID(), sharedData.context->getMemoryManager());
        srcVector.state = DataChunkState::getSingleValueDataChunkState();
        std::vector<
            std::pair<std::shared_ptr<RelTableInfo>, std::shared_ptr<storage::RelTableScanState>>>
            relTableScanStates;
        for (auto& [tableID, info] : *sharedData.reltables) {
            auto readState = std::make_shared<storage::RelTableScanState>(info->columnIDs,
                RelDataDirection::FWD);
            readState->nodeIDVector = &srcVector;
            for (const auto& item : rs->getDataChunk(0)->valueVectors) {
                readState->outputVectors.push_back(item.get());
            }
            relTableScanStates.emplace_back(info, std::move(readState));
        }
        evaluator::ExpressionEvaluator* relFilter = nullptr;
        if (relEvaluate) {
            relFilter = relEvaluate.get();
        }
        auto threadBitSet = sharedData.getThreadBitSet(threadId);
        auto size = nextFrontier.size();
        while (true) {
            uint32_t tid = taskID.fetch_add(1, std::memory_order_relaxed);
            if (tid >= size) {
                break;
            }
            auto& data = nextFrontier[tid];
            auto tableID = data[0].tableID;
            for (auto& [info, relDataReadState] : relTableScanStates) {
                if (direction == "out" || direction == "both") {
                    if (tableID == info->srcTableID) {
                        scanTask(relDataReadState, srcVector, info, threadBitSet,
                            RelDataDirection::FWD, data, *rs.get(), relFilter);
                    }
                }

                if (direction == "in" || direction == "both") {
                    if (tableID == info->dstTableID) {
                        scanTask(relDataReadState, srcVector, info, threadBitSet,
                            RelDataDirection::BWD, data, *rs.get(), relFilter);
                    }
                }
            }
        }
    }

    CountUnionResult unionTask(uint32_t tid, const CountScanState& otherScanState) {
        uint64_t nodeCount = 0;
        uint64_t ans = 0;
        std::vector<std::vector<nodeID_t>> tempFrontier;
        auto numThreads = sharedData.numThreads;
        auto& threadBitSets = sharedData.threadBitSets;
        for (auto tableID = 0u; tableID < globalBitSet->getTableNum(); ++tableID) {
            uint32_t tableSize = globalBitSet->getTableSize(tableID);
            if (!tableSize) {
                continue;
            }
            uint32_t l = tableSize * tid / numThreads, r = tableSize * (tid + 1) / numThreads;
            std::vector<Count> tempMark;
            tempMark.reserve(r - l);
            tempMark.resize(r - l);
            for (auto i = 0u; i < numThreads; ++i) {
                auto threadBitSet = threadBitSets[i];
                for (auto offset = l; offset < r; ++offset) {
                    auto& value = threadBitSet->getNodeValue(tableID, offset);
                    if (value.mark) {
                        if (tempMark[offset - l].count == nullptr) {
                            tempMark[offset - l].count = new uint32_t[64]();
                        }
                        tempMark[offset - l].merge(value);
                        value.reset();
                    }
                }
            }
            std::vector<nodeID_t> nextMission;
            for (auto i = l; i < r; ++i) {
                auto& mark = tempMark[i - l];
                uint64_t now = mark.mark;
                if (!now) {
                    continue;
                }

                globalBitSet->merge(tableID, i, mark);
                mark.free();

                uint64_t pos;
                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = InternalIDBitSet::getNodeOffset(i, pos);
                    auto nowNode = nodeID_t{offset, tableID};
                    auto otherSideCount = otherScanState.globalBitSet->getNodeValueCount(nowNode);
                    if (otherSideCount) {
                        ans += 1LL * globalBitSet->getNodeValueCount(nowNode) * otherSideCount;
                    }
                    if (!ans) {
                        if ((!nextMission.empty() &&
                                ((nextMission.back().offset ^ nowNode.offset) >>
                                    StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                            nodeCount += nextMission.size();
                            tempFrontier.emplace_back(std::move(nextMission));
                        }
                        nextMission.emplace_back(nowNode);
                    }

                    // 还原成 now & (now - 1) ,和result配合,统计now中1的个数 now=now & (now - 1)
                    now ^= pos;
                }
            }
            if (!ans && !nextMission.empty()) {
                nodeCount += nextMission.size();
                tempFrontier.emplace_back(std::move(nextMission));
            }
        }
        return {ans, nodeCount, tempFrontier};
    }

    bool isFrontierEmpty() const { return nextFrontier.empty(); }

    uint64_t getNextFrontier(CountScanState& other) {
        nodeNumbers = taskID = 0;
        std::vector<std::thread> threads;
        for (auto i = 0u; i < sharedData.numThreads; ++i) {
            threads.emplace_back([&, i] { funcTask(i); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        std::vector<std::future<CountUnionResult>> unionFuture;
        for (auto i = 0u; i < sharedData.numThreads; i++) {
            unionFuture.emplace_back(std::async(std::launch::async, &CountScanState::unionTask,
                this, i, std::ref(other)));
        }
        nextFrontier.clear();
        uint64_t pathCount = 0;
        for (auto& future : unionFuture) {
            auto [ans, count, tempFrontier] = future.get();
            pathCount += ans;
            if (pathCount == 0) {
                nextFrontier.insert(nextFrontier.end(),
                    std::make_move_iterator(tempFrontier.begin()),
                    std::make_move_iterator(tempFrontier.end()));
                nodeNumbers += count;
            }
        }
        return pathCount;
    }

    SharedDataWithValue& sharedData;
    std::string direction;
    std::atomic_uint64_t taskID;
    uint64_t nodeNumbers;
    std::vector<std::vector<nodeID_t>> nextFrontier; //  block划分的内部vector

    // 记录点的count数
    std::shared_ptr<InternalIDCountBitSet> globalBitSet;
};

common::offset_t countFunc(TableFuncInput& input, TableFuncOutput& output) {
    auto sharedState = input.sharedState->ptrCast<CallFuncSharedState>();
    if (!sharedState->getMorsel().hasMoreToOutput()) {
        return 0;
    }
    auto& dataChunk = output.dataChunk;
    auto pos = dataChunk.state->getSelVector()[0];
    auto bindData = input.bindData->constPtrCast<SsspBindData>();
    auto resultType = bindData->resultType;
    auto numThreads = bindData->numThreads;
    auto direction = bindData->direction;
    auto maxHop = bindData->maxHop;
    SharedDataWithValue sharedData(bindData, numThreads);
    auto srcNodeID = getNodeID(sharedData.context, bindData->srcTableName, bindData->srcPrimaryKey);
    auto dstNodeID = getNodeID(sharedData.context, bindData->dstTableName, bindData->dstPrimaryKey);
    auto srcDirection = direction;
    auto dstDirection = (direction == "in") ? "out" : ((direction == "out") ? "in" : "both");
    CountScanState srcScanState(sharedData, srcNodeID, srcDirection);
    CountScanState dstScanState(sharedData, dstNodeID, dstDirection);
    int64_t lengthResult = -1, numberResult = 0;
    if (srcNodeID == dstNodeID) {
        lengthResult = 0;
        numberResult = 1;
    } else {
        int64_t hop = 0;
        while (hop < maxHop) {
            auto srcIsEmpty = srcScanState.isFrontierEmpty();
            auto dstIsEmpty = dstScanState.isFrontierEmpty();
            if (srcIsEmpty && dstIsEmpty) {
                break;
            }
            ++hop;
            if (!srcIsEmpty && !dstIsEmpty) {
                if (srcScanState.nodeNumbers <= dstScanState.nodeNumbers) {
                    numberResult = srcScanState.getNextFrontier(dstScanState);
                } else {
                    numberResult = dstScanState.getNextFrontier(srcScanState);
                }
            } else if (!srcIsEmpty) {
                numberResult = srcScanState.getNextFrontier(dstScanState);
            } else {
                numberResult = dstScanState.getNextFrontier(srcScanState);
            }

            if (numberResult) {
                lengthResult = hop;
                break;
            }
        }
    }
    if (resultType == "count") {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, numberResult);
    } else {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, lengthResult);
        dataChunk.getValueVector(1)->setValue<int64_t>(pos, numberResult);
    }
    return 1;
}

} // namespace function
} // namespace kuzu