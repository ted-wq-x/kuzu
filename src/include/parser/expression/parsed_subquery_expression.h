#pragma once

#include "common/assert.h"
#include "common/enums/subquery_type.h"
#include "parsed_expression.h"
#include "parser/query/graph_pattern/pattern_element.h"

namespace kuzu {
namespace parser {

class ParsedSubqueryExpression : public ParsedExpression {
public:
    ParsedSubqueryExpression(common::SubqueryType subqueryType, std::string rawName)
        : ParsedExpression{common::ExpressionType::SUBQUERY, std::move(rawName)},
          subqueryType{subqueryType} {}

    inline common::SubqueryType getSubqueryType() const { return subqueryType; }

    inline void addPatternElement(PatternElement element) {
        patternElements.push_back(std::move(element));
    }
    inline void setPatternElements(std::vector<PatternElement> elements) {
        patternElements = std::move(elements);
    }
    inline const std::vector<PatternElement>& getPatternElements() const { return patternElements; }

    inline void setWhereClause(std::unique_ptr<ParsedExpression> expression) {
        whereClause = std::move(expression);
    }
    inline bool hasWhereClause() const { return whereClause != nullptr; }
    inline const ParsedExpression* getWhereClause() const { return whereClause.get(); }

    static std::unique_ptr<ParsedSubqueryExpression> deserialize(common::Deserializer&) {
        KU_UNREACHABLE;
    }

    std::unique_ptr<ParsedExpression> copy() const override { KU_UNREACHABLE; }

private:
    void serializeInternal(common::Serializer&) const override { KU_UNREACHABLE; }

private:
    common::SubqueryType subqueryType;
    std::vector<PatternElement> patternElements;
    std::unique_ptr<ParsedExpression> whereClause;
};

} // namespace parser
} // namespace kuzu
