#include "function/table/sssp.h"

namespace kuzu {
namespace function {

struct LengthUnionResult {
    bool isIntersect = 0;
    uint64_t nodeCount = 0;
    std::vector<std::vector<nodeID_t>> tempFrontier;
};

class LengthScanState {
public:
    explicit LengthScanState(SharedDataWithoutValue& sharedData, nodeID_t nodeID,
        std::string direction)
        : sharedData(sharedData), direction(direction) {

        nodeNumbers = 1;

        std::vector<nodeID_t> nextMission;
        nextMission.emplace_back(nodeID);
        nextFrontier.emplace_back(std::move(nextMission));

        globalBitSet = std::make_shared<InternalIDBitSet>(sharedData.context->getCatalog(),
            sharedData.context->getStorageManager(), sharedData.context->getTx());
        globalBitSet->markVisited(nodeID);
    }

    void scanTask(std::shared_ptr<storage::RelTableScanState> readState,
        common::ValueVector& srcVector, std::shared_ptr<RelTableInfo> info,
        std::shared_ptr<InternalIDBitSet> threadBitSet, RelDataDirection relDataDirection,
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
            while (readState->hasMoreToRead(tx)) {
                relTable->scan(tx, *readState.get());

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
                    threadBitSet->markIfUnVisitedReturnVisited(*globalBitSet, nbrID);
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

    LengthUnionResult unionTask(uint32_t tid, const LengthScanState& otherScanState) {
        uint64_t nodeCount = 0;
        bool isIntersect = false;
        std::vector<std::vector<nodeID_t>> tempFrontier;
        auto numThreads = sharedData.numThreads;
        for (auto tableID = 0u; tableID < globalBitSet->getTableNum(); ++tableID) {
            uint32_t tableSize = globalBitSet->getTableSize(tableID);
            if (!tableSize) {
                continue;
            }
            uint32_t l = tableSize * tid / numThreads, r = tableSize * (tid + 1) / numThreads;
            std::vector<uint64_t> tempMark;
            tempMark.reserve(r - l);
            tempMark.resize(r - l, 0);
            for (auto i = 0u; i < numThreads; ++i) {
                for (auto offset = l; offset < r; ++offset) {
                    auto mark = sharedData.threadBitSets[i]->getAndReset(tableID, offset);
                    tempMark[offset - l] |= mark;
                }
            }

            std::vector<nodeID_t> nextMission;
            for (auto i = l; i < r; ++i) {
                uint64_t now = tempMark[i - l], pos = 0;
                if (!now) {
                    continue;
                }
                globalBitSet->markVisited(tableID, i, now);

                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = InternalIDBitSet::getNodeOffset(i, pos);
                    auto nowNode = nodeID_t{offset, tableID};
                    isIntersect |= otherScanState.globalBitSet->isVisited(nowNode);
                    if (!isIntersect) {
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
            if (!isIntersect && !nextMission.empty()) {
                nodeCount += nextMission.size();
                tempFrontier.emplace_back(std::move(nextMission));
            }
        }
        return {isIntersect, nodeCount, tempFrontier};
    }

    bool isFrontierEmpty() const { return nextFrontier.empty(); }

    bool getNextFrontier(LengthScanState& other) {
        nodeNumbers = taskID = 0;
        std::vector<std::thread> threads;
        for (auto i = 0u; i < sharedData.numThreads; ++i) {
            threads.emplace_back([&, i] { funcTask(i); });
        }
        for (auto& thread : threads) {
            thread.join();
        }

        std::vector<std::future<LengthUnionResult>> unionFuture;
        for (auto i = 0u; i < sharedData.numThreads; i++) {
            unionFuture.emplace_back(std::async(std::launch::async, &LengthScanState::unionTask,
                this, i, std::ref(other)));
        }
        nextFrontier.clear();
        bool isIntersect = false;
        for (auto& future : unionFuture) {
            auto [flag, count, tempFrontier] = future.get();
            isIntersect |= flag;
            if (!isIntersect) {
                nextFrontier.insert(nextFrontier.end(),
                    std::make_move_iterator(tempFrontier.begin()),
                    std::make_move_iterator(tempFrontier.end()));
                nodeNumbers += count;
            }
        }
        return isIntersect;
    }

    SharedDataWithoutValue& sharedData;
    std::string direction;
    std::atomic_uint64_t taskID;
    uint64_t nodeNumbers;
    std::vector<std::vector<nodeID_t>> nextFrontier; //  block划分的内部vector

    // 记录点的count数
    std::shared_ptr<InternalIDBitSet> globalBitSet;
};

common::offset_t lengthFunc(TableFuncInput& input, TableFuncOutput& output) {
    auto sharedState = input.sharedState->ptrCast<CallFuncSharedState>();
    if (!sharedState->getMorsel().hasMoreToOutput()) {
        return 0;
    }

    auto& dataChunk = output.dataChunk;
    auto pos = dataChunk.state->getSelVector()[0];
    auto bindData = input.bindData->constPtrCast<SsspBindData>();
    auto numThreads = bindData->numThreads;
    auto direction = bindData->direction;
    auto maxHop = bindData->maxHop;
    SharedDataWithoutValue sharedData(bindData, numThreads);
    auto srcNodeID = getNodeID(sharedData.context, bindData->srcTableName, bindData->srcPrimaryKey);
    auto dstNodeID = getNodeID(sharedData.context, bindData->dstTableName, bindData->dstPrimaryKey);
    auto srcDirection = direction;
    auto dstDirection = (direction == "in") ? "out" : ((direction == "out") ? "in" : "both");
    LengthScanState srcScanState(sharedData, srcNodeID, srcDirection);
    LengthScanState dstScanState(sharedData, dstNodeID, dstDirection);
    int64_t result = -1;
    if (srcNodeID == dstNodeID) {
        result = 0;
    } else {
        int64_t hop = 0;
        bool isIntersect = false;
        while (hop < maxHop) {
            auto srcIsEmpty = srcScanState.isFrontierEmpty();
            auto dstIsEmpty = dstScanState.isFrontierEmpty();
            if (srcIsEmpty && dstIsEmpty) {
                break;
            }
            ++hop;
            if (!srcIsEmpty && !dstIsEmpty) {
                if (srcScanState.nodeNumbers <= dstScanState.nodeNumbers) {
                    isIntersect = srcScanState.getNextFrontier(dstScanState);
                } else {
                    isIntersect = dstScanState.getNextFrontier(srcScanState);
                }
            } else if (!srcIsEmpty) {
                isIntersect = srcScanState.getNextFrontier(dstScanState);
            } else {
                isIntersect = dstScanState.getNextFrontier(srcScanState);
            }
            if (isIntersect) {
                result = hop;
                break;
            }
        }
    }
    dataChunk.getValueVector(0)->setValue<int64_t>(pos, result);
    return 1;
}

} // namespace function
} // namespace kuzu