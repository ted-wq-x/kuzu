#include "function/table/sssp.h"

namespace kuzu {
namespace function {

struct CountUnionResult {
    uint64_t ans = 0;
    uint64_t nodeCount = 0;
    std::vector<Mission> tempFrontier;
};

class CountScanState {
public:
    explicit CountScanState(SharedDataWithValue& sharedData, nodeID_t nodeID, std::string direction)
        : sharedData(sharedData), direction(direction) {

        nodeNumbers = 1;

        Mission nextMission;
        nextMission.tableID = nodeID.tableID;
        nextMission.offsets.push_back(nodeID.offset);
        nextFrontier.emplace_back(std::move(nextMission));

        globalBitSet = std::make_shared<InternalIDCountBitSet>(sharedData.context->getCatalog(),
            sharedData.context->getStorageManager(), sharedData.context->getTx());
        globalBitSet->markVisited(nodeID, 1);

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
            auto tableID = data.tableID;
            if (!localScanners.contains(tableID)) {
                continue;
            }
            auto& currentScanner = localScanners.at(tableID);
            for (auto& offset : data.offsets) {
                internalID_t currentNodeID({offset, tableID});
                srcIdValueVector->setValue<nodeID_t>(0, currentNodeID);
                currentScanner.resetState();
                auto count = globalBitSet->getNodeValueCount(currentNodeID);
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
                        if (globalBitSet->isVisited(nbrID)) {
                            continue;
                        }
                        threadBitSet->markVisited(nbrID, count);
                    }
                }
            }
        }
    }

    CountUnionResult unionTask(uint32_t tid, const CountScanState& otherScanState) {
        uint64_t nodeCount = 0;
        uint64_t ans = 0;
        std::vector<Mission> tempFrontier;
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
            for (auto i = 1u; i < numThreads; ++i) {
                auto threadBitSet = threadBitSets[i];
                auto& flags = threadBitSet->blockFlags[tableID];
                for (const auto& offset : flags.range(l, r)) {
                    auto& value = threadBitSet->getNodeValue(tableID, offset);
                    targetBitSet->merge(tableID, offset, value);
                    value.reset();
                }
            }
            Mission nextMission;
            nextMission.tableID = tableID;
            for (auto i : targetBitSet->blockFlags[tableID].range(l, r)) {
                auto& value = targetBitSet->getNodeValue(tableID, i);
                uint64_t now = value.mark();
                if (!now) {
                    continue;
                }
                globalBitSet->merge(tableID, i, value);
                value.reset();
                uint64_t pos;
                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = getNodeOffset(i, pos);
                    auto nowNode = nodeID_t{offset, tableID};
                    auto otherSideCount = otherScanState.globalBitSet->getNodeValueCount(nowNode);
                    if (otherSideCount) {
                        ans += 1LL * globalBitSet->getNodeValueCount(nowNode) * otherSideCount;
                    }
                    if (!ans) {
                        if ((!nextMission.offsets.empty() &&
                                ((nextMission.offsets.back() ^ nowNode.offset) >>
                                    StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                            nodeCount += nextMission.offsets.size();
                            tempFrontier.emplace_back(std::move(nextMission));
                        }
                        nextMission.offsets.emplace_back(offset);
                    }

                    // 还原成 now & (now - 1) ,和result配合,统计now中1的个数 now=now & (now - 1)
                    now ^= pos;
                }
            }
            if (!ans && !nextMission.offsets.empty()) {
                nodeCount += nextMission.offsets.size();
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
        for (auto i = 0u; i < sharedData.numThreads; i++) {
            sharedData.threadBitSets[i]->resetFlag();
        }
        return pathCount;
    }

    SharedDataWithValue& sharedData;
    std::string direction;
    std::atomic_uint64_t taskID;
    uint64_t nodeNumbers;
    std::vector<Mission> nextFrontier; //  block划分的内部vector

    // 记录点的count数
    std::shared_ptr<InternalIDCountBitSet> globalBitSet;

    common::table_id_map_t<RelTableCollectionScanner> scanners;
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