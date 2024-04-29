#pragma once

#include "binder/query/query_graph.h"
#include "bound_reading_clause.h"

namespace kuzu {
namespace binder {

class BoundMatchClause : public BoundReadingClause {
public:
    BoundMatchClause(QueryGraphCollection queryGraphCollection,
        common::MatchClauseType matchClauseType, std::vector<std::vector<std::string>> hint)
        : BoundReadingClause{common::ClauseType::MATCH},
          queryGraphCollection{std::move(queryGraphCollection)}, matchClauseType{matchClauseType},
          hint{std::move(hint)} {}

    inline const QueryGraphCollection* getQueryGraphCollection() const {
        return &queryGraphCollection;
    }

    inline common::MatchClauseType getMatchClauseType() const { return matchClauseType; }

    inline const std::vector<std::vector<std::string>>* getHint() const { return &hint; }

private:
    QueryGraphCollection queryGraphCollection;
    common::MatchClauseType matchClauseType;
    std::vector<std::vector<std::string>> hint;
};

} // namespace binder
} // namespace kuzu
