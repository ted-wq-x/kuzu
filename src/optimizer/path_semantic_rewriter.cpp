#include "optimizer/path_semantic_rewriter.h"

#include "binder/expression/path_expression.h"
#include "binder/expression/property_expression.h"
#include "binder/expression/scalar_function_expression.h"
#include "binder/expression_visitor.h"
#include "catalog/catalog.h"
#include "common/exception/internal.h"
#include "function/built_in_function_utils.h"
#include "function/path/vector_path_functions.h"
#include "function/scalar_function.h"
#include "main/client_context.h"
#include "planner/operator/extend/logical_extend.h"
#include "planner/operator/logical_filter.h"
#include "planner/operator/logical_hash_join.h"
using namespace kuzu::common;
using namespace kuzu::planner;

namespace kuzu {
namespace optimizer {

void PathSemanticRewriter::rewrite(planner::LogicalPlan* plan) {
    auto root = plan->getLastOperator();
    visitOperator(root, nullptr, 0);
    if (hasRecursive) {
        topOp->setChild(replaceIndex, appendPathSemanticFilter(topOp->getChild(replaceIndex)));
    }
}

void PathSemanticRewriter::visitOperator(const std::shared_ptr<planner::LogicalOperator>& op,
    const std::shared_ptr<planner::LogicalOperator>& parent, int index) {
    for (auto i = 0u; i < op->getNumChildren(); ++i) {
        visitOperator(op->getChild(i), op, i);
    }
    auto result = op;
    switch (op->getOperatorType()) {
    case planner::LogicalOperatorType::HASH_JOIN:
        result = visitHashJoinReplace(op);
        if (hasRecursive) {
            topOp = parent;
            replaceIndex = index;
        }
        break;
    case planner::LogicalOperatorType::CROSS_PRODUCT:
        if (hasRecursive) {
            topOp = parent;
            replaceIndex = index;
        }
        break;
    default:
        break;
    }
    if (parent != nullptr) {
        parent->setChild(index, result);
    }
}

std::string semanticSwitch(const common::PathSemantic& semantic) {
    switch (semantic) {
    case common::PathSemantic::TRAIL:
        return function::IsTrailFunction::name;
    case common::PathSemantic::ACYCLIC:
        return function::IsACyclicFunction::name;
    default:
        return std::string();
    }
}

bool checkPattern(const common::PathSemantic& semantic, int nodeCount, int relCount) {
    switch (semantic) {
        case common::PathSemantic::TRAIL:
            return relCount > 1;
        case common::PathSemantic::ACYCLIC:
            return nodeCount > 1;
        default:
            return false;
    }
}

std::shared_ptr<LogicalOperator> PathSemanticRewriter::visitHashJoinReplace(
    std::shared_ptr<LogicalOperator> op) {
    auto hashJoin = (LogicalHashJoin*)op.get();
    auto schema = hashJoin->getSchema();
    auto exprs = schema->getExpressionsInScope();

    for (auto expr : exprs) {
        if (expr->dataType.getLogicalTypeID() == LogicalTypeID::RECURSIVE_REL) {
            hasRecursive = true;
            return op;
        }
    }
    binder::expression_vector patterns;
    int nodeCount = 0, relCount = 0;
    std::unordered_set<std::string> nameSet;
    for (auto expr : exprs) {
        if (expr->expressionType == ExpressionType::PROPERTY) {
            auto rawName = expr->constCast<binder::PropertyExpression>().getRawVariableName();
            if(nameSet.contains(rawName)){
                continue;
            }
            nameSet.insert(rawName);
            auto scopeExprVector = context->getBinder()->getBinderScope().getExpressions();
            for (auto scopeExpr : scopeExprVector) {
                if (scopeExpr->toString() == rawName) {
                    if (scopeExpr->dataType.getLogicalTypeID() == LogicalTypeID::NODE) {
                        auto& nodeExpr = scopeExpr->constCast<binder::NodeExpression>();
                        std::vector<catalog::TableCatalogEntry*> nodeEntries(nodeExpr.getEntries());
                        auto queryNode =
                            make_shared<binder::NodeExpression>(LogicalType(LogicalTypeID::NODE),
                                nodeExpr.getUniqueName(), nodeExpr.getVariableName(), nodeEntries);

                        queryNode->setInternalID(nodeExpr.getInternalID()->copy());
                        queryNode->setLabelExpression(nodeExpr.getLabelExpression());
                        queryNode->addPropertyExpression(nodeExpr.getInternalID()->toString(),
                            nodeExpr.getInternalID()->copy());
                        queryNode->setAlias(nodeExpr.getVariableName());
                        std::vector<std::string> fieldNames;
                        std::vector<LogicalType> fieldTypes;
                        fieldNames.emplace_back(InternalKeyword::ID);
                        fieldNames.emplace_back(InternalKeyword::LABEL);
                        fieldTypes.push_back(queryNode->getInternalID()->getDataType().copy());
                        fieldTypes.push_back(queryNode->getLabelExpression()->getDataType().copy());
                        for (auto& expression : queryNode->getPropertyExprsRef()) {
                            auto prop =
                                ku_dynamic_cast<binder::PropertyExpression*>(expression.get());
                            fieldNames.emplace_back(prop->getPropertyName());
                            fieldTypes.emplace_back(prop->dataType.copy());
                        }
                        auto extraInfo = std::make_unique<StructTypeInfo>(fieldNames, fieldTypes);
                        queryNode->setExtraTypeInfo(std::move(extraInfo));
                        patterns.push_back(queryNode);
                        nodeCount++;
                    } else if (scopeExpr->dataType.getLogicalTypeID() == LogicalTypeID::REL) {
                        auto& relExpr = scopeExpr->constCast<binder::RelExpression>();
                        std::vector<catalog::TableCatalogEntry*> relTableEntries(
                            relExpr.getEntries());
                        auto queryRel =
                            make_shared<binder::RelExpression>(LogicalType(LogicalTypeID::REL),
                                relExpr.getUniqueName(), relExpr.getVariableName(), relTableEntries,
                                relExpr.getSrcNode(), relExpr.getDstNode(),
                                relExpr.getDirectionType(), QueryRelType::NON_RECURSIVE);
                        queryRel->setAlias(relExpr.getVariableName());
                        queryRel->setLabelExpression(relExpr.getLabelExpression());

                        queryRel->addPropertyExpression(relExpr.getInternalIDProperty()->toString(),
                            relExpr.getInternalIDProperty()->copy());
                        std::vector<StructField> fields;
                        fields.emplace_back(InternalKeyword::SRC, LogicalType::INTERNAL_ID());
                        fields.emplace_back(InternalKeyword::DST, LogicalType::INTERNAL_ID());
                        // Bind internal expressions.
                        fields.emplace_back(InternalKeyword::LABEL,
                            queryRel->getLabelExpression()->getDataType().copy());
                        // Bind properties.
                        for (auto& expression : queryRel->getPropertyExprsRef()) {
                            auto& prop = expression->constCast<binder::PropertyExpression>();
                            fields.emplace_back(prop.getPropertyName(), prop.getDataType().copy());
                        }
                        auto extraInfo = std::make_unique<StructTypeInfo>(std::move(fields));
                        queryRel->setExtraTypeInfo(std::move(extraInfo));
                        patterns.push_back(queryRel);
                        relCount++;
                    }
                }
            }
        }
    }
    if (!patterns.empty()) {
        auto pathName = context->getBinder()->getInternalPathName();
        auto pathExpression = context->getBinder()->createPath(pathName, patterns);
        auto catalog = context->getCatalog();
        auto transaction = context->getTx();
        auto semanticFunctionName =
            semanticSwitch(context->getClientConfig()->recursivePatternSemantic);
        if (semanticFunctionName.empty() ||
            !checkPattern(context->getClientConfig()->recursivePatternSemantic, nodeCount,
                         relCount)) {
            return op;
        }

        auto resultOp = op;
        // append is_trail or is_acyclic function filter

        std::vector<LogicalType> childrenTypes;
        childrenTypes.push_back(pathExpression->getDataType().copy());

        auto bindExpr = binder::expression_vector{pathExpression};
        auto functions = catalog->getFunctions(transaction);
        auto function = function::BuiltInFunctionsUtils::matchFunction(transaction,
            semanticFunctionName, childrenTypes, functions)
                            ->ptrCast<function::ScalarFunction>()
                            ->copy();

        std::unique_ptr<function::FunctionBindData> bindData;
        {
            if (function.bindFunc) {
                bindData = function.bindFunc({bindExpr, &function, context});
            } else {
                bindData = std::make_unique<function::FunctionBindData>(
                    LogicalType(function.returnTypeID));
            }
        }

        auto uniqueExpressionName =
            binder::ScalarFunctionExpression::getUniqueName(function.name, bindExpr);
        auto filterExpression =
            std::make_shared<binder::ScalarFunctionExpression>(ExpressionType::FUNCTION,
                std::move(function), std::move(bindData), bindExpr, uniqueExpressionName);
        auto printInfo = std::make_unique<OPPrintInfo>();

        auto filter = std::make_shared<LogicalFilter>(
            std::static_pointer_cast<binder::Expression>(filterExpression), resultOp);
        filter->computeFlatSchema();
        resultOp = filter;

        return resultOp;
    }
    return op;
    //    return appendPathSemanticFilter(op);
}

std::shared_ptr<LogicalOperator> PathSemanticRewriter::appendPathSemanticFilter(
    const std::shared_ptr<LogicalOperator> op) {
    // get path expression from binder
    auto pathExpressions = context->getBinder()->findPathExpressionInScope();
    auto catalog = context->getCatalog();
    auto transaction = context->getTx();
    auto semanticFunctionName =
        semanticSwitch(context->getClientConfig()->recursivePatternSemantic);
    if (semanticFunctionName.empty()) {
        return op;
    }

    auto resultOp = op;
    // append is_trail or is_acyclic function filter
    for (auto& expr : pathExpressions) {

        std::vector<LogicalType> childrenTypes;
        childrenTypes.push_back(expr->getDataType().copy());

        auto bindExpr = binder::expression_vector{expr};
        auto functions = catalog->getFunctions(transaction);
        auto function = function::BuiltInFunctionsUtils::matchFunction(transaction,
            semanticFunctionName, childrenTypes, functions)
                            ->ptrCast<function::ScalarFunction>()
                            ->copy();

        std::unique_ptr<function::FunctionBindData> bindData;
        {
            if (function.bindFunc) {
                bindData = function.bindFunc({bindExpr, &function, context});
            } else {
                bindData = std::make_unique<function::FunctionBindData>(
                    LogicalType(function.returnTypeID));
            }
        }

        auto uniqueExpressionName =
            binder::ScalarFunctionExpression::getUniqueName(function.name, bindExpr);
        auto filterExpression =
            std::make_shared<binder::ScalarFunctionExpression>(ExpressionType::FUNCTION,
                std::move(function), std::move(bindData), bindExpr, uniqueExpressionName);

        auto filter = std::make_shared<LogicalFilter>(
            std::static_pointer_cast<binder::Expression>(filterExpression), resultOp);
        filter->computeFlatSchema();
        resultOp = filter;
    }
    return resultOp;
}
std::shared_ptr<planner::LogicalOperator> PathSemanticRewriter::visitCrossProductReplace(
    std::shared_ptr<planner::LogicalOperator> op) {
    return appendPathSemanticFilter(op);
}

} // namespace optimizer
} // namespace kuzu
