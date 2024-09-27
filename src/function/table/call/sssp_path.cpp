#include "catalog/catalog_entry/node_table_catalog_entry.h"
#include "function/table/sssp.h"
#include "processor/operator/scan/scan_node_table.h"
namespace kuzu {
namespace function {

/**
 * 回溯路径时点的表示方式
 */
struct BackTrackPathNode {
    internalID_t id;
    std::string rk;
};

class SsspNodeTableScanState {
private:
    std::shared_ptr<std::unordered_map<common::table_id_t, processor::ScanNodeTableInfo>>
        nodeTableInfo;
    std::shared_ptr<std::unordered_map<common::table_id_t, common::LogicalTypeID>>
        outputVectorDataType;
    // running data
    std::shared_ptr<common::ValueVector> inputVector;
    std::shared_ptr<std::unordered_map<common::table_id_t, std::shared_ptr<common::ValueVector>>>
        outputVector;
    transaction::Transaction* transaction;
    storage::MemoryManager* memoryManager;

public:
    explicit SsspNodeTableScanState(const SsspBindData* bindData) {
        transaction = bindData->context->getTx();
        memoryManager = bindData->context->getMemoryManager();
        initScanNodeTableInfo(bindData);
    }

    explicit SsspNodeTableScanState(
        std::shared_ptr<std::unordered_map<common::table_id_t, processor::ScanNodeTableInfo>>
            nodeTableInfo,
        transaction::Transaction* transaction, std::shared_ptr<common::ValueVector> inputVector,
        std::shared_ptr<
            std::unordered_map<common::table_id_t, std::shared_ptr<common::ValueVector>>>
            outputVector,
        storage::MemoryManager* memoryManager)
        : nodeTableInfo(nodeTableInfo), inputVector(inputVector), outputVector(outputVector),
          transaction(transaction), memoryManager(memoryManager) {}

    /**
     * 每个线程都得初始化自己的
     */
    SsspNodeTableScanState initLocalState() {
        auto ans = std::make_shared<
            std::unordered_map<common::table_id_t, processor::ScanNodeTableInfo>>();
        auto newInputVector =
            std::make_shared<common::ValueVector>(common::LogicalType::INTERNAL_ID(), nullptr);
        newInputVector->state = DataChunkState::getSingleValueDataChunkState();

        auto newOutputVectors = std::make_shared<
            std::unordered_map<common::table_id_t, std::shared_ptr<common::ValueVector>>>();

        for (const auto& pair : *nodeTableInfo.get()) {
            auto nodeInfo = pair.second.copy();
            std::vector<Column*> columns;
            columns.reserve(nodeInfo.columnIDs.size());
            for (const auto columnID : nodeInfo.columnIDs) {
                if (columnID == INVALID_COLUMN_ID) {
                    columns.push_back(nullptr);
                } else {
                    columns.push_back(&nodeInfo.table->getColumn(columnID));
                }
            }
            nodeInfo.localScanState = std::make_unique<storage::NodeTableScanState>(
                nodeInfo.table->getTableID(), nodeInfo.columnIDs, columns);
            nodeInfo.localScanState->nodeIDVector = newInputVector.get();
            nodeInfo.localScanState->rowIdxVector->state = newInputVector->state;
            auto newOutputVector = std::make_shared<common::ValueVector>(
                outputVectorDataType->at(pair.first), memoryManager);
            newOutputVector->state = DataChunkState::getSingleValueDataChunkState();
            newOutputVectors->emplace(pair.first, newOutputVector);
            nodeInfo.localScanState->outputVectors.push_back(newOutputVector.get());
            ans->emplace(pair.first, std::move(nodeInfo));
        }

        return SsspNodeTableScanState(ans, this->transaction, newInputVector, newOutputVectors,
            this->memoryManager);
    }

    std::string lookUp(internalID_t nodeID) {
        auto& nodeInfo = nodeTableInfo->at(nodeID.tableID);
        inputVector->setValue<nodeID_t>(0, nodeID);
        nodeInfo.localScanState->source = storage::TableScanSource::COMMITTED;
        nodeInfo.localScanState->nodeGroupIdx =
            storage::StorageUtils::getNodeGroupIdx(nodeID.offset);
        nodeInfo.table->initScanState(transaction, *nodeInfo.localScanState);
        nodeInfo.table->lookup(transaction, *nodeInfo.localScanState);
        return outputVector->at(nodeID.tableID)->getAsValue(0)->toString();
    };

private:
    void initScanNodeTableInfo(const SsspBindData* bindData) {
        auto context = bindData->context;
        auto catalog = context->getCatalog();
        auto storageManager = context->getStorageManager();
        nodeTableInfo = std::make_shared<
            std::unordered_map<common::table_id_t, processor::ScanNodeTableInfo>>();
        outputVectorDataType =
            std::make_shared<std::unordered_map<common::table_id_t, common::LogicalTypeID>>();
        for (auto& nodeTableEntry : catalog->getNodeTableEntries(context->getTx())) {
            auto tableID = nodeTableEntry->getTableID();
            auto table = storageManager->getTable(tableID)->ptrCast<storage::NodeTable>();
            std::vector<column_id_t> columnIDs;
            columnIDs.push_back(table->getPKColumnID());
            processor::ScanNodeTableInfo info{table, std::move(columnIDs)};
            nodeTableInfo->insert(std::make_pair(tableID, std::move(info)));
            outputVectorDataType->insert(std::make_pair(tableID,
                nodeTableEntry->getPrimaryKeyDefinition().getType().getLogicalTypeID()));
        }
    }
};

struct PathUnionResult {
    uint64_t nodeCount = 0;
    std::vector<nodeID_t> intersectionVector;
    std::vector<Mission> tempFrontier;
};

class PathScanState {
public:
    explicit PathScanState(SharedDataWithoutValue& sharedData, nodeID_t nodeID,
        std::string direction)
        : sharedData(sharedData), fwdDirection(direction) {
        bwdDirection = (direction == "in") ? "out" : ((direction == "out") ? "in" : "both");
        hop = 0;
        nodeNumbers = 1;

        Mission nextMission;
        nextMission.tableID = nodeID.tableID;
        nextMission.offsets.push_back(nodeID.offset);
        nextFrontier.emplace_back(std::move(nextMission));

        globalBitSet = std::make_shared<InternalIDDistBitSet>(sharedData.context->getCatalog(),
            sharedData.context->getStorageManager(), sharedData.context->getTx());
        globalBitSet->markVisited(nodeID, hop + 1);

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

        auto threadBitSet = sharedData.getThreadBitSet(threadId);
        auto size = nextFrontier.size();

        auto srcIdValueVector = rs->dataChunks[0]->getValueVector(0);
        auto dstIdValueVector = rs->dataChunks[0]->getValueVector(1);
        auto& selectVector = dstIdValueVector->state->getSelVectorUnsafe();
        auto tx = sharedData.context->getTx();

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
                internalID_t currentNodeID{offset, data.tableID};
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
                        if (globalBitSet->isVisited(nbrID)) {
                            continue;
                        }
                        threadBitSet->markVisited(nbrID);
                    }
                }
            }
        }
    }

    PathUnionResult unionTask2(uint32_t tid, const PathScanState& otherScanState) {
        uint64_t nodeCount = 0;
        std::vector<nodeID_t> intersectionVector;
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
                    auto mark = threadBitSet->getAndReset(tableID, offset);
                    targetBitSet->markVisited(tableID, offset, mark);
                }
            }
            Mission nextMission;
            nextMission.tableID = tableID;
            for (auto i : targetBitSet->blockFlags[tableID].range(l, r)) {
                uint64_t now = targetBitSet->getAndReset(tableID, i), pos = 0;
                if (!now) {
                    continue;
                }
                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = getNodeOffset(i, pos);
                    auto nowNode = nodeID_t{offset, tableID};
                    globalBitSet->markVisited(nowNode, hop + 1);
                    if (otherScanState.globalBitSet->isVisited(nowNode)) {
                        intersectionVector.emplace_back(nowNode);
                    }
                    if (intersectionVector.empty()) {
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
            if (intersectionVector.empty() && !nextMission.offsets.empty()) {
                nodeCount += nextMission.offsets.size();
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
            unionFuture.emplace_back(std::async(std::launch::async, &PathScanState::unionTask2,
                this, i, std::ref(other)));
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
        for (auto i = 0u; i < sharedData.numThreads; i++) {
            sharedData.threadBitSets[i]->resetFlag();
        }

        return intersectionVector;
    }

    SharedDataWithoutValue& sharedData;
    std::string fwdDirection, bwdDirection;
    std::atomic_uint64_t taskID;
    uint32_t hop;
    uint64_t nodeNumbers;
    std::vector<Mission> currentFrontier;
    std::vector<Mission> nextFrontier; //  block划分的内部vector

    // 记录点的count数
    std::shared_ptr<InternalIDDistBitSet> globalBitSet;

    common::table_id_map_t<RelTableCollectionScanner> scanners;
};

class HalfPathState {
public:
    explicit HalfPathState(PathScanState& scanState, std::vector<nodeID_t>& intersectionVector,
        bool backTrackUsingFB, SsspNodeTableScanState& nodeTableScanState, int length)
        : scanState(scanState), nodeTableScanState(nodeTableScanState) {
        taskID = 0;
        std::vector<Mission> tempFrontier;
        Mission nextMission;
        for (auto& nodeID : intersectionVector) {
            if ((!nextMission.empty() && ((nextMission.back() ^ nodeID.offset) >>
                                             StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                tempFrontier.emplace_back(std::move(nextMission));
            }
            nextMission.tableID = nodeID.tableID;
            nextMission.emplace_back(nodeID.offset);
        }
        if (!nextMission.empty()) {
            tempFrontier.emplace_back(std::move(nextMission));
        }
        // 对于入a->b->c->d 在d相遇
        // 该姿势是从d->c , c->b ,b -> a
        if (length == 1 || !backTrackUsingFB ||
            tempFrontier.size() < scanState.currentFrontier.size()) {
            nextFrontier = std::move(tempFrontier);
            auto localNodeTableScanState = nodeTableScanState.initLocalState();
            for (auto& nodeID : intersectionVector) {
                auto rk = localNodeTableScanState.lookUp(nodeID);
                halfPath.insert({nodeID, {{{nodeID, rk}}}});
            }
            pathLength = 1;
        } else {
            // 该姿势是先回溯 c->d, 然后c->b ,b -> a
            // 大多数场景下,是越扩越多,所以这种算法性能更好
            nextFrontier = std::move(scanState.currentFrontier);
            initPath(intersectionVector);
            pathLength = 2;
        }
    }

    inline std::vector<std::vector<BackTrackPathNode>> expandPath(
        std::vector<std::vector<BackTrackPathNode>> resultPath, std::string nodeRk,
        internalID_t id) {
        for (auto& path : resultPath) {
            path.push_back({id, nodeRk});
        }
        return resultPath;
    }

    inline void addVectors(
        std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>>& currentPath,
        nodeID_t nodeID, std::vector<std::vector<BackTrackPathNode>> pathVectors) {
        auto it = currentPath.find(nodeID);
        if (it == currentPath.end()) {
            currentPath[nodeID] = std::move(pathVectors);
        } else {
            it->second.insert(it->second.end(), std::make_move_iterator(pathVectors.begin()),
                std::make_move_iterator(pathVectors.end()));
        }
    }

    inline void unionPath(std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>>& leftPath,
        std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>>& rightPath) {
        for (auto& [nodeID, pathVectors] : rightPath) {
            addVectors(leftPath, nodeID, pathVectors);
        }
    }

    // 回溯的过程是相反的方向,在合并两个方向的路径时需要把交点放到halfPath
    inline void reconstructHalfPath() {
        std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>> tempHalfPath;
        for (auto& [nodeID, pathVector] : halfPath) {
            for (auto& path : pathVector) {
                tempHalfPath[path[0].id].emplace_back(path);
            }
        }
        halfPath.clear();
        halfPath = std::move(tempHalfPath);
    }

    std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>> backTask(
        InternalIDBitSet& intersectionBitSet, uint32_t threadId,
        common::table_id_map_t<RelTableCollectionScanner>& scanners) {
        auto& sharedData = scanState.sharedData;
        auto rs = sharedData.createResultSet();

        auto localScanners = copyScanners(scanners);
        initRelTableCollectionScanner(*sharedData.context, localScanners, rs.get());

        auto relEvaluate = sharedData.initEvaluator(*rs.get());
        evaluator::ExpressionEvaluator* relFilter = nullptr;
        if (relEvaluate) {
            relFilter = relEvaluate.get();
        }
        auto threadBitSet = sharedData.getThreadBitSet(threadId);
        auto size = nextFrontier.size();

        auto srcIdValueVector = rs->dataChunks[0]->getValueVector(0);
        auto dstIdValueVector = rs->dataChunks[0]->getValueVector(1);
        auto& selectVector = dstIdValueVector->state->getSelVectorUnsafe();
        auto tx = sharedData.context->getTx();

        std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>> tempHalfPath;
        auto localNodeTableScanState = nodeTableScanState.initLocalState();
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
                auto currentNodeRk = localNodeTableScanState.lookUp(currentNodeID);
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
                        if (intersectionBitSet.isVisited(nbrID)) {
                            threadBitSet->markVisited(currentNodeID);
                            auto nbrRk = localNodeTableScanState.lookUp(nbrID);
                            addVectors(tempHalfPath, currentNodeID,
                                {{{nbrID, nbrRk}, {currentNodeID, currentNodeRk}}});
                        }
                    }
                }
            }
        }
        return tempHalfPath;
    }

    void initPath(std::vector<nodeID_t>& intersectionVector) {
        InternalIDBitSet intersectionBitSet(scanState.sharedData.context->getCatalog(),
            scanState.sharedData.context->getStorageManager(),
            scanState.sharedData.context->getTx());
        for (auto& node : intersectionVector) {
            intersectionBitSet.markVisited(node);
        }
        std::vector<std::future<std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>>>>
            pathFuture;
        common::ExtendDirection extendDirection = getExtendDirection(scanState.fwdDirection);
        std::shared_ptr<Expression> filterExpr = nullptr;
        if (scanState.sharedData.relFilter) {
            filterExpr = scanState.sharedData.relFilter->getExpression();
        }
        auto scanners = createRelTableCollectionScanner(*scanState.sharedData.context,
            extendDirection, filterExpr);
        for (auto i = 0u; i < scanState.sharedData.numThreads; ++i) {
            pathFuture.emplace_back(std::async(std::launch::async, &HalfPathState::backTask, this,
                std::ref(intersectionBitSet), i, std::ref(scanners)));
        }
        std::vector<std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>>> threadHalfPath;
        for (auto& future : pathFuture) {
            auto tempHalfPath = future.get();
            threadHalfPath.emplace_back(std::move(tempHalfPath));
        }

        for (auto& tempHalfPath : threadHalfPath) {
            unionPath(halfPath, tempHalfPath);
        }

        std::vector<std::future<std::vector<Mission>>> unionFuture;
        for (auto i = 0u; i < scanState.sharedData.numThreads; i++) {
            unionFuture.emplace_back(
                std::async(std::launch::async, &HalfPathState::unionTask1, this, i));
        }
        nextFrontier.clear();
        for (auto& future : unionFuture) {
            auto tempFrontier = future.get();
            nextFrontier.insert(nextFrontier.end(), std::make_move_iterator(tempFrontier.begin()),
                std::make_move_iterator(tempFrontier.end()));
        }
        for (auto i = 0u; i < scanState.sharedData.numThreads; i++) {
            scanState.sharedData.threadBitSets[i]->resetFlag();
        }
    }

    std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>> pathTask(uint32_t threadId,
        common::table_id_map_t<RelTableCollectionScanner>& scanners) {
        auto& sharedData = scanState.sharedData;
        auto rs = scanState.sharedData.createResultSet();

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
        auto direction = scanState.bwdDirection;
        auto size = nextFrontier.size();
        auto locaNodeTableScanState = nodeTableScanState.initLocalState();
        std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>> tempHalfPath;
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
            // 每次调用是个定值
            auto target_dist =
                scanState.globalBitSet->getNodeValueDist({data.offsets[0], data.tableID}) - 1;

            for (auto& offset : data.offsets) {
                internalID_t currentNodeID({offset, tableID});
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
                        if (scanState.globalBitSet->getNodeValueDist(nbrID) == target_dist) {
                            threadBitSet->markVisited(nbrID);
                            auto nbrRk = locaNodeTableScanState.lookUp(nbrID);
                            auto expandVector = expandPath(halfPath[currentNodeID], nbrRk, nbrID);
                            addVectors(tempHalfPath, nbrID, expandVector);
                        }
                    }
                }
            }
        }
        return tempHalfPath;
    }

    std::vector<Mission> unionTask1(uint32_t tid) {
        std::vector<Mission> tempFrontier;
        auto numThreads = scanState.sharedData.numThreads;
        auto& threadBitSets = scanState.sharedData.threadBitSets;
        for (auto tableID = 0u; tableID < scanState.globalBitSet->getTableNum(); ++tableID) {
            uint32_t tableSize = scanState.globalBitSet->getTableSize(tableID);
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

                    auto mark = threadBitSets[i]->getAndReset(tableID, offset);
                    targetBitSet->markVisited(tableID, offset, mark);
                }
            }
            Mission nextMission;
            nextMission.tableID = tableID;
            for (auto i : targetBitSet->blockFlags[tableID].range(l, r)) {

                uint64_t now = targetBitSet->getAndReset(tableID, i), pos = 0;
                if (!now) {
                    continue;
                }

                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = getNodeOffset(i, pos);

                    if ((!nextMission.empty() && ((nextMission.back() ^ offset) >>
                                                     StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                        tempFrontier.emplace_back(std::move(nextMission));
                    }
                    nextMission.emplace_back(offset);

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

    void getHalfPath() {
        common::ExtendDirection extendDirection = getExtendDirection(scanState.bwdDirection);
        std::shared_ptr<Expression> filterExpr = nullptr;
        if (scanState.sharedData.relFilter) {
            filterExpr = scanState.sharedData.relFilter->getExpression();
        }
        auto scanners = createRelTableCollectionScanner(*scanState.sharedData.context,
            extendDirection, filterExpr);

        int32_t hop = (pathLength == 1) ? scanState.hop : scanState.hop - 1;
        while (hop-- > 0) {
            taskID = 0;
            std::vector<
                std::future<std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>>>>
                pathFuture;
            // 回溯路径
            for (auto i = 0u; i < scanState.sharedData.numThreads; i++) {
                pathFuture.emplace_back(std::async(std::launch::async, &HalfPathState::pathTask,
                    this, i, std::ref(scanners)));
            }
            std::vector<std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>>>
                threadHalfPath;
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
            std::vector<std::future<std::vector<Mission>>> unionFuture;
            for (auto i = 0u; i < scanState.sharedData.numThreads; ++i) {
                unionFuture.emplace_back(
                    std::async(std::launch::async, &HalfPathState::unionTask1, this, i));
            }
            nextFrontier.clear();
            for (auto& future : unionFuture) {
                auto tempFrontier = future.get();
                nextFrontier.insert(nextFrontier.end(),
                    std::make_move_iterator(tempFrontier.begin()),
                    std::make_move_iterator(tempFrontier.end()));
            }
            for (auto i = 0u; i < scanState.sharedData.numThreads; i++) {
                scanState.sharedData.threadBitSets[i]->resetFlag();
            }
        }
        reconstructHalfPath();
    }

    PathScanState& scanState;
    uint32_t pathLength;
    std::atomic_uint32_t taskID;
    SsspNodeTableScanState nodeTableScanState;
    std::vector<Mission> nextFrontier;
    std::map<nodeID_t, std::vector<std::vector<BackTrackPathNode>>> halfPath;
};

inline void concatPath(std::vector<std::vector<BackTrackPathNode>>& left,
    std::vector<std::vector<BackTrackPathNode>>& right, std::vector<std::string>& resultVector) {
    for (auto& leftPath : left) {
        reverse(leftPath.begin(), leftPath.end());
        leftPath.pop_back();
        for (auto& rightPath : right) {
            std::string result;
            for (auto& nodeID : leftPath) {
                result += nodeID.rk + ',';
            }
            for (auto& nodeID : rightPath) {
                result += nodeID.rk + ',';
            }
            result.pop_back();
            resultVector.emplace_back(result);
        }
    }
}

inline std::vector<std::string> getPath(PathScanState& srcScanState, PathScanState& dstScanState,
    std::vector<nodeID_t>& intersectionVector, const SsspBindData* bindData, int length) {

    auto nodeTableInfo = SsspNodeTableScanState(bindData);
    HalfPathState leftPath(srcScanState, intersectionVector, bindData->backTrackUsingFB,
        nodeTableInfo, length);
    HalfPathState rightPath(dstScanState, intersectionVector, bindData->backTrackUsingFB,
        nodeTableInfo, length);

    leftPath.getHalfPath();
    rightPath.getHalfPath();
    std::vector<std::string> resultVector;
    for (auto& nodeID : intersectionVector) {
        concatPath(leftPath.halfPath[nodeID], rightPath.halfPath[nodeID], resultVector);
    }
    return resultVector;
}

inline uint64_t fillResVector(const CallFuncMorsel& morsel, const std::string& resultType,
    DataChunk& dataChunk, const SsspLocalState& localState) {
    auto numTablesToOutput = morsel.endOffset - morsel.startOffset;
    for (auto i = 0u; i < numTablesToOutput; ++i) {
        auto result = localState.resultVector[morsel.startOffset + i];
        if (resultType == "path") {
            dataChunk.getValueVector(0)->setValue(i, result);
        } else {
            dataChunk.getValueVector(0)->setValue(i, localState.length);
            dataChunk.getValueVector(1)->setValue(i, result);
        }
    }
    return numTablesToOutput;
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
        return fillResVector(morsel, resultType, dataChunk, *localState);
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

    if (length > 0) {
        std::sort(intersectionVector.begin(), intersectionVector.end(),
            [](const internalID_t& entry1, const internalID_t& entry2) -> bool {
                if (entry1.tableID == entry2.tableID) {
                    return entry1.offset < entry2.offset;
                } else {
                    return entry1.tableID < entry2.tableID;
                }
            });
        // 构建结果路径
        auto resultVector =
            getPath(srcScanState, dstScanState, intersectionVector, bindData, length);
        // 重装结果数量
        sharedState->curOffset = 0;
        sharedState->maxOffset = resultVector.size();
        // 保存数据到localState
        localState->length = length;
        localState->resultVector = std::move(resultVector);
    } else {
        sharedState->curOffset = 0;
        sharedState->maxOffset = 0;
        localState->length = length;
    }

    return fillResVector(sharedState->getMorsel(), resultType, dataChunk, *localState);
}

} // namespace function
} // namespace kuzu