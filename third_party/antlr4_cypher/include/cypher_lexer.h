
// Generated from Cypher.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"




class  CypherLexer : public antlr4::Lexer {
public:
  enum {
    T__0 = 1, T__1 = 2, T__2 = 3, T__3 = 4, T__4 = 5, T__5 = 6, T__6 = 7, 
    T__7 = 8, T__8 = 9, T__9 = 10, T__10 = 11, T__11 = 12, T__12 = 13, T__13 = 14, 
    T__14 = 15, T__15 = 16, T__16 = 17, T__17 = 18, T__18 = 19, T__19 = 20, 
    T__20 = 21, T__21 = 22, T__22 = 23, T__23 = 24, T__24 = 25, T__25 = 26, 
    T__26 = 27, T__27 = 28, T__28 = 29, T__29 = 30, T__30 = 31, T__31 = 32, 
    T__32 = 33, T__33 = 34, T__34 = 35, T__35 = 36, T__36 = 37, T__37 = 38, 
    T__38 = 39, T__39 = 40, T__40 = 41, T__41 = 42, T__42 = 43, T__43 = 44, 
    T__44 = 45, T__45 = 46, ADD = 47, ALL = 48, ALTER = 49, AND = 50, AS = 51, 
    ASC = 52, ASCENDING = 53, ATTACH = 54, BEGIN = 55, BY = 56, CALL = 57, 
    CASE = 58, CAST = 59, COLUMN = 60, COMMENT = 61, COMMIT = 62, COMMIT_SKIP_CHECKPOINT = 63, 
    CONTAINS = 64, COPY = 65, COUNT = 66, CREATE = 67, CYCLE = 68, DATABASE = 69, 
    DBTYPE = 70, DEFAULT = 71, DELETE = 72, DESC = 73, DESCENDING = 74, 
    DETACH = 75, DISTINCT = 76, DROP = 77, ELSE = 78, END = 79, ENDS = 80, 
    EXISTS = 81, EXPLAIN = 82, EXPORT = 83, EXTENSION = 84, FALSE = 85, 
    FROM = 86, GLOB = 87, GRAPH = 88, GROUP = 89, HEADERS = 90, HINT = 91, 
    IMPORT = 92, IF = 93, IN = 94, INCREMENT = 95, INSTALL = 96, IS = 97, 
    JOIN = 98, KEY = 99, LIMIT = 100, LOAD = 101, MACRO = 102, MATCH = 103, 
    MAXVALUE = 104, MERGE = 105, MINVALUE = 106, MULTI_JOIN = 107, NO = 108, 
    NODE = 109, NOT = 110, NULL_ = 111, ON = 112, ONLY = 113, OPTIONAL = 114, 
    OR = 115, ORDER = 116, PRIMARY = 117, PROFILE = 118, PROJECT = 119, 
    RDFGRAPH = 120, READ = 121, REL = 122, RENAME = 123, RETURN = 124, ROLLBACK = 125, 
    ROLLBACK_SKIP_CHECKPOINT = 126, SEQUENCE = 127, SET = 128, SHORTEST = 129, 
    START = 130, STARTS = 131, TABLE = 132, THEN = 133, TO = 134, TRANSACTION = 135, 
    TRUE = 136, TYPE = 137, UNION = 138, UNWIND = 139, USE = 140, WHEN = 141, 
    WHERE = 142, WITH = 143, WRITE = 144, XOR = 145, DECIMAL = 146, STAR = 147, 
    L_SKIP = 148, MINUS = 149, FACTORIAL = 150, COLON = 151, StringLiteral = 152, 
    EscapedChar = 153, DecimalInteger = 154, HexLetter = 155, HexDigit = 156, 
    Digit = 157, NonZeroDigit = 158, NonZeroOctDigit = 159, ZeroDigit = 160, 
    RegularDecimalReal = 161, UnescapedSymbolicName = 162, IdentifierStart = 163, 
    IdentifierPart = 164, EscapedSymbolicName = 165, SP = 166, WHITESPACE = 167, 
    CypherComment = 168, Unknown = 169
  };

  explicit CypherLexer(antlr4::CharStream *input);

  ~CypherLexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

};

