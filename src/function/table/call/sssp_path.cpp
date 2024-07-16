#include "function/table/sssp.h"

namespace kuzu {
namespace function {

struct PathUnionResult {
    uint64_t nodeCount = 0;
    std::vector<nodeID_t> intersectionVector;
    std::vector<std::vector<nodeID_t>> tempFrontier;
};

class PathScanState {
public:
    explicit PathScanState(SharedDataWithoutValue& sharedData, nodeID_t nodeID,
        std::string direction)
        : sharedData(sharedData), sourceNode(nodeID), fwdDirection(direction) {
        bwdDirection = (direction == "in") ? "out" : ((direction == "out") ? "in" : "both");
        hop = 0;
        nodeNumbers = 1;

        std::vector<nodeID_t> nextMission;
        nextMission.emplace_back(nodeID);
        nextFrontier.emplace_back(std::move(nextMission));

        globalBitSet = std::make_shared<InternalIDDistBitSet>(sharedData.context->getCatalog(),
            sharedData.context->getStorageManager(), sharedData.context->getTx());
        globalBitSet->markVisited(nodeID, hop + 1);
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
                    if (globalBitSet->isVisited(nbrID)) {
                        continue;
                    }
                    threadBitSet->markVisited(nbrID);
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
                if (fwdDirection == "out" || fwdDirection == "both") {
                    if (tableID == info->srcTableID) {
                        scanTask(relDataReadState, srcVector, info, threadBitSet,
                            RelDataDirection::FWD, data, *rs.get(), relFilter);
                    }
                }

                if (fwdDirection == "in" || fwdDirection == "both") {
                    if (tableID == info->dstTableID) {
                        scanTask(relDataReadState, srcVector, info, threadBitSet,
                            RelDataDirection::BWD, data, *rs.get(), relFilter);
                    }
                }
            }
        }
    }

    PathUnionResult unionTask(uint32_t tid, const PathScanState& otherScanState) {
        uint64_t nodeCount = 0;
        std::vector<nodeID_t> intersectionVector;
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

                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = InternalIDBitSet::getNodeOffset(i, pos);
                    auto nowNode = nodeID_t{offset, tableID};
                    globalBitSet->markVisited(nowNode, hop + 1);
                    if (otherScanState.globalBitSet->isVisited(nowNode)) {
                        intersectionVector.emplace_back(nowNode);
                    }
                    if (intersectionVector.empty()) {
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
            if (intersectionVector.empty() && !nextMission.empty()) {
                nodeCount += nextMission.size();
                tempFrontier.emplace_back(std::move(nextMission));
            }
        }
        return {nodeCount, intersectionVector, tempFrontier};
    }

    bool isFrontierEmpty() const { return nextFrontier.empty(); }

    std::vector<nodeID_t> getNextFrontier(PathScanState& other,
        std::vector<nodeID_t>& intersectionVector) {
        nodeNumbers = taskID = 0;
        ++hop;
        std::vector<std::thread> threads;
        for (auto i = 0u; i < sharedData.numThreads; ++i) {
            threads.emplace_back([&, i] { funcTask(i); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        std::vector<std::future<PathUnionResult>> unionFuture;
        for (auto i = 0u; i < sharedData.numThreads; ++i) {
            unionFuture.emplace_back(std::async(std::launch::async, &PathScanState::unionTask, this,
                i, std::ref(other)));
        }
        currentFrontier = std::move(nextFrontier);
        for (auto& future : unionFuture) {
            auto [count, tempVector, tempFrontier] = future.get();
            intersectionVector.insert(intersectionVector.end(),
                std::make_move_iterator(tempVector.begin()),
                std::make_move_iterator(tempVector.end()));
            if (intersectionVector.empty()) {
                nextFrontier.insert(nextFrontier.end(),
                    std::make_move_iterator(tempFrontier.begin()),
                    std::make_move_iterator(tempFrontier.end()));
                nodeNumbers += count;
            }
        }
        return intersectionVector;
    }

    SharedDataWithoutValue& sharedData;
    nodeID_t sourceNode;
    std::string fwdDirection, bwdDirection;
    std::atomic_uint64_t taskID;
    uint32_t hop;
    uint64_t nodeNumbers;
    std::vector<std::vector<nodeID_t>> currentFrontier;
    std::vector<std::vector<nodeID_t>> nextFrontier; //  block划分的内部vector

    // 记录点的count数
    std::shared_ptr<InternalIDDistBitSet> globalBitSet;
};

class HalfPathState {
public:
    explicit HalfPathState(PathScanState& scanState, std::vector<nodeID_t>& intersectionVector,
        bool isParameter)
        : scanState(scanState) {
        taskID = 0;
        std::vector<std::vector<nodeID_t>> tempFrontier;
        std::vector<nodeID_t> nextMission;
        for (auto& nodeID : intersectionVector) {
            if ((!nextMission.empty() && ((nextMission.back().offset ^ nodeID.offset) >>
                                             StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                tempFrontier.emplace_back(std::move(nextMission));
            }
            nextMission.emplace_back(nodeID);
        }
        if (!nextMission.empty()) {
            tempFrontier.emplace_back(std::move(nextMission));
        }

        if (!isParameter || tempFrontier.size() < scanState.currentFrontier.size()) {
            nextFrontier = std::move(tempFrontier);
            for (auto& nodeID : intersectionVector) {
                halfPath.insert({nodeID, {{nodeID}}});
            }
            pathLength = 1;
        } else {
            nextFrontier = std::move(scanState.currentFrontier);
            initPath(intersectionVector);
            pathLength = 2;
        }
    }

    inline std::vector<std::vector<nodeID_t>> expandPath(
        std::vector<std::vector<nodeID_t>> resultPath, nodeID_t& nodeID) {
        for (auto& path : resultPath) {
            path.emplace_back(nodeID);
        }
        return resultPath;
    }

    inline void addVectors(std::map<nodeID_t, std::vector<std::vector<nodeID_t>>>& currentPath,
        nodeID_t nodeID, std::vector<std::vector<nodeID_t>> pathVectors) {
        auto it = currentPath.find(nodeID);
        if (it == currentPath.end()) {
            currentPath[nodeID] = std::move(pathVectors);
        } else {
            it->second.insert(it->second.end(), std::make_move_iterator(pathVectors.begin()),
                std::make_move_iterator(pathVectors.end()));
        }
    }

    inline void unionPath(std::map<nodeID_t, std::vector<std::vector<nodeID_t>>>& leftPath,
        std::map<nodeID_t, std::vector<std::vector<nodeID_t>>>& rightPath) {
        for (auto& [nodeID, pathVectors] : rightPath) {
            addVectors(leftPath, nodeID, pathVectors);
        }
    }

    inline void reconstructHalfPath() {
        std::map<nodeID_t, std::vector<std::vector<nodeID_t>>> tempHalfPath;
        for (auto& [nodeID, pathVector] : halfPath) {
            for (auto& path : pathVector) {
                tempHalfPath[path[0]].emplace_back(path);
            }
        }
        halfPath.clear();
        halfPath = std::move(tempHalfPath);
    }

    void pathScan(std::shared_ptr<storage::RelTableScanState> readState,
        common::ValueVector& srcVector, std::shared_ptr<RelTableInfo> info,
        std::shared_ptr<InternalIDBitSet> threadBitSet,
        std::map<nodeID_t, std::vector<std::vector<nodeID_t>>>& tempHalfPath,
        RelDataDirection relDataDirection, std::vector<internalID_t>& data,
        const processor::ResultSet& resultSet, evaluator::ExpressionEvaluator* relExpr) {
        auto relTable = info->relTable;
        readState->direction = relDataDirection;
        // 因为读取的都是相同nodeGroup里数据,故不需要每次都reset
        readState->dataScanState->resetState();
        auto dataChunkToSelect = resultSet.dataChunks[0];
        auto& selectVector = dataChunkToSelect->state->getSelVectorUnsafe();
        auto dstIdDataChunk = dataChunkToSelect->getValueVector(0);
        auto tx = scanState.sharedData.context->getTx();
        for (auto& currentNodeID : data) {
            srcVector.setValue<nodeID_t>(0, currentNodeID);
            relTable->initializeScanState(tx, *readState.get());
            auto dist = scanState.globalBitSet->getNodeValueDist(currentNodeID);
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
                    if (scanState.globalBitSet->getNodeValueDist(nbrID) + 1 == dist) {
                        threadBitSet->markVisited(nbrID);
                        auto expandVector = expandPath(halfPath[currentNodeID], nbrID);
                        addVectors(tempHalfPath, nbrID, expandVector);
                    }
                }
            }
        }
    }

    void pathScan(std::shared_ptr<storage::RelTableScanState> readState,
        common::ValueVector& srcVector, std::shared_ptr<RelTableInfo> info,
        std::shared_ptr<InternalIDBitSet> threadBitSet, InternalIDBitSet& intersectionBitSet,
        std::map<nodeID_t, std::vector<std::vector<nodeID_t>>>& tempHalfPath,
        RelDataDirection relDataDirection, std::vector<internalID_t>& data,
        const processor::ResultSet& resultSet, evaluator::ExpressionEvaluator* relExpr) {
        auto relTable = info->relTable;
        readState->direction = relDataDirection;
        // 因为读取的都是相同nodeGroup里数据,故不需要每次都reset
        readState->dataScanState->resetState();
        auto dataChunkToSelect = resultSet.dataChunks[0];
        auto& selectVector = dataChunkToSelect->state->getSelVectorUnsafe();
        auto dstIdDataChunk = dataChunkToSelect->getValueVector(0);
        auto tx = scanState.sharedData.context->getTx();
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
                    if (intersectionBitSet.isVisited(nbrID)) {
                        threadBitSet->markVisited(currentNodeID);
                        addVectors(tempHalfPath, currentNodeID, {{nbrID, currentNodeID}});
                    }
                }
            }
        }
    }

    std::map<nodeID_t, std::vector<std::vector<nodeID_t>>> backTask(
        InternalIDBitSet& intersectionBitSet, uint32_t threadId) {
        auto& sharedData = scanState.sharedData;
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
        auto direction = scanState.fwdDirection;
        auto size = nextFrontier.size();
        std::map<nodeID_t, std::vector<std::vector<nodeID_t>>> tempHalfPath;
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
                        pathScan(relDataReadState, srcVector, info, threadBitSet,
                            intersectionBitSet, tempHalfPath, RelDataDirection::FWD, data,
                            *rs.get(), relFilter);
                    }
                }
                if (direction == "in" || direction == "both") {
                    if (tableID == info->dstTableID) {
                        pathScan(relDataReadState, srcVector, info, threadBitSet,
                            intersectionBitSet, tempHalfPath, RelDataDirection::BWD, data,
                            *rs.get(), relFilter);
                    }
                }
            }
        }
        return tempHalfPath;
    }

    std::map<nodeID_t, std::vector<std::vector<nodeID_t>>> pathTask(uint32_t threadId) {
        auto& sharedData = scanState.sharedData;
        auto rs = scanState.sharedData.createResultSet();
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
        auto direction = scanState.bwdDirection;
        auto size = nextFrontier.size();
        std::map<nodeID_t, std::vector<std::vector<nodeID_t>>> tempHalfPath;
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
                        pathScan(relDataReadState, srcVector, info, threadBitSet, tempHalfPath,
                            RelDataDirection::FWD, data, *rs.get(), relFilter);
                    }
                }
                if (direction == "in" || direction == "both") {
                    if (tableID == info->dstTableID) {
                        pathScan(relDataReadState, srcVector, info, threadBitSet, tempHalfPath,
                            RelDataDirection::BWD, data, *rs.get(), relFilter);
                    }
                }
            }
        }
        return tempHalfPath;
    }

    std::vector<std::vector<nodeID_t>> unionTask(uint32_t tid) {
        std::vector<std::vector<nodeID_t>> tempFrontier;
        auto numThreads = scanState.sharedData.numThreads;
        for (auto tableID = 0u; tableID < scanState.globalBitSet->getTableNum(); ++tableID) {
            uint32_t tableSize = scanState.globalBitSet->getTableSize(tableID);
            if (!tableSize) {
                continue;
            }
            uint32_t l = tableSize * tid / numThreads, r = tableSize * (tid + 1) / numThreads;
            std::vector<uint64_t> tempMark;
            tempMark.reserve(r - l);
            tempMark.resize(r - l, 0);
            for (auto i = 0u; i < numThreads; ++i) {
                for (auto offset = l; offset < r; ++offset) {
                    auto mark = scanState.sharedData.threadBitSets[i]->getAndReset(tableID, offset);
                    tempMark[offset - l] |= mark;
                }
            }
            std::vector<nodeID_t> nextMission;
            for (auto i = l; i < r; ++i) {
                uint64_t now = tempMark[i - l], pos = 0;
                if (!now) {
                    continue;
                }

                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = InternalIDBitSet::getNodeOffset(i, pos);
                    auto nowNode = nodeID_t{offset, tableID};

                    if ((!nextMission.empty() && ((nextMission.back().offset ^ nowNode.offset) >>
                                                     StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                        tempFrontier.emplace_back(std::move(nextMission));
                    }
                    nextMission.emplace_back(nowNode);

                    // 还原成 now & (now - 1) ,和result配合,统计now中1的个数 now=now & (now - 1)
                    now ^= pos;
                }
            }
            if (!nextMission.empty()) {
                tempFrontier.emplace_back(std::move(nextMission));
            }
        }
        return tempFrontier;
    }

    void initPath(std::vector<nodeID_t>& intersectionVector) {
        InternalIDBitSet intersectionBitSet(scanState.sharedData.context->getCatalog(),
            scanState.sharedData.context->getStorageManager(),
            scanState.sharedData.context->getTx());
        for (auto& node : intersectionVector) {
            intersectionBitSet.markVisited(node);
        }
        std::vector<std::future<std::map<nodeID_t, std::vector<std::vector<nodeID_t>>>>> pathFuture;
        for (auto i = 0u; i < scanState.sharedData.numThreads; ++i) {
            pathFuture.emplace_back(std::async(std::launch::async, &HalfPathState::backTask, this,
                std::ref(intersectionBitSet), i));
        }
        std::vector<std::map<nodeID_t, std::vector<std::vector<nodeID_t>>>> threadHalfPath;
        for (auto& future : pathFuture) {
            auto tempHalfPath = future.get();
            threadHalfPath.emplace_back(std::move(tempHalfPath));
        }

        for (auto& tempHalfPath : threadHalfPath) {
            unionPath(halfPath, tempHalfPath);
        }

        std::vector<std::future<std::vector<std::vector<nodeID_t>>>> unionFuture;
        for (auto i = 0u; i < scanState.sharedData.numThreads; i++) {
            unionFuture.emplace_back(
                std::async(std::launch::async, &HalfPathState::unionTask, this, i));
        }
        nextFrontier.clear();
        for (auto& future : unionFuture) {
            auto tempFrontier = future.get();
            nextFrontier.insert(nextFrontier.end(), std::make_move_iterator(tempFrontier.begin()),
                std::make_move_iterator(tempFrontier.end()));
        }
    }

    void getHalfPath() {
        auto hop = (pathLength == 1) ? scanState.hop : scanState.hop - 1;
        while (hop--) {
            taskID = 0;
            std::vector<std::future<std::map<nodeID_t, std::vector<std::vector<nodeID_t>>>>>
                pathFuture;
            for (auto i = 0u; i < scanState.sharedData.numThreads; i++) {
                pathFuture.emplace_back(
                    std::async(std::launch::async, &HalfPathState::pathTask, this, i));
            }
            std::vector<std::map<nodeID_t, std::vector<std::vector<nodeID_t>>>> threadHalfPath;
            for (auto& future : pathFuture) {
                auto tempHalfPath = future.get();
                threadHalfPath.emplace_back(std::move(tempHalfPath));
            }
            halfPath.clear();
            for (auto& tempHalfPath : threadHalfPath) {
                unionPath(halfPath, tempHalfPath);
            }
            if (!hop) {
                break;
            }
            std::vector<std::future<std::vector<std::vector<nodeID_t>>>> unionFuture;
            for (auto i = 0u; i < scanState.sharedData.numThreads; ++i) {
                unionFuture.emplace_back(
                    std::async(std::launch::async, &HalfPathState::unionTask, this, i));
            }
            nextFrontier.clear();
            for (auto& future : unionFuture) {
                auto tempFrontier = future.get();
                nextFrontier.insert(nextFrontier.end(),
                    std::make_move_iterator(tempFrontier.begin()),
                    std::make_move_iterator(tempFrontier.end()));
            }
        }
        reconstructHalfPath();
    }

    PathScanState& scanState;
    uint32_t pathLength;
    std::atomic_uint32_t taskID;
    std::vector<std::vector<nodeID_t>> nextFrontier;
    std::map<nodeID_t, std::vector<std::vector<nodeID_t>>> halfPath;
};

inline void concatPath(std::vector<std::vector<nodeID_t>>& left,
    std::vector<std::vector<nodeID_t>>& right, std::vector<std::string>& resultVector) {
    for (auto& leftPath : left) {
        reverse(leftPath.begin(), leftPath.end());
        leftPath.pop_back();
        for (auto& rightPath : right) {
            std::string result;
            for (auto& nodeID : leftPath) {
                result +=
                    std::to_string(nodeID.tableID) + ":" + std::to_string(nodeID.offset) + ',';
            }
            for (auto& nodeID : rightPath) {
                result +=
                    std::to_string(nodeID.tableID) + ":" + std::to_string(nodeID.offset) + ',';
            }
            result.pop_back();
            resultVector.emplace_back(result);
        }
    }
}

void getPath(HalfPathState& leftPath, HalfPathState& rightPath,
    std::vector<nodeID_t>& intersectionVector, std::vector<std::string>& resultVector) {
    leftPath.getHalfPath();
    rightPath.getHalfPath();
    for (auto& nodeID : intersectionVector) {
        concatPath(leftPath.halfPath[nodeID], rightPath.halfPath[nodeID], resultVector);
    }
}

offset_t pathFunc(TableFuncInput& input, TableFuncOutput& output) {
    auto sharedState = input.sharedState->ptrCast<CallFuncSharedState>();
    auto morsel = sharedState->getMorsel();
    if (!morsel.hasMoreToOutput()) {
        return 0;
    }
    auto localState = input.localState->ptrCast<SsspLocalState>();
    auto bindData = input.bindData->constPtrCast<SsspBindData>();
    auto& dataChunk = output.dataChunk;
    auto resultType = bindData->resultType;
    if (!localState->resultVector.empty()) {
        auto numTablesToOutput = morsel.endOffset - morsel.startOffset;
        for (auto i = 0u; i < numTablesToOutput; ++i) {
            auto result = localState->resultVector[morsel.startOffset + i];
            if (resultType == "path") {
                dataChunk.getValueVector(0)->setValue(i, result);
            } else {
                dataChunk.getValueVector(0)->setValue(i, localState->length);
                dataChunk.getValueVector(1)->setValue(i, result);
            }
        }
        return numTablesToOutput;
    }
    auto numThreads = bindData->numThreads;
    auto direction = bindData->direction;
    auto maxHop = bindData->maxHop;
    SharedDataWithoutValue sharedData(bindData, numThreads);
    auto srcNodeID = getNodeID(sharedData.context, bindData->srcTableName, bindData->srcPrimaryKey);
    auto dstNodeID = getNodeID(sharedData.context, bindData->dstTableName, bindData->dstPrimaryKey);
    auto srcDirection = direction;
    auto dstDirection = (direction == "in") ? "out" : ((direction == "out") ? "in" : "both");
    PathScanState srcScanState(sharedData, srcNodeID, srcDirection);
    PathScanState dstScanState(sharedData, dstNodeID, dstDirection);
    int64_t length = -1;
    std::vector<nodeID_t> intersectionVector;
    if (srcNodeID == dstNodeID) {
        length = 0;
        intersectionVector.emplace_back(srcNodeID);
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
                    srcScanState.getNextFrontier(dstScanState, intersectionVector);
                } else {
                    dstScanState.getNextFrontier(srcScanState, intersectionVector);
                }
            } else if (!srcIsEmpty) {
                srcScanState.getNextFrontier(dstScanState, intersectionVector);
            } else {
                dstScanState.getNextFrontier(srcScanState, intersectionVector);
            }
            if (!intersectionVector.empty()) {
                length = hop;
                break;
            }
        }
    }
    HalfPathState leftPath(srcScanState, intersectionVector, bindData->isParameter);
    HalfPathState rightPath(dstScanState, intersectionVector, bindData->isParameter);
    std::vector<std::string> resultVector;
    getPath(leftPath, rightPath, intersectionVector, resultVector);
    sharedState->curOffset = 0;
    sharedState->maxOffset = resultVector.size();
    localState->length = length;
    localState->resultVector = std::move(resultVector);
    morsel = sharedState->getMorsel();
    auto numTablesToOutput = morsel.endOffset - morsel.startOffset;
    for (auto i = 0u; i < numTablesToOutput; ++i) {
        auto result = localState->resultVector[morsel.startOffset + i];
        if (resultType == "path") {
            dataChunk.getValueVector(0)->setValue(i, result);
        } else {
            dataChunk.getValueVector(0)->setValue(i, localState->length);
            dataChunk.getValueVector(1)->setValue(i, result);
        }
    }
    return numTablesToOutput;
}

} // namespace function
} // namespace kuzu