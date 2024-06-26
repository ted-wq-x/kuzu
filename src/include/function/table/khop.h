#include <future>
#include <thread>

#include "function/table/basic.h"

namespace kuzu {
namespace function {

struct KhopBindData : public CallTableFuncBindData {
    KhopBindData(main::ClientContext* context, std::string primaryKey, std::string tableName,
        std::string direction, int64_t maxHop, int64_t mode, int64_t numThreads,
        std::vector<LogicalType> returnTypes, std::vector<std::string> returnColumnNames,
        offset_t maxOffset, std::unique_ptr<evaluator::ExpressionEvaluator> relFilter,
        std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds,
        std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
            relTableInfos)
        : CallTableFuncBindData{std::move(returnTypes), std::move(returnColumnNames), maxOffset},
          context(context), primaryKey(primaryKey), tableName(tableName), direction(direction),
          maxHop(maxHop), mode(mode), numThreads(numThreads), relFilter(std::move(relFilter)),
          relColumnTypeIds(std::move(relColumnTypeIds)), relTableInfos(std::move(relTableInfos)) {}

    std::unique_ptr<TableFuncBindData> copy() const override {
        std::unique_ptr<evaluator::ExpressionEvaluator> localRelFilter = nullptr;
        if (relFilter) {
            localRelFilter = relFilter->clone();
        }

        return std::make_unique<KhopBindData>(context, primaryKey, tableName, direction, maxHop,
            mode, numThreads, common::LogicalType::copy(columnTypes), columnNames, maxOffset,
            std::move(localRelFilter), relColumnTypeIds, relTableInfos);
    }
    main::ClientContext* context;
    std::string primaryKey, tableName, direction;
    int64_t maxHop, mode, numThreads;

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
        mm = context->getMemoryManager();
        relFilter = bindData->relFilter.get();
        relColumnTypeIds = bindData->relColumnTypeIds;
        direction = bindData->direction;
        tx = context->getTx();
        catalog = context->getCatalog();
        storage = context->getStorageManager();
        reltables = bindData->relTableInfos;
        auto nodeTableIDs = catalog->getNodeTableIDs(tx);
        auto allTableSize = catalog->getRelTableIDs(tx).size() + nodeTableIDs.size();
        nodeIDMark.resize(allTableSize);
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto size = (nodeTable->getNumTuples(tx) + 63) >> 6;
            nodeIDMark[tableID].reserve(size);
            nodeIDMark[tableID].resize(size, 0);
        }
    }

    std::unique_ptr<processor::ResultSet> createResultSet() {
        processor::ResultSet resultSet(1);
        auto columnType = *relColumnTypeIds.get();
        auto numValueVectors = columnType.size();
        auto dataChunk = std::make_shared<common::DataChunk>(numValueVectors);
        for (auto j = 0u; j < numValueVectors; ++j) {
            dataChunk->insert(j, std::make_shared<ValueVector>(columnType[j], mm));
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

    std::atomic_int32_t taskID;
    std::string direction;
    Catalog* catalog;
    storage::MemoryManager* mm;
    main::ClientContext* context;
    transaction::Transaction* tx;
    storage::StorageManager* storage;
    std::vector<std::vector<uint64_t>> nodeIDMark; // tableID,全局记录点的count
    std::vector<std::vector<std::vector<uint64_t>>> threadMarks;
    // 内部的vector,是按照block大小划分的,都是相同的tableid
    std::vector<std::vector<nodeID_t>> nextFrontier;
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
        reltables;
    evaluator::ExpressionEvaluator* relFilter;
    std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds;
};

static nodeID_t getNodeID(KhopSharedData& sharedData, std::string tableName,
    std::string primaryKey) {
    if (tableName.empty()) {
        auto nodeTableIDs = sharedData.catalog->getNodeTableIDs(sharedData.tx);
        std::vector<nodeID_t> nodeIDs;
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = sharedData.storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto offset = getOffset(sharedData.tx, nodeTable, primaryKey);
            if (offset != INVALID_OFFSET) {
                nodeIDs.emplace_back(offset, tableID);
            }
        }
        if (nodeIDs.size() != 1) {
            throw RuntimeException("Invalid primary key");
        }
        return nodeIDs.back();
    } else {
        auto tableID = sharedData.catalog->getTableID(sharedData.tx, tableName);
        auto nodeTable = sharedData.storage->getTable(tableID)->ptrCast<storage::NodeTable>();
        auto offset = getOffset(sharedData.tx, nodeTable, primaryKey);
        if (offset == INVALID_OFFSET) {
            throw RuntimeException("Invalid primary key");
        }
        return nodeID_t{offset, tableID};
    }
}

static void doScan(KhopSharedData& sharedData,
    std::shared_ptr<storage::RelTableScanState> readState, common::ValueVector& srcVector,
    std::shared_ptr<RelTableInfo> info, std::vector<std::vector<uint64_t>>& threadMark,
    RelDataDirection relDataDirection, uint64_t& threadResult, uint32_t tid, bool isFinal,
    const processor::ResultSet& resultSet, evaluator::ExpressionEvaluator* relExpr) {
    auto prevTableID =
        relDataDirection == RelDataDirection::FWD ? info->srcTableID : info->dstTableID;

    auto relTable = info->relTable;
    readState->direction = relDataDirection;
    auto relDataReadState =
        common::ku_dynamic_cast<storage::TableDataScanState*, storage::RelDataReadState*>(
            readState->dataScanState.get());
    relDataReadState->nodeGroupIdx = INVALID_NODE_GROUP_IDX;
    relDataReadState->numNodes = 0;
    relDataReadState->chunkStates.clear();
    relDataReadState->chunkStates.resize(relDataReadState->columnIDs.size());
    //    relDataReadState->chunkStates[0]
    auto dataChunkToSelect = resultSet.dataChunks[0];
    auto& selectVector = dataChunkToSelect->state->getSelVectorUnsafe();
    auto dstIdDataChunk = dataChunkToSelect->getValueVector(0);
    for (auto currentNodeID : sharedData.nextFrontier[tid]) {
        if (prevTableID != currentNodeID.tableID) {
            break;
        }
        /*we can quick check here (part of checkIfNodeHasRels)*/
        srcVector.setValue<nodeID_t>(0, currentNodeID);
        relTable->initializeScanState(sharedData.tx, *readState.get());
        while (readState->hasMoreToRead(sharedData.tx)) {
            relTable->scan(sharedData.tx, *readState.get());

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

            if (relDataDirection == RelDataDirection::FWD ||
                (relDataDirection == RelDataDirection::BWD && sharedData.direction == "in")) {
                threadResult += selectVector.getSelSize();
            }

            for (auto i = 0u; i < selectVector.getSelSize(); ++i) {
                auto nbrID = dstIdDataChunk->getValue<nodeID_t>(i);
                uint64_t block = (nbrID.offset >> 6), pos = (nbrID.offset & 63);
                if ((sharedData.nodeIDMark[nbrID.tableID][block] >> pos) & 1) {
                    continue;
                }
                threadMark[nbrID.tableID][block] |= (1ULL << pos);
                if (sharedData.direction == "both" && isFinal &&
                    relDataDirection == RelDataDirection::BWD) {
                    ++threadResult;
                }
            }
        }
    }
}

static uint64_t funcTask(KhopSharedData& sharedData, uint32_t threadId, bool isFinal) {

    auto& threadMark = sharedData.threadMarks[threadId];
    if (threadMark.empty()) {
        threadMark.resize((sharedData.nodeIDMark.size()));
        for (auto i = 0u; i < threadMark.size(); ++i) {
            threadMark[i].reserve(sharedData.nodeIDMark[i].size());
            threadMark[i].resize(sharedData.nodeIDMark[i].size(), 0);
        }
    }

    auto rs = sharedData.createResultSet();
    auto relEvaluate = sharedData.initEvaluator(*rs.get());
    auto srcVector = common::ValueVector(LogicalType::INTERNAL_ID(), sharedData.mm);
    srcVector.state = DataChunkState::getSingleValueDataChunkState();

    std::vector<
        std::pair<std::shared_ptr<RelTableInfo>, std::shared_ptr<storage::RelTableScanState>>>
        relTableScanStates;
    for (auto& [tableID, info] : *sharedData.reltables) {
        auto readState =
            std::make_shared<storage::RelTableScanState>(info->columnIDs, RelDataDirection::FWD);
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
    uint64_t edgeCount = 0;
    while (true) {
        uint32_t tid = sharedData.taskID++;
        if (tid >= sharedData.nextFrontier.size()) {
            break;
        }
        for (auto& [info, relDataReadState] : relTableScanStates) {
            if (sharedData.direction == "out" || sharedData.direction == "both") {
                doScan(sharedData, relDataReadState, srcVector, info, threadMark,
                    RelDataDirection::FWD, edgeCount, tid, isFinal, *rs.get(), relFilter);
            }
            if (sharedData.direction == "in" || sharedData.direction == "both") {
                doScan(sharedData, relDataReadState, srcVector, info, threadMark,
                    RelDataDirection::BWD, edgeCount, tid, isFinal, *rs.get(), relFilter);
            }
        }
    }

    return edgeCount;
}

static std::pair<uint64_t, std::vector<std::vector<nodeID_t>>> unionTask(KhopSharedData& sharedData,
    int64_t numThreads, uint32_t tid, std::vector<uint8_t>& numTable) {
    uint64_t nodeCount = 0;
    std::vector<std::vector<nodeID_t>> nextFrontier;
    for (auto tableID = 0u; tableID < sharedData.nodeIDMark.size(); ++tableID) {
        uint32_t tableSize = sharedData.nodeIDMark[tableID].size();
        if (!tableSize) {
            continue;
        }

        uint32_t l = tableSize * tid / numThreads, r = tableSize * (tid + 1) / numThreads;
        std::vector<uint64_t> tempMark;
        tempMark.resize(r - l, 0);
        for (auto i = 0u; i < numThreads; ++i) {
            for (auto offset = l; offset < r; ++offset) {
                if (!sharedData.threadMarks[i][tableID][offset]) {
                    continue;
                }
                auto& mark = sharedData.threadMarks[i][tableID][offset];
                tempMark[offset - l] |= mark;
                mark = 0;
            }
        }

        // 构建mission
        std::vector<nodeID_t> nextMission;
        for (auto i = l; i < r; ++i) {
            uint64_t now = tempMark[i - l], pos = 0;
            sharedData.nodeIDMark[tableID][i] |= now;
            while (now) {
                // now & (now - 1) 去掉最低位的1 ,取最低位的值  pos=now & -now
                pos = now ^ (now & (now - 1));
                /*bitset的每一个元素实际标记了64个元素是否存在,i<<6是该元素offset的起点位置，加上pos%67就是实际offset的值*/
                auto nowNode = nodeID_t{(i << 6) + numTable[pos % 67], tableID};
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
    return {nodeCount, std::move(nextFrontier)};
}

static common::offset_t rewriteTableFunc(TableFuncInput& input, TableFuncOutput& output,
    bool isRc) {
    auto sharedState = input.sharedState->ptrCast<CallFuncSharedState>();
    if (!sharedState->getMorsel().hasMoreToOutput()) {
        return 0;
    }
    /*2^i%67的值互不相同，根据该值可以判断它的幂次*/
    std::vector<uint8_t> numTable(67);
    for (uint8_t i = 0; i < 64; ++i) {
        uint64_t now = (1ULL << i);
        numTable[now % 67] = i;
    }
    auto& dataChunk = output.dataChunk;
    auto pos = dataChunk.state->getSelVector()[0];
    auto bindData = input.bindData->constPtrCast<KhopBindData>();
    auto maxHop = bindData->maxHop;
    auto numThreads = bindData->numThreads;
    if (maxHop == 0) {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, 0);
        return 1;
    }
    KhopSharedData sharedData(bindData);
    auto nodeID = getNodeID(sharedData, bindData->tableName, bindData->primaryKey);
    sharedData.nodeIDMark[nodeID.tableID][(nodeID.offset >> 6)] |= (1ULL << ((nodeID.offset & 63)));
    std::vector<nodeID_t> nextMission;
    nextMission.emplace_back(nodeID);
    sharedData.nextFrontier.emplace_back(std::move(nextMission));
    sharedData.threadMarks.resize(numThreads);
    std::vector<int64_t> nodeResults, edgeResults;
    for (auto currentLevel = 0u; currentLevel < maxHop; ++currentLevel) {

        bool isFinal = (currentLevel == maxHop - 1);
        sharedData.taskID = 0;
        std::vector<std::future<uint64_t>> funcFuture;
        for (uint32_t i = 0u; i < numThreads; i++) {
            funcFuture.emplace_back(
                std::async(std::launch::async, funcTask, std::ref(sharedData), i, isFinal));
        }
        uint64_t edgeResult = 0;
        for (auto& future : funcFuture) {
            edgeResult += future.get();
        }
        std::vector<std::future<std::pair<uint64_t, std::vector<std::vector<nodeID_t>>>>>
            unionFuture;
        for (auto i = 0u; i < numThreads; i++) {
            unionFuture.emplace_back(std::async(std::launch::async, unionTask, std::ref(sharedData),
                numThreads, i, std::ref(numTable)));
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
    }
    int64_t nodeResult = 0, edgeResult = 0;
    for (auto i = 0u; i < nodeResults.size(); i++) {
        nodeResult += nodeResults[i];
        edgeResult += edgeResults[i];
    }
    if (isRc) {
        dataChunk.getValueVector(0)->setValue(pos,
            (bindData->mode ? nodeResults.back() : nodeResult));
        dataChunk.getValueVector(1)->setValue(pos,
            (bindData->mode ? edgeResults.back() : edgeResult));
    } else {
        dataChunk.getValueVector(0)->setValue(pos,
            (bindData->mode ? nodeResults.back() : nodeResult));
    }
    return 1;
}

static std::unique_ptr<TableFuncBindData> rewriteBindFunc(main::ClientContext* context,
    TableFuncBindInput* input, std::vector<std::string> columnNames) {
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
    auto mode = input->inputs[4].getValue<int64_t>();
    if (mode != 0 && mode != 1) {
        throw BinderException("wrong mode number");
    }

    auto numThreads = input->inputs[5].getValue<int64_t>();
    KU_ASSERT(numThreads >= 0);
    auto nodeFilterStr = input->inputs[6].getValue<std::string>();

    if (!nodeFilterStr.empty()) {
        KU_ASSERT(false);
    }

    // fixme wq 当前只支持边的属性过滤
    auto relFilterStr = input->inputs[7].getValue<std::string>();
    std::unique_ptr<evaluator::ExpressionEvaluator> relFilter = nullptr;
    std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds = nullptr;
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
        relTableInfos = nullptr;
    computeRelFilter(context, relFilterStr, relFilter, relColumnTypeIds, relTableInfos);

    auto bindData =
        std::make_unique<KhopBindData>(context, input->inputs[0].getValue<std::string>(),
            input->inputs[1].getValue<std::string>(), direction, maxHop, mode, numThreads,
            std::move(returnTypes), std::move(returnColumnNames), 1 /* one line of results */,
            std::move(relFilter), std::move(relColumnTypeIds), std::move(relTableInfos));
    return bindData;
}

} // namespace function
} // namespace kuzu
