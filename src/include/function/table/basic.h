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

    relTableInfos =
        std::make_shared<std::vector<std::pair<common::table_id_t, std::shared_ptr<RelTableInfo>>>>(
            makeRelTableInfos(&props, context, relLabels));

    if (!relColumnTypeIds) {
        std::vector<LogicalTypeID> relColumnTypes;
        relColumnTypes.push_back(LogicalTypeID::INTERNAL_ID);
        relColumnTypeIds = std::make_shared<std::vector<LogicalTypeID>>(relColumnTypes);
    }
}

} // namespace function
} // namespace kuzu