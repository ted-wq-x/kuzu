#pragma once

#include <string>

#include "parser/expression/parsed_expression.h"

namespace kuzu {
namespace parser {

class AlgoParameter {
public:
    AlgoParameter(std::string name, std::vector<std::string> tableNames)
        : variableName(std::move(name)), tableNames(std::move(tableNames)) {}
    DELETE_COPY_DEFAULT_MOVE(AlgoParameter);

    inline std::string getVariableName() const { return variableName; }

    inline std::vector<std::string> getTableNames() const { return tableNames; }
    void setWherePredicate(std::unique_ptr<ParsedExpression> expression) {
        wherePredicate = std::move(expression);
    }
    bool hasWherePredicate() const { return wherePredicate != nullptr; }
    const ParsedExpression* getWherePredicate() const { return wherePredicate.get(); }

private:
    std::string variableName;
    std::vector<std::string> tableNames;
    std::unique_ptr<ParsedExpression> wherePredicate;
};
} // namespace parser
} // namespace kuzu