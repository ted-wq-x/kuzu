
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
    T__44 = 45, T__45 = 46, T__46 = 47, ADD = 48, ALL = 49, ALTER = 50, 
    AND = 51, AS = 52, ASC = 53, ASCENDING = 54, ATTACH = 55, BEGIN = 56, 
    BY = 57, CALL = 58, CASE = 59, CAST = 60, COLUMN = 61, COMMENT = 62, 
    COMMIT = 63, COMMIT_SKIP_CHECKPOINT = 64, CONTAINS = 65, COPY = 66, 
    COUNT = 67, CREATE = 68, CYCLE = 69, DATABASE = 70, DBTYPE = 71, DEFAULT = 72, 
    DELETE = 73, DESC = 74, DESCENDING = 75, DETACH = 76, DISTINCT = 77, 
    DROP = 78, ELSE = 79, END = 80, ENDS = 81, EXISTS = 82, EXPLAIN = 83, 
    EXPORT = 84, EXTENSION = 85, FALSE = 86, FROM = 87, GLOB = 88, GRAPH = 89, 
    GROUP = 90, HEADERS = 91, IMPORT = 92, IF = 93, IN = 94, INCREMENT = 95, 
    INSTALL = 96, IS = 97, KEY = 98, LIMIT = 99, LOAD = 100, MACRO = 101, 
    MATCH = 102, MAXVALUE = 103, MERGE = 104, MINVALUE = 105, NO = 106, 
    NODE = 107, NOT = 108, NULL_ = 109, ON = 110, ONLY = 111, OPTIONAL = 112, 
    OR = 113, ORDER = 114, PRIMARY = 115, PROFILE = 116, PROJECT = 117, 
    RDFGRAPH = 118, READ = 119, REL = 120, RENAME = 121, RETURN = 122, ROLLBACK = 123, 
    ROLLBACK_SKIP_CHECKPOINT = 124, SEQUENCE = 125, SET = 126, SHORTEST = 127, 
    START = 128, STARTS = 129, TABLE = 130, THEN = 131, TO = 132, TRANSACTION = 133, 
    TRUE = 134, TYPE = 135, UNION = 136, UNWIND = 137, USE = 138, WHEN = 139, 
    WHERE = 140, WITH = 141, WRITE = 142, XOR = 143, DECIMAL = 144, STAR = 145, 
    L_SKIP = 146, MINUS = 147, FACTORIAL = 148, COLON = 149, StringLiteral = 150, 
    EscapedChar = 151, DecimalInteger = 152, HexLetter = 153, HexDigit = 154, 
    Digit = 155, NonZeroDigit = 156, NonZeroOctDigit = 157, ZeroDigit = 158, 
    RegularDecimalReal = 159, UnescapedSymbolicName = 160, IdentifierStart = 161, 
    IdentifierPart = 162, EscapedSymbolicName = 163, SP = 164, WHITESPACE = 165, 
    CypherComment = 166, Unknown = 167
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

