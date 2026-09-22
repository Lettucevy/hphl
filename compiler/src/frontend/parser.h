#pragma once
#include "ast/ast.h"
#include "lexer.h"
#include <memory>
#include <vector>

namespace hphl {

class Parser {
public:
  explicit Parser(std::vector<Token> tokens, const std::string& filename);

  std::unique_ptr<Program> parseProgram();

private:
  std::vector<Token> tokens_;
  size_t pos_ = 0;
  std::string filename_;

  // ">>" (Shr do lexer) fecha dois genéricos aninhados: consome um '>' aqui
  // e devolve um '>' residual para o tipo externo (ex.: list<list<int>>).
  void splitShr();

  // parâmetros de tipo ativos (contexto genérico durante o parse de corpos)
  std::vector<std::string> typeParamStack_;
  bool fnCompiletimePending_ = false; // v0.46: `compiletime` prefixo de função

  const Token& peek(size_t lookahead = 0) const;
  const Token& current() const;
  bool check(TokenType type) const;
  // `pool`/`base` sao contextuais: valem como identificador fora das
  // posicoes de keyword (policy `pool T x`, chamada `base.M()`).
  bool atIdent() const;
  bool atIdentAt(size_t pos) const;
  bool match(TokenType type);
  const Token& expect(TokenType type, const std::string& what);
  const Token& expectIdentifier(const std::string& what);
  std::string expectIdentOrKeyword(const std::string& what);
  [[noreturn]] void error(const Token& tok, const std::string& msg);

  int recursionDepth_ = 0;
  static constexpr int MAX_RECURSION_DEPTH = 256;
  struct RecursionGuard {
    int& depth;
    RecursionGuard(int& d, const Token& tok, Parser* p) : depth(d) {
      if (++depth > MAX_RECURSION_DEPTH) {
        p->error(tok, "limite de profundidade de aninhamento excedido (máximo 256 níveis)");
      }
    }
    ~RecursionGuard() { --depth; }
  };

  // atributos C++-style: [[deprecated("...")]] etc.
  struct ParsedAttributes {
    bool isDeprecated = false;
    std::string deprecatedReason;
    bool suppressDeprecation = false;
    bool isInline = false;
    bool isNoReturn = false;
    bool isPacked = false;
    bool isNoGc = false;
    bool hasAny() const {
      return isDeprecated || suppressDeprecation || isInline || isNoReturn || isPacked || isNoGc;
    }
  };
  ParsedAttributes parseAttributes();

  // declarações
  std::vector<std::unique_ptr<Decl>> parseTopLevelDecl();
  std::unique_ptr<ModuleDecl> parseModule();
  std::unique_ptr<ImportDecl> parseImport();
  std::unique_ptr<UsingDecl> parseUsingDecl();
  std::unique_ptr<Decl> parseClassDecl(bool isStruct, bool isInterface,
                                       const Token& nameTok);
  std::unique_ptr<Decl> parseEnumDecl(const Token& nameTok);
  std::unique_ptr<FunctionDecl> parseFunctionDecl(Access access, bool isStatic,
                                                  bool isInline, const std::string& ownerClass,
                                                  bool isMethod, const Token& nameTok,
                                                  bool hasReturnType, Type retType,
                                                  bool allowBodyless = false,
                                                  bool isAsync = false);
  std::unique_ptr<Decl> parseMethodOrCtor(Access access, const std::string& ownerClass,
                                          bool allowBodyless = false);
  std::unique_ptr<Decl> parseProperty(Access access, const Token& nameTok,
                                       Type propType, bool isStatic = false);
  std::vector<std::unique_ptr<VarDecl>> parseGlobalVarDecl();
  std::unique_ptr<Decl> parseMethodOrCtor(Access access, const std::string& ownerClass);

  // tipos
  Type parseType();
  Type parsePrimitiveType(TokenType tt);
  StoragePolicy parseStoragePolicy();
  std::vector<TypeParam> parseTypeParams();
  TypeParam parseTypeParam();
  void parseWhereClause(std::vector<TypeParam>& params);
  std::string parseConstraintName();

  // instruções
  std::unique_ptr<Stmt> parseStatement();
  std::unique_ptr<BlockStmt> parseBlock();
  std::vector<std::unique_ptr<VarDecl>> parseVarDeclStmt(bool allowSemicolon = true,
                                                       bool allowMultiple = true);
  std::unique_ptr<Stmt> parseIf();
  std::unique_ptr<Stmt> parseFor();
  std::unique_ptr<Stmt> parseWhile();
  std::unique_ptr<Stmt> parseDoWhile();
  std::unique_ptr<Stmt> parseSwitch();
  std::unique_ptr<Stmt> parseForeach(bool parallel);
  std::unique_ptr<Stmt> parseTry();
  std::unique_ptr<Stmt> parseThrow();
  std::unique_ptr<Stmt> parseLock();
  std::unique_ptr<Stmt> parseSpawn();
  std::unique_ptr<Stmt> parseParallel();
  std::unique_ptr<Stmt> parseReturn();

  // expressões (precedência crescente)
  std::unique_ptr<Expr> parseExpression();
  std::unique_ptr<Expr> parseAssign();
  std::unique_ptr<Expr> parseTernary();
  std::unique_ptr<Expr> parseCoalesce();
  std::unique_ptr<Expr> parseOr();
  std::unique_ptr<Expr> parseAnd();
  std::unique_ptr<Expr> parseBitOr();
  std::unique_ptr<Expr> parseBitXor();
  std::unique_ptr<Expr> parseBitAnd();
  std::unique_ptr<Expr> parseEquality();
  std::unique_ptr<Expr> parseRelational();
  std::unique_ptr<Expr> parseShift();
  std::unique_ptr<Expr> parseAdditive();
  std::unique_ptr<Expr> parseMultiplicative();
  std::unique_ptr<Expr> parseUnary();
  std::unique_ptr<Expr> parsePostfix();
  std::unique_ptr<Expr> parsePrimary();
  // v0.95 (lambdas): tenta `(params) => corpo`; nullptr se não for lambda
  // (restaura pos_). Falhas internas de parse propagam erro normalmente.
  std::unique_ptr<Expr> tryParseLambda(const Token& lparen);
  std::unique_ptr<Expr> parseMatchExpr();
  std::unique_ptr<Pattern> parsePattern(bool neg = false);
  std::unique_ptr<Pattern> parsePatternAtom();

  std::vector<std::unique_ptr<Expr>> parseArgs(std::vector<std::string>* names = nullptr);
  bool isTypeStart() const;
  bool identGenericDeclAhead() const; // M10: `Nome<a, 4> var;` lookahead
  // keywords numéricas que aceitam tamanho (int, float, u8..u64)
  bool isNumericTypeKeyword(TokenType tt) const;
  // token que pode iniciar um primário (desambigua `x?` de `x ? a : b`)
  bool isPrimaryStart(TokenType tt) const;
};

} // namespace hphl