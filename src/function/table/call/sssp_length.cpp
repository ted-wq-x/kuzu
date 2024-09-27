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

        common::ExtendDirection extendDirection = getExtendDirection(direction);
        std::shared_ptr<Expression> filterExpr = nullptr;
        if (sharedData.relFilter) {
            filterExpr = sharedData.relFilter->getExpression();
        }
        scanners =
            createRelTableCollectionScanner(*sharedData.context, extendDirection, filterExpr);
    }

    void funcTask(uint32_t threadId) {
        auto rs = sharedData.createResultSet();

        auto localScanners = copyScanners(scanners);
        initRelTableCollectionScanner(*sharedData.context, localScanners, rs.get());

        auto relEvaluate = sharedData.initEvaluator(*rs.get());
        evaluator::ExpressionEvaluator* relFilter = nullptr;
        if (relEvaluate) {
            relFilter = relEvaluate.get();
        }

        auto srcIdValueVector = rs->dataChunks[0]->getValueVector(0);
        auto dstIdValueVector = rs->dataChunks[0]->getValueVector(1);
        auto& selectVector = dstIdValueVector->state->getSelVectorUnsafe();
        auto tx = sharedData.context->getTx();

        auto threadBitSet = sharedData.getThreadBitSet(threadId);
        auto size = nextFrontier.size();
        while (true) {
            uint32_t tid = taskID.fetch_add(1, std::memory_order_relaxed);
            if (tid >= size) {
                break;
            }
            auto& data = nextFrontier[tid];
            auto tableID = data[0].tableID;
            if (!localScanners.contains(tableID)) {
                continue;
            }
            auto& currentScanner = localScanners.at(tableID);
            for (auto& currentNodeID : data) {
                srcIdValueVector->setValue<nodeID_t>(0, currentNodeID);
                currentScanner.resetState();
                while (currentScanner.scan(tx)) {
                    if (relFilter) {
                        bool hasAtLeastOneSelectedValue = relFilter->select(selectVector);
                        if (!dstIdValueVector->state->isFlat() &&
                            dstIdValueVector->state->getSelVector().isUnfiltered()) {
                            dstIdValueVector->state->getSelVectorUnsafe().setToFiltered();
                        }
                        if (!hasAtLeastOneSelectedValue) {
                            continue;
                        }
                    }
                    const auto nbrData = reinterpret_cast<nodeID_t*>(dstIdValueVector->getData());
                    for (auto i = 0u; i < selectVector.getSelSize(); ++i) {
                        auto nbrID = nbrData[selectVector[i]];
                        threadBitSet->markIfUnVisitedReturnVisited(*globalBitSet, nbrID);
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
        auto& threadBitSets = sharedData.threadBitSets;
        for (auto tableID = 0u; tableID < globalBitSet->getTableNum(); ++tableID) {
            uint32_t tableSize = globalBitSet->getTableSize(tableID);
            if (!tableSize) {
                continue;
            }
            // 必须64对齐,否则会存在多线程同时markVisited里的markflag
            auto [l, r] = distributeTasks(tableSize, numThreads, tid);
            // 将结果汇总到第一个bt中
            auto targetBitSet = threadBitSets[0];
            for (auto i = 0u; i < numThreads; ++i) {
                auto threadBitSet = threadBitSets[i];
                auto& flags = threadBitSet->blockFlags[tableID];
                for (const auto& offset : flags.range(l, r)) {
                    auto mark = threadBitSet->getAndReset(tableID, offset);
                    targetBitSet->markVisited(tableID, offset, mark);
                }
            }

            std::vector<nodeID_t> nextMission;
            for (auto i : targetBitSet->blockFlags[tableID].range(l, r)) {
                uint64_t now = targetBitSet->getAndReset(tableID, i), pos = 0;
                if (!now) {
                    continue;
                }
                globalBitSet->markVisited(tableID, i, now);

                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = getNodeOffset(i, pos);
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
        for (auto i = 0u; i < sharedData.numThreads; i++) {
            sharedData.threadBitSets[i]->resetFlag();
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

    common::table_id_map_t<RelTableCollectionScanner> scanners;
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