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

class BSet {
public:
    uint64_t value;
    pthread_mutex_t mtx;
    BSet() {
        value = 0;
        pthread_mutex_init(&mtx, NULL);
    }
    ~BSet() { pthread_mutex_destroy(&mtx); }
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
        nextMark.resize(allTableSize);
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto nodeNum = nodeTable->getNumTuples(tx);
            nodeIDMark[tableID].resize((nodeNum + 63) >> 6);
            nextMark[tableID].resize((nodeNum + 63) >> 6);
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

    std::mutex mtx;
    uint32_t taskID;
    std::string direction;
    Catalog* catalog;
    storage::MemoryManager* mm;
    main::ClientContext* context;
    transaction::Transaction* tx;
    storage::StorageManager* storage;
    std::vector<std::vector<BSet>> nextMark;
    std::vector<std::vector<uint64_t>> nodeIDMark;
    std::vector<std::vector<std::vector<uint64_t>>> threadMarks;
    std::vector<std::vector<nodeID_t>> currentFrontier, nextFrontier;
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

static void scanTask(KhopSharedData& sharedData,
    std::shared_ptr<storage::RelTableScanState> readState, common::ValueVector& srcVector,
    std::shared_ptr<RelTableInfo> info, std::vector<std::vector<uint64_t>>& threadMark,
    RelDataDirection relDataDirection, int64_t& threadResult, uint32_t tid, bool isFinal,
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
    for (auto currentNodeID : sharedData.currentFrontier[tid]) {
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

static void tableFuncTask(KhopSharedData& sharedData,
    std::vector<std::vector<uint64_t>>& threadMark, int64_t& edgeResult, bool isFinal) {
    int64_t threadResult = 0;
    if (threadMark.empty()) {
        threadMark.resize((sharedData.nodeIDMark.size()));
        for (auto i = 0u; i < threadMark.size(); ++i) {
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
    while (true) {
        sharedData.mtx.lock();
        uint32_t tid = sharedData.taskID++;
        sharedData.mtx.unlock();
        if (tid >= sharedData.currentFrontier.size()) {
            break;
        }
        for (auto& [info, relDataReadState] : relTableScanStates) {
            if (sharedData.direction == "out" || sharedData.direction == "both") {
                scanTask(sharedData, relDataReadState, srcVector, info, threadMark,
                    RelDataDirection::FWD, threadResult, tid, isFinal, *rs.get(), relFilter);
            }
            if (sharedData.direction == "in" || sharedData.direction == "both") {
                scanTask(sharedData, relDataReadState, srcVector, info, threadMark,
                    RelDataDirection::BWD, threadResult, tid, isFinal, *rs.get(), relFilter);
            }
        }
    }
    for (auto tableID = 0u; tableID < threadMark.size(); ++tableID) {
        for (auto blockID = 0u; blockID < threadMark[tableID].size(); ++blockID) {
            auto mark = threadMark[tableID][blockID];
            if (!mark) {
                continue;
            }
            auto& bitSet = sharedData.nextMark[tableID][blockID];
            auto mtx = &bitSet.mtx;
            pthread_mutex_lock(mtx);
            bitSet.value |= mark;
            pthread_mutex_unlock(mtx);
        }
    }
    sharedData.mtx.lock();
    edgeResult += threadResult;
    sharedData.mtx.unlock();
}

static common::offset_t rewriteTableFunc(TableFuncInput& input, TableFuncOutput& output,
    bool isEdge) {
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
    auto outputVector = dataChunk.getValueVector(0);
    auto pos = dataChunk.state->getSelVector()[0];
    auto bindData = input.bindData->constPtrCast<KhopBindData>();
    auto maxHop = bindData->maxHop;
    auto numThreads = bindData->numThreads;
    if (maxHop == 0) {
        outputVector->setValue<int64_t>(pos, 0);
        return 1;
    }
    KhopSharedData sharedData(bindData);
    auto nodeID = getNodeID(sharedData, bindData->tableName, bindData->primaryKey);
    sharedData.nodeIDMark[nodeID.tableID][(nodeID.offset >> 6)] |= (1ULL << ((nodeID.offset & 63)));
    std::vector<nodeID_t> nextMission;
    nextMission.emplace_back(nodeID);
    sharedData.currentFrontier.emplace_back(std::move(nextMission));
    sharedData.threadMarks.resize(numThreads);
    std::vector<int64_t> nodeResults, edgeResults;
    for (auto currentLevel = 0u; currentLevel < maxHop; ++currentLevel) {
        int64_t nodeResult = 0, edgeResult = 0;
        bool isFinal = (currentLevel == maxHop - 1);
        std::vector<std::thread> threads;
        sharedData.taskID = 0;
        for (auto i = 0u; i < numThreads; i++) {
            threads.emplace_back([&sharedData, i, &edgeResult, isFinal] {
                tableFuncTask(sharedData, sharedData.threadMarks[i], edgeResult, isFinal);
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        for (auto tableID = 0u; tableID < sharedData.nextMark.size(); ++tableID) {
            for (auto i = 0u; i < sharedData.nextMark[tableID].size(); ++i) {
                uint64_t now = sharedData.nextMark[tableID][i].value, pos = 0;
                sharedData.nodeIDMark[tableID][i] |= now;
                while (now) {
                    pos = now ^ (now & (now - 1));
                    /*bitset的每一个元素实际标记了64个元素是否存在,i<<6是该元素offset的起点位置，加上pos%67就是实际offset的值*/
                    auto nowNode = nodeID_t{(i << 6) + numTable[pos % 67], tableID};
                    if ((!nextMission.empty() && ((nextMission.back().offset ^ nowNode.offset) >>
                                                     StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                        nodeResult += nextMission.size();
                        sharedData.nextFrontier.emplace_back(std::move(nextMission));
                    }
                    nextMission.emplace_back(nowNode);
                    now ^= pos;
                }
                sharedData.nextMark[tableID][i].value = 0;
            }
            if (!nextMission.empty()) {
                nodeResult += nextMission.size();
                sharedData.nextFrontier.emplace_back(std::move(nextMission));
            }
        }
        sharedData.currentFrontier = std::move(sharedData.nextFrontier);
        edgeResults.emplace_back(edgeResult);
        nodeResults.emplace_back(nodeResult);
        if (sharedData.currentFrontier.empty()) {
            break;
        }
    }
    if (isEdge) {
        int64_t result = 0;
        for (auto& res : edgeResults) {
            result += res;
        }
        outputVector->setValue(pos, (bindData->mode ? edgeResults.back() : result));
    } else {
        int64_t result = 0;
        for (auto& res : nodeResults) {
            result += res;
        }
        outputVector->setValue<int64_t>(pos, (bindData->mode ? nodeResults.back() : result));
    }
    return 1;
}

static std::unique_ptr<TableFuncBindData> rewriteBindFunc(main::ClientContext* context,
    TableFuncBindInput* input, std::string columnName) {
    std::vector<std::string> returnColumnNames;
    std::vector<LogicalType> returnTypes;
    returnColumnNames.emplace_back(columnName);
    returnTypes.emplace_back(LogicalType::INT64());
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