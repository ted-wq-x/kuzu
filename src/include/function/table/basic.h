#include <sys/mman.h>

#include "binder/binder.h"
#include "binder/expression/expression_util.h"
#include "binder/expression/property_expression.h"
#include "binder/expression_visitor.h"
#include "catalog/catalog.h"
#include "common/cast.h"
#include "common/data_chunk/sel_vector.h"
#include "common/exception/binder.h"
#include "common/string_format.h"
#include "common/system_message.h"
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

static std::vector<uint8_t> initNumTable() {
    std::vector<uint8_t> numTable(67);
    for (uint8_t i = 0; i < 64; ++i) {
        uint64_t now = (1ULL << i);
        numTable[now % 67] = i;
    }
    return numTable;
}

const static std::vector<uint8_t> numTable = initNumTable();

static offset_t getNodeOffset(uint32_t blockID, uint64_t pos) {
    // bitset的每一个元素实际标记了64个元素是否存在,i<<6是该元素offset的起点位置，加上pos%67就是实际offset的值
    return (blockID << 6) + numTable[pos % 67];
}

/**
 * 分配任务并且任务是64对齐的
 */
static std::pair<uint32_t, uint32_t> distributeTasks(uint32_t tableSize, uint32_t numThreads,
    uint32_t tid) {
    uint32_t alignedTableSize = (tableSize + 63) & ~63;
    uint32_t l = (alignedTableSize * tid / numThreads) & ~63;
    uint32_t r = (alignedTableSize * (tid + 1) / numThreads) & ~63;
    if (r > tableSize) {
        r = tableSize;
    }
    return {l, r};
}

/**
 * 通过这个bitset降低访问内存的开销
 */
class BitSet {
public:
    BitSet() : bits(0) {}
    BitSet(size_t size) : bits((size + 63) / 64) {}

    void set(size_t index) { bits[index / 64] |= (1ULL << (index % 64)); }

    bool test(size_t index) const { return bits[index / 64] & (1ULL << (index % 64)); }
    bool testAndReset(size_t index) {
        uint64_t& x = bits[index / 64];
        auto y = (1ULL << (index % 64));
        auto ans = x & y;
        if (ans) {
            x &= ~y;
        }
        return ans;
    }

    void resize(size_t size) { bits.resize((size + 63) / 64); }


private:
    // right<=63
    inline uint64_t preserve_range(uint64_t value, int left, int right) {
        // 创建掩码，保留 left 位（包含）到 right 位（不包含）
        uint64_t mask = ((1ULL << (right - left)) - 1) << (left);
        return value & mask;
    }
    // 创建掩码，保留 left 位（包含）到 right 位（包含）
    inline uint64_t preserve_range_include(uint64_t value, int left) {
        uint64_t mask = (~0ULL << left);
        return value & mask;
    }

    inline void range(size_t blockId, uint64_t now, std::vector<size_t>& result) {
        while (now) {
            auto pos = now ^ (now & (now - 1));
            auto offset = getNodeOffset(blockId, pos);
            result.push_back(offset);
            now ^= pos;
        }
    }

public:
    // include,exclude
    std::vector<size_t> range(size_t l, size_t r) {
        std::vector<size_t> result;
        auto startBlock = l >> 6;
        auto endBlock = r >> 6;
        auto startOffset = l % 64;
        auto endOffset = r % 64;

        if (startBlock == endBlock) {
            auto value = preserve_range(bits[startBlock], startOffset, endOffset);
            range(startBlock, value, result);
        } else {
            auto value = preserve_range_include(bits[startBlock], startOffset);
            range(startBlock, value, result);
            for (auto i = startBlock + 1; i < endBlock; ++i) {
                range(i, bits[i], result);
            }
            value = preserve_range(bits[endBlock], 0, endOffset);
            range(endBlock, value, result);
        }

        return result;
    }

    void resetMark() { std::fill(bits.begin(), bits.end(), 0); }

private:
    std::vector<uint64_t> bits;
};

class InternalIDBitSet {
public:
    InternalIDBitSet(Catalog* catalog, storage::StorageManager* storage,
        transaction::Transaction* tx) {
        auto nodeTableIDs = catalog->getNodeTableIDs(tx);
        auto maxNodeTableID = *std::max_element(nodeTableIDs.begin(), nodeTableIDs.end()) + 1;
        nodeIDMark.resize(maxNodeTableID);
        blockFlags.resize(maxNodeTableID);
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto size = (nodeTable->getNumTuples(tx) + 63) >> 6;
            nodeIDMark[tableID].reserve(size);
            nodeIDMark[tableID].resize(size, 0);
            blockFlags[tableID].resize(size);
        }
    }

    bool isVisited(internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        return (nodeIDMark[nodeID.tableID][block] >> pos) & 1;
    }

    void markVisited(internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& t = nodeIDMark[nodeID.tableID][block];
        if (t == 0) {
            markFlag(nodeID.tableID, block);
        }
        t |= (1ULL << pos);
    }

    void markVisited(uint32_t tableID, uint32_t pos, uint64_t value) {
        auto& t = nodeIDMark[tableID][pos];
        if (t == 0) {
            markFlag(tableID, pos);
        }
        t |= value;
    }

    uint64_t getAndReset(uint32_t tableID, uint32_t pos) {
        auto& val = nodeIDMark[tableID][pos];
        uint64_t temp = val;
        val = 0;
        return temp;
    }

    bool markIfUnVisitedReturnVisited(InternalIDBitSet& visitedBitSet, internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        if ((visitedBitSet.nodeIDMark[nodeID.tableID][block] >> pos) & 1) {
            return true;
        } else {
            auto& t = nodeIDMark[nodeID.tableID][block];
            if (t == 0) {
                markFlag(nodeID.tableID, block);
            }
            t |= (1ULL << pos);

            return false;
        }
    }

    uint32_t getTableNum() { return nodeIDMark.size(); }

    uint32_t getTableSize(uint32_t tableID) { return nodeIDMark[tableID].size(); }

    void resetFlag() {
        for (auto& item : blockFlags) {
            item.resetMark();
        }
    }

private:
    void markFlag(table_id_t tableID, uint32_t blockID) { blockFlags[tableID].set(blockID); }

public:
    std::vector<BitSet> blockFlags;

private:
    // tableID==>blockID==>mark
    std::vector<std::vector<uint64_t>> nodeIDMark;
};

struct alignas(64) PaddedAtomic {
    std::atomic_uint32_t value;

    // 默认构造函数
    PaddedAtomic() : value(0) {}

    // 禁用拷贝构造函数和赋值操作符
    PaddedAtomic(const PaddedAtomic&) = delete;
    PaddedAtomic& operator=(const PaddedAtomic&) = delete;

    // 移动构造函数和赋值操作符
    PaddedAtomic(PaddedAtomic&& other) noexcept : value(other.value.load()) {}
    PaddedAtomic& operator=(PaddedAtomic&& other) noexcept {
        if (this != &other) {
            value.store(other.value.load());
        }
        return *this;
    }
};
/**
 * 将内存分配按照tableID划分,cpu cache更加优化,并发测试能够提升qps
 * 测试过直接按block拿性能最差
 * 直接new并发性能更好,在20并发下qps有35,而使用pool只有23.但是单跑性能很差,因为有析构函数的存在
 * 在使用mimelloc开启大页后,mmap+MADV_HUGEPAGE qps不如 直接new大数组
 */
struct MemoryPoolUint32 {
    ~MemoryPoolUint32() { delete[] poolMemory; }
    static constexpr uint32_t UINT32_BLOCK_SIZE = 64 * sizeof(uint32_t);
    constexpr static std::size_t huge_page_size = 1 << 21; // 2 MiB
    size_t round_to_huge_page_size(size_t n) {
        return (((n - 1) / huge_page_size) + 1) * huge_page_size;
    }
    void init(uint32_t numBlocks, std::vector<uint64_t> tableBlockNum) {
        // 不初始化为0,因为分配的是虚拟内存,故内存占用不大
        // 不使用mmap,是为了降低page fault

#if defined(__linux__) && !defined(__aarch64__)
        auto size = round_to_huge_page_size(numBlocks * UINT32_BLOCK_SIZE);
        auto arr = aligned_alloc(huge_page_size, size);
        madvise(arr, size, MADV_HUGEPAGE);
        poolMemory = static_cast<uint32_t*>(arr);
#else
        poolMemory = new uint32_t[numBlocks * 64];
#endif

        uint64_t index = 0;
        atomicUsedBlocks.resize(tableBlockNum.size());
        for (auto i = 0u; i < tableBlockNum.size(); ++i) {
            atomicUsedBlocks[i].value = index;
            index += tableBlockNum[i];
        }
    }

    uint32_t* allocateAtomic(table_id_t tableID) {
        auto index = atomicUsedBlocks[tableID].value.fetch_add(1, std::memory_order_relaxed);
        auto ans = poolMemory + index * 64;
        // 如果使用mmap,这里触发物理内存分配,这里的调用能显著提升性能,原因未知
        //        ans[0] = 0;
        std::memset(ans, 0, UINT32_BLOCK_SIZE);
        return ans;
    }

    uint32_t* poolMemory;
    std::vector<PaddedAtomic> atomicUsedBlocks;
};

struct Count {
    uint32_t* count = nullptr;

    // 以下方法都是unsafe的,需要自己保证count!=nullptr
    void merge(const Count& other) {
        for (auto i = 0u; i < 64; ++i) {
            count[i] += other.count[i];
        }
    }

    uint64_t mark() {
        if (count == nullptr) {
            return 0;
        }
        uint64_t ans = 0;
        for (auto i = 0u; i < 64u; ++i) {
            if (count[i] != 0) {
                ans |= (1ULL << i);
            }
        }
        return ans;
    }

    // 因为是两侧访问,所以会被重复访问,故不能直接设置null
    void reset() { std::memset(count, 0, MemoryPoolUint32::UINT32_BLOCK_SIZE); }
};

class InternalIDCountBitSet {
public:
    InternalIDCountBitSet(Catalog* catalog, storage::StorageManager* storage,
        transaction::Transaction* tx) {
        auto nodeTableIDs = catalog->getNodeTableIDs(tx);
        auto maxNodeTableID = *std::max_element(nodeTableIDs.begin(), nodeTableIDs.end()) + 1;
        nodeIDMark.resize(maxNodeTableID);
        uint64_t numBlocks = 0;
        std::vector<uint64_t> tableBlockNum;
        tableBlockNum.resize(maxNodeTableID);
        blockFlags.resize(maxNodeTableID);
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto size = (nodeTable->getNumTuples(tx) + 63) >> 6;
            numBlocks += size;
            nodeIDMark[tableID].reserve(size);
            nodeIDMark[tableID].resize(size);
            tableBlockNum[tableID] = size;
            blockFlags[tableID].resize(size);
        }
        pool.init(numBlocks, tableBlockNum);
    }

    bool isVisited(internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& temp = nodeIDMark[nodeID.tableID][block];
        return temp.count != nullptr && temp.count[pos] != 0;
    }

    void markVisited(internalID_t& nodeID, uint32_t count) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& value = nodeIDMark[nodeID.tableID][block];
        if (value.count == nullptr) {
            value.count = pool.allocateAtomic(nodeID.tableID);
        }
        value.count[pos] += count;

        markFlag(nodeID.tableID, block);
    }

    uint32_t getNodeValueCount(internalID_t& nodeID) const {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& value = nodeIDMark[nodeID.tableID][block];
        if (value.count == nullptr) {
            return 0;
        }
        return value.count[pos];
    }

    Count& getNodeValue(uint32_t tableID, uint32_t pos) { return nodeIDMark[tableID][pos]; }

    uint32_t getTableNum() const { return nodeIDMark.size(); }

    uint32_t getTableSize(uint32_t tableID) const { return nodeIDMark[tableID].size(); }

    void merge(uint32_t tableID, uint32_t pos, Count& other) {
        auto& value = nodeIDMark[tableID][pos];
        if (value.count == nullptr) {
            value.count = pool.allocateAtomic(tableID);
        }
        value.merge(other);
        markFlag(tableID, pos);
    }

    void markFlag(table_id_t tableID, uint32_t blockID) { blockFlags[tableID].set(blockID); }

    void resetFlag() {
        for (auto& item : blockFlags) {
            item.resetMark();
        }
    }

private:
    MemoryPoolUint32 pool;
    //  tableID==>blockID==>mark
    //    std::unique_ptr<Count*>
    std::vector<std::vector<Count>> nodeIDMark;

public:
    //  tableID==>block bitset
    // 内存增加不多,但效果明显
    std::vector<BitSet> blockFlags;
};

struct MemoryPoolUint8 {
    ~MemoryPoolUint8() { delete[] poolMemory; }
    static constexpr uint32_t UINT8_BLOCK_SIZE = 64 * sizeof(uint8_t);
    constexpr static std::size_t huge_page_size = 1 << 21; // 2 MiB
    size_t round_to_huge_page_size(size_t n) {
        return (((n - 1) / huge_page_size) + 1) * huge_page_size;
    }

    void init(uint32_t numBlocks, std::vector<uint64_t> tableBlockNum) {
#if defined(__linux__) && !defined(__aarch64__)
        auto size = round_to_huge_page_size(numBlocks * UINT8_BLOCK_SIZE);
        auto arr = aligned_alloc(huge_page_size, size);
        madvise(arr, size, MADV_HUGEPAGE);
        poolMemory = static_cast<uint8_t*>(arr);
#else
        poolMemory = new uint8_t[numBlocks * 64];
#endif

        uint64_t index = 0;
        atomicUsedBlocks.resize(tableBlockNum.size());
        for (auto i = 0u; i < tableBlockNum.size(); ++i) {
            atomicUsedBlocks[i].value = index;
            index += tableBlockNum[i];
        }
    }

    uint8_t* allocate(table_id_t tableID) {
        auto index = atomicUsedBlocks[tableID].value.fetch_add(1, std::memory_order_relaxed);
        auto ans = poolMemory + index * 64;
        // 初始化为0
        std::memset(ans, 0, UINT8_BLOCK_SIZE);
        return ans;
    }

    uint8_t* poolMemory;
    std::vector<PaddedAtomic> atomicUsedBlocks;
};

struct Dist {
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
        std::vector<uint64_t> tableBlockNum;
        tableBlockNum.resize(maxNodeTableID);
        for (auto tableID : nodeTableIDs) {
            auto nodeTable = storage->getTable(tableID)->ptrCast<storage::NodeTable>();
            auto size = (nodeTable->getNumTuples(tx) + 63) >> 6;
            numBlocks += size;
            nodeIDMark[tableID].reserve(size);
            nodeIDMark[tableID].resize(size);
            tableBlockNum[tableID] = size;
        }
        pool.init(numBlocks, tableBlockNum);
    }

    bool isVisited(const internalID_t& nodeID) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& temp = nodeIDMark[nodeID.tableID][block];
        return temp.dist != nullptr && temp.dist[pos] != 0;
    }

    void markVisited(const internalID_t& nodeID, uint32_t dist) {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& value = nodeIDMark[nodeID.tableID][block];
        if (value.dist == nullptr) {
            value.dist = pool.allocate(nodeID.tableID);
        }
        value.dist[pos] = dist;
    }

    uint32_t getNodeValueDist(const internalID_t& nodeID) const {
        uint64_t block = (nodeID.offset >> 6), pos = (nodeID.offset & 63);
        auto& value = nodeIDMark[nodeID.tableID][block];
        if (value.dist == nullptr) {
            return 0;
        }
        return value.dist[pos];
    }

    uint32_t getTableNum() const { return nodeIDMark.size(); }

    uint32_t getTableSize(const uint32_t tableID) const { return nodeIDMark[tableID].size(); }

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

struct Mission {
    table_id_t tableID;
    std::vector<offset_t> offsets;

    bool empty() { return offsets.empty(); }
    offset_t back() { return offsets.back(); }
    void emplace_back(offset_t offset_t) { offsets.emplace_back(offset_t); }
};
} // namespace function
} // namespace kuzu