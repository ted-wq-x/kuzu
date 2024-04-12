#include "main/query_summary.h"

#include "common/enums/statement_type.h"

using namespace kuzu::common;

namespace kuzu {
namespace main {

double QuerySummary::getCompilingTime() const {
    return preparedSummary.compilingTime;
}

double QuerySummary::getParsingTime() const {
    return preparedSummary.parsingTime;
}

double QuerySummary::getExecutionTime() const {
    return executionTime;
}

void QuerySummary::setPreparedSummary(PreparedSummary preparedSummary_) {
    preparedSummary = preparedSummary_;
}

bool QuerySummary::isExplain() const {
    return preparedSummary.statementType == StatementType::EXPLAIN;
}

} // namespace main
} // namespace kuzu
