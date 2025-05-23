#include "binder/binder.h"
#include "binder/copy/bound_copy_to.h"
#include "catalog/catalog.h"
#include "common/exception/catalog.h"
#include "common/exception/runtime.h"
#include "function/built_in_function_utils.h"
#include "main/client_context.h"
#include "parser/copy.h"

using namespace kuzu::common;
using namespace kuzu::parser;

namespace kuzu {
namespace binder {

std::unique_ptr<BoundStatement> Binder::bindCopyToClause(const Statement& statement) {
    auto& copyToStatement = statement.constCast<CopyTo>();
    auto boundFilePath = copyToStatement.getFilePath();
    auto fileTypeInfo = bindFileTypeInfo({boundFilePath});
    std::vector<std::string> columnNames;
    auto parsedQuery = copyToStatement.getStatement();
    auto query = bindQuery(*parsedQuery);
    auto columns = query->getStatementResult()->getColumns();
    auto fileTypeStr = fileTypeInfo.fileTypeStr;
    auto name = stringFormat("COPY_{}", fileTypeStr);
    catalog::CatalogEntry* entry = nullptr;
    try {
        entry =
            clientContext->getCatalog()->getFunctionEntry(clientContext->getTransaction(), name);
    } catch (common::CatalogException& exception) {
        throw common::RuntimeException{common::stringFormat(
            "Exporting query result to the '{}' file is currently not supported.", fileTypeStr)};
    }
    auto exportFunc = function::BuiltInFunctionsUtils::matchFunction(name,
        entry->ptrCast<catalog::FunctionCatalogEntry>())
                          ->constPtrCast<function::ExportFunction>();
    for (auto& column : columns) {
        auto columnName = column->hasAlias() ? column->getAlias() : column->toString();
        columnNames.push_back(columnName);
    }
    function::ExportFuncBindInput bindInput{std::move(columnNames), std::move(boundFilePath),
        bindParsingOptions(copyToStatement.getParsingOptions())};
    auto bindData = exportFunc->bind(bindInput);
    return std::make_unique<BoundCopyTo>(std::move(bindData), *exportFunc, std::move(query));
}

} // namespace binder
} // namespace kuzu
