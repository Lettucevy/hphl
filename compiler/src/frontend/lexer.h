#pragma once
#include "token.h"
#include <string>
#include <vector>

namespace hphl {

struct CompileError {
  int line;
  int column;
  std::string message;
};

class Lexer {
public:
  explicit Lexer(const std::string& source, const std::string& filename);
  std::vector<Token> tokenize();

private:
  std::string src_; // cópia (permite remover o BOM UTF-8 no construtor)
  std::string filename_;
  size_t pos_ = 0;
  int line_ = 1;
  int column_ = 1;

  bool atEnd() const { return pos_ >= src_.size(); }
  char peek(size_t lookahead = 0) const;
  char advance();
  bool match(char expected);
  void error(const std::string& msg);
  void skipWhitespaceAndComments();
  Token makeToken(TokenType type, const std::string& text) const;
  Token readNumber();
  Token readString();
  Token readChar();
  Token readIdentifier();
};

} // namespace hphl