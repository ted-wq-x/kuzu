
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
    T__44 = 45, T__45 = 46, ATTACH = 47, DBTYPE = 48, USE = 49, CALL = 50, 
    COMMENT_ = 51, MACRO = 52, GLOB = 53, COPY = 54, FROM = 55, COLUMN = 56, 
    EXPORT = 57, IMPORT = 58, DATABASE = 59, NODE = 60, TABLE = 61, GROUP = 62, 
    RDFGRAPH = 63, DROP = 64, ALTER = 65, DEFAULT = 66, RENAME = 67, ADD = 68, 
    PRIMARY = 69, KEY = 70, REL = 71, TO = 72, EXPLAIN = 73, PROFILE = 74, 
    BEGIN = 75, TRANSACTION = 76, READ = 77, ONLY = 78, WRITE = 79, COMMIT = 80, 
    COMMIT_SKIP_CHECKPOINT = 81, ROLLBACK = 82, ROLLBACK_SKIP_CHECKPOINT = 83, 
    INSTALL = 84, EXTENSION = 85, UNION = 86, ALL = 87, LOAD = 88, HEADERS = 89, 
    OPTIONAL = 90, MATCH = 91, UNWIND = 92, CREATE = 93, MERGE = 94, ON = 95, 
    SET = 96, DETACH = 97, DELETE = 98, WITH = 99, RETURN = 100, DISTINCT = 101, 
    STAR = 102, AS = 103, ORDER = 104, BY = 105, L_SKIP = 106, LIMIT = 107, 
    ASCENDING = 108, ASC = 109, DESCENDING = 110, DESC = 111, WHERE = 112, 
    SHORTEST = 113, OR = 114, XOR = 115, AND = 116, NOT = 117, MINUS = 118, 
    FACTORIAL = 119, COLON = 120, IN = 121, STARTS = 122, ENDS = 123, CONTAINS = 124, 
    IS = 125, NULL_ = 126, TRUE = 127, FALSE = 128, COUNT = 129, EXISTS = 130, 
    CASE = 131, ELSE = 132, END = 133, WHEN = 134, THEN = 135, StringLiteral = 136, 
    EscapedChar = 137, DecimalInteger = 138, HexLetter = 139, HexDigit = 140, 
    Digit = 141, NonZeroDigit = 142, NonZeroOctDigit = 143, ZeroDigit = 144, 
    RegularDecimalReal = 145, UnescapedSymbolicName = 146, IdentifierStart = 147, 
    IdentifierPart = 148, EscapedSymbolicName = 149, SP = 150, WHITESPACE = 151, 
    Comment = 152, Unknown = 153
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

