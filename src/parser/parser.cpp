#include "parser/parser.h"

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

using namespace antlr4;

namespace kuzu {
namespace parser {

std::vector<std::shared_ptr<Statement>> Parser::parseQuery(std::string_view query) {
    return parse<std::vector<std::shared_ptr<Statement>>>(query,
        [](KuzuCypherParser& kuzuCypherParser) -> std::vector<std::shared_ptr<Statement>> {
            Transformer transformer((kuzuCypherParser.ku_Statements()));
            return transformer.transform();
        });
}

std::unique_ptr<AlgoParameter> Parser::parseAlgoParams(std::string_view parameter_str) {
    return parse<std::unique_ptr<AlgoParameter>>(parameter_str,
        [](KuzuCypherParser& kuzuCypherParser) -> std::unique_ptr<AlgoParameter> {
                        Transformer transformer(nullptr);
                        return transformer.transformAlgoParameter(*kuzuCypherParser.oC_AlgoParameter());
        });
}
} // namespace parser
} // namespace kuzu
