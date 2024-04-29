#include "binder/binder.h"
#include "binder/query/reading_clause/bound_match_clause.h"
#include "common/exception/binder.h"
#include "parser/query/reading_clause/match_clause.h"

using namespace kuzu::common;
using namespace kuzu::parser;

namespace kuzu {
namespace binder {

// 验证hint的合法性
void verifyHint(const QueryGraphCollection& queryGraphCollection,
    const std::vector<std::vector<std::string>>& hint) {
    if(hint.empty()){
        return;
    }
    std::set<std::string> names;
    std::set<std::string> aliasNames;
    for (auto i = 0u; i < queryGraphCollection.getNumQueryGraphs(); ++i) {
        auto queryGraph = queryGraphCollection.getQueryGraph(i);
        auto nodeNames = queryGraph->getQueryNodeNames();
        names.insert(nodeNames.begin(), nodeNames.end());
        auto relNames = queryGraph->getQueryRelNames();
        names.insert(relNames.begin(), relNames.end());
        auto nodeAliasNames = queryGraph->getQueryNodeAliasNames();
        aliasNames.insert(nodeAliasNames.begin(), nodeAliasNames.end());
        auto relAliasNames = queryGraph->getQueryRelAliasNames();
        aliasNames.insert(relAliasNames.begin(), relAliasNames.end());
    }
    if (names.size() != aliasNames.size()) {
        throw BinderException("All nodes&relations must have an alias");
    }
    for (const auto& group : hint) {
        for (const auto& item : group) {
            if (!aliasNames.erase(item)) {
                throw BinderException(stringFormat("Alias {} in hint is illegal", item));
            }
        }
    }
    if (!aliasNames.empty()) {
        throw BinderException(
            stringFormat("Alias {} does not exists in hint", *aliasNames.begin()));
    }
}

std::unique_ptr<BoundReadingClause> Binder::bindMatchClause(const ReadingClause& readingClause) {
    auto& matchClause = readingClause.constCast<MatchClause>();
    auto boundGraphPattern = bindGraphPattern(matchClause.getPatternElementsRef());
    if (matchClause.hasWherePredicate()) {
        boundGraphPattern.where = bindWhereExpression(*matchClause.getWherePredicate());
    }
    rewriteMatchPattern(boundGraphPattern, !matchClause.getHint().empty());
    // verify hint is illegal
    verifyHint(boundGraphPattern.queryGraphCollection, matchClause.getHint());

    auto boundMatch =
        std::make_unique<BoundMatchClause>(std::move(boundGraphPattern.queryGraphCollection),
            matchClause.getMatchClauseType(), matchClause.getHint());
    boundMatch->setPredicate(boundGraphPattern.where);
    return boundMatch;
}

void Binder::rewriteMatchPattern(BoundGraphPattern& boundGraphPattern, bool hasHint) {
    // Rewrite self loop edge
    // e.g. rewrite (a)-[e]->(a) as [a]-[e]->(b) WHERE id(a) = id(b)
    expression_vector selfLoopEdgePredicates;
    auto& graphCollection = boundGraphPattern.queryGraphCollection;
    for (auto i = 0u; i < graphCollection.getNumQueryGraphs(); ++i) {
        auto queryGraph = graphCollection.getQueryGraphUnsafe(i);
        for (auto& queryRel : queryGraph->getQueryRels()) {
            if (!queryRel->isSelfLoop()) {
                continue;
            }
            if (hasHint) {
                throw BinderException("UnSupport hint for Self loop edge");
            }
            auto src = queryRel->getSrcNode();
            auto dst = queryRel->getDstNode();
            auto newDst = createQueryNode(dst->getVariableName(), dst->getTableIDs());
            queryGraph->addQueryNode(newDst);
            queryRel->setDstNode(newDst);
            auto predicate = expressionBinder.createEqualityComparisonExpression(
                src->getInternalID(), newDst->getInternalID());
            selfLoopEdgePredicates.push_back(std::move(predicate));
        }
    }
    auto where = boundGraphPattern.where;
    for (auto& predicate : selfLoopEdgePredicates) {
        where = expressionBinder.combineBooleanExpressions(ExpressionType::AND, predicate, where);
    }
    // Rewrite key value pairs in MATCH clause as predicate
    for (auto i = 0u; i < graphCollection.getNumQueryGraphs(); ++i) {
        auto queryGraph = graphCollection.getQueryGraphUnsafe(i);
        for (auto& pattern : queryGraph->getAllPatterns()) {
            for (auto& [propertyName, rhs] : pattern->getPropertyDataExprRef()) {
                auto propertyExpr =
                    expressionBinder.bindNodeOrRelPropertyExpression(*pattern, propertyName);
                auto predicate =
                    expressionBinder.createEqualityComparisonExpression(propertyExpr, rhs);
                where = expressionBinder.combineBooleanExpressions(ExpressionType::AND, predicate,
                    where);
            }
        }
    }
    boundGraphPattern.where = std::move(where);
}

} // namespace binder
} // namespace kuzu
