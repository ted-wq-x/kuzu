
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
    T__44 = 45, T__45 = 46, T__46 = 47, ATTACH = 48, DBTYPE = 49, USE = 50, 
    CALL = 51, COMMENT_ = 52, MACRO = 53, GLOB = 54, COPY = 55, FROM = 56, 
    COLUMN = 57, EXPORT = 58, IMPORT = 59, DATABASE = 60, NODE = 61, TABLE = 62, 
    GROUP = 63, RDFGRAPH = 64, SEQUENCE = 65, INCREMENT = 66, MINVALUE = 67, 
    MAXVALUE = 68, START = 69, NO = 70, CYCLE = 71, DROP = 72, ALTER = 73, 
    DEFAULT = 74, RENAME = 75, ADD = 76, PRIMARY = 77, KEY = 78, REL = 79, 
    TO = 80, DECIMAL = 81, EXPLAIN = 82, PROFILE = 83, BEGIN = 84, TRANSACTION = 85, 
    READ = 86, ONLY = 87, WRITE = 88, COMMIT = 89, COMMIT_SKIP_CHECKPOINT = 90, 
    ROLLBACK = 91, ROLLBACK_SKIP_CHECKPOINT = 92, INSTALL = 93, EXTENSION = 94, 
    UNION = 95, ALL = 96, LOAD = 97, HEADERS = 98, OPTIONAL = 99, MATCH = 100, 
    UNWIND = 101, CREATE = 102, MERGE = 103, ON = 104, SET = 105, DETACH = 106, 
    DELETE = 107, WITH = 108, RETURN = 109, DISTINCT = 110, STAR = 111, 
    AS = 112, ORDER = 113, BY = 114, L_SKIP = 115, LIMIT = 116, ASCENDING = 117, 
    ASC = 118, DESCENDING = 119, DESC = 120, WHERE = 121, SHORTEST = 122, 
    OR = 123, XOR = 124, AND = 125, NOT = 126, MINUS = 127, FACTORIAL = 128, 
    COLON = 129, IN = 130, STARTS = 131, ENDS = 132, CONTAINS = 133, IS = 134, 
    NULL_ = 135, TRUE = 136, FALSE = 137, COUNT = 138, EXISTS = 139, CASE = 140, 
    ELSE = 141, END = 142, WHEN = 143, THEN = 144, StringLiteral = 145, 
    EscapedChar = 146, DecimalInteger = 147, HexLetter = 148, HexDigit = 149, 
    Digit = 150, NonZeroDigit = 151, NonZeroOctDigit = 152, ZeroDigit = 153, 
    RegularDecimalReal = 154, UnescapedSymbolicName = 155, IdentifierStart = 156, 
    IdentifierPart = 157, EscapedSymbolicName = 158, SP = 159, WHITESPACE = 160, 
    Comment = 161, Unknown = 162
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

