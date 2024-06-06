#include <thread>

#include "catalog/catalog.h"
#include "common/exception/binder.h"
#include "common/types/value/nested.h"
#include "function/table/call_functions.h"
#include "main/database_manager.h"
#include "storage/store/node_table.h"
#include "storage/store/rel_table.h"
using namespace kuzu::catalog;
using namespace kuzu::common;

namespace kuzu {
namespace function {

struct KhopBindData : public CallTableFuncBindData {
    KhopBindData(main::ClientContext* context, std::string primaryKey, std::string tableName,
        std::string direction, int64_t maxHop, int64_t mode, int64_t numThreads,
        std::unordered_set<std::string> nodeFilter, std::unordered_set<std::string> relFilter,
        std::vector<LogicalType> returnTypes, std::vector<std::string> returnColumnNames,
        offset_t maxOffset)
        : CallTableFuncBindData{std::move(returnTypes), std::move(returnColumnNames), maxOffset},
          context(context), primaryKey(primaryKey), tableName(tableName), direction(direction),
          maxHop(maxHop), mode(mode), numThreads(numThreads), nodeFilter(std::move(nodeFilter)),
          relFilter(std::move(relFilter)) {}

    std::unique_ptr<TableFuncBindData> copy() const override {
        return std::make_unique<KhopBindData>(context, primaryKey, tableName, direction, maxHop,
            mode, numThreads, nodeFilter, relFilter, columnTypes, columnNames, maxOffset);
    }
    main::ClientContext* context;
    std::string primaryKey, tableName, direction;
    int64_t maxHop, mode, numThreads;
    std::unordered_set<std::string> nodeFilter, relFilter;
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

class RelTableInfo {
public:
    explicit RelTableInfo(transaction::Transaction* tx, storage::StorageManager* storage,
        catalog::Catalog* catalog, table_id_t tableID) {
        relTable = storage->getTable(tableID)->ptrCast<storage::RelTable>();
        auto relTableEntry =
            ku_dynamic_cast<catalog::TableCatalogEntry*, catalog::RelTableCatalogEntry*>(
                catalog->getTableCatalogEntry(tx, tableID));
        srcTableID = relTableEntry->getSrcTableID();
        dstTableID = relTableEntry->getDstTableID();
    }
    storage::RelTable* relTable;
    common::table_id_t srcTableID, dstTableID;
};

class SharedData {
public:
    explicit SharedData(const KhopBindData* bindData) {
        auto context = bindData->context;
        mm = context->getMemoryManager();
        direction = bindData->direction;
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
        nodeIDMark.resize(allTableSize);
        nextMark.resize(allTableSize);
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto nodeNum = nodeTable->getNumTuples(tx);
            nodeIDMark[tableID].resize((nodeNum + 63) >> 6);
            nextMark[tableID].resize((nodeNum + 63) >> 6);
        }
    }
    std::mutex mtx;
    uint32_t taskID;
    bool hasNodeFilter;
    std::string direction;
    catalog::Catalog* catalog;
    storage::MemoryManager* mm;
    transaction::Transaction* tx;
    storage::StorageManager* storage;
    std::unordered_set<table_id_t> nodeFilter;
    std::vector<std::vector<BSet>> nodeIDMark, nextMark;
    std::vector<std::vector<nodeID_t>> currentFrontier, nextFrontier;
    std::vector<std::pair<common::table_id_t, std::unique_ptr<RelTableInfo>>> reltables;
};

template<typename T>
static common::offset_t lookupPK(transaction::Transaction* tx, storage::NodeTable* nodeTable,
    const T key) {
    common::offset_t result;
    if (!nodeTable->getPKIndex()->lookup(tx, key, result)) {
        return INVALID_OFFSET;
    }
    return result;
}

static common::offset_t getOffset(transaction::Transaction* tx, storage::NodeTable* nodeTable,
    std::string primaryKey) {
    auto primaryKeyType = nodeTable->getColumn(nodeTable->getPKColumnID())->getDataType();
    switch (primaryKeyType.getPhysicalType()) {
    case PhysicalTypeID::UINT8: {
        uint8_t key = std::stoull(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::UINT16: {
        uint16_t key = std::stoull(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::UINT32: {
        uint32_t key = std::stoull(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::UINT64: {
        uint64_t key = std::stoull(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::INT8: {
        int8_t key = std::stoll(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::INT16: {
        int16_t key = std::stoll(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::INT32: {
        int32_t key = std::stoll(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::INT64: {
        int64_t key = std::stoll(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::INT128: {
        auto stolll = [](const std::string& str) -> int128_t {
            int128_t result = 0;
            for (auto i = 0u; i < str.size(); ++i) {
                result = result * 10 + (str[i] - '0');
            }
            return result;
        };
        int128_t key = stolll(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::STRING: {
        return lookupPK(tx, nodeTable, primaryKey);
    }
    case PhysicalTypeID::FLOAT: {
        float key = std::stof(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    case PhysicalTypeID::DOUBLE: {
        double key = std::stod(primaryKey);
        return lookupPK(tx, nodeTable, key);
    }
    default:
        throw RuntimeException("Unsupported primary key type");
    }
}

static nodeID_t getNodeID(SharedData& sharedData, const KhopBindData* bindData) {
    if (bindData->tableName.empty()) {
        auto nodeTableIDs = sharedData.catalog->getNodeTableIDs(sharedData.tx);
        std::vector<nodeID_t> nodeIDs;
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = sharedData.storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto offset = getOffset(sharedData.tx, nodeTable, bindData->primaryKey);
            if (offset != INVALID_OFFSET) {
                nodeIDs.emplace_back(offset, tableID);
            }
        }
        if (nodeIDs.size() != 1) {
            throw RuntimeException("Invalid primary key");
        }
        return nodeIDs.back();
    } else {
        auto tableID = sharedData.catalog->getTableID(sharedData.tx, bindData->tableName);
        auto offset = getOffset(sharedData.tx,
            sharedData.storage->getTable(tableID)->ptrCast<storage::NodeTable>(),
            bindData->primaryKey);
        return nodeID_t{offset, tableID};
    }
}
static void scanTask(SharedData& sharedData, storage::RelTableScanState& readState,
    storage::RelDataReadState* relDataReadState, common::ValueVector& srcVector,
    common::ValueVector& dstVector, common::DataChunkState* dstState, RelTableInfo* info,
    std::vector<std::vector<uint64_t>>& threadMark, RelDataDirection relDataDirection,
    int64_t& threadResult, uint32_t tid, bool isFinal) {
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
    for (auto currentNodeID : sharedData.currentFrontier[tid]) {
        if (prevTableID != currentNodeID.tableID) {
            break;
        }
        /*we can quick check here (part of checkIfNodeHasRels)*/
        srcVector.setValue<nodeID_t>(0, currentNodeID);
        relTable->initializeScanState(sharedData.tx, readState);
        while (readState.hasMoreToRead(sharedData.tx)) {
            relTable->scan(sharedData.tx, readState);
            KU_ASSERT(dstState->getSelVector().isUnfiltered());
            if (relDataDirection == RelDataDirection::FWD ||
                (relDataDirection == RelDataDirection::BWD && sharedData.direction == "in")) {
                threadResult += dstState->getSelVector().getSelSize();
            }
            for (auto i = 0u; i < dstState->getSelVector().getSelSize(); ++i) {
                auto nbrID = dstVector.getValue<nodeID_t>(i);
                uint64_t block = (nbrID.offset >> 6), pos = (nbrID.offset & 63);
                if ((sharedData.nodeIDMark[nbrID.tableID][block].value >> pos) & 1) {
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

static void tableFuncTask(SharedData& sharedData, int64_t& edgeResult, bool isFinal) {
    int64_t threadResult = 0;
    std::vector<std::vector<uint64_t>> threadMark;
    threadMark.resize((sharedData.nodeIDMark.size()));
    for (auto i = 0u; i < threadMark.size(); ++i) {
        threadMark[i].resize(sharedData.nodeIDMark[i].size(), 0);
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
        sharedData.mtx.lock();
        uint32_t tid = sharedData.taskID++;
        sharedData.mtx.unlock();
        if (tid >= sharedData.currentFrontier.size()) {
            break;
        }
        for (auto& [tableID, info] : sharedData.reltables) {
            if (sharedData.direction == "out" || sharedData.direction == "both") {
                scanTask(sharedData, readState, relDataReadState, srcVector, dstVector,
                    dstState.get(), info.get(), threadMark, RelDataDirection::FWD, threadResult,
                    tid, isFinal);
            }
            if (sharedData.direction == "in" || sharedData.direction == "both") {
                scanTask(sharedData, readState, relDataReadState, srcVector, dstVector,
                    dstState.get(), info.get(), threadMark, RelDataDirection::BWD, threadResult,
                    tid, isFinal);
            }
        }
    }
    for (auto tableID = 0u; tableID < threadMark.size(); ++tableID) {
        for (auto blockID = 0u; blockID < threadMark[tableID].size(); ++blockID) {
            if (!threadMark[tableID][blockID]) {
                continue;
            }
            pthread_mutex_lock(&sharedData.nextMark[tableID][blockID].mtx);
            sharedData.nextMark[tableID][blockID].value |= threadMark[tableID][blockID];
            pthread_mutex_unlock(&sharedData.nextMark[tableID][blockID].mtx);
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
        outputVector->setValue(pos, 0);
        outputVector->setNull(pos, false);
        return 1;
    }
    SharedData sharedData(bindData);
    auto nodeID = getNodeID(sharedData, bindData);
    sharedData.nodeIDMark[nodeID.tableID][(nodeID.offset >> 6)].value |=
        (1ULL << ((nodeID.offset & 63)));
    std::vector<nodeID_t> nextMission;
    nextMission.emplace_back(nodeID);
    sharedData.currentFrontier.emplace_back(std::move(nextMission));
    std::vector<int64_t> nodeResults, edgeResults;
    for (auto currentLevel = 0u; currentLevel < maxHop; ++currentLevel) {
        int64_t nodeResult = 0, edgeResult = 0;
        bool isFinal = (currentLevel == maxHop - 1);
        std::vector<std::thread> threads;
        sharedData.taskID = 0;
        for (auto i = 0u; i < numThreads; i++) {
            threads.emplace_back([&sharedData, &edgeResult, isFinal] {
                tableFuncTask(sharedData, edgeResult, isFinal);
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        for (auto tableID = 0u; tableID < sharedData.nextMark.size(); ++tableID) {
            auto prevNode = nodeID_t{0, 0};
            for (auto i = 0u; i < sharedData.nextMark[tableID].size(); ++i) {
                uint64_t now = sharedData.nextMark[tableID][i].value, pos = 0;
                sharedData.nodeIDMark[tableID][i].value |= now;
                while (now) {
                    pos = now ^ (now & (now - 1));
                    /*bitset的每一个元素实际标记了64个元素是否存在,i<<6是该元素offset的起点位置，加上pos%67就是实际offset的值*/
                    auto nowNode = nodeID_t{(i << 6) + numTable[pos % 67], tableID};
                    if ((!nextMission.empty() && ((prevNode.offset ^ nowNode.offset) >>
                                                     StorageConstants::NODE_GROUP_SIZE_LOG2))) {
                        nodeResult += nextMission.size();
                        sharedData.nextFrontier.emplace_back(std::move(nextMission));
                    }
                    nextMission.emplace_back(nowNode);
                    prevNode = nowNode;
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
        outputVector->setValue(pos, (bindData->mode ? nodeResults.back() : result));
    }
    outputVector->setNull(pos, false);
    return 1;
}

static std::unique_ptr<TableFuncBindData> rewriteBindFunc(main::ClientContext* context,
    TableFuncBindInput* input, std::string columnName) {
    std::vector<std::string> returnColumnNames;
    std::vector<LogicalType> returnTypes;
    returnColumnNames.emplace_back(columnName);
    returnTypes.emplace_back(*LogicalType::INT64());
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
    // filter
    auto numThreads = input->inputs[5].getValue<int64_t>();
    KU_ASSERT(numThreads >= 0);
    auto nodeList = input->inputs[6];
    auto relList = input->inputs[7];
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
        /*we can check whether the reltable is exist here*/
        relFilter.insert(rel->getValue<std::string>());
    }
    auto bindData = std::make_unique<KhopBindData>(context,
        input->inputs[0].getValue<std::string>(), input->inputs[1].getValue<std::string>(),
        direction, maxHop, mode, numThreads, nodeFilter, relFilter, std::move(returnTypes),
        std::move(returnColumnNames), 1 /* one line of results */);
    return bindData;
}

} // namespace function
} // namespace kuzu