#include <thread>

#include "function/table/basic.h"

namespace kuzu {
namespace function {

struct SsspBindData : public CallTableFuncBindData {
    SsspBindData(main::ClientContext* context, std::string srcPrimaryKey, std::string srcTableName,
        std::string dstPrimaryKey, std::string dstTableName, std::string direction, int64_t maxHop,
        std::string mode, int64_t numThreads, std::unordered_set<std::string> nodeFilter,
        std::unordered_set<std::string> relFilter, std::vector<LogicalType> returnTypes,
        std::vector<std::string> returnColumnNames, offset_t maxOffset)
        : CallTableFuncBindData{std::move(returnTypes), std::move(returnColumnNames), maxOffset},
          context(context), srcPrimaryKey(srcPrimaryKey), srcTableName(srcTableName),
          dstPrimaryKey(dstPrimaryKey), dstTableName(dstTableName), direction(direction),
          maxHop(maxHop), mode(mode), numThreads(numThreads), nodeFilter(std::move(nodeFilter)),
          relFilter(std::move(relFilter)) {}

    std::unique_ptr<TableFuncBindData> copy() const override {
        return std::make_unique<SsspBindData>(context, srcPrimaryKey, srcTableName, dstPrimaryKey,
            dstTableName, direction, maxHop, mode, numThreads, nodeFilter, relFilter, columnTypes,
            columnNames, maxOffset);
    }
    main::ClientContext* context;
    std::string srcPrimaryKey, srcTableName, dstPrimaryKey, dstTableName, direction;
    int64_t maxHop;
    std::string mode;
    int64_t numThreads;
    std::unordered_set<std::string> nodeFilter, relFilter;
};

class Value {
public:
    uint16_t count;
    pthread_mutex_t mtx;
    Value() {
        count = 0;
        pthread_mutex_init(&mtx, NULL);
    }
    ~Value() { pthread_mutex_destroy(&mtx); }
};

class SsspSharedData {
public:
    explicit SsspSharedData(const SsspBindData* bindData) {
        auto context = bindData->context;
        mm = context->getMemoryManager();
        tx = context->getTx();
        catalog = context->getCatalog();
        storage = context->getStorageManager();
        for (auto& nodeTableName : bindData->nodeFilter) {
            auto tableID = context->getCatalog()->getTableID(context->getTx(), nodeTableName);
            nodeFilter.insert(tableID);
        }
        std::unordered_set<table_id_t> relFilter;
        for (auto& relTableName : bindData->relFilter) {
            auto tableID = context->getCatalog()->getTableID(context->getTx(), relTableName);
            relFilter.insert(tableID);
        }
        hasNodeFilter = nodeFilter.size() ? true : false;
        bool hasRelFilter = relFilter.size() ? true : false;
        auto relTableIDs = catalog->getRelTableIDs(tx);
        for (auto& tableID : relTableIDs) {
            if (!hasRelFilter || relFilter.count(tableID)) {
                auto relTableInfo = std::make_unique<RelTableInfo>(tx, storage, catalog, tableID);
                reltables.emplace_back(tableID, std::move(relTableInfo));
            }
        }
        auto nodeTableIDs = catalog->getNodeTableIDs(tx);
        auto allTableSize = relTableIDs.size() + nodeTableIDs.size();
        nextMark.resize(allTableSize);
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            nextMark[tableID].resize(nodeTable->getNumTuples(tx));
        }
    }
    bool hasNodeFilter;
    catalog::Catalog* catalog;
    storage::MemoryManager* mm;
    transaction::Transaction* tx;
    storage::StorageManager* storage;
    std::vector<std::vector<Value>> nextMark; 
    std::unordered_set<table_id_t> nodeFilter;
    std::vector<std::vector<std::vector<uint16_t>>> threadMarks; 
    std::vector<std::pair<common::table_id_t, std::unique_ptr<RelTableInfo>>> reltables;
};

class ScanState {
public:
    explicit ScanState(SsspSharedData& sharedData, nodeID_t nodeID, std::string direction,
        int64_t numThreads)
        : sharedData(sharedData), direction(direction), numThreads(numThreads), currentLevel(0) {
        nodeIDMark.resize(sharedData.nextMark.size());
        for (auto tableID = 0u; tableID < nodeIDMark.size(); ++tableID) {
            nodeIDMark[tableID].resize(sharedData.nextMark[tableID].size(), 0);
        }
        nodeNumbers = 1;
        nodeIDMark[nodeID.tableID][nodeID.offset] = 1;
        nextMission.emplace_back(nodeID);
        currentFrontier.emplace_back(std::move(nextMission));
    }
    void scanTask(storage::RelTableScanState& readState,
        storage::RelDataReadState* relDataReadState, common::ValueVector& srcVector,
        common::ValueVector& dstVector, common::DataChunkState* dstState, RelTableInfo* info,
        std::vector<std::vector<uint16_t>>& threadMark, RelDataDirection relDataDirection,
        uint32_t tid) {
        auto prevTableID =
            relDataDirection == RelDataDirection::FWD ? info->srcTableID : info->dstTableID;
        auto nextTableID =
            relDataDirection == RelDataDirection::BWD ? info->srcTableID : info->dstTableID;
        if (sharedData.hasNodeFilter && !sharedData.nodeFilter.count(nextTableID)) {
            return;
        }
        auto relTable = info->relTable;
        readState.direction = relDataDirection;
        relDataReadState->nodeGroupIdx = INVALID_NODE_GROUP_IDX;
        relDataReadState->numNodes = 0;
        relDataReadState->chunkStates.clear();
        relDataReadState->chunkStates.resize(1);
        for (auto currentNodeID : currentFrontier[tid]) {
            if (prevTableID != currentNodeID.tableID) {
                break;
            }
            /*we can quick check here (part of checkIfNodeHasRels)*/
            srcVector.setValue<nodeID_t>(0, currentNodeID);
            relTable->initializeScanState(sharedData.tx, readState);
            auto count = nodeIDMark[currentNodeID.tableID][currentNodeID.offset];
            while (readState.hasMoreToRead(sharedData.tx)) {
                relTable->scan(sharedData.tx, readState);
                KU_ASSERT(dstState->getSelVector().isUnfiltered());
                for (auto i = 0u; i < dstState->getSelVector().getSelSize(); ++i) {
                    auto nbrID = dstVector.getValue<nodeID_t>(i);

                    if (nodeIDMark[nbrID.tableID][nbrID.offset]) {
                        continue;
                    }
                    threadMark[nbrID.tableID][nbrID.offset] += count;
                }
            }
        }
    }
    void tableFuncTask(std::vector<std::vector<uint16_t>>& threadMark) {
        if (threadMark.empty()) {
            threadMark.resize(nodeIDMark.size());
            for (auto i = 0u; i < threadMark.size(); ++i) {
                threadMark[i].resize(nodeIDMark[i].size(), 0);
            }
        }
        std::vector<common::column_id_t> columnIDs;
        auto readState = storage::RelTableScanState(columnIDs, RelDataDirection::FWD);
        auto relDataReadState =
            common::ku_dynamic_cast<storage::TableDataScanState*, storage::RelDataReadState*>(
                readState.dataScanState.get());
        auto srcState = DataChunkState::getSingleValueDataChunkState();
        auto dstState = std::make_shared<common::DataChunkState>();
        auto srcVector = common::ValueVector(*LogicalType::INTERNAL_ID(), sharedData.mm);
        auto dstVector = common::ValueVector(*LogicalType::INTERNAL_ID(), sharedData.mm);
        srcVector.state = srcState;
        dstVector.state = dstState;
        readState.nodeIDVector = &srcVector;
        readState.outputVectors.clear();
        readState.outputVectors.emplace_back(&dstVector);
        std::vector<common::column_id_t> dataScanColumnIDs{storage::NBR_ID_COLUMN_ID};
        dataScanColumnIDs.insert(dataScanColumnIDs.end(), columnIDs.begin(), columnIDs.end());
        relDataReadState->columnIDs = dataScanColumnIDs;
        while (true) {
            mtx.lock();
            uint32_t tid = taskID++;
            mtx.unlock();
            if (tid >= currentFrontier.size()) {
                break;
            }
            for (auto& [tableID, info] : sharedData.reltables) {
                if (direction == "out" || direction == "both") {
                    scanTask(readState, relDataReadState, srcVector, dstVector, dstState.get(),
                        info.get(), threadMark, RelDataDirection::FWD, tid);
                }
                if (direction == "in" || direction == "both") {
                    scanTask(readState, relDataReadState, srcVector, dstVector, dstState.get(),
                        info.get(), threadMark, RelDataDirection::BWD, tid);
                }
            }
        }
        for (auto tableID = 0u; tableID < threadMark.size(); ++tableID) {
            for (auto offset = 0u; offset < threadMark[tableID].size(); ++offset) {
                if (!threadMark[tableID][offset]) {
                    continue;
                }
                auto& count = threadMark[tableID][offset];
                auto& mtx = sharedData.nextMark[tableID][offset].mtx;
                pthread_mutex_lock(&mtx);
                sharedData.nextMark[tableID][offset].count += count;
                count = 0;
                pthread_mutex_unlock(&mtx);
            }
        }
    }

    bool isForntierEmpty() const { return currentFrontier.empty(); }

    void getNextForntier(ScanState& other, bool& isIntersect) {
        nodeNumbers = taskID = 0;
        ++currentLevel;
        std::vector<std::thread> threads;
        for (auto i = 0u; i < numThreads; ++i) {
            threads.emplace_back([&, i] { tableFuncTask(sharedData.threadMarks[i]); });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        for (auto tableID = 0u; tableID < nodeIDMark.size(); ++tableID) {
            for (auto offset = 0u; offset < nodeIDMark[tableID].size(); ++offset) {
                if (!sharedData.nextMark[tableID][offset].count) {
                    continue;
                }
                if (!isIntersect && other.nodeIDMark[tableID][offset]) {
                    isIntersect = true;
                }
                auto& count = sharedData.nextMark[tableID][offset].count;
                nodeIDMark[tableID][offset] += count;
                ++nodeNumbers;
                count = 0;
                if (!nextMission.empty() && ((nextMission.back().offset ^ offset) >>
                                                StorageConstants::NODE_GROUP_SIZE_LOG2)) {
                    nextFrontier.emplace_back(std::move(nextMission));
                }
                nextMission.emplace_back(offset, tableID);
            }
            if (!nextMission.empty()) {
                nextFrontier.emplace_back(std::move(nextMission));
            }
        }
        currentFrontier = std::move(nextFrontier);
    }
    std::mutex mtx;
    SsspSharedData& sharedData;
    std::string direction;
    int64_t numThreads, taskID, currentLevel, nodeNumbers;
    std::vector<nodeID_t> nextMission;
    std::vector<std::vector<uint16_t>> nodeIDMark;
    std::vector<std::vector<nodeID_t>> currentFrontier, nextFrontier;
};

static nodeID_t getNodeID(SsspSharedData& sharedData, std::string tableName,
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

int64_t computeResult(ScanState& srcScanState, ScanState& dstScanState) {
    int64_t result = 0;
    for (auto tableID = 0u; tableID < srcScanState.nodeIDMark.size(); ++tableID) {
        for (auto offset = 0u; offset < srcScanState.nodeIDMark[tableID].size(); ++offset) {
            if (srcScanState.nodeIDMark[tableID][offset] &&
                dstScanState.nodeIDMark[tableID][offset]) {
                result += 1LL * srcScanState.nodeIDMark[tableID][offset] *
                          dstScanState.nodeIDMark[tableID][offset];
            }
        }
    }
    return result;
}

static common::offset_t tableFunc(TableFuncInput& input, TableFuncOutput& output) {
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
    auto mode = bindData->mode;
    SsspSharedData sharedData(bindData);
    sharedData.threadMarks.resize(numThreads);
    auto srcNodeID = getNodeID(sharedData, bindData->srcTableName, bindData->srcPrimaryKey);
    auto dstNodeID = getNodeID(sharedData, bindData->dstTableName, bindData->dstPrimaryKey);
    auto srcDirection = direction;
    auto dstDirection = (direction == "in") ? "out" : ((direction == "out") ? "in" : "both");
    ScanState srcScanState(sharedData, srcNodeID, srcDirection, numThreads);
    ScanState dstScanState(sharedData, dstNodeID, dstDirection, numThreads);
    int64_t lengthResult = -1, numberResult = 0;
    if (srcNodeID == dstNodeID) {
        lengthResult = 0;
        numberResult = 1;
    } else {
        bool isIntersect = false;
        while (srcScanState.currentLevel + dstScanState.currentLevel < maxHop &&
               (!srcScanState.isForntierEmpty() || !dstScanState.isForntierEmpty())) {
            if (srcScanState.nodeNumbers <= dstScanState.nodeNumbers &&
                !srcScanState.isForntierEmpty()) {
                srcScanState.getNextForntier(dstScanState, isIntersect);
            } else if (!dstScanState.isForntierEmpty()) {
                dstScanState.getNextForntier(srcScanState, isIntersect);
            }
            if (isIntersect) {
                lengthResult = srcScanState.currentLevel + dstScanState.currentLevel;
                numberResult = computeResult(srcScanState, dstScanState);
                break;
            }
        }
    }
    if (mode == "length") {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, lengthResult);
    } else if (mode == "count") {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, numberResult);
    } else {
        dataChunk.getValueVector(0)->setValue<int64_t>(pos, lengthResult);
        dataChunk.getValueVector(1)->setValue<int64_t>(pos, numberResult);
    }
    return 1;
}

static std::unique_ptr<TableFuncBindData> bindFunc(main::ClientContext* context,
    TableFuncBindInput* input) {
    std::vector<std::string> returnColumnNames;
    std::vector<LogicalType> returnTypes;
    auto mode = input->inputs[6].getValue<std::string>();
    if (mode == "length") {
        returnColumnNames.emplace_back("length");
        returnTypes.emplace_back(*LogicalType::INT64());
    } else if (mode == "count") {
        returnColumnNames.emplace_back("count");
        returnTypes.emplace_back(*LogicalType::INT64());
    } else if (mode == "length_count") {
        returnColumnNames.emplace_back("length");
        returnTypes.emplace_back(*LogicalType::INT64());
        returnColumnNames.emplace_back("count");
        returnTypes.emplace_back(*LogicalType::INT64());
    } else {
        throw BinderException(
            "unknown mode, following modes are supported: length, count, length_count");
    }
    auto direction = input->inputs[4].getValue<std::string>();
    if (direction != "in" && direction != "out" && direction != "both") {
        throw BinderException(
            "unknown direction, following directions are supported: in, out, both");
    }
    auto maxHop = input->inputs[5].getValue<int64_t>();
    KU_ASSERT(maxHop >= -1);
    maxHop = (maxHop == -1) ? INT64_MAX : maxHop;
    auto numThreads = input->inputs[7].getValue<int64_t>();
    KU_ASSERT(numThreads >= 0);
    // filter
    auto nodeList = input->inputs[8];
    auto relList = input->inputs[9];
    std::unordered_set<std::string> nodeFilter, relFilter;
    for (auto pos = 0u; pos < NestedVal::getChildrenSize(&nodeList); ++pos) {
        auto node = NestedVal::getChildVal(&nodeList, pos);
        if (node->getDataType()->getLogicalTypeID() != common::LogicalTypeID::STRING) {
            throw BinderException("wrong node name type");
        }
        /*we can check whether the nodetable is exist here*/
        nodeFilter.insert(node->getValue<std::string>());
    }
    for (auto pos = 0u; pos < NestedVal::getChildrenSize(&relList); ++pos) {
        auto rel = NestedVal::getChildVal(&relList, pos);
        if (rel->getDataType()->getLogicalTypeID() != common::LogicalTypeID::STRING) {
            throw BinderException("wrong rel name type");
        }
        relFilter.insert(rel->getValue<std::string>());
    }
    auto bindData = std::make_unique<SsspBindData>(context,
        input->inputs[0].getValue<std::string>(), input->inputs[1].getValue<std::string>(),
        input->inputs[2].getValue<std::string>(), input->inputs[3].getValue<std::string>(),
        direction, maxHop, mode, numThreads, nodeFilter, relFilter, std::move(returnTypes),
        std::move(returnColumnNames), 1 /* one line of results */);
    return bindData;
}

function_set GraphBspSsspFunction::getFunctionSet() {
    function_set functionSet;
    functionSet.push_back(std::make_unique<TableFunction>(name, tableFunc, bindFunc,
        initSharedState, initEmptyLocalState,
        std::vector<LogicalTypeID>{LogicalTypeID::STRING, LogicalTypeID::STRING,
            LogicalTypeID::STRING, LogicalTypeID::STRING, LogicalTypeID::STRING,
            LogicalTypeID::INT64, LogicalTypeID::STRING, LogicalTypeID::INT64, LogicalTypeID::LIST,
            LogicalTypeID::LIST}));
    return functionSet;
}

} // namespace function
} // namespace kuzu