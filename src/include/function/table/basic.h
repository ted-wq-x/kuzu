#include <sys/mman.h>

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

class InternalIDBitSet {
public:
    InternalIDBitSet(Catalog* catalog, storage::StorageManager* storage,
        transaction::Transaction* tx) {
        auto nodeTableIDs = catalog->getNodeTableIDs(tx);
        auto maxNodeTableID = *std::max_element(nodeTableIDs.begin(), nodeTableIDs.end()) + 1;
        nodeIDMark.resize(maxNodeTableID);
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto size = (nodeTable->getNumTuples(tx) + 63) >> 6;
            nodeIDMark[tableID].reserve(size);
            nodeIDMark[tableID].resize(size, 0);
        }
    }

    inline bool isVisited(internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        return (nodeIDMark[nodeID.tableID][block] >> pos) & 1;
    }

    inline void markVisited(internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        nodeIDMark[nodeID.tableID][block] |= (1ULL << pos);
    }

    inline void markVisited(uint32_t tableID, uint32_t pos, uint64_t value) {
        nodeIDMark[tableID][pos] |= value;
    }

    inline uint64_t getAndReset(uint32_t tableID, uint32_t pos) {
        auto& val = nodeIDMark[tableID][pos];
        uint64_t temp = val;
        val = 0;
        return temp;
    }

    inline bool markIfUnVisitedReturnVisited(InternalIDBitSet& visitedBitSet,
        internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        if ((visitedBitSet.nodeIDMark[nodeID.tableID][block] >> pos) & 1) {
            return true;
        } else {
            nodeIDMark[nodeID.tableID][block] |= (1ULL << pos);
            return false;
        }
    }

    inline uint32_t getTableNum() { return nodeIDMark.size(); }

    inline uint32_t getTableSize(uint32_t tableID) { return nodeIDMark[tableID].size(); }

    inline static offset_t getNodeOffset(uint32_t blockID, uint64_t pos) {
        // bitset的每一个元素实际标记了64个元素是否存在,i<<6是该元素offset的起点位置，加上pos%67就是实际offset的值
        return (blockID << 6) + numTable[pos % 67];
    }

private:
    // tableID==>blockID==>mark
    std::vector<std::vector<uint64_t>> nodeIDMark;

    const static std::vector<uint8_t> numTable;
};

struct MemoryPoolUint32 {
    static constexpr uint32_t UINT32_BLOCK_SIZE = 64 * sizeof(uint32_t);

    ~MemoryPoolUint32() { munmap(memory, memorySize); }

    inline void init(uint32_t numBlocks) {
        memorySize = numBlocks * UINT32_BLOCK_SIZE;
        // 默认初始化为0
        memory = mmap(NULL, memorySize, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        poolMemory = static_cast<uint32_t*>(memory);
    }

    inline uint32_t* allocateAtomic() {
        auto index = atomicUsedBlocks.fetch_add(1, std::memory_order_relaxed);
        return poolMemory + index * 64;
    }

    inline uint32_t* allocate() {
        auto index = usedBlocks++;
        return poolMemory + index * 64;
    }

    uint32_t memorySize;
    uint32_t usedBlocks = 0;
    std::atomic_uint32_t atomicUsedBlocks = 0;
    void* memory;
    uint32_t* poolMemory;
};

struct Count {
    uint64_t mark = 0;
    uint32_t* count = nullptr;
    // 以下方法都是unsafe的,需要自己保证count!=nullptr
    inline void merge(Count& other) {
        mark |= other.mark;

        for (auto i = 0u; i < 64; ++i) {
            count[i] += other.count[i];
        }
    }

    inline void reset() {
        mark = 0;
        std::memset(count, 0, MemoryPoolUint32::UINT32_BLOCK_SIZE);
    }

    inline void free() { delete[] count; }
};

class InternalIDCountBitSet {
public:
    InternalIDCountBitSet(Catalog* catalog, storage::StorageManager* storage,
        transaction::Transaction* tx) {
        auto nodeTableIDs = catalog->getNodeTableIDs(tx);
        auto maxNodeTableID = *std::max_element(nodeTableIDs.begin(), nodeTableIDs.end()) + 1;
        nodeIDMark.resize(maxNodeTableID);
        uint32_t numBlocks = 0;
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto size = (nodeTable->getNumTuples(tx) + 63) >> 6;
            numBlocks += size;
            nodeIDMark[tableID].reserve(size);
            nodeIDMark[tableID].resize(size);
        }
        pool.init(numBlocks);
    }

    inline bool isVisited(internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        return (nodeIDMark[nodeID.tableID][block].mark >> pos) & 1;
    }

    inline void markVisited(internalID_t& nodeID, uint32_t count) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& value = nodeIDMark[nodeID.tableID][block];
        value.mark |= (1ULL << pos);
        if (value.count == nullptr) {
            value.count = pool.allocate();
        }
        value.count[pos] += count;
    }

    inline uint32_t getNodeValueCount(internalID_t& nodeID) const {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& value = nodeIDMark[nodeID.tableID][block];
        if (value.count == nullptr) {
            return 0;
        }
        return value.count[pos];
    }

    inline Count& getNodeValue(uint32_t tableID, uint32_t pos) { return nodeIDMark[tableID][pos]; }

    inline uint32_t getTableNum() const { return nodeIDMark.size(); }

    inline uint32_t getTableSize(uint32_t tableID) const { return nodeIDMark[tableID].size(); }

    inline void merge(uint32_t tableID, uint32_t pos, Count& other) {
        auto& value = nodeIDMark[tableID][pos];
        if (value.count == nullptr) {
            value.count = pool.allocateAtomic();
        }
        value.merge(other);
    }

private:
    MemoryPoolUint32 pool;
    // tableID==>blockID==>mark
    std::vector<std::vector<Count>> nodeIDMark;
};

struct MemoryPoolUint8 {
    static constexpr uint32_t UINT8_BLOCK_SIZE = 64 * sizeof(uint8_t);

    ~MemoryPoolUint8() { munmap(memory, memorySize); }

    inline void init(uint32_t numBlocks) {
        memorySize = numBlocks * UINT8_BLOCK_SIZE;
        // 默认初始化为0
        memory = mmap(NULL, memorySize, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        poolMemory = static_cast<uint8_t*>(memory);
    }

    inline uint8_t* allocate() {
        auto index = usedBlocks.fetch_add(1, std::memory_order_relaxed);
        return poolMemory + index * 64;
    }

    uint32_t memorySize;
    std::atomic_uint32_t usedBlocks = 0;
    void* memory;
    uint8_t* poolMemory;
};

struct Dist {
    uint64_t mark = 0;
    uint8_t* dist = nullptr;
};

class InternalIDDistBitSet {
public:
    InternalIDDistBitSet(Catalog* catalog, storage::StorageManager* storage,
        transaction::Transaction* tx) {
        auto nodeTableIDs = catalog->getNodeTableIDs(tx);
        auto maxNodeTableID = *std::max_element(nodeTableIDs.begin(), nodeTableIDs.end()) + 1;
        nodeIDMark.resize(maxNodeTableID);
        uint32_t numBlocks = 0;
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto size = (nodeTable->getNumTuples(tx) + 63) >> 6;
            numBlocks += size;
            nodeIDMark[tableID].reserve(size);
            nodeIDMark[tableID].resize(size);
        }
        pool.init(numBlocks);
    }

    inline bool isVisited(internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        return (nodeIDMark[nodeID.tableID][block].mark >> pos) & 1;
    }

    inline void markVisited(internalID_t& nodeID, uint32_t dist) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& value = nodeIDMark[nodeID.tableID][block];
        value.mark |= (1ULL << pos);
        if (value.dist == nullptr) {
            value.dist = pool.allocate();
        }
        value.dist[pos] = dist;
    }

    inline uint32_t getNodeValueDist(internalID_t& nodeID) const {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& value = nodeIDMark[nodeID.tableID][block];
        if (value.dist == nullptr) {
            return 0;
        }
        return value.dist[pos];
    }

    inline Dist& getNodeValue(uint32_t tableID, uint32_t pos) { return nodeIDMark[tableID][pos]; }

    inline uint32_t getTableNum() const { return nodeIDMark.size(); }

    inline uint32_t getTableSize(uint32_t tableID) const { return nodeIDMark[tableID].size(); }

private:
    MemoryPoolUint8 pool;
    // tableID==>blockID==>mark
    std::vector<std::vector<Dist>> nodeIDMark;
};

class RelTableInfo {
public:
    explicit RelTableInfo(transaction::Transaction* tx, storage::StorageManager* storage,
        Catalog* catalog, table_id_t tableID) {
        relTable = storage->getTable(tableID)->ptrCast<storage::RelTable>();
        auto relTableEntry = ku_dynamic_cast<TableCatalogEntry*, RelTableCatalogEntry*>(
            catalog->getTableCatalogEntry(tx, tableID));
        srcTableID = relTableEntry->getSrcTableID();
        dstTableID = relTableEntry->getDstTableID();
    }
    explicit RelTableInfo(transaction::Transaction* tx, storage::StorageManager* storage,
        Catalog* catalog, table_id_t tableID, std::vector<common::column_id_t> columnIDs)
        : columnIDs(std::move(columnIDs)) {
        relTable = storage->getTable(tableID)->ptrCast<storage::RelTable>();
        auto relTableEntry = ku_dynamic_cast<TableCatalogEntry*, RelTableCatalogEntry*>(
            catalog->getTableCatalogEntry(tx, tableID));
        srcTableID = relTableEntry->getSrcTableID();
        dstTableID = relTableEntry->getDstTableID();
    }
    storage::RelTable* relTable;
    std::vector<common::column_id_t> columnIDs;
    common::table_id_t srcTableID, dstTableID;
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

static void computeRelFilter(main::ClientContext* context, std::string& relFilterStr,
    std::unique_ptr<evaluator::ExpressionEvaluator>& relFilter,
    std::shared_ptr<std::vector<LogicalTypeID>>& relColumnTypeIds,
    std::shared_ptr<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>&
        relTableInfos) {
    std::unordered_set<std::string> relLabels;
    expression_vector props;
    if (!relFilterStr.empty()) {
        auto algoPara = parser::Parser::parseAlgoParams(relFilterStr);
        auto list = algoPara->getTableNames();
        relLabels.insert(list.begin(), list.end());

        if (algoPara->hasWherePredicate()) {
            auto [whereExpression, nbrNodeExp] = parseExpr(context, algoPara.get());
            // 确定属性的位置
            auto expressionCollector = binder::PropertyExprCollector();
            expressionCollector.visit(whereExpression);
            props =
                binder::ExpressionUtil::removeDuplication(expressionCollector.getPropertyExprs());

            auto schema = planner::Schema();
            schema.createGroup();
            schema.insertToGroupAndScope(nbrNodeExp, 0); // nbr node id
            for (auto& prop : props) {
                schema.insertToGroupAndScope(prop, 0);
            }
            processor::ExpressionMapper expressionMapper(&schema);
            relFilter = expressionMapper.getEvaluator(whereExpression);

            std::vector<LogicalTypeID> relColumnTypes;
            for (const auto& item : schema.getExpressionsInScope()) {
                relColumnTypes.push_back(item->getDataType().getLogicalTypeID());
            }

            relColumnTypeIds = std::make_shared<std::vector<LogicalTypeID>>(relColumnTypes);
        }
    }

    relTableInfos =
        std::make_shared<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>(
            makeRelTableInfos(&props, context, relLabels));

    if (!relColumnTypeIds) {
        std::vector<LogicalTypeID> relColumnTypes;
        relColumnTypes.push_back(LogicalTypeID::INTERNAL_ID);
        relColumnTypeIds = std::make_shared<std::vector<LogicalTypeID>>(relColumnTypes);
    }
}

static nodeID_t getNodeID(main::ClientContext* context, std::string tableName,
    std::string primaryKey) {
    auto catalog = context->getCatalog();
    auto tx = context->getTx();
    auto storage = context->getStorageManager();
    if (tableName.empty()) {
        auto nodeTableIDs = catalog->getNodeTableIDs(tx);
        std::vector<nodeID_t> nodeIDs;
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto offset = getOffset(tx, nodeTable, primaryKey);
            if (offset != INVALID_OFFSET) {
                nodeIDs.emplace_back(offset, tableID);
            }
        }
        if (nodeIDs.size() != 1) {
            throw RuntimeException("Invalid primary key");
        }
        return nodeIDs.back();
    } else {
        auto tableID = catalog->getTableID(tx, tableName);
        auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
        auto offset = getOffset(tx, nodeTable, primaryKey);
        if (offset == INVALID_OFFSET) {
            throw RuntimeException("Invalid primary key");
        }
        return nodeID_t{offset, tableID};
    }
}

} // namespace function
} // namespace kuzu