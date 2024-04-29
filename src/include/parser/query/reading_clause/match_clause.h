#pragma once

#include "parser/expression/parsed_expression.h"
#include "parser/query/graph_pattern/pattern_element.h"
#include "reading_clause.h"

namespace kuzu {
namespace parser {

class MatchClause : public ReadingClause {
public:
    MatchClause(std::vector<PatternElement> patternElements,
        common::MatchClauseType matchClauseType)
        : ReadingClause{common::ClauseType::MATCH}, patternElements{std::move(patternElements)},
          matchClauseType{matchClauseType} {}

    inline const std::vector<PatternElement>& getPatternElementsRef() const {
        return patternElements;
    }

    inline void setWherePredicate(std::unique_ptr<ParsedExpression> expression) {
        wherePredicate = std::move(expression);
    }
    inline bool hasWherePredicate() const { return wherePredicate != nullptr; }
    inline const ParsedExpression* getWherePredicate() const { return wherePredicate.get(); }

    inline common::MatchClauseType getMatchClauseType() const { return matchClauseType; }

    const std::vector<std::vector<std::string>>& getHint() const { return hint; }

    void setHint(const std::vector<std::vector<std::string>>& hintVector) { MatchClause::hint = std::move(hintVector); }

private:
    std::vector<PatternElement> patternElements;
    std::unique_ptr<ParsedExpression> wherePredicate;
    common::MatchClauseType matchClauseType;
    std::vector<std::vector<std::string>> hint;
};

} // namespace parser
} // namespace kuzu
