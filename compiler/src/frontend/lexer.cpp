#include "lexer.h"
#include <cctype>
#include <sstream>

namespace hphl {

Lexer::Lexer(const std::string& source, const std::string& filename)
    : src_(source), filename_(filename) {
  // pula BOM UTF-8 (EF BB BF) — arquivos salvos por editores como PS/Notepad
  if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
      static_cast<unsigned char>(src_[1]) == 0xBB &&
      static_cast<unsigned char>(src_[2]) == 0xBF) {
    src_.erase(0, 3);
  }
}

void Lexer::error(const std::string& msg) {
  std::ostringstream ss;
  ss << filename_ << ":" << line_ << ":" << column_ << ": erro: " << msg;
  throw CompileError{line_, column_, ss.str()};
}

char Lexer::peek(size_t lookahead) const {
  if (pos_ + lookahead >= src_.size()) return '\0';
  return src_[pos_ + lookahead];
}

char Lexer::advance() {
  char c = src_[pos_++];
  if (c == '\n') {
    line_++;
    column_ = 1;
  } else {
    column_++;
  }
  return c;
}

bool Lexer::match(char expected) {
  if (atEnd() || src_[pos_] != expected) return false;
  advance();
  return true;
}

Token Lexer::makeToken(TokenType type, const std::string& text) const {
  Token t;
  t.type = type;
  t.text = text;
  t.line = line_;
  t.column = column_;
  return t;
}

void Lexer::skipWhitespaceAndComments() {
  for (;;) {
    while (!atEnd() && std::isspace(static_cast<unsigned char>(peek()))) advance();
    if (peek() == '/' && peek(1) == '/') {
      while (!atEnd() && peek() != '\n') advance();
    } else if (peek() == '/' && peek(1) == '*') {
      advance();
      advance();
      bool closed = false;
      while (!atEnd()) {
        if (peek() == '*' && peek(1) == '/') {
          advance();
          advance();
          closed = true;
          break;
        }
        advance();
      }
      if (!closed) error("comentário /* não fechado");
    } else {
      break;
    }
  }
}

Token Lexer::readNumber() {
  size_t start = pos_;
  bool isFloat = false;

  // hex / binary / decimal
  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X' || peek(1) == 'b' || peek(1) == 'B')) {
    char base = peek(1);
    advance();
    advance();
    while (!atEnd()) {
      char c = peek();
      bool hexDigit = base == 'x' || base == 'X'
                          ? std::isxdigit(static_cast<unsigned char>(c))
                          : (c == '0' || c == '1');
      if (!hexDigit && c != '_') break;
      advance();
    }
    std::string text = src_.substr(start, pos_ - start);
    std::string clean;
    for (char c : text) if (c != '_') clean += c;
    Token t = makeToken(TokenType::IntLiteral, text);
    long long v = 0;
    try {
      if (base == 'x' || base == 'X') v = std::stoll(clean.substr(2), nullptr, 16);
      else v = std::stoll(clean.substr(2), nullptr, 2);
    } catch (const std::exception&) {
      error("literal inteiro fora do intervalo representável: " + text);
    }
    t.intValue = v;
    return t;
  }

  while (!atEnd() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
    advance();
  }
  // fraction
  if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
    isFloat = true;
    advance();
    while (!atEnd() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) advance();
  }
  // exponent
  if (peek() == 'e' || peek() == 'E') {
    size_t save = pos_;
    advance();
    if (peek() == '+' || peek() == '-') advance();
    if (std::isdigit(static_cast<unsigned char>(peek()))) {
      isFloat = true;
      while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
    } else {
      pos_ = save;
    }
  }

  std::string text = src_.substr(start, pos_ - start);
  std::string clean;
  for (char c : text) if (c != '_') clean += c;

  Token t = makeToken(isFloat ? TokenType::FloatLiteral : TokenType::IntLiteral, text);
  try {
    if (isFloat) {
      t.floatValue = std::stod(clean);
    } else {
      t.intValue = std::stoll(clean);
    }
  } catch (const std::exception&) {
    error("literal numérico fora do intervalo representável: " + text);
  }
  return t;
}

Token Lexer::readString() {
  advance(); // opening quote
  std::string value;
  int startCol = column_;
  while (!atEnd() && peek() != '"') {
    char c = advance();
    if (c == '\\') {
      if (atEnd()) break;
      char e = advance();
      switch (e) {
        case 'n': value += '\n'; break;
        case 't': value += '\t'; break;
        case 'r': value += '\r'; break;
        case 'b': value += '\b'; break;
        case 'f': value += '\f'; break;
        case 'v': value += '\v'; break;
        case 'a': value += '\a'; break;
        case '0': value += '\0'; break;
        case '\\': value += '\\'; break;
        case '"': value += '"'; break;
        case '\'': value += '\''; break;
        default: value += e; break;
      }
    } else {
      value += c;
    }
  }
  if (atEnd()) error("string não terminada");
  advance(); // closing quote
  Token t = makeToken(TokenType::StringLiteral, value);
  (void)startCol;
  return t;
}

Token Lexer::readChar() {
  advance(); // opening quote
  int value = 0;
  if (atEnd()) error("char não terminado");
  char c = advance();
  if (c == '\\') {
    if (atEnd()) error("escape incompleto em char literal");
    char e = advance();
    switch (e) {
      case 'n': value = '\n'; break;
      case 't': value = '\t'; break;
      case 'r': value = '\r'; break;
      case 'b': value = '\b'; break;
      case 'f': value = '\f'; break;
      case 'v': value = '\v'; break;
      case 'a': value = '\a'; break;
      case '0': value = '\0'; break;
      case '\\': value = '\\'; break;
      case '\'': value = '\''; break;
      case '"': value = '"'; break;
      default: error("escape char inválido");
    }
  } else if (c == '\'') {
    error("char vazio");
  } else {
    value = (unsigned char)c;
  }
  if (atEnd() || peek() != '\'') error("char não terminado");
  advance();
  Token t = makeToken(TokenType::CharLiteral, std::string(1, (char)value));
  t.intValue = value;
  return t;
}

Token Lexer::readIdentifier() {  size_t start = pos_;
  while (!atEnd()) {
    unsigned char c = (unsigned char)peek();
    if (std::isalnum(c) || c == '_') { advance(); continue; }
    // M20.3.3: UTF-8 multibyte identifiers (acentos, símbolos)
    if (c >= 0x80) { advance(); continue; }
    break;
  }
  std::string text = src_.substr(start, pos_ - start);
  const auto& kw = keywordMap();
  auto it = kw.find(text);
  if (it != kw.end()) return makeToken(it->second, text);
  return makeToken(TokenType::Identifier, text);
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> tokens;
  skipWhitespaceAndComments();
  while (!atEnd()) {
    char c = peek();
    Token t{};
    switch (c) {
      case '(': t = makeToken(TokenType::LParen, "("); advance(); break;
      case ')': t = makeToken(TokenType::RParen, ")"); advance(); break;
      case '{': t = makeToken(TokenType::LBrace, "{"); advance(); break;
      case '}': t = makeToken(TokenType::RBrace, "}"); advance(); break;
      case '[': t = makeToken(TokenType::LBracket, "["); advance(); break;
      case ']': t = makeToken(TokenType::RBracket, "]"); advance(); break;
      case ',': t = makeToken(TokenType::Comma, ","); advance(); break;
      case ';': t = makeToken(TokenType::Semicolon, ";"); advance(); break;
      case ':': t = makeToken(TokenType::Colon, ":"); advance(); break;
      case '.': {
        if (peek(1) == '.') {
          if (peek(2) == '.') { t = makeToken(TokenType::Range, "..."); advance(); advance(); advance(); }
          else { t = makeToken(TokenType::Range, ".."); advance(); advance(); }
        } else { t = makeToken(TokenType::Dot, "."); advance(); }
        break;
      }
      case '?': {
        if (peek(1) == '.') { t = makeToken(TokenType::QuestionDot, "?."); advance(); advance(); }
        else if (peek(1) == '?') { t = makeToken(TokenType::QuestionQuestion, "??"); advance(); advance(); }
        else if (peek(1) == '[') { t = makeToken(TokenType::QuestionBracket, "?["); advance(); advance(); }
        else { t = makeToken(TokenType::Question, "?"); advance(); }
        break;
      }
      case '@': t = makeToken(TokenType::At, "@"); advance(); break;
      case '+': {
        if (peek(1) == '+') { t = makeToken(TokenType::PlusPlus, "++"); advance(); advance(); }
        else if (peek(1) == '=') { t = makeToken(TokenType::PlusAssign, "+="); advance(); advance(); }
        else { t = makeToken(TokenType::Plus, "+"); advance(); }
        break;
      }
      case '-': {
        if (peek(1) == '-') { t = makeToken(TokenType::MinusMinus, "--"); advance(); advance(); }
        else if (peek(1) == '=') { t = makeToken(TokenType::MinusAssign, "-="); advance(); advance(); }
        else if (peek(1) == '>') { t = makeToken(TokenType::Arrow, "->"); advance(); advance(); }
        else { t = makeToken(TokenType::Minus, "-"); advance(); }
        break;
      }
      case '*': {
        if (peek(1) == '=') { t = makeToken(TokenType::StarAssign, "*="); advance(); advance(); }
        else { t = makeToken(TokenType::Star, "*"); advance(); }
        break;
      }
      case '/': {
        if (peek(1) == '=') { t = makeToken(TokenType::SlashAssign, "/="); advance(); advance(); }
        else { t = makeToken(TokenType::Slash, "/"); advance(); }
        break;
      }
      case '%': {
        if (peek(1) == '=') { t = makeToken(TokenType::PercentAssign, "%="); advance(); advance(); }
        else { t = makeToken(TokenType::Percent, "%"); advance(); }
        break;
      }
      case '=': {
        if (peek(1) == '=') { t = makeToken(TokenType::EqEq, "=="); advance(); advance(); }
        else if (peek(1) == '>') { t = makeToken(TokenType::FatArrow, "=>"); advance(); advance(); }
        else { t = makeToken(TokenType::Assign, "="); advance(); }
        break;
      }
      case '!': {
        if (peek(1) == '=') { t = makeToken(TokenType::NotEq, "!="); advance(); advance(); }
        else { t = makeToken(TokenType::Bang, "!"); advance(); }
        break;
      }
      case '<': {
        if (peek(1) == '<') { t = makeToken(TokenType::Shl, "<<"); advance(); advance(); }
        else if (peek(1) == '=') { t = makeToken(TokenType::LtEq, "<="); advance(); advance(); }
        else { t = makeToken(TokenType::Lt, "<"); advance(); }
        break;
      }
      case '>': {
        if (peek(1) == '>') { t = makeToken(TokenType::Shr, ">>"); advance(); advance(); }
        else if (peek(1) == '=') { t = makeToken(TokenType::GtEq, ">="); advance(); advance(); }
        else { t = makeToken(TokenType::Gt, ">"); advance(); }
        break;
      }
      case '&': {
        if (peek(1) == '&') { t = makeToken(TokenType::AndAnd, "&&"); advance(); advance(); }
        else { t = makeToken(TokenType::Amp, "&"); advance(); }
        break;
      }
      case '|': {
        if (peek(1) == '|') { t = makeToken(TokenType::OrOr, "||"); advance(); advance(); }
        else { t = makeToken(TokenType::Pipe, "|"); advance(); }
        break;
      }
      case '^': t = makeToken(TokenType::Caret, "^"); advance(); break;
      case '~': t = makeToken(TokenType::Tilde, "~"); advance(); break;
      case '"': {
        // M20.3.5: multiline strings """...""" (raw, sem escape)
        if (peek(1) == '"' && peek(2) == '"') {
          advance(); advance(); advance();
          std::string value;
          while (!atEnd()) {
            if (peek() == '"' && peek(1) == '"' && peek(2) == '"') {
              advance(); advance(); advance();
              break;
            }
            value += advance();
          }
          if (atEnd()) error("string multilinha não terminada");
          t = makeToken(TokenType::StringLiteral, value);
          break;
        }
        t = readString();
        break;
      }
      case '\'': t = readChar(); break;
      default: {
        if (std::isdigit(static_cast<unsigned char>(c))) {
          t = readNumber();
        } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || (static_cast<unsigned char>(c) >= 0x80)) {
          // M20.3.4: raw strings r"..." ou r#"..."# (delimitador custom)
          if (c == 'r' && pos_ + 1 < src_.size() &&
              (src_[pos_ + 1] == '"' ||
               (src_[pos_ + 1] == '#' && pos_ + 2 < src_.size() && src_[pos_ + 2] == '"'))) {
            int hashes = 0;
            size_t pi = pos_ + 1;
            while (pi < src_.size() && src_[pi] == '#') { hashes++; pi++; }
            advance();  // r
            for (int i = 0; i < hashes; i++) advance(); // #
            advance();  // "
            std::string value;
            while (!atEnd()) {
              if (peek() == '"') {
                bool ok = true;
                for (int i = 0; i < hashes; i++) if (peek(1 + i) != '#') { ok = false; break; }
                if (ok) {
                  advance();  // "
                  for (int i = 0; i < hashes; i++) advance(); // #
                  break;
                }
              }
              value += advance();
            }
            if (atEnd()) error("raw string não terminada");
            t = makeToken(TokenType::StringLiteral, value);
            break;
          }
          t = readIdentifier();
        } else {
          error(std::string("caractere inesperado '") + c + "'");
        }
        break;
      }
    }
    tokens.push_back(t);
    skipWhitespaceAndComments();
  }
  Token eof = makeToken(TokenType::EndOfFile, "");
  tokens.push_back(eof);
  return tokens;
}

} // namespace hphl