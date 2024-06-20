#include <thread>

#include "binder/binder.h"
#include "binder/expression/expression_util.h"
#include "binder/expression/property_expression.h"
#include "binder/expression_visitor.h"
#include "catalog/catalog.h"
#include "common/cast.h"
#include "common/data_chunk/sel_vector.h"
#include "common/exception/binder.h"
#include "common/types/value/nested.h"
#include "expression_evaluator/expression_evaluator.h"
#include "function/table/call_functions.h"
#include "main/database_manager.h"
#include "parser/parser.h"
#include "parser/query/graph_pattern/rel_pattern.h"
#include "planner/operator/schema.h"
#include "processor/expression_mapper.h"
#include "storage/store/node_table.h"
#include "storage/store/rel_table.h"
using namespace kuzu::catalog;
using namespace kuzu::common;
using namespace kuzu::binder;

namespace kuzu {
namespace function {

class RelTableInfo {
public:
    explicit RelTableInfo(transaction::Transaction* tx, storage::StorageManager* storage,
        catalog::Catalog* catalog, table_id_t tableID, std::vector<common::column_id_t> columnIDs)
        : columnIDs(std::move(columnIDs)) {
        relTable = storage->getTable(tableID)->ptrCast<storage::RelTable>();
        auto relTableEntry =
            ku_dynamic_cast<catalog::TableCatalogEntry*, catalog::RelTableCatalogEntry*>(
                catalog->getTableCatalogEntry(tx, tableID));
        srcTableID = relTableEntry->getSrcTableID();
        dstTableID = relTableEntry->getDstTableID();
    }
    storage::RelTable* relTable;
    std::vector<common::column_id_t> columnIDs;
    common::table_id_t srcTableID, dstTableID;
};

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

class SharedData {
public:
    explicit SharedData(const KhopBindData* bindData) {
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
    catalog::Catalog* catalog;
    storage::MemoryManager* mm;
    transaction::Transaction* tx;
    storage::StorageManager* storage;

    std::vector<std::vector<BSet>> nodeIDMark, nextMark;
    std::vector<std::vector<nodeID_t>> currentFrontier, nextFrontier;
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>
        reltables;
    main::ClientContext* context;
    evaluator::ExpressionEvaluator* relFilter;
    std::shared_ptr<std::vector<LogicalTypeID>> relColumnTypeIds;
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
    auto& primaryKeyType = nodeTable->getColumn(nodeTable->getPKColumnID())->getDataType();
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
static void scanTask(SharedData& sharedData, std::shared_ptr<storage::RelTableScanState> readState,
    common::ValueVector& srcVector, std::shared_ptr<RelTableInfo> info,
    std::vector<std::vector<uint64_t>>& threadMark, RelDataDirection relDataDirection,
    int64_t& threadResult, uint32_t tid, bool isFinal, const processor::ResultSet& resultSet,
    evaluator::ExpressionEvaluator* relExpr) {
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
        for (auto i = 0u; i < bindData->numThreads; i++) {
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

static std::pair<std::shared_ptr<Expression>, std::shared_ptr<Expression>> parseExpr(
    main::ClientContext* context, const parser::AlgoParameter* algoParameter) {
    binder::Binder binder(context);
    auto recursiveInfo = parser::RecursiveRelPatternInfo();
    auto relPattern = parser::RelPattern(algoParameter->getVariableName(),
        algoParameter->getTableNames(), QueryRelType::NON_RECURSIVE, parser::ArrowDirection::BOTH,
        std::vector<parser::s_parsed_expr_pair>{}, std::move(recursiveInfo));

    auto nodeTableIDs = context->getCatalog()->getNodeTableIDs(context->getTx());
    auto leftNode = std::make_shared<NodeExpression>(LogicalType(LogicalTypeID::NODE), "wq_left",
        "", nodeTableIDs);
    auto rightNode = std::make_shared<NodeExpression>(LogicalType(LogicalTypeID::NODE), "wq_right",
        "", nodeTableIDs);
    rightNode->setInternalID(
        PropertyExpression::construct(LogicalType::INTERNAL_ID(), InternalKeyword::ID, *rightNode));

    auto qg = binder::QueryGraph();
    auto relExpression = binder.bindQueryRel(relPattern, leftNode, rightNode, qg);

    return {binder.bindWhereExpression(*algoParameter->getWherePredicate()),
        rightNode->getInternalID()};
}

static std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>> makeRelTableInfos(
    const binder::expression_vector* props, main::ClientContext* context,
    std::unordered_set<std::string>& relLabels) {
    auto catalog = context->getCatalog();
    auto transaction = context->getTx();
    std::vector<common::table_id_t> tableIDs;
    if (!relLabels.empty()) {
        for (const auto& label : relLabels) {
            auto id = catalog->getTableID(transaction, label);
            tableIDs.push_back(id);
        }
    } else {
        tableIDs = catalog->getRelTableIDs(transaction);
    }
    std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>> reltables;

    for (const auto& tableID : tableIDs) {
        std::vector<column_id_t> columnIDs;
        for (const auto& prop : *props) {
            auto property = *ku_dynamic_cast<Expression*, PropertyExpression*>(prop.get());
            if (!property.hasPropertyID(tableID)) {
                columnIDs.push_back(UINT32_MAX);
            } else {
                auto propertyID = property.getPropertyID(tableID);
                auto tableEntry = catalog->getTableCatalogEntry(transaction, tableID);
                columnIDs.push_back(tableEntry->getColumnID(propertyID));
            }
        }
        auto relTableInfo = std::make_shared<RelTableInfo>(transaction,
            context->getStorageManager(), catalog, tableID, columnIDs);
        reltables.emplace_back(tableID, std::move(relTableInfo));
    }

    return reltables;
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
    // filter
    std::unordered_set<std::string> relLabels;
    expression_vector props;
    if (!relFilterStr.empty()) {
        auto algoPara = parser::Parser::parseAlgoParams(relFilterStr);

        auto list = algoPara->getTableNames();
        relLabels.insert(list.begin(), list.end());

        if (algoPara->hasWherePredicate()) {

            auto [whereExpression, nbrNodeExp] = parseExpr(context, algoPara.get());
            // 确定属性的位置
            auto expressionCollector = binder::ExpressionCollector();
            props = binder::ExpressionUtil::removeDuplication(
                expressionCollector.collectPropertyExpressions(whereExpression));

            auto schema = planner::Schema();
            schema.createGroup();
            schema.insertToGroupAndScope(nbrNodeExp, 0); // nbr node id
            for (auto& prop : props) {
                schema.insertToGroupAndScope(prop, 0);
            }
            relFilter = processor::ExpressionMapper::getEvaluator(whereExpression, &schema);

            std::vector<LogicalTypeID> relColumnTypes;
            for (const auto& item : schema.getExpressionsInScope()) {
                relColumnTypes.push_back(item->getDataType().getLogicalTypeID());
            }

            relColumnTypeIds = std::make_shared<std::vector<LogicalTypeID>>(relColumnTypes);
        }
    }

    auto relTableInfos =
        std::make_shared<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>(
            makeRelTableInfos(&props, context, relLabels));

    if (!relColumnTypeIds) {
        std::vector<LogicalTypeID> relColumnTypes;
        relColumnTypes.push_back(LogicalTypeID::INTERNAL_ID);
        relColumnTypeIds = std::make_shared<std::vector<LogicalTypeID>>(relColumnTypes);
    }

    auto bindData =
        std::make_unique<KhopBindData>(context, input->inputs[0].getValue<std::string>(),
            input->inputs[1].getValue<std::string>(), direction, maxHop, mode, numThreads,
            std::move(returnTypes), std::move(returnColumnNames), 1 /* one line of results */,
            std::move(relFilter), std::move(relColumnTypeIds), std::move(relTableInfos));
    return bindData;
}

} // namespace function
} // namespace kuzu