#include "binder/expression/expression_util.h"
#include "binder/expression/node_expression.h"
#include "binder/expression/rel_expression.h"
#include "binder/expression_binder.h"
#include "catalog/catalog_entry/catalog_entry.h"
#include "catalog/catalog_entry/table_catalog_entry.h"
#include "common/types/value/value.h"
#include "function/rewrite_function.h"
#include "function/schema/vector_node_rel_functions.h"
#include "function/struct/vector_struct_functions.h"
using namespace kuzu::common;
using namespace kuzu::binder;

namespace kuzu {
namespace function {

static std::shared_ptr<Expression> rewriteFunc(const expression_vector& params,
    ExpressionBinder* binder) {
    KU_ASSERT(params.size() == 1);
    auto param = params[0].get();
    if (ExpressionUtil::isNodePattern(*param)) {
        auto node = param->constPtrCast<NodeExpression>();
        return node->getInternalID();
    }
    if (ExpressionUtil::isRelPattern(*param)) {
        auto rel = param->constPtrCast<RelExpression>();
        return rel->getPropertyExpression(InternalKeyword::ID);
    }
    // Bind as struct_extract(param, "_id")
    auto keyExpr =
        binder->createLiteralExpression(Value(LogicalType::STRING(), InternalKeyword::ID));
    auto newParams = expression_vector{params[0], keyExpr};
    return binder->bindScalarFunctionExpression(newParams, StructExtractFunctions::name);
}

function_set IDFunction::getFunctionSet() {
    function_set functionSet;
    auto inputTypes =
        std::vector<LogicalTypeID>{LogicalTypeID::NODE, LogicalTypeID::REL, LogicalTypeID::STRUCT};
    for (auto& inputType : inputTypes) {
        auto function = std::make_unique<RewriteFunction>(name,
            std::vector<LogicalTypeID>{inputType}, rewriteFunc);
        functionSet.push_back(std::move(function));
    }
    return functionSet;
}

static std::shared_ptr<binder::Expression> UidRewriteFunc(const expression_vector& params,
    ExpressionBinder* binder) {
    KU_ASSERT(params.size() == 1);
    auto node = ku_dynamic_cast<Expression*, NodeExpression*>(params[0].get());
    KU_ASSERT(!node->isMultiLabeled());
    return node->getPrimaryKey(node->getSingleEntry()->getTableID());
}

function_set UIDFunction::getFunctionSet() {
    function_set functionSet;
    functionSet.emplace_back(std::make_unique<RewriteFunction>(name,
        std::vector<LogicalTypeID>{LogicalTypeID::NODE}, UidRewriteFunc));
    return functionSet;
}

} // namespace function
} // namespace kuzu
