#include "binder/binder.h"
#include "binder/expression/expression_util.h"
#include "binder/expression/graph_expression.h"
#include "binder/expression/literal_expression.h"
#include "binder/expression_binder.h"
#include "catalog/catalog.h"
#include "function/graph/graph_functions.h"
#include "function/rewrite_function.h"
#include "main/client_context.h"

using namespace kuzu::binder;
using namespace kuzu::common;

namespace kuzu {
namespace function {

static std::shared_ptr<Expression> rewriteFunc(const expression_vector& params,
    ExpressionBinder* expressionBinder) {
    auto catalog = expressionBinder->getClientContext()->getCatalog();
    auto tx = expressionBinder->getClientContext()->getTx();
    auto uniqueName = expressionBinder->getUniqueName(CreateGraphFunction::name);
    std::vector<std::string> tableNames;
    if (params.size() == 0) { // Rewrite as all
        for (auto& entry : catalog->getTableEntries(tx)) {
            tableNames.push_back(entry->getName());
        }
    } else {
        for (auto& param : params) {
            ExpressionUtil::validateExpressionType(*param, ExpressionType::LITERAL);
            // TODO(Xiyang): Change the following line to ASSERT.
            ExpressionUtil::validateDataType(*param, *LogicalType::STRING());
            KU_ASSERT(param->dataType == *LogicalType::STRING());
            auto tableName =
                param->constPtrCast<LiteralExpression>()->getValue().getValue<std::string>();
            tableNames.push_back(std::move(tableName));
        }
    }
    return std::make_shared<GraphExpression>(std::move(uniqueName), std::move(tableNames));
}

function_set CreateGraphFunction::getFunctionSet() {
    function_set functionSet;
    auto function = std::make_unique<RewriteFunction>(name,
        std::vector<LogicalTypeID>{LogicalTypeID::STRING}, rewriteFunc);
    function->isVarLength = true;
    functionSet.push_back(std::move(function));
    return functionSet;
}

} // namespace function
} // namespace kuzu
