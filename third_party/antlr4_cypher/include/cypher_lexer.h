
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
    TO = 80, EXPLAIN = 81, PROFILE = 82, BEGIN = 83, TRANSACTION = 84, READ = 85, 
    ONLY = 86, WRITE = 87, COMMIT = 88, COMMIT_SKIP_CHECKPOINT = 89, ROLLBACK = 90, 
    ROLLBACK_SKIP_CHECKPOINT = 91, INSTALL = 92, EXTENSION = 93, UNION = 94, 
    ALL = 95, LOAD = 96, HEADERS = 97, OPTIONAL = 98, MATCH = 99, UNWIND = 100, 
    CREATE = 101, MERGE = 102, ON = 103, SET = 104, DETACH = 105, DELETE = 106, 
    WITH = 107, RETURN = 108, DISTINCT = 109, STAR = 110, AS = 111, ORDER = 112, 
    BY = 113, L_SKIP = 114, LIMIT = 115, ASCENDING = 116, ASC = 117, DESCENDING = 118, 
    DESC = 119, WHERE = 120, SHORTEST = 121, OR = 122, XOR = 123, AND = 124, 
    NOT = 125, MINUS = 126, FACTORIAL = 127, COLON = 128, IN = 129, STARTS = 130, 
    ENDS = 131, CONTAINS = 132, IS = 133, NULL_ = 134, TRUE = 135, FALSE = 136, 
    COUNT = 137, EXISTS = 138, CASE = 139, ELSE = 140, END = 141, WHEN = 142, 
    THEN = 143, StringLiteral = 144, EscapedChar = 145, DecimalInteger = 146, 
    HexLetter = 147, HexDigit = 148, Digit = 149, NonZeroDigit = 150, NonZeroOctDigit = 151, 
    ZeroDigit = 152, RegularDecimalReal = 153, UnescapedSymbolicName = 154, 
    IdentifierStart = 155, IdentifierPart = 156, EscapedSymbolicName = 157, 
    SP = 158, WHITESPACE = 159, Comment = 160, Unknown = 161
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

