#include <future>

#include "function/table/basic.h"

namespace kuzu {
namespace function {

struct KhopBindData : public CallTableFuncBindData {
    KhopBindData(main::ClientContext* context, std::string primaryKey, std::string tableName,
        std::string direction, int64_t maxHop, int64_t parameter, int64_t numThreads,
        std::vector<LogicalType> returnTypes, std::vector<std::string> returnColumnNames,
        offset_t maxOffset, std::unique_ptr<evaluator::ExpressionEvaluator> relFilter,
        std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds,
        std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
            relTableInfos)
        : CallTableFuncBindData{std::move(returnTypes), std::move(returnColumnNames), maxOffset},
          context(context), primaryKey(primaryKey), tableName(tableName), direction(direction),
          maxHop(maxHop), parameter(parameter), numThreads(numThreads),
          relFilter(std::move(relFilter)), relColumnTypeIds(std::move(relColumnTypeIds)),
          relTableInfos(std::move(relTableInfos)) {}

    std::unique_ptr<TableFuncBindData> copy() const override {
        std::unique_ptr<evaluator::ExpressionEvaluator> localRelFilter = nullptr;
        if (relFilter) {
            localRelFilter = relFilter->clone();
        }

        return std::make_unique<KhopBindData>(context, primaryKey, tableName, direction, maxHop,
            parameter, numThreads, common::LogicalType::copy(columnTypes), columnNames, maxOffset,
            std::move(localRelFilter), relColumnTypeIds, relTableInfos);
    }
    main::ClientContext* context;
    std::string primaryKey, tableName, direction;
    int64_t maxHop, parameter, numThreads;

    // nullable
    std::unique_ptr<evaluator::ExpressionEvaluator> relFilter;
    // 边的输出列类型
    std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds;
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
        relTableInfos;
};

class KhopSharedData {
public:
    explicit KhopSharedData(const KhopBindData* bindData) {
        context = bindData->context;
        relFilter = bindData->relFilter.get();
        relColumnTypeIds = bindData->relColumnTypeIds;
        direction = bindData->direction;
        reltables = bindData->relTableInfos;
        globalBitSet = std::make_shared<InternalIDBitSet>(context->getCatalog(),
            context->getStorageManager(), context->getTx());
        threadBitSets = std::make_unique<std::shared_ptr<InternalIDBitSet>[]>(bindData->numThreads);
    }

    std::unique_ptr<processor::ResultSet> createResultSet() {
        processor::ResultSet resultSet(1);
        auto columnType = *relColumnTypeIds.get();
        auto numValueVectors = columnType.size();
        auto dataChunk = std::make_shared<common::DataChunk>(numValueVectors);
        for (auto j = 0u; j < numValueVectors; ++j) {
            dataChunk->insert(j,
                std::make_shared<ValueVector>(columnType[j], context->getMemoryManager()));
        }
        resultSet.insert(0, dataChunk);
        return std::make_unique<processor::ResultSet>(resultSet);
    }

    std::unique_ptr<evaluator::ExpressionEvaluator> initEvaluator(
        const processor::ResultSet& resultSet) {
        if (relFilter) {
            auto evaluator = relFilter->clone();
            evaluator->init(resultSet, context);
            return evaluator;
        } else {
            return nullptr;
        }
    }

    std::shared_ptr<InternalIDBitSet> getThreadBitSet(uint32_t threadId) {
        auto threadBitSet = threadBitSets[threadId];
        if (threadBitSet == nullptr) {
            auto localBitSet = std::make_shared<InternalIDBitSet>(context->getCatalog(),
                context->getStorageManager(), context->getTx());
            threadBitSets[threadId] = localBitSet;
            return localBitSet;
        } else {
            return threadBitSet;
        }
    }

    std::atomic_int32_t taskID;
    std::string direction;
    main::ClientContext* context;
    std::shared_ptr<InternalIDBitSet> globalBitSet;
    std::unique_ptr<std::shared_ptr<InternalIDBitSet>[]> threadBitSets;
    // 内部的vector,是按照block大小划分的,都是相同的tableid
    std::vector<std::vector<nodeID_t>> nextFrontier;
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
        reltables;
    evaluator::ExpressionEvaluator* relFilter;
    std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds;
};

static uint64_t funcTask(KhopSharedData& sharedData, uint32_t threadId,
    common::table_id_map_t<RelTableCollectionScanner>& scanners) {
    auto threadBitSet = sharedData.getThreadBitSet(threadId);

    auto rs = sharedData.createResultSet();
    auto localScanners = copyScanners(scanners);
    initRelTableCollectionScanner(*sharedData.context, localScanners, rs.get());

    auto relEvaluate = sharedData.initEvaluator(*rs.get());
    evaluator::ExpressionEvaluator* relFilter = nullptr;
    if (relEvaluate) {
        relFilter = relEvaluate.get();
    }

    auto srcIdValueVector = rs->dataChunks[0]->getValueVector(0);
    auto dstIdValueVector = rs->dataChunks[0]->getValueVector(2);
    auto& selectVector = dstIdValueVector->state->getSelVectorUnsafe();
    auto tx = sharedData.context->getTx();
    auto& globalBitSet = *sharedData.globalBitSet;
    while (true) {
        uint32_t tid = sharedData.taskID.fetch_add(1, std::memory_order_relaxed);
        if (tid >= sharedData.nextFrontier.size()) {
            break;
        }
        auto& data = sharedData.nextFrontier[tid];
        auto tableID = data[0].tableID;
        if (!localScanners.contains(tableID)) {
            continue;
        }
        auto& currentScanner = localScanners.at(tableID);
        for (auto& currentNodeID : data) {
            srcIdValueVector->setValue<nodeID_t>(0, currentNodeID);
            currentScanner.resetState();
            while (currentScanner.scan(selectVector, tx)) {
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
                    threadBitSet->markIfUnVisitedReturnVisited(globalBitSet, nbrData[i]);
                }
            }
        }
    }
    return 0;
}

static uint64_t funcRCTask(KhopSharedData& sharedData, uint32_t threadId, bool isFinal,
    common::table_id_map_t<RelTableCollectionScanner>& scanners) {
    auto threadBitSet = sharedData.getThreadBitSet(threadId);

    auto rs = sharedData.createResultSet();
    auto localScanners = copyScanners(scanners);
    initRelTableCollectionScanner(*sharedData.context, localScanners, rs.get());

    evaluator::ExpressionEvaluator* relFilter = nullptr;
    auto relEvaluate = sharedData.initEvaluator(*rs.get());
    if (relEvaluate) {
        relFilter = relEvaluate.get();
    }
    uint64_t edgeCount = 0;

    auto srcIdValueVector = rs->dataChunks[0]->getValueVector(0);
    auto dstIdValueVector = rs->dataChunks[0]->getValueVector(2);
    auto& selectVector = dstIdValueVector->state->getSelVectorUnsafe();
    auto tx = sharedData.context->getTx();

    while (true) {
        uint32_t tid = sharedData.taskID.fetch_add(1, std::memory_order_relaxed);
        if (tid >= sharedData.nextFrontier.size()) {
            break;
        }
        auto& data = sharedData.nextFrontier[tid];
        auto tableID = data[0].tableID;

        auto& currentScanner = localScanners.at(tableID);
        auto& globalBitSet = *sharedData.globalBitSet;
        for (auto& currentNodeID : data) {
            srcIdValueVector->setValue<nodeID_t>(0, currentNodeID);
            currentScanner.resetState();
            while (currentScanner.scan(selectVector, tx)) {
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
                auto& relInfo = currentScanner.relInfos[currentScanner.currentTableIdx];
                if (relInfo.direction == RelDataDirection::FWD || sharedData.direction == "in") {
                    edgeCount += selectVector.getSelSize();
                }

                for (auto i = 0u; i < selectVector.getSelSize(); ++i) {
                    auto nbrID = dstIdValueVector->getValue<nodeID_t>(i);
                    if (threadBitSet->markIfUnVisitedReturnVisited(globalBitSet, nbrID)) {
                        continue;
                    }
                    if (sharedData.direction == "both" && isFinal &&
                        relInfo.direction == RelDataDirection::BWD) {
                        ++edgeCount;
                    }
                }
            }
        }
    }

    return edgeCount;
}

static std::pair<uint64_t, std::vector<std::vector<nodeID_t>>> unionTask(KhopSharedData& sharedData,
    int64_t numThreads, uint32_t tid, bool isFinal) {
    uint64_t nodeCount = 0;
    std::vector<std::vector<nodeID_t>> nextFrontier;
    auto& threadBitSets = sharedData.threadBitSets;
    for (auto tableID = 0u; tableID < sharedData.globalBitSet->getTableNum(); ++tableID) {
        uint32_t tableSize = sharedData.globalBitSet->getTableSize(tableID);
        if (!tableSize) {
            continue;
        }
        // 必须64对齐,否则会存在多线程同时markVisited里的markflag
        auto [l, r] = distributeTasks(tableSize, numThreads, tid);
        auto targetBitSet = threadBitSets[0];
        for (auto i = 1u; i < numThreads; ++i) {
            auto threadBitSet = threadBitSets[i];
            auto& flags = threadBitSet->blockFlags[tableID];
            for (const auto& offset : flags.range(l, r)) {
                auto mark = threadBitSet->getAndReset(tableID, offset);
                targetBitSet->markVisited(tableID, offset, mark);
            }
        }

        if (isFinal) {
            for (auto i : targetBitSet->blockFlags[tableID].range(l, r)) {
                uint64_t mark = targetBitSet->getAndReset(tableID, i);
                nodeCount += __builtin_popcountll(mark);
            }
        } else {
            // 构建mission
            std::vector<nodeID_t> nextMission;
            for (auto i : targetBitSet->blockFlags[tableID].range(l, r)) {
                uint64_t now = targetBitSet->getAndReset(tableID, i), pos = 0;
                sharedData.globalBitSet->markVisited(tableID, i, now);
                while (now) {
                    // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                    pos = now ^ (now & (now - 1));
                    auto offset = getNodeOffset(i, pos);
                    auto nowNode = nodeID_t{offset, tableID};
                    if ((!nextMission.empty() && ((nextMission.back().offset ^ nowNode.offset) >>
                                                     StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                        nodeCount += nextMission.size();
                        nextFrontier.emplace_back(std::move(nextMission));
                    }
                    nextMission.emplace_back(nowNode);
                    // 还原成 now & (now - 1) ,和result配合,统计now中1的个数 now=now & (now - 1)
                    now ^= pos;
                }
            }
            if (!nextMission.empty()) {
                nodeCount += nextMission.size();
                nextFrontier.emplace_back(std::move(nextMission));
            }
        }
    }
    return {nodeCount, std::move(nextFrontier)};
}

static common::offset_t rewriteTableFunc(TableFuncInput& input, TableFuncOutput& output,
    bool isRc) {
    auto sharedState = input.sharedState->ptrCast<CallFuncSharedState>();
    if (!sharedState->getMorsel().hasMoreToOutput()) {
        return 0;
    }

    auto& dataChunk = output.dataChunk;
    auto pos = dataChunk.state->getSelVector()[0];
    auto bindData = input.bindData->constPtrCast<KhopBindData>();
    auto maxHop = bindData->maxHop;
    auto numThreads = bindData->numThreads;
    if (maxHop == 0) {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, 0);
        if (isRc) {
            dataChunk.getValueVector(1)->setValue<int64_t>(pos, 0);
        }
        return 1;
    }
    KhopSharedData sharedData(bindData);
    auto nodeID = getNodeID(sharedData.context, bindData->tableName, bindData->primaryKey);
    sharedData.globalBitSet->markVisited(nodeID);

    std::vector<nodeID_t> nextMission;
    nextMission.emplace_back(nodeID);
    sharedData.nextFrontier.emplace_back(std::move(nextMission));

    common::ExtendDirection extendDirection = getExtendDirection(sharedData.direction);
    std::shared_ptr<Expression> filterExpr = nullptr;
    if (sharedData.relFilter) {
        filterExpr = sharedData.relFilter->getExpression();
    }
    auto scanners =
        createRelTableCollectionScanner(*sharedData.context, extendDirection, filterExpr);

    std::vector<int64_t> nodeResults, edgeResults;
    for (auto currentLevel = 0u; currentLevel < maxHop; ++currentLevel) {

        bool isFinal = (currentLevel == maxHop - 1);
        sharedData.taskID = 0;
        std::vector<std::future<uint64_t>> funcFuture;
        for (uint32_t i = 0u; i < numThreads; i++) {
            if (isRc) {
                funcFuture.emplace_back(std::async(std::launch::async, funcRCTask,
                    std::ref(sharedData), i, isFinal, std::ref(scanners)));
            } else {
                funcFuture.emplace_back(std::async(std::launch::async, funcTask,
                    std::ref(sharedData), i, std::ref(scanners)));
            }
        }
        uint64_t edgeResult = 0;
        for (auto& future : funcFuture) {
            edgeResult += future.get();
        }
        std::vector<std::future<std::pair<uint64_t, std::vector<std::vector<nodeID_t>>>>>
            unionFuture;
        for (auto i = 0u; i < numThreads; i++) {
            unionFuture.emplace_back(std::async(std::launch::async, unionTask, std::ref(sharedData),
                numThreads, i, isFinal));
        }
        uint64_t nodeResult = 0;
        sharedData.nextFrontier.clear();
        for (auto& future : unionFuture) {
            auto [nodeCount, nextFrontier] = future.get();
            nodeResult += nodeCount;
            sharedData.nextFrontier.insert(sharedData.nextFrontier.end(),
                std::make_move_iterator(nextFrontier.begin()),
                std::make_move_iterator(nextFrontier.end()));
        }
        edgeResults.emplace_back(edgeResult);
        nodeResults.emplace_back(nodeResult);
        if (sharedData.nextFrontier.empty()) {
            break;
        }
        for (auto i = 0u; i < numThreads; i++) {
            sharedData.threadBitSets[i]->resetFlag();
        }
    }
    int64_t nodeResult = 0, edgeResult = 0;
    for (auto i = 0u; i < nodeResults.size(); i++) {
        nodeResult += nodeResults[i];
        edgeResult += edgeResults[i];
    }
    if (isRc) {
        dataChunk.getValueVector(0)->setValue(pos,
            (bindData->parameter ? nodeResults.back() : nodeResult));
        dataChunk.getValueVector(1)->setValue(pos,
            (bindData->parameter ? edgeResults.back() : edgeResult));
    } else {
        dataChunk.getValueVector(0)->setValue(pos,
            (bindData->parameter ? nodeResults.back() : nodeResult));
    }
    return 1;
}

static std::unique_ptr<TableFuncBindData> rewriteBindFunc(main::ClientContext* context,
    ScanTableFuncBindInput* input, std::vector<std::string> columnNames) {
    std::vector<std::string> returnColumnNames;
    std::vector<LogicalType> returnTypes;
    for (auto i = 0u; i < columnNames.size(); i++) {
        returnColumnNames.emplace_back(columnNames[i]);
        returnTypes.emplace_back(LogicalType::INT64());
    }
    auto direction = input->inputs[2].getValue<std::string>();
    if (direction != "in" && direction != "out" && direction != "both") {
        throw BinderException(
            "unknown direction, following directions are supported: in, out, both");
    }
    auto maxHop = input->inputs[3].getValue<int64_t>();
    KU_ASSERT(maxHop >= 0);
    auto parameter = input->inputs[4].getValue<int64_t>();
    if (parameter != 0 && parameter != 1) {
        throw BinderException("parameter value error");
    }

    auto numThreads = input->inputs[5].getValue<int64_t>();
    KU_ASSERT(numThreads >= 0);
    auto nodeFilterStr = input->inputs[6].getValue<std::string>();
    KU_ASSERT(nodeFilterStr.empty());

    // fixme wq 当前只支持边的属性过滤
    auto relFilterStr = input->inputs[7].getValue<std::string>();
    std::unique_ptr<evaluator::ExpressionEvaluator> relFilter = nullptr;
    std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds = nullptr;
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
        relTableInfos = nullptr;
    computeRelFilter(context, relFilterStr, relFilter, relColumnTypeIds, relTableInfos);

    auto bindData =
        std::make_unique<KhopBindData>(context, input->inputs[0].getValue<std::string>(),
            input->inputs[1].getValue<std::string>(), direction, maxHop, parameter, numThreads,
            std::move(returnTypes), std::move(returnColumnNames), 1 /* one line of results */,
            std::move(relFilter), std::move(relColumnTypeIds), std::move(relTableInfos));
    return bindData;
}

} // namespace function
} // namespace kuzu
