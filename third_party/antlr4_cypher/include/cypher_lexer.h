
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
    GROUP = 63, RDFGRAPH = 64, DROP = 65, ALTER = 66, DEFAULT = 67, RENAME = 68, 
    ADD = 69, PRIMARY = 70, KEY = 71, REL = 72, TO = 73, EXPLAIN = 74, PROFILE = 75, 
    BEGIN = 76, TRANSACTION = 77, READ = 78, ONLY = 79, WRITE = 80, COMMIT = 81, 
    COMMIT_SKIP_CHECKPOINT = 82, ROLLBACK = 83, ROLLBACK_SKIP_CHECKPOINT = 84, 
    INSTALL = 85, EXTENSION = 86, UNION = 87, ALL = 88, LOAD = 89, HEADERS = 90, 
    OPTIONAL = 91, MATCH = 92, UNWIND = 93, CREATE = 94, MERGE = 95, ON = 96, 
    SET = 97, DETACH = 98, DELETE = 99, WITH = 100, RETURN = 101, DISTINCT = 102, 
    STAR = 103, AS = 104, ORDER = 105, BY = 106, L_SKIP = 107, LIMIT = 108, 
    ASCENDING = 109, ASC = 110, DESCENDING = 111, DESC = 112, WHERE = 113, 
    SHORTEST = 114, OR = 115, XOR = 116, AND = 117, NOT = 118, MINUS = 119, 
    FACTORIAL = 120, COLON = 121, IN = 122, STARTS = 123, ENDS = 124, CONTAINS = 125, 
    IS = 126, NULL_ = 127, TRUE = 128, FALSE = 129, COUNT = 130, EXISTS = 131, 
    CASE = 132, ELSE = 133, END = 134, WHEN = 135, THEN = 136, StringLiteral = 137, 
    EscapedChar = 138, DecimalInteger = 139, HexLetter = 140, HexDigit = 141, 
    Digit = 142, NonZeroDigit = 143, NonZeroOctDigit = 144, ZeroDigit = 145, 
    RegularDecimalReal = 146, UnescapedSymbolicName = 147, IdentifierStart = 148, 
    IdentifierPart = 149, EscapedSymbolicName = 150, SP = 151, WHITESPACE = 152, 
    Comment = 153, Unknown = 154
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

