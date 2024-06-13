#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "expression/parsed_expression.h"
#include "parser/antlr_parser/kuzu_cypher_parser.h"
#include "parser/parser.h"
#include "statement.h"

// ANTLR4 generates code with unused parameters.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "cypher_lexer.h"
#pragma GCC diagnostic pop

#include "common/exception/parser.h"
#include "parser/antlr_parser/kuzu_cypher_parser.h"
#include "parser/antlr_parser/parser_error_listener.h"
#include "parser/antlr_parser/parser_error_strategy.h"
#include "parser/transformer.h"

namespace kuzu {
namespace parser {

class Parser {

public:
    static std::vector<std::shared_ptr<Statement>> parseQuery(std::string_view query);
    static std::unique_ptr<AlgoParameter> parseAlgoParams(
        std::string_view parameter_str);

private:
    template<typename T>
    static T parse(std::string_view query, const std::function<T(KuzuCypherParser&)>& transformer) {
        // LCOV_EXCL_START
        // We should have enforced this in connection, but I also realize empty query will cause
        // antlr to hang. So enforce a duplicate check here.
        if (query.empty()) {
            throw common::ParserException(
                "Cannot parse empty query. This should be handled in connection.");
        }
        // LCOV_EXCL_STOP
        auto inputStream = antlr4::ANTLRInputStream(query);
        auto parserErrorListener = ParserErrorListener();

        auto cypherLexer = CypherLexer(&inputStream);
        cypherLexer.removeErrorListeners();
        cypherLexer.addErrorListener(&parserErrorListener);
        auto tokens = antlr4::CommonTokenStream(&cypherLexer);
        tokens.fill();

        auto kuzuCypherParser = KuzuCypherParser(&tokens);
        kuzuCypherParser.removeErrorListeners();
        kuzuCypherParser.addErrorListener(&parserErrorListener);
        kuzuCypherParser.setErrorHandler(std::make_shared<ParserErrorStrategy>());
        return transformer(kuzuCypherParser);
    }
};

} // namespace parser
} // namespace kuzu
