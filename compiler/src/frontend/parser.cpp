#include "parser.h"
#include "messages/message_loader.h"
#include <sstream>
#include <unordered_map>

namespace hphl {

Parser::Parser(std::vector<Token> tokens, const std::string& filename)
    : tokens_(std::move(tokens)), filename_(filename) {}

const Token& Parser::peek(size_t lookahead) const {
  size_t i = pos_ + lookahead;
  if (i >= tokens_.size()) i = tokens_.size() - 1;
  return tokens_[i];
}

const Token& Parser::current() const { return tokens_[pos_]; }

void Parser::splitShr() {
  // current() é o Shr (">>"): este nível consome o primeiro '>' e insere um
  // '>' residual logo em seguida, consumido pelo genérico externo.
  Token gt = current();
  gt.type = TokenType::Gt;
  gt.text = ">";
  tokens_[pos_] = gt;
  pos_++;
  tokens_.insert(tokens_.begin() + pos_, gt);
}

bool Parser::check(TokenType type) const { return current().type == type; }

bool Parser::match(TokenType type) {
  if (check(type)) {
    pos_++;
    return true;
  }
  return false;
}

bool Parser::atIdent() const {
  return atIdentAt(pos_);
}

bool Parser::atIdentAt(size_t pos) const {
  if (pos >= tokens_.size()) return false;
  TokenType t = tokens_[pos].type;
  if (t == TokenType::Identifier) return true;
  if (t == TokenType::KwPool) return true;
  // `base` sozinho e nome; `base.` continua sendo chamada da base
  if (t == TokenType::KwBase) {
    size_t nx = pos + 1 < tokens_.size() ? pos + 1 : tokens_.size() - 1;
    return tokens_[nx].type != TokenType::Dot;
  }
  return false;
}

const Token& Parser::expect(TokenType type, const std::string& what) {
  if (!check(type)) error(current(), hphl::messages().get("parser_expected", {what}));
  return tokens_[pos_++];
}

const Token& Parser::expectIdentifier(const std::string& what) {
  if (!atIdent())
    error(current(), hphl::messages().get("parser_expected", {what}));
  return tokens_[pos_++];
}

std::string Parser::expectIdentOrKeyword(const std::string& what) {
  if (atIdent() ||
      (current().type >= TokenType::KwPublic && current().type <= TokenType::KwVoid)) {
    return tokens_[pos_++].text;
  }
  error(current(), hphl::messages().get("parser_expected", {what}));
}

void Parser::error(const Token& tok, const std::string& msg) {
  std::ostringstream ss;
  std::string prefix = hphl::messages().get("syntax_error_prefix");
  std::string found = hphl::messages().get("parser_found", {tok.text});
  ss << filename_ << ":" << tok.line << ":" << tok.column << ": " << prefix << ": " << msg
     << " (" << found << ")";
  throw CompileError{tok.line, tok.column, ss.str()};
}

// ---------------------------------------------------------------------------
// Genéricos: parâmetros de tipo e constraints (`where`)
// ---------------------------------------------------------------------------
std::vector<TypeParam> Parser::parseTypeParams() {
  std::vector<TypeParam> ps;
  expect(TokenType::Lt, "'<' para parâmetros de tipo");
  ps.push_back(parseTypeParam());
  while (match(TokenType::Comma)) ps.push_back(parseTypeParam());
  if (!match(TokenType::Gt)) {
    if (current().type == TokenType::Shr) {
      splitShr(); // `>>` fecha dois genéricos aninhados
    } else {
      error(current(), "'>' esperado para fechar parâmetros de tipo");
    }
  }
  return ps;
}

TypeParam Parser::parseTypeParam() {
  TypeParam p;
  // M10 (v0.45): parâmetro de valor `const int N` / `int N` (genéricos com valor)
  if (check(TokenType::KwConst)) pos_++;
  if (isNumericTypeKeyword(current().type)) {
    p.isValue = true;
    pos_++; // tipo do valor (int/u8..u64) — semântica exige inteiro
  }
  if (match(TokenType::KwOut)) {
    p.variance = Variance::Covariant;
  } else if (match(TokenType::KwIn)) {
    p.variance = Variance::Contravariant;
  }
  p.name = expectIdentifier("nome do parâmetro de tipo").text;
  if (match(TokenType::KwWhere)) {
    // `where T : class, Nome, new()`
    expect(TokenType::Colon, "':' após o parâmetro em 'where'");
    do {
      p.constraints.push_back(parseConstraintName());
    } while (match(TokenType::Comma));
  }
  return p;
}

// `where T : class, NomeClasse` — anexa a T aos constraints já declarados
void Parser::parseWhereClause(std::vector<TypeParam>& params) {
  std::string tname = expectIdentifier("nome do parâmetro de tipo").text;
  expect(TokenType::Colon, "':' após o parâmetro em 'where'");
  std::vector<std::string> cons;
  do {
    cons.push_back(parseConstraintName());
  } while (match(TokenType::Comma));
  for (auto& p : params)
    if (p.name == tname) {
      p.constraints.insert(p.constraints.end(), cons.begin(), cons.end());
      return;
    }
  error(tokens_[pos_ - 1], "parâmetro de tipo '" + tname + "' não declarado");
}

std::string Parser::parseConstraintName() {
  if (check(TokenType::KwClass) || check(TokenType::KwStruct)) {
    std::string s = current().text;
    pos_++;
    return s;
  }
  if (check(TokenType::KwNew)) {
    pos_++;
    expect(TokenType::LParen, "'(' após 'new' em constraint");
    expect(TokenType::RParen, "')' em constraint 'new()'");
    return "new()";
  }
  // M10.1b: `supports +` / `supports ==` — operador como capacidade
  if (current().type == TokenType::Identifier && current().text == "supports") {
    static const std::unordered_map<int, std::string> opNames = {
        {(int)TokenType::Plus, "+"},   {(int)TokenType::Minus, "-"},
        {(int)TokenType::Star, "*"},   {(int)TokenType::Slash, "/"},
        {(int)TokenType::Percent, "%"},{(int)TokenType::EqEq, "=="},
        {(int)TokenType::NotEq, "!="}, {(int)TokenType::Lt, "<"},
        {(int)TokenType::Gt, ">"},     {(int)TokenType::LtEq, "<="},
        {(int)TokenType::GtEq, ">="},
    };
    pos_++; // 'supports'
    auto it = opNames.find((int)current().type);
    if (it == opNames.end()) {
      error(current(), "operador esperado após 'supports' em constraint "
                       "(+, -, *, /, %, ==, !=, <, >, <=, >=)");
    }
    std::string c = "supports " + it->second;
    pos_++;
    return c;
  }
  // capacidades por nome: immutable/sendable/shareable/thread_safe e nomes
  // de classe/interface/traço (opcionalmente com módulo e argumentos de tipo)
  std::string name = expectIdentifier("constraint").text;
  while (check(TokenType::Dot)) {
    pos_++;
    name += "." + expectIdentifier("nome").text;
  }
  if (check(TokenType::Lt)) {
    pos_++;
    name += "<";
    name += parseConstraintName();
    while (match(TokenType::Comma)) {
      name += ", ";
      name += parseConstraintName();
    }
    if (!match(TokenType::Gt)) {
      if (current().type == TokenType::Shr) {
        splitShr();
      } else {
        expect(TokenType::Gt, "'>' para fechar argumentos de tipo da constraint");
      }
    }
    name += ">";
  }
  return name;
}

// ---------------------------------------------------------------------------
// Programa
// ---------------------------------------------------------------------------
std::unique_ptr<Program> Parser::parseProgram() {
  auto prog = std::make_unique<Program>();
  prog->moduleName = "main";
  while (!check(TokenType::EndOfFile)) {
    if (check(TokenType::KwModule)) {
      auto mod = parseModule();
      if (prog->moduleName == "main") prog->moduleName = mod->name;
      if (!mod->dependsOn.empty())
        prog->moduleDeps.push_back({mod->name, mod->dependsOn});
      for (auto& d : mod->decls) prog->decls.push_back(std::move(d));
    } else {
      auto decls = parseTopLevelDecl();
      for (auto& d : decls) prog->decls.push_back(std::move(d));
    }
  }
  return prog;
}

std::unique_ptr<ModuleDecl> Parser::parseModule() {
  expect(TokenType::KwModule, "'module'");
  auto mod = std::make_unique<ModuleDecl>();
  // nome pode ser 'A.B.C'
  mod->name = expectIdentOrKeyword("nome do módulo");
  while (check(TokenType::Dot)) {
    pos_++;
    mod->name += "." + expectIdentOrKeyword("nome do módulo");
  }
  // dependências declaradas: `module X depends on A, B;`
  if (match(TokenType::KwDepends)) {
    const Token& on = expectIdentifier("'on' após 'depends'");
    if (on.text != "on") error(on, "esperava 'on' após 'depends'");
    for (;;) {
      std::string dep = expectIdentOrKeyword("nome do módulo dependido");
      while (check(TokenType::Dot)) {
        pos_++;
        dep += "." + expectIdentOrKeyword("nome do módulo dependido");
      }
      mod->dependsOn.push_back(dep);
      if (!match(TokenType::Comma)) break;
    }
  }
  if (check(TokenType::Semicolon)) {
    // forma curta: `module Nome;` (sem bloco)
    pos_++;
    return mod;
  }
  expect(TokenType::LBrace, "'{' após nome do módulo");
  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    auto decls = parseTopLevelDecl();
    for (auto& d : decls) mod->decls.push_back(std::move(d));
  }
  expect(TokenType::RBrace, "'}' para fechar módulo");
  return mod;
}

std::unique_ptr<ImportDecl> Parser::parseImport() {
  expect(TokenType::KwImport, "'import'");
  auto imp = std::make_unique<ImportDecl>();
  if (check(TokenType::StringLiteral)) {
    imp->isPath = true;
    imp->path = tokens_[pos_++].text;
  } else {
    imp->name = expectIdentOrKeyword("nome do módulo");
    while (check(TokenType::Dot)) {
      pos_++;
      imp->name += "." + expectIdentOrKeyword("nome do módulo");
    }
  }
  // `import X as Y;` — alias local para o módulo
  if (match(TokenType::KwAs)) {
    imp->alias = expectIdentOrKeyword("nome do alias");
  }
  expect(TokenType::Semicolon, "';' depois do import");
  return imp;
}

// `using X = Tipo;` (alias) OU `public use Mod.Nome;` (reexportação)
std::unique_ptr<UsingDecl> Parser::parseUsingDecl() {
  auto ud = std::make_unique<UsingDecl>();
  if (check(TokenType::KwUsing)) {
    pos_++;
    ud->alias = expectIdentifier("nome do alias").text;
    expect(TokenType::Assign, "'=' após o alias");
    ud->target = parseType();
    expect(TokenType::Semicolon, "';' depois do 'using'");
    ud->isReexport = false;
    return ud;
  }
  // 'use' — reexportação (public já foi consumido pelo caller)
  pos_++;
  std::string name = expectIdentifier("nome do tipo (reexportação)").text;
  while (check(TokenType::Dot)) {
    pos_++;
    name += "." + expectIdentifier("nome do tipo (reexportação)").text;
  }
  if (name.find('.') == std::string::npos)
    error(tokens_[pos_ - 1], "'public use' exige nome qualificado (Mod.Nome)");
  ud->target = Type::makeClass(name);
  ud->alias = name.substr(name.find_last_of('.') + 1);
  expect(TokenType::Semicolon, "';' depois do 'use'");
  ud->isReexport = true;
  return ud;
}

Parser::ParsedAttributes Parser::parseAttributes() {
  ParsedAttributes attrs;
  while (check(TokenType::LBracket) && peek(1).type == TokenType::LBracket) {
    pos_ += 2; // consome "[["
    while (!check(TokenType::RBracket) && !check(TokenType::EndOfFile)) {
      std::string name;
      if (atIdent()) {
        name = expectIdentifier("nome do atributo").text;
      } else if (check(TokenType::KwInline)) {
        name = "inline";
        pos_++;
      } else {
        name = current().text;
        pos_++;
      }

      std::string arg;
      if (match(TokenType::LParen)) {
        if (check(TokenType::StringLiteral)) {
          arg = tokens_[pos_++].text;
        } else if (!check(TokenType::RParen)) {
          arg = current().text;
          pos_++;
        }
        expect(TokenType::RParen, "')' para fechar argumento do atributo");
      }

      if (name == "deprecated") {
        attrs.isDeprecated = true;
        attrs.deprecatedReason = arg;
      } else if (name == "suppress_deprecation") {
        attrs.suppressDeprecation = true;
      } else if (name == "inline") {
        attrs.isInline = true;
      } else if (name == "noreturn") {
        attrs.isNoReturn = true;
      } else if (name == "packed") {
        attrs.isPacked = true;
      } else if (name == "no_gc") {
        attrs.isNoGc = true;
      }

      if (!match(TokenType::Comma)) break;
    }
    expect(TokenType::RBracket, "']' para fechar atributo");
    expect(TokenType::RBracket, "']' para fechar atributo");
  }
  return attrs;
}

std::vector<std::unique_ptr<Decl>> Parser::parseTopLevelDecl() {
  ParsedAttributes attrs = parseAttributes();
  auto applyAttrs = [&](std::vector<std::unique_ptr<Decl>> v) {
    for (auto& d : v) {
      if (attrs.isDeprecated && d) {
        d->isDeprecated = true;
        d->deprecatedReason = attrs.deprecatedReason;
      }
      if (attrs.isInline && d && d->kind == DeclKind::Function) {
        static_cast<FunctionDecl*>(d.get())->isInline = true;
      }
    }
    return v;
  };
  auto one = [&](std::unique_ptr<Decl> d) {
    std::vector<std::unique_ptr<Decl>> v;
    v.push_back(std::move(d));
    return applyAttrs(std::move(v));
  };
  // import pode aparecer no topo (fora de módulo/bloco também)
  if (check(TokenType::KwImport)) return one(parseImport());
  // `using X = Tipo;` — alias de tipo
  if (check(TokenType::KwUsing)) return one(parseUsingDecl());

  // `derive Serializable` antes da declaração (spec 37/38): prefixo
  // `derive Equatable, Comparable` / `derive(Equatable, Comparable)` /
  // `derive A derive B` — associa ao enum seguinte.
  std::vector<std::string> traitDerives;
  bool hasDerive = false;
  while (check(TokenType::KwDerive)) {
    hasDerive = true;
    pos_++;
    if (match(TokenType::LParen)) {
      while (!check(TokenType::RParen)) {
        traitDerives.push_back(expectIdentifier("traço derivado").text);
        if (!match(TokenType::Comma)) break;
      }
      expect(TokenType::RParen, "')' para fechar derive(...)");
    } else {
      traitDerives.push_back(expectIdentifier("traço derivado").text);
      while (match(TokenType::Comma) && atIdent())
        traitDerives.push_back(expectIdentifier("traço derivado").text);
    }
  }

  // acesso opcional
  Access access = Access::Public;
  if (check(TokenType::KwPublic)) { pos_++; }
  else if (check(TokenType::KwPrivate)) { access = Access::Private; pos_++; }
  else if (check(TokenType::KwInternal)) { access = Access::Internal; pos_++; }
  else if (check(TokenType::KwProtected)) { access = Access::Protected; pos_++; }

  if (!attrs.hasAny()) {
    ParsedAttributes a2 = parseAttributes();
    if (a2.hasAny()) attrs = a2;
  }

  // FFI nativo: `extern ["lib"] [tipo] Nome(params);` (sem corpo)
  if (check(TokenType::KwExtern)) {
    pos_++; // 'extern'
    std::string lib;
    if (check(TokenType::StringLiteral)) {
      lib = current().text;
      pos_++;
    }
    // [tipo ret] Nome ( ... ) ;
    if (isTypeStart()) {
      size_t save = pos_;
      Type ret = parseType();
      if (atIdent() &&
          (peek(1).type == TokenType::LParen || peek(1).type == TokenType::Lt)) {
        Token nameTok = expectIdentifier("nome da função externa");
        auto fn = parseFunctionDecl(access, false, false, "", false,
                                    nameTok, true, ret, true);
        fn->isExtern = true;
        fn->externLib = lib;
        if (fn->body) error(nameTok, "função extern não pode ter corpo");
        return one(std::move(fn));
      }
      pos_ = save;
    }
    Token nameTok = expectIdentifier("nome da função externa");
    auto fn = parseFunctionDecl(access, false, false, "", false,
                                nameTok, false, Type::makeVoid(), true);
    fn->isExtern = true;
    fn->externLib = lib;
    if (fn->body) error(nameTok, "função extern não pode ter corpo");
    return one(std::move(fn));
  }

  // M10 (v0.46): `compiletime int F(int v)` — função avaliada em compilação
  while (match(TokenType::KwCompiletime)) fnCompiletimePending_ = true;

  if (!attrs.hasAny()) {
    ParsedAttributes a2 = parseAttributes();
    if (a2.hasAny()) attrs = a2;
  }

  // reexportação: `public use Mod.Nome;` (ou `use Mod.Nome;`)
  if (atIdent() && current().text == "use") {
    return one(parseUsingDecl());
  }

  // M10 (v0.46): especialização manual — `specialize Buffer<int, 64>;`
  if (check(TokenType::KwSpecialize)) {
    pos_++;
    auto sd = std::make_unique<SpecializeDecl>();
    sd->line = current().line;
    sd->type = parseType();
    expect(TokenType::Semicolon, "';' após 'specialize <Tipo>'");
    return one(std::move(sd));
  }

  // M10.1c (v0.46): reflexão opt-in — `reflect Player;` (spec §40)
  if (check(TokenType::KwReflect)) {
    pos_++;
    auto rd = std::make_unique<ReflectDecl>();
    rd->line = current().line;
    rd->typeName = expectIdentifier("nome do tipo refletido").text;
    while (check(TokenType::Dot)) {
      pos_++;
      rd->typeName += "." + expectIdentifier("nome do tipo refletido").text;
    }
    expect(TokenType::Semicolon, "';' após 'reflect <Tipo>'");
    return one(std::move(rd));
  }

  if (check(TokenType::KwClass) || check(TokenType::KwStruct)) {
    bool isStruct = check(TokenType::KwStruct);
    pos_++;
    auto cd = parseClassDecl(isStruct, false, expectIdentifier("nome da classe"));
    if (hasDerive) {
      // `derive` prefixo (v0.25.0): Equatable/Comparable/Hashable/Cloneable —
      // os operadores/helpers são sintetizados na semântica
      static_cast<ClassDecl*>(cd.get())->derives = traitDerives;
    }
    return one(std::move(cd));
  }
  if (check(TokenType::KwActor)) {
    // `actor Nome { ... }` (v0.23.0, spec §10): classe cujo estado próprio é
    // acessível só por métodos; chamadas externas são serializadas em runtime
    pos_++;
    auto cd = parseClassDecl(false, false, expectIdentifier("nome do actor"));
    static_cast<ClassDecl*>(cd.get())->isActor = true;
    if (hasDerive) static_cast<ClassDecl*>(cd.get())->derives = traitDerives;
    return one(std::move(cd));
  }
  if (check(TokenType::KwInterface)) {
    pos_++;
    auto cd = parseClassDecl(false, true, expectIdentifier("nome da interface"));
    if (hasDerive) static_cast<ClassDecl*>(cd.get())->derives = traitDerives;
    return one(std::move(cd));
  }
  if (check(TokenType::KwEnum)) {
    pos_++;
    auto en = parseEnumDecl(expectIdentifier("nome do enum"));
    if (hasDerive) {
      // `derive` prefixo anexa os traços ao enum (posfixo não suportado)
      static_cast<EnumDecl*>(en.get())->derives = traitDerives;
    }
    return one(std::move(en));
  }
  if (hasDerive)
    error(current(), "'derive' exige uma declaração de enum ou tipo");
  if (check(TokenType::KwStatic) || check(TokenType::KwInline)) {
    bool isStatic = false, isInline = false;
    while (check(TokenType::KwStatic) || check(TokenType::KwInline)) {
      if (check(TokenType::KwStatic)) isStatic = true;
      else isInline = true;
      pos_++;
    }
    // [tipo] Nome(...) — genérico usa '<' após o nome
    if (isTypeStart()) {
      Type ret = parseType();
      if (atIdent() && peek(1).type == TokenType::Lt)
        return one(parseFunctionDecl(access, isStatic, isInline, "", false,
                                     expectIdentifier("nome da função"), true, ret));
      return one(parseFunctionDecl(access, isStatic, isInline, "", false,
                                   expectIdentifier("nome da função"), true, ret));
    }
    return one(parseFunctionDecl(access, isStatic, isInline, "", false,
                                 expectIdentifier("nome da função"), false, Type::makeVoid()));
  }

  // função: [tipo ret] Nome ( ... )
  if (check(TokenType::KwAsync)) {
    // `async [static] [tipo ret] Nome(...)` — spec §10: a chamada devolve
    // task<T> e o corpo roda numa thread; sem tipo de retorno = task<void>
    pos_++; // 'async'
    bool isStatic = false, isInline = false;
    while (check(TokenType::KwStatic) || check(TokenType::KwInline)) {
      if (check(TokenType::KwStatic)) isStatic = true;
      else isInline = true;
      pos_++;
    }
    if (isTypeStart()) {
      size_t save = pos_;
      Type ret = parseType();
      if (atIdent() &&
          (peek(1).type == TokenType::LParen || peek(1).type == TokenType::Lt)) {
        return one(parseFunctionDecl(access, isStatic, isInline, "", false,
                                     expectIdentifier("nome da função"), true, ret, false, true));
      }
      pos_ = save;
    }
    return one(parseFunctionDecl(access, isStatic, isInline, "", false,
                                 expectIdentifier("nome da função"), false, Type::makeVoid(),
                                 false, true));
  }

  if (check(TokenType::LParen)) {
    // M10.1b: função com retorno tupla: `(int, int) Obter()`
    size_t save = pos_;
    Type ret = parseType();
    if (ret.kind == Type::Kind::Tuple && atIdent() &&
        peek(1).type == TokenType::LParen) {
      return one(parseFunctionDecl(access, false, false, "", false,
                                   expectIdentifier("nome da função"), true, ret));
    }
    pos_ = save;
  }

  if (isTypeStart()) {
    // cuidado: pode ser declaração de variável global: `int x = 5;`
    // distinguimos por lookahead: tipo, depois Ident, depois '(' → função
    // (ou '<' quando genérica: `T F<T>(...)`)
    size_t save = pos_;
    Type ret = parseType();
    if (atIdent() &&
        (peek(1).type == TokenType::LParen || peek(1).type == TokenType::Lt)) {
      return one(parseFunctionDecl(access, false, false, "", false,
                                   expectIdentifier("nome da função"), true, ret));
    }
    pos_ = save;
  } else if (atIdent() && peek(1).type == TokenType::LParen) {
    return one(parseFunctionDecl(access, false, false, "", false,
                                 expectIdentifier("nome da função"), false, Type::makeVoid()));
  }

  // variável global (M5: múltiplas na mesma linha — `int a = 1, b = 2;`)
  auto globals = parseGlobalVarDecl();
  std::vector<std::unique_ptr<Decl>> out;
  for (auto& g : globals) out.push_back(std::move(g));
  return applyAttrs(std::move(out));
}

std::unique_ptr<Decl> Parser::parseClassDecl(bool isStruct, bool isInterface,
                                             const Token& nameTok) {
  auto cls = std::make_unique<ClassDecl>();
  cls->isStruct = isStruct;
  cls->isInterface = isInterface;
  cls->name = nameTok.text;
  cls->line = nameTok.line;

  // genérico: `class X<T, U>` — lista antes de ':'/'{'
  if (check(TokenType::Lt)) {
    if (isInterface) {
      // A2: interface genérica agora aceita (warning, monomorfização parcial)
      // Apenas emitimos o aviso na semântica
    }
    cls->typeParams = parseTypeParams();
    while (match(TokenType::KwWhere)) parseWhereClause(cls->typeParams);
  }

  // herança/interfaces: todos os nomes pós ':' vão para `interfaces`;
  // a semântica separa base (classe) de interfaces
  if (match(TokenType::Colon)) {
    while (!check(TokenType::LBrace) && !check(TokenType::EndOfFile)) {
      std::string ifaceName = expectIdentifier("tipo base ou interface").text;
      // A2: aceitar interface genérica (ex.: I<int>) preservando os type args
      std::vector<Type> args;
      if (check(TokenType::Lt)) {
        pos_++; // '<'
        args.push_back(parseType());
        while (match(TokenType::Comma)) args.push_back(parseType());
        if (!match(TokenType::Gt)) {
          if (current().type == TokenType::Shr) splitShr();
          else error(current(), "'>' esperado para fechar 'Interface<'");
        }
      }
      cls->interfaces.push_back(ifaceName);
      cls->interfaceTypeArgs.push_back(std::move(args));
      if (!match(TokenType::Comma)) break;
    }
  }
  expect(TokenType::LBrace, "'{' para abrir classe");

  for (auto& tp : cls->typeParams) typeParamStack_.push_back(tp.name);

  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    if (check(TokenType::KwEnum)) {
      pos_++;
      auto e = parseEnumDecl(expectIdentifier("enum aninhado"));
      // enum aninhado: nome qualificado, armazenado na classe
      static_cast<EnumDecl*>(e.get())->name = cls->name + "." + static_cast<EnumDecl*>(e.get())->name;
      cls->enums.push_back(std::unique_ptr<EnumDecl>(static_cast<EnumDecl*>(e.release())));
      continue;
    }
    auto member = parseMethodOrCtor(Access::Public, cls->name, cls->isInterface);
    if (member->kind == DeclKind::Function) {
      cls->methods.push_back(
          std::unique_ptr<FunctionDecl>(static_cast<FunctionDecl*>(member.release())));
    } else if (member->kind == DeclKind::GlobalVar) {
      cls->fields.push_back(
          std::unique_ptr<VarDecl>(static_cast<VarDecl*>(member.release())));
    } else if (member->kind == DeclKind::Property) {
      cls->properties.push_back(
          std::unique_ptr<PropertyDecl>(static_cast<PropertyDecl*>(member.release())));
    }
  }
  expect(TokenType::RBrace, "'}' para fechar classe");
  for (size_t i = 0; i < cls->typeParams.size(); i++) typeParamStack_.pop_back();
  return cls;
}

std::unique_ptr<Decl> Parser::parseEnumDecl(const Token& nameTok) {
  auto en = std::make_unique<EnumDecl>();
  en->name = nameTok.text;
  en->line = nameTok.line;
  if (match(TokenType::Colon)) {
    en->baseType = parsePrimitiveType(current().type);
    pos_++;
  }
  expect(TokenType::LBrace, "'{' para abrir enum");
  long long next = 0;
  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    EnumEntry entry;
    entry.name = expectIdentifier("variante do enum").text;
    // payload de dados: `Connected(int ping)`, `Connecting(string addr)`
    if (match(TokenType::LParen)) {
      entry.hasPayload = true;
      while (!check(TokenType::RParen)) {
        EnumParam p;
        p.line = current().line;
        p.type = parseType();
        p.name = expectIdentifier("nome do parâmetro do payload").text;
        entry.params.push_back(p);
        if (!match(TokenType::Comma)) break;
      }
      expect(TokenType::RParen, "')' para fechar payload");
    }
    if (match(TokenType::Assign)) {
      entry.value = expect(TokenType::IntLiteral, "valor inteiro").intValue;
      next = entry.value + 1;
    } else {
      entry.value = next++;
    }
    en->entries.push_back(entry);
    if (!match(TokenType::Comma)) break;
  }
  expect(TokenType::RBrace, "'}' para fechar enum");
  return en;
}

// ---------------------------------------------------------------------------
// Funções e métodos
// ---------------------------------------------------------------------------
std::unique_ptr<FunctionDecl> Parser::parseFunctionDecl(
    Access access, bool isStatic, bool isInline, const std::string& ownerClass,
    bool isMethod, const Token& nameTok, bool hasReturnType, Type retType,
    bool allowBodyless, bool isAsync) {
  auto fn = std::make_unique<FunctionDecl>();
  fn->access = access;
  fn->isStatic = isStatic;
  fn->isInline = isInline;
  fn->isAsync = isAsync;
  if (fnCompiletimePending_) {
    fn->isCompiletime = true;
    fnCompiletimePending_ = false;
  }
  fn->ownerClass = ownerClass;
  fn->isMethod = isMethod;
  fn->name = nameTok.text;
  fn->line = nameTok.line;
  fn->hasReturnType = hasReturnType;
  fn->returnType = hasReturnType ? retType : Type::makeVoid();
  if (isMethod && nameTok.text == ownerClass) fn->isConstructor = true;

  // genérico: `T F<T>(T v)` — lista de parâmetros de tipo antes de '('
  if (check(TokenType::Lt)) {
    if (fn->isConstructor)
      error(nameTok, "construtor não pode ser genérico");
    fn->typeParams = parseTypeParams();
  }
  for (auto& tp : fn->typeParams) typeParamStack_.push_back(tp.name);
  expect(TokenType::LParen, "'(' para parâmetros");
  while (!check(TokenType::RParen)) {
    auto p = std::make_unique<Param>();
    p->line = current().line;
    // ref/out/in: modificadores de passagem por referência (spec §5)
    if (match(TokenType::KwRef)) {
      p->byRef = true;
    } else if (match(TokenType::KwOut)) {
      p->byOut = true;
    } else if (match(TokenType::KwIn)) {
      p->byIn = true;
    }
    p->type = parseType();
    p->name = expectIdentifier("nome do parâmetro").text;
    // `= 0`: valor padrão → parâmetro opcional (spec §5)
    if (match(TokenType::Assign)) {
      p->defaultVal = parseExpression();
    }
    fn->params.push_back(std::move(p));
    if (!match(TokenType::Comma)) break;
  }
  expect(TokenType::RParen, "')' após parâmetros");
  // `where T : class, NomeClasse` — constraints após a assinatura
  while (match(TokenType::KwWhere)) parseWhereClause(fn->typeParams);
  // A4: contratos — `require cond` / `ensure cond` (identificadores
  // contextuais) antes do corpo; `result` só vale no ensure
  while (atIdent() &&
         (current().text == "require" || current().text == "ensure")) {
    bool isReq = current().text == "require";
    int cline = current().line;
    pos_++; // require|ensure
    auto cond = parseExpression();
    cond->line = cline;
    if (isReq) fn->requires.push_back(std::move(cond));
    else fn->ensures.push_back(std::move(cond));
  }
  if (allowBodyless && check(TokenType::Semicolon)) {
    if (!fn->requires.empty() || !fn->ensures.empty())
      error(current(), "require/ensure exige corpo (não vale em assinatura)");
    pos_++;
    fn->body = nullptr; // assinatura de interface
  } else {
    fn->body = parseBlock();
  }
  for (size_t i = 0; i < fn->typeParams.size(); i++) typeParamStack_.pop_back();
  return fn;
}

std::unique_ptr<Decl> Parser::parseMethodOrCtor(Access access,
                                                const std::string& ownerClass,
                                                bool allowBodyless) {
  ParsedAttributes attrs = parseAttributes();
  Access a = access;
  if (check(TokenType::KwPublic)) { pos_++; }
  else if (check(TokenType::KwPrivate)) { a = Access::Private; pos_++; }
  else if (check(TokenType::KwProtected)) { a = Access::Protected; pos_++; }
  else if (check(TokenType::KwInternal)) { a = Access::Internal; pos_++; }
  if (!attrs.hasAny()) {
    ParsedAttributes a2 = parseAttributes();
    if (a2.hasAny()) attrs = a2;
  }

  bool isStatic = false, isInline = false;
  while (check(TokenType::KwStatic) || check(TokenType::KwInline)) {
    if (check(TokenType::KwStatic)) isStatic = true;
    else isInline = true;
    pos_++;
  }
  if (!attrs.hasAny()) {
    ParsedAttributes a2 = parseAttributes();
    if (a2.hasAny()) attrs = a2;
  }
  if (attrs.isInline) isInline = true;

  auto applyAttrs = [&](std::unique_ptr<Decl> m) -> std::unique_ptr<Decl> {
    if (m && attrs.isDeprecated) {
      m->isDeprecated = true;
      m->deprecatedReason = attrs.deprecatedReason;
    }
    return m;
  };

  // A3: async em método de classe (m->isAsync = true; runtime precisa this no env)
  bool methodIsAsync = false;
  if (check(TokenType::KwAsync)) {
    methodIsAsync = true;
    pos_++; // consome o 'async'
  }

  // campo? `int vida = 100;` → tratado aqui (variavel de classe)
  // distinguir: [tipo] Ident { '=' ... | '(' → método | '{' → propriedade }
  // 'atomic' é prefixo de campo: `atomic int saldo;`
  if (check(TokenType::KwAtomic)) {
    auto fields = parseVarDeclStmt(false, false);
    expect(TokenType::Semicolon, "';' após campo");
    auto field = std::move(fields[0]);
    field->access = a;
    field->line = current().line;
    return applyAttrs(std::move(field));
  }
  if (isTypeStart()) {
    size_t save = pos_;
    Type t = parseType();
    if (atIdent()) {
      if (peek(1).type == TokenType::LParen) {
        auto tok = current();
        pos_++;
        return applyAttrs(parseFunctionDecl(a, isStatic, isInline, ownerClass, true, tok, true, t,
                                            allowBodyless, methodIsAsync));
      }
      if (peek(1).type == TokenType::LBrace) {
        // propriedade: `int Vida { get { ... } set { ... } }` (spec §19)
        // M_RV1 A4: propriedade estatica eh suportada (init una vez, acesso
        // via label global .Lg_<Class>_<Prop> sem `this`).
        auto nameTok = current();
        pos_++;
        return applyAttrs(parseProperty(a, nameTok, t, isStatic));
      }
      // campo
      pos_ = save;
      auto fields = parseVarDeclStmt(false, false);
      expect(TokenType::Semicolon, "';' após campo");
      auto field = std::move(fields[0]);
      field->access = a;
      field->line = tokens_[save].line;
      return applyAttrs(std::move(field)); // VarDecl usado como "membro" — tratado na semântica
    }
    pos_ = save;
  }
  if (atIdent() && peek(1).type == TokenType::LParen) {
    auto tok = current();
    pos_++;
    return applyAttrs(parseFunctionDecl(a, isStatic, isInline, ownerClass, true, tok, false,
                                        Type::makeVoid(), allowBodyless, methodIsAsync));
  }
  error(current(), "esperava membro de classe (campo, método ou construtor)");
}

std::unique_ptr<Decl> Parser::parseProperty(Access access, const Token& nameTok,
                                            Type propType, bool isStatic) {
  auto p = std::make_unique<PropertyDecl>();
  p->access = access;
  p->type = propType;
  p->name = nameTok.text;
  p->line = nameTok.line;
  p->isStatic = isStatic; // M_RV1 A4: propriedade estática (init uma vez, sem this)
  expect(TokenType::LBrace, "'{' para abrir propriedade");
  bool seenGet = false, seenSet = false;
  while (!check(TokenType::RBrace)) {
    if (atIdent() && current().text == "get") {
      if (seenGet) error(current(), "propriedade já possui 'get'");
      seenGet = true;
      pos_++;
      p->getBody = parseBlock();
    } else if (atIdent() && current().text == "set") {
      if (seenSet) error(current(), "propriedade já possui 'set'");
      seenSet = true;
      pos_++;
      p->setBody = parseBlock();
    } else {
      error(current(), "esperava 'get' ou 'set' dentro da propriedade");
    }
  }
  expect(TokenType::RBrace, "'}' para fechar propriedade");
  if (!seenGet && !seenSet)
    error(nameTok, "propriedade precisa de 'get' ou 'set'");
  return p;
}

// ---------------------------------------------------------------------------
// Tipos
// ---------------------------------------------------------------------------
// primitivas de sincronização (v0.24.0, spec §10): `mutex/semaphore/event/
// barrier` — semantica e codegen específicos (runtime, não classes)
static bool isPrimitiveSyncType(Type::Kind k) {
  return k == Type::Kind::Mutex || k == Type::Kind::Semaphore ||
         k == Type::Kind::Event || k == Type::Kind::Barrier;
}

bool Parser::isTypeStart() const {
  switch (current().type) {
    case TokenType::KwInt: case TokenType::KwFloat: case TokenType::KwDouble:
    case TokenType::KwBool: case TokenType::KwChar: case TokenType::KwString:
    case TokenType::KwPtr:
    case TokenType::KwU8: case TokenType::KwU16: case TokenType::KwU32:
    case TokenType::KwU64: case TokenType::KwVoid:
    case TokenType::KwTask: case TokenType::KwChannel:
    case TokenType::KwMutex: case TokenType::KwSemaphore:
    case TokenType::KwEvent: case TokenType::KwBarrier:
      return true;
    case TokenType::Identifier:
      // Ident + Ident — tipo (ex.: `Player p;`, `Player pool;`)
      // Ident + '(' — pode ser tipo de retorno `Player Criar()` ou nome de funcao
      if (atIdentAt(pos_ + 1) || peek(1).type == TokenType::Lt) return true;
      // Ident + '.' → nome qualificado (ex.: `Mod.Classe x`); é declaração se
      // depois da sequência de segmentos vier o nome da variável
      if (peek(1).type == TokenType::Dot) {
        size_t i = 2;
        while (pos_ + i < tokens_.size() &&
               tokens_[pos_ + i].type == TokenType::Identifier) {
          if (pos_ + i + 1 < tokens_.size() &&
              tokens_[pos_ + i + 1].type == TokenType::Dot) {
            i += 2;
            continue;
          }
          i++;
          break;
        }
        if (pos_ + i >= tokens_.size()) return false;
        if (tokens_[pos_ + i].type == TokenType::Identifier) return true;
        if (tokens_[pos_ + i].type == TokenType::LBracket) {
          size_t j = i + 1;
          while (pos_ + j < tokens_.size() &&
                 tokens_[pos_ + j].type != TokenType::RBracket &&
                 tokens_[pos_ + j].type != TokenType::EndOfFile) {
            j++;
          }
          return pos_ + j + 1 < tokens_.size() &&
                 tokens_[pos_ + j + 1].type == TokenType::Identifier;
        }
        return false;
      }
      // Ident + '[' → pode ser array: `Player[3] ps;` ou indexação `nums[0] = 1;`
      // distingue-se pelo que vem após o ']': variável (declaração) ou não (expressão)
      if (peek(1).type == TokenType::LBracket) {
        size_t i = 2;
        while (pos_ + i < tokens_.size() &&
               tokens_[pos_ + i].type != TokenType::RBracket &&
               tokens_[pos_ + i].type != TokenType::EndOfFile) {
          i++;
        }
        return pos_ + i + 1 < tokens_.size() &&
               tokens_[pos_ + i + 1].type == TokenType::Identifier;
      }
      return false;
    default:
      return false;
  }
}

// M10 (v0.45): a partir de `Nome <` (pos_ no Nome), decide se `Nome<...> X`
// é declaração de variável de classe genérica: percorre os argumentos
// balanceando <>, e exige Identifier logo após o '>' fechante.
bool Parser::identGenericDeclAhead() const {
  size_t i = pos_ + 2; // depois de '<'
  int depth = 1;
  while (i < tokens_.size()) {
    TokenType t = tokens_[i].type;
    if (t == TokenType::Lt) { depth++; i++; continue; }
    if (t == TokenType::Gt) {
      depth--;
      i++;
      if (depth == 0)
        return i < tokens_.size() && tokens_[i].type == TokenType::Identifier;
      continue;
    }
    if (t == TokenType::Shr) {
      depth -= 2;
      i++;
      if (depth <= 0)
        return depth == 0 && i < tokens_.size() &&
               tokens_[i].type == TokenType::Identifier;
      continue;
    }
    switch (t) {
      case TokenType::Identifier: case TokenType::Comma: case TokenType::Dot:
      case TokenType::KwInt: case TokenType::KwFloat: case TokenType::KwBool:
      case TokenType::KwChar: case TokenType::KwString: case TokenType::KwPtr: case TokenType::KwConst:
      case TokenType::KwTask: case TokenType::KwChannel: case TokenType::KwMutex:
      case TokenType::KwSemaphore: case TokenType::KwEvent:
      case TokenType::KwBarrier: case TokenType::IntLiteral:
        i++;
        continue;
      default:
        // KwU8/KwU16/KwU32/KwU64 são keywords consecutivas no enum
        if (t >= TokenType::KwU8 && t <= TokenType::KwU64) { i++; continue; }
        return false;
    }
  }
  return false;
}

bool Parser::isNumericTypeKeyword(TokenType tt) const {
  return tt == TokenType::KwInt || tt == TokenType::KwFloat || tt == TokenType::KwDouble ||
         tt == TokenType::KwU8 || tt == TokenType::KwU16 || tt == TokenType::KwU32 ||
         tt == TokenType::KwU64;
}

Type Parser::parsePrimitiveType(TokenType tt) {
  switch (tt) {
    case TokenType::KwInt: return Type::makeInt(32); // padrão plataforma (M1: 32 bits)
    case TokenType::KwFloat: return Type::makeFloat(32);
    case TokenType::KwDouble: return Type::makeFloat(64);
    case TokenType::KwBool: return Type::makeBool();
    case TokenType::KwChar: return Type::makeChar();
    case TokenType::KwString: return Type::makeString();
    case TokenType::KwPtr: return Type::makeRawPtr(); // FFI v2: ponteiro opaco (void*)
    case TokenType::KwU8: return Type::makeUInt(8);
    case TokenType::KwU16: return Type::makeUInt(16);
    case TokenType::KwU32: return Type::makeUInt(32);
    case TokenType::KwU64: return Type::makeUInt(64);
    case TokenType::KwVoid: return Type::makeVoid();
    default: break;
  }
  error(current(), "tipo primitivo inválido");
}

Type Parser::parseType() {
  RecursionGuard guard(recursionDepth_, current(), this);
  Type t;
  if (check(TokenType::LParen)) {
    // M10.1b: tipo tupla `(T1, T2, ...)` — nomes de elemento são documentação
    pos_++; // '('
    std::vector<Type> elems;
    elems.push_back(parseType());
    if (atIdent()) pos_++; // nome opcional `(int vida, ...)`
    while (match(TokenType::Comma)) {
      elems.push_back(parseType());
      if (atIdent()) pos_++;
    }
    expect(TokenType::RParen, "')' para fechar o tipo tupla");
    return Type::makeTuple(elems);
  }
  if (check(TokenType::KwInt) || check(TokenType::KwFloat) || check(TokenType::KwDouble) ||
      check(TokenType::KwBool) || check(TokenType::KwChar) || check(TokenType::KwString) ||
      check(TokenType::KwPtr) ||
      check(TokenType::KwU8) || check(TokenType::KwU16) || check(TokenType::KwU32) ||
      check(TokenType::KwU64) || check(TokenType::KwVoid)) {
    t = parsePrimitiveType(current().type);
    pos_++;
  } else if (check(TokenType::KwTask)) {
    // `task<T>` — handle de execução paralela com payload T
    pos_++; // 'task'
    expect(TokenType::Lt, "'<' após 'task'");
    Type pt = parseType();
    if (!match(TokenType::Gt)) {
      if (current().type == TokenType::Shr) {
        splitShr();
      } else {
        error(current(), "'>' esperado para fechar 'task<'");
      }
    }
    t = Type::makeTask(pt);
  } else if (check(TokenType::KwChannel)) {
    // `channel<T>` — fila de comunicação entre threads com slots de 8 bytes
    pos_++; // 'channel'
    expect(TokenType::Lt, "'<' após 'channel'");
    Type pt = parseType();
    if (!match(TokenType::Gt)) {
      if (current().type == TokenType::Shr) {
        splitShr();
      } else {
        error(current(), "'>' esperado para fechar 'channel<'");
      }
    }
    t = Type::makeChannel(pt);
  } else if (check(TokenType::KwMutex)) {
    pos_++; // 'mutex'
    t = Type::makeMutex();
  } else if (check(TokenType::KwSemaphore)) {
    pos_++; // 'semaphore'
    t = Type::makeSemaphore();
  } else if (check(TokenType::KwEvent)) {
    pos_++; // 'event'
    t = Type::makeEvent();
  } else if (check(TokenType::KwBarrier)) {
    pos_++; // 'barrier'
    t = Type::makeBarrier();
  } else if (atIdent()) {
    if (current().text == "list" && peek(1).type == TokenType::Lt) {
      pos_++; // 'list'
      pos_++; // '<'
      Type et = parseType();
      if (!match(TokenType::Gt)) {
        // ">>" do lexer fecha dois aninhamentos (ex.: list<list<int>>)
        if (current().type == TokenType::Shr) {
          splitShr();
        } else {
          error(current(), "'>' esperado para fechar 'list<'");
        }
      }
      t = Type::makeList(et);
    } else if (current().text == "map" && peek(1).type == TokenType::Lt) {
      pos_++; // 'map'
      pos_++; // '<'
      Type kt = parseType();
      expect(TokenType::Comma, "',' entre os parâmetros de 'map<K, V>'");
      Type vt = parseType();
      if (!match(TokenType::Gt)) {
        if (current().type == TokenType::Shr) {
          splitShr();
        } else {
          error(current(), "'>' esperado para fechar 'map<'");
        }
      }
      t = Type::makeMap(kt, vt);
    } else if (current().text == "func" && peek(1).type == TokenType::Lt) {
      // v0.95 (lambdas): `func<R, P...>` — função R(P...); `func<void>` = `void()`
      pos_++; // 'func'
      pos_++; // '<'
      Type rt = parseType();
      std::vector<Type> fparams;
      while (match(TokenType::Comma)) fparams.push_back(parseType());
      if (!match(TokenType::Gt)) {
        if (current().type == TokenType::Shr) {
          splitShr();
        } else {
          error(current(), "'>' esperado para fechar 'func<'");
        }
      }
      t = Type::makeFunc(rt, fparams);
    } else if ((current().text == "Option" || current().text == "option") && peek(1).type == TokenType::Lt) {
      pos_++; // 'Option'
      pos_++; // '<'
      Type et = parseType();
      if (!match(TokenType::Gt)) {
        if (current().type == TokenType::Shr) {
          splitShr();
        } else {
          error(current(), "'>' esperado para fechar 'Option<'");
        }
      }
      t = Type::makeOption(et);
    } else if ((current().text == "Result" || current().text == "result") && peek(1).type == TokenType::Lt) {
      pos_++; // 'Result'
      pos_++; // '<'
      Type ok = parseType();
      expect(TokenType::Comma, "',' entre os parâmetros de 'Result<T, E>'");
      Type err = parseType();
      if (!match(TokenType::Gt)) {
        // ">>" fecha um '>' do Result e outro do tipo externo
        if (current().type == TokenType::Shr) {
          splitShr();
        } else {
          error(current(), "'>' esperado para fechar 'Result<'");
        }
      }
      t = Type::makeResult(ok, err);
    } else {
      t.kind = Type::Kind::Class;
      t.name = current().text;
      pos_++;
      // nome qualificado: A.B.Type (classe/enum em outro módulo)
      while (check(TokenType::Dot)) {
        pos_++;
        t.name += "." + expectIdentifier("nome do tipo").text;
      }
      // parâmetro de tipo em contexto genérico: `class X<T>` → `T` é TypeVar
      // (nome simples, sem pontos — o nome qualificado nunca é TypeVar)
      if (t.name.find('.') == std::string::npos) {
        for (auto& tp : typeParamStack_) {
          if (tp == t.name) {
            t = Type::makeTypeVar(t.name);
            break;
          }
        }
      }
    }
  } else {
    error(current(), "tipo esperado");
  }

  // classe genérica: `Caixa<int>` / `Caixa<int, string>` / `Buffer<int, 64>`
  if (t.kind == Type::Kind::Class && check(TokenType::Lt)) {
    pos_++; // '<'
    // M10 (v0.45): literal inteiro como argumento de valor
    if (check(TokenType::IntLiteral)) {
      t.genericArgs.push_back(
          Type::makeLitValue(current().intValue));
      pos_++;
    } else {
      t.genericArgs.push_back(parseType());
    }
    while (match(TokenType::Comma)) {
      if (check(TokenType::IntLiteral)) {
        t.genericArgs.push_back(Type::makeLitValue(current().intValue));
        pos_++;
      } else {
        t.genericArgs.push_back(parseType());
      }
    }
    if (!match(TokenType::Gt)) {
      if (current().type == TokenType::Shr) {
        splitShr();
      } else {
        error(current(), "'>' esperado para fechar 'Nome<'");
      }
    }
  }

  // tipo parametrizável: int<32> / int<32, wrap> / float<64>
  if (match(TokenType::Lt)) {
    if (!check(TokenType::IntLiteral)) {
      error(current(), "tamanho inteiro esperado em tipo parametrizado");
    }
    int bits = (int)current().intValue;
    pos_++;
    if (match(TokenType::Comma)) {
      // overflow policy
      if (atIdent() || isTypeStart()) {
        std::string pol = current().text;
        pos_++;
        OverflowPolicy op = OverflowPolicy::Default;
        if (pol == "wrap") op = OverflowPolicy::Wrap;
        else if (pol == "checked") op = OverflowPolicy::Checked;
        else if (pol == "saturate") op = OverflowPolicy::Saturate;
        else if (pol == "promote") op = OverflowPolicy::Promote;
        else if (pol != "fixed" && pol != "none") {
          error(tokens_[pos_ - 1], "overflow policy desconhecida '" + pol + "'");
        }
        if (op != OverflowPolicy::Default) t.policy = op;
      } else {
        error(current(), "nome da overflow policy esperado");
      }
    }
    expect(TokenType::Gt, "'>' para fechar tipo parametrizado");
    if (t.policy != OverflowPolicy::Default && t.kind != Type::Kind::Int &&
        t.kind != Type::Kind::UInt && t.kind != Type::Kind::Float) {
      error(tokens_[0], "overflow policy só se aplica a tipos numéricos");
    }
    if (bits <= 0 || bits > 8192) {
      error(tokens_[pos_ - 1], "tamanho em bits inválido (deve ser entre 1 e 8192)");
    }
    if (t.kind == Type::Kind::Int || t.kind == Type::Kind::UInt || t.kind == Type::Kind::Float ||
        t.kind == Type::Kind::Char || t.kind == Type::Kind::Bool || t.kind == Type::Kind::String) {
      t.bits = bits;
    } else {
      error(tokens_[0], "tipo não aceita parâmetro de tamanho em bits");
    }
  }

  // array fixo: int[10] / int[2][3] / T[N] (N = parâmetro de valor do
  // template corrente — M10 v0.45). A sintaxe lê da esquerda para a direita:
  // int[2][3] = array de 2 (arrays de 3 ints). Dimensões simbólicas ficam em
  // arraySizeSym e são concretizadas na instanciação.
  std::vector<int> dims;
  std::vector<std::string> dimSyms;
  while (match(TokenType::LBracket)) {
    if (atIdent() && !check(TokenType::KwConst)) {
      // dimensão simbólica: precisa ser um parâmetro de valor declarado
      std::string sym = current().text;
      bool isParam = false;
      for (auto& tp : typeParamStack_)
        if (tp == sym) { isParam = true; break; }
      if (!isParam)
        error(current(), "tamanho de array deve ser literal inteiro ou parâmetro "
                         "de valor ('" + sym + "' não é parâmetro de tipo)");
      pos_++;
      expect(TokenType::RBracket, "']' para fechar array");
      dims.push_back(-1);
      dimSyms.push_back(sym);
      continue;
    }
    int n = (int)expect(TokenType::IntLiteral, "tamanho do array").intValue;
    if (n <= 0) error(tokens_[0], "tamanho do array deve ser positivo");
    expect(TokenType::RBracket, "']' para fechar array");
    dims.push_back(n);
    dimSyms.push_back("");
  }
  for (size_t k = dims.size(); k >= 1; k--) {
    t = Type::makeArray(t, dims[k - 1]);
    if (!dimSyms[k - 1].empty()) {
      t.arraySize = -1;             // simbólico
      t.arraySizeSym = dimSyms[k - 1];
    }
  }

  // tipo opcional: `T?` ≡ Option<T>
  if (match(TokenType::Question)) {
    t = Type::makeOption(t);
  }
  return t;
}

StoragePolicy Parser::parseStoragePolicy() {
  switch (current().type) {
    case TokenType::KwStack: pos_++; return StoragePolicy::Stack;
    case TokenType::KwHeap: pos_++; return StoragePolicy::Heap;
    case TokenType::KwArena: pos_++; return StoragePolicy::Arena;
    case TokenType::KwPool: pos_++; return StoragePolicy::Pool;
    case TokenType::KwShared: pos_++; return StoragePolicy::Shared;
    case TokenType::KwThreadLocal: pos_++; return StoragePolicy::ThreadLocal;
    default: return StoragePolicy::Auto;
  }
}

// ---------------------------------------------------------------------------
// Declarações de variáveis
// ---------------------------------------------------------------------------
// M5 (v0.36.0): múltiplas variáveis na mesma linha (`int a = 1, b, c = 3;`).
// Devolve um VarDecl POR NOME (mesmo tipo declarado; `var` exige init em
// cada nome — validado na semântica). allowMultiple=false mantém o
// comportamento antigo (campos de classe/struct: vírgula = erro).
std::vector<std::unique_ptr<VarDecl>> Parser::parseVarDeclStmt(bool allowSemicolon,
                                                               bool allowMultiple) {
  std::vector<std::unique_ptr<VarDecl>> out;
  // primeiro nome: parse completo (flags + tipo/'var' + nome [+ init]);
  // nomes seguintes: só nome [+ init] — tipo/flags herdados do primeiro
  auto makeOne = [&](bool first) {
    auto v = std::make_unique<VarDecl>();
    v->line = current().line;
    if (first) {
      if (check(TokenType::KwConst)) { v->isConst = true; pos_++; }
      else if (check(TokenType::KwReadonly)) { v->isReadonly = true; pos_++; }
      else if (check(TokenType::KwVolatile)) { v->isVolatile = true; pos_++; }

      v->storage = parseStoragePolicy();

      if (match(TokenType::KwAtomic)) {
        v->atomic = true;
      }

      if (match(TokenType::KwVar)) {
        if (v->atomic) error(current(), "'atomic' exige tipo explícito (não pode ser 'var')");
        v->isVar = true;
      } else if (isTypeStart()) {
        v->type = parseType();
        v->hasType = true;
      } else {
        error(current(), "tipo ou 'var' esperado");
      }
    } else {
      v->isVar = out[0]->isVar;
      v->hasType = out[0]->hasType;
      v->isConst = out[0]->isConst;
      v->isReadonly = out[0]->isReadonly;
      v->isVolatile = out[0]->isVolatile;
      v->atomic = out[0]->atomic;
      v->storage = out[0]->storage;
      if (out[0]->hasType) v->type = out[0]->type;
    }

    v->name = expectIdentifier("nome da variável").text;
    if (first && isPrimitiveSyncType(v->type.kind) && check(TokenType::LParen)) {
      // primitivas de sincronização (v0.24.0): `semaphore s(3);` `barrier b(4);`
      // (mutex/event não aceitam argumento — validado na semântica)
      pos_++; // '('
      Token t = expect(TokenType::IntLiteral, "inteiro esperado em '" + v->name + "(N)'");
      v->primitiveInit = t.intValue;
      expect(TokenType::RParen, "')' esperado fechando o argumento de '" + v->name + "'");
    } else if (match(TokenType::Assign)) {
      v->init = parseExpression();
    }
    return v;
  };

  out.push_back(makeOne(true));
  while (allowMultiple && match(TokenType::Comma)) {
    out.push_back(makeOne(false));
  }
  if (allowSemicolon) expect(TokenType::Semicolon, "';'");
  return out;
}

std::vector<std::unique_ptr<VarDecl>> Parser::parseGlobalVarDecl() {
  std::vector<std::unique_ptr<VarDecl>> out;
  auto makeOne = [&](bool first) {
    auto v = std::make_unique<VarDecl>();
    v->isGlobal = true;
    v->line = current().line;
    if (first) {
      if (check(TokenType::KwConst)) { v->isConst = true; pos_++; }
      else if (check(TokenType::KwReadonly)) { v->isReadonly = true; pos_++; }
      else if (check(TokenType::KwVolatile)) { v->isVolatile = true; pos_++; }

      v->storage = parseStoragePolicy();
      if (match(TokenType::KwAtomic)) {
        v->atomic = true;
      }
      if (isTypeStart()) {
        v->type = parseType();
        v->hasType = true;
      } else {
        error(current(), "tipo esperado em variável global");
      }
    } else {
      v->hasType = out[0]->hasType;
      v->isConst = out[0]->isConst;
      v->isReadonly = out[0]->isReadonly;
      v->isVolatile = out[0]->isVolatile;
      v->atomic = out[0]->atomic;
      v->storage = out[0]->storage;
      if (out[0]->hasType) v->type = out[0]->type;
    }
    v->name = expectIdentifier("nome da variável").text;
    if (first && isPrimitiveSyncType(v->type.kind) && check(TokenType::LParen)) {
      pos_++; // '('
      Token t = expect(TokenType::IntLiteral, "inteiro esperado em '" + v->name + "(N)'");
      v->primitiveInit = t.intValue;
      expect(TokenType::RParen, "')' esperado fechando o argumento de '" + v->name + "'");
    } else if (match(TokenType::Assign)) {
      v->init = parseExpression();
    }
    return v;
  };

  out.push_back(makeOne(true));
  while (match(TokenType::Comma)) {
    out.push_back(makeOne(false));
  }
  expect(TokenType::Semicolon, "';'");
  return out;
}

// ---------------------------------------------------------------------------
// Instruções
// ---------------------------------------------------------------------------
namespace {
// `pool` contextual no inicio de instrucao: continua declaracao com policy
// se o proximo token pode seguir uma storage policy ([atomic] (var|tipo)).
bool poolDeclContinues(TokenType t) {
  switch (t) {
    case TokenType::KwVar:
    case TokenType::KwAtomic:
    case TokenType::Identifier:
    case TokenType::KwInt: case TokenType::KwFloat: case TokenType::KwDouble:
    case TokenType::KwBool: case TokenType::KwChar: case TokenType::KwString:
    case TokenType::KwPtr:
    case TokenType::KwU8: case TokenType::KwU16: case TokenType::KwU32:
    case TokenType::KwU64: case TokenType::KwVoid:
    case TokenType::KwTask: case TokenType::KwChannel:
    case TokenType::KwMutex: case TokenType::KwSemaphore:
    case TokenType::KwEvent: case TokenType::KwBarrier:
      return true;
    default:
      return false;
  }
}
}  // namespace
std::unique_ptr<Stmt> Parser::parseStatement() {
  RecursionGuard guard(recursionDepth_, current(), this);
  if (check(TokenType::LBracket) && peek(1).type == TokenType::LBracket) {
    ParsedAttributes attrs = parseAttributes();
    auto stmt = parseStatement();
    if (attrs.suppressDeprecation && stmt) {
      if (stmt->kind == StmtKind::Block) {
        static_cast<BlockStmt*>(stmt.get())->suppressDeprecation = true;
      } else {
        auto blk = std::make_unique<BlockStmt>();
        blk->line = stmt->line;
        blk->suppressDeprecation = true;
        blk->stmts.push_back(std::move(stmt));
        return blk;
      }
    }
    return stmt;
  }
  switch (current().type) {
    case TokenType::LBrace: return parseBlock();
    case TokenType::KwIf: return parseIf();
    case TokenType::KwFor: return parseFor();
    case TokenType::KwWhile: return parseWhile();
    case TokenType::KwDo: return parseDoWhile();
    case TokenType::KwBreak: { auto s = std::make_unique<BreakStmt>(); s->line = current().line; pos_++; expect(TokenType::Semicolon, "';'"); return s; }
    case TokenType::KwContinue: { auto s = std::make_unique<ContinueStmt>(); s->line = current().line; pos_++; expect(TokenType::Semicolon, "';'"); return s; }
    case TokenType::KwReturn: return parseReturn();
    case TokenType::KwSwitch: return parseSwitch();
    case TokenType::KwForeach: return parseForeach(false);
    case TokenType::KwConst: case TokenType::KwReadonly:
    case TokenType::KwVolatile:
    case TokenType::KwPool: {
      // contextual: `pool` como variavel/funcao, exceto abrindo declaracao
      // com policy (`pool T x`, `pool var x`, `pool atomic ...`, `pool P p`).
      if (!poolDeclContinues(peek(1).type)) {
        auto s = std::make_unique<ExprStmt>();
        s->expr = parseExpression();
        s->line = s->expr->line;
        expect(TokenType::Semicolon, "';'");
        return s;
      }
      [[fallthrough]];
    }
    case TokenType::KwVar:
    case TokenType::KwAtomic:
    case TokenType::KwStack: case TokenType::KwHeap: case TokenType::KwArena:
    case TokenType::KwShared: case TokenType::KwThreadLocal: {
      // M10.1b: `var (a, b) = expr;` — destructuring de tuple
      if (current().type == TokenType::KwVar && peek(1).type == TokenType::LParen) {
        auto d = std::make_unique<StmtDestructure>();
        d->line = current().line;
        d->line2 = current().line;
        d->isVar = true;
        pos_++; // 'var'
        pos_++; // '('
        d->names.push_back(expectIdentifier("nome da variável").text);
        while (match(TokenType::Comma))
          d->names.push_back(expectIdentifier("nome da variável").text);
        expect(TokenType::RParen, "')' para fechar o destructuring");
        expect(TokenType::Assign, "'=' no destructuring");
        d->init = parseExpression();
        expect(TokenType::Semicolon, "';'");
        return d;
      }
      auto w = std::make_unique<StmtVarDecl>();
      w->line = current().line;
      auto decls = parseVarDeclStmt();
      for (auto& d : decls) w->decls.push_back(std::move(d));
      return w;
    }
    case TokenType::KwMatch: {
      auto s = std::make_unique<ExprStmt>();
      s->expr = parseMatchExpr();
      s->line = s->expr->line;
      return s;
    }
    case TokenType::KwPanic: {
      auto s = std::make_unique<PanicStmt>();
      s->line = current().line;
      pos_++;
      expect(TokenType::LParen, "'(' após 'panic'");
      s->message = parseExpression();
      expect(TokenType::RParen, "')'");
      expect(TokenType::Semicolon, "';'");
      return s;
    }
    case TokenType::KwAssert: {
      auto s = std::make_unique<AssertStmt>();
      s->line = current().line;
      pos_++;
      expect(TokenType::LParen, "'(' após 'assert'");
      s->cond = parseExpression();
      expect(TokenType::RParen, "')'");
      expect(TokenType::Semicolon, "';'");
      return s;
    }
    case TokenType::KwTry: return parseTry();
    case TokenType::KwThrow: return parseThrow();
    case TokenType::KwLock: return parseLock();
    case TokenType::KwSpawn: return parseSpawn();
    case TokenType::KwParallel:
      if (peek(1).type == TokenType::KwForeach) {
        // `parallel foreach (x in xs) { ... }` (v0.22.5)
        pos_++; // consume 'parallel'
        return parseForeach(true);
      }
      return parseParallel();
    default:
      if (isTypeStart() &&
          (atIdentAt(pos_ + 1) || peek(1).type == TokenType::LBracket ||
           peek(1).type == TokenType::Dot ||
           (current().text == "list" && peek(1).type == TokenType::Lt) ||
           (current().text == "map" && peek(1).type == TokenType::Lt) ||
           ((current().text == "Option" || current().text == "option" ||
             current().text == "Result" || current().text == "result") &&
            peek(1).type == TokenType::Lt) ||
            (current().type == TokenType::KwTask && peek(1).type == TokenType::Lt) ||
            (current().type == TokenType::KwChannel && peek(1).type == TokenType::Lt) ||
            (peek(1).type == TokenType::Lt && isNumericTypeKeyword(current().type)) ||
            (peek(1).type == TokenType::Lt && identGenericDeclAhead()))) {
        auto w = std::make_unique<StmtVarDecl>();
        w->line = current().line;
        auto decls = parseVarDeclStmt();
        for (auto& d : decls) w->decls.push_back(std::move(d));
        return w;
      }
      {
        auto s = std::make_unique<ExprStmt>();
        s->expr = parseExpression();
        s->line = s->expr->line;
        expect(TokenType::Semicolon, "';'");
        return s;
      }
  }
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
  RecursionGuard guard(recursionDepth_, current(), this);
  auto b = std::make_unique<BlockStmt>();
  b->line = current().line;
  expect(TokenType::LBrace, "'{'");
  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    b->stmts.push_back(parseStatement());
  }
  expect(TokenType::RBrace, "'}'");
  return b;
}

std::unique_ptr<Stmt> Parser::parseIf() {
  auto s = std::make_unique<IfStmt>();
  s->line = current().line;
  pos_++;
  expect(TokenType::LParen, "'('");
  s->cond = parseExpression();
  expect(TokenType::RParen, "')'");
  s->thenBranch = parseStatement();
  if (match(TokenType::KwElse)) {
    s->elseBranch = parseStatement();
  }
  return s;
}

std::unique_ptr<Stmt> Parser::parseFor() {
  auto s = std::make_unique<ForStmt>();
  s->line = current().line;
  pos_++;
  expect(TokenType::LParen, "'('");
  if (!check(TokenType::Semicolon)) {
    if (isTypeStart() || check(TokenType::KwVar) || check(TokenType::KwConst)) {
      auto w = std::make_unique<StmtVarDecl>();
      w->line = current().line;
      auto decls = parseVarDeclStmt(false);
      for (auto& d : decls) w->decls.push_back(std::move(d));
      s->init = std::move(w);
      expect(TokenType::Semicolon, "';'");
    } else {
      auto e = std::make_unique<ExprStmt>();
      e->expr = parseExpression();
      e->line = e->expr->line;
      s->init = std::move(e);
      expect(TokenType::Semicolon, "';'");
    }
  } else {
    pos_++;
  }
  if (!check(TokenType::Semicolon)) {
    s->cond = parseExpression();
  }
  expect(TokenType::Semicolon, "';'");
  if (!check(TokenType::RParen)) {
    s->step = parseExpression();
  }
  expect(TokenType::RParen, "')'");
  s->body = parseStatement();
  return s;
}

std::unique_ptr<Stmt> Parser::parseWhile() {
  auto s = std::make_unique<WhileStmt>();
  s->line = current().line;
  pos_++;
  expect(TokenType::LParen, "'('");
  s->cond = parseExpression();
  expect(TokenType::RParen, "')'");
  s->body = parseStatement();
  return s;
}

std::unique_ptr<Stmt> Parser::parseDoWhile() {
  auto s = std::make_unique<DoWhileStmt>();
  s->line = current().line;
  pos_++;
  s->body = parseStatement();
  expect(TokenType::KwWhile, "'while'");
  expect(TokenType::LParen, "'('");
  s->cond = parseExpression();
  expect(TokenType::RParen, "')'");
  expect(TokenType::Semicolon, "';'");
  return s;
}

std::unique_ptr<Stmt> Parser::parseSwitch() {
  auto s = std::make_unique<SwitchStmt>();
  s->line = current().line;
  pos_++;
  expect(TokenType::LParen, "'('");
  s->subject = parseExpression();
  expect(TokenType::RParen, "')'");
  expect(TokenType::LBrace, "'{'");

  SwitchCase* currentCase = nullptr;
  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    if (check(TokenType::KwCase)) {
      pos_++;
      auto v = expect(TokenType::IntLiteral, "valor do case");
      s->cases.push_back(SwitchCase{});
      currentCase = &s->cases.back();
      currentCase->value = v.intValue;
      expect(TokenType::Colon, "':'");
    } else if (check(TokenType::KwDefault)) {
      pos_++;
      expect(TokenType::Colon, "':'");
      currentCase = nullptr;
    } else {
      auto st = parseStatement();
      if (currentCase) currentCase->body.push_back(std::move(st));
      else s->defaultBody.push_back(std::move(st));
    }
  }
  expect(TokenType::RBrace, "'}'");
  return s;
}

std::unique_ptr<Stmt> Parser::parseForeach(bool parallel) {
  auto s = std::make_unique<ForeachStmt>();
  s->line = current().line;
  s->parallel = parallel;
  pos_++; // foreach
  expect(TokenType::LParen, "'('");
  if (match(TokenType::KwVar)) {
    s->isVar = true;
  } else if (isTypeStart()) {
    s->itemType = parseType();
    s->hasType = true;
  } else if (atIdent() && peek(1).type == TokenType::KwIn) {
    // `foreach (x in xs)`: sem tipo nem var, infere do elemento (como var)
    s->isVar = true;
  } else {
    error(current(), "tipo ou 'var' esperado no foreach");
  }
  s->itemName = expectIdentifier("nome da variável do foreach").text;
  expect(TokenType::KwIn, "'in'");
  s->collection = parseExpression();
  expect(TokenType::RParen, "')'");
  // `parallel foreach (...) batch: N` (v0.23.2): particionamento em chunks
  // CONTÍGUOS de tamanho N fixo (adaptativo em M3) — apenas no parallel
  if (parallel && atIdent() && current().text == "batch") {
    pos_++;
    expect(TokenType::Colon, "':' após 'batch'");
    s->batchSize = (int)expect(TokenType::IntLiteral,
                               "tamanho do batch (inteiro)").intValue;
    if (s->batchSize < 1)
      error(current(), "'batch: N' exige N >= 1");
  }
  s->body = parseStatement();
  return s;
}

std::unique_ptr<Stmt> Parser::parseReturn() {
  auto s = std::make_unique<ReturnStmt>();
  s->line = current().line;
  pos_++;
  if (!check(TokenType::Semicolon)) {
    // M10.1b: `return (e1, e2);` — literal de tupla (≥2 expressões separadas
    // por vírgula; `(e)` continua sendo só parênteses)
    if (check(TokenType::LParen)) {
      size_t save = pos_;
      pos_++; // '('
      auto lit = std::make_unique<TupleLitExpr>();
      lit->line = current().line;
      bool ok = true;
      lit->elements.push_back(parseExpression());
      while (match(TokenType::Comma)) lit->elements.push_back(parseExpression());
      expect(TokenType::RParen, "')' para fechar a tupla");
      if (lit->elements.size() >= 2) {
        s->value = std::move(lit);
      } else {
        // 1 elemento: expressão parenthesizada comum — reparsa do save
        pos_ = save;
        s->value = parseExpression();
        (void)ok;
      }
    } else {
      s->value = parseExpression();
    }
  }
  expect(TokenType::Semicolon, "';'");
  return s;
}

std::unique_ptr<Stmt> Parser::parseTry() {
  auto s = std::make_unique<TryStmt>();
  s->line = current().line;
  pos_++;
  s->body = parseBlock();
  if (match(TokenType::KwCatch)) {
    expect(TokenType::LParen, "'(' após 'catch'");
    s->catchType = parseType();
    s->catchVar = expect(TokenType::Identifier, "nome do parâmetro do catch").text;
    expect(TokenType::RParen, "')'");
    s->catchBody = parseBlock();
    s->hasCatch = true;
  } else {
    error(current(), "esperava 'catch (Tipo nome)' após o bloco try");
  }
  return s;
}

std::unique_ptr<Stmt> Parser::parseLock() {
  auto s = std::make_unique<LockStmt>();
  s->line = current().line;
  pos_++;
  expect(TokenType::LParen, "'(' após 'lock'");
  s->target = parseExpression();
  expect(TokenType::RParen, "')'");
  s->body = parseBlock();
  return s;
}

// `spawn [task] { ... }` — corpo em thread nova, sem espera
std::unique_ptr<Stmt> Parser::parseSpawn() {
  auto s = std::make_unique<SpawnStmt>();
  s->line = current().line;
  pos_++;
  if (check(TokenType::KwTask)) pos_++; // 'task' é opcional/aceno
  s->body = parseBlock();
  return s;
}

// `parallel [deterministic] { stm; stm; ... }` — uma tarefa por instrução + 
// barreira; com o modificador `deterministic` (v0.23.0), as partes são
// executadas INLINE em ordem (sem spawn/barreira) — determinismo garantido.
std::unique_ptr<Stmt> Parser::parseParallel() {
  auto s = std::make_unique<ParallelStmt>();
  s->line = current().line;
  pos_++;
  if (atIdent() && current().text == "deterministic") {
    s->isDeterministic = true;
    pos_++;
  }
  auto block = parseBlock();
  s->parts = std::move(block->stmts);
  if (s->parts.empty())
    error(current(), "'parallel { }' exige ao menos uma instrução");
  return s;
}

std::unique_ptr<Stmt> Parser::parseThrow() {
  auto s = std::make_unique<ThrowStmt>();
  s->line = current().line;
  pos_++;
  s->value = parseExpression();
  expect(TokenType::Semicolon, "';'");
  return s;
}

// ---------------------------------------------------------------------------
// Expressões
// ---------------------------------------------------------------------------
std::unique_ptr<Expr> Parser::parseExpression() {
  RecursionGuard guard(recursionDepth_, current(), this);
  return parseAssign();
}

std::unique_ptr<Expr> Parser::parseAssign() {
  auto lhs = parseTernary();
  TokenType tt = current().type;
  if (tt == TokenType::Assign || tt == TokenType::PlusAssign || tt == TokenType::MinusAssign ||
      tt == TokenType::StarAssign || tt == TokenType::SlashAssign ||
      tt == TokenType::PercentAssign) {
    pos_++;
    auto a = std::make_unique<AssignExpr>();
    a->line = lhs->line;
    a->target = std::move(lhs);
    switch (tt) {
      case TokenType::Assign: a->op = AssignOp::Plain; break;
      case TokenType::PlusAssign: a->op = AssignOp::Add; break;
      case TokenType::MinusAssign: a->op = AssignOp::Sub; break;
      case TokenType::StarAssign: a->op = AssignOp::Mul; break;
      case TokenType::SlashAssign: a->op = AssignOp::Div; break;
      case TokenType::PercentAssign: a->op = AssignOp::Mod; break;
      default: break;
    }
    a->value = parseAssign(); // associatividade à direita
    return a;
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseTernary() {
  auto cond = parseCoalesce();
  if (match(TokenType::Question)) {
    auto t = std::make_unique<TernaryExpr>();
    t->line = cond->line;
    t->cond = std::move(cond);
    t->thenExpr = parseExpression();
    expect(TokenType::Colon, "':' no operador ternário");
    t->elseExpr = parseTernary();
    return t;
  }
  return cond;
}

// `a ?? padrao` — Option<T>: valor do Some ou o padrão
std::unique_ptr<Expr> Parser::parseCoalesce() {
  auto lhs = parseOr();
  while (check(TokenType::QuestionQuestion)) {
    auto c = std::make_unique<CoalesceExpr>();
    c->line = lhs->line;
    c->lhs = std::move(lhs);
    pos_++;
    c->rhs = parseCoalesce();
    lhs = std::move(c);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseOr() {
  auto lhs = parseAnd();
  while (check(TokenType::OrOr)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    b->op = BinOp::Or;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseAnd();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseAnd() {
  auto lhs = parseBitOr();
  while (check(TokenType::AndAnd)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    b->op = BinOp::And;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseBitOr();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseBitOr() {
  auto lhs = parseBitXor();
  while (check(TokenType::Pipe)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    b->op = BinOp::BitOr;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseBitXor();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseBitXor() {
  auto lhs = parseBitAnd();
  while (check(TokenType::Caret)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    b->op = BinOp::BitXor;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseBitAnd();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseBitAnd() {
  auto lhs = parseEquality();
  while (check(TokenType::Amp)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    b->op = BinOp::BitAnd;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseEquality();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseEquality() {
  auto lhs = parseRelational();
  while (check(TokenType::EqEq) || check(TokenType::NotEq)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    b->op = check(TokenType::EqEq) ? BinOp::Eq : BinOp::Ne;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseRelational();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseRelational() {
  auto lhs = parseShift();
  while (check(TokenType::Lt) || check(TokenType::Gt) || check(TokenType::LtEq) ||
         check(TokenType::GtEq)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    if (check(TokenType::Lt)) b->op = BinOp::Lt;
    else if (check(TokenType::Gt)) b->op = BinOp::Gt;
    else if (check(TokenType::LtEq)) b->op = BinOp::Le;
    else b->op = BinOp::Ge;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseShift();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseShift() {
  auto lhs = parseAdditive();
  while (check(TokenType::Shl) || check(TokenType::Shr)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    b->op = check(TokenType::Shl) ? BinOp::Shl : BinOp::Shr;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseAdditive();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseAdditive() {
  auto lhs = parseMultiplicative();
  while (check(TokenType::Plus) || check(TokenType::Minus)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    b->op = check(TokenType::Plus) ? BinOp::Add : BinOp::Sub;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseMultiplicative();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseMultiplicative() {
  auto lhs = parseUnary();
  while (check(TokenType::Star) || check(TokenType::Slash) || check(TokenType::Percent)) {
    auto b = std::make_unique<BinaryExpr>();
    b->line = lhs->line;
    if (check(TokenType::Star)) b->op = BinOp::Mul;
    else if (check(TokenType::Slash)) b->op = BinOp::Div;
    else b->op = BinOp::Mod;
    b->lhs = std::move(lhs);
    pos_++;
    b->rhs = parseUnary();
    lhs = std::move(b);
  }
  return lhs;
}

std::unique_ptr<Expr> Parser::parseUnary() {
  if (check(TokenType::KwAwait)) {
    // `await E` (spec §10): espera a task E e devolve o payload
    auto a = std::make_unique<AwaitExpr>();
    a->line = current().line;
    pos_++;
    a->operand = parseUnary();
    return a;
  }
  if (check(TokenType::Minus) || check(TokenType::Bang) || check(TokenType::Tilde) ||
      check(TokenType::PlusPlus) || check(TokenType::MinusMinus)) {
    auto u = std::make_unique<UnaryExpr>();
    u->line = current().line;
    if (check(TokenType::Minus)) u->op = UnOp::Neg;
    else if (check(TokenType::Bang)) u->op = UnOp::Not;
    else if (check(TokenType::Tilde)) u->op = UnOp::BitNot;
    else if (check(TokenType::PlusPlus)) u->op = UnOp::PreInc;
    else u->op = UnOp::PreDec;
    pos_++;
    u->operand = parseUnary();
    return u;
  }
  return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix() {
  auto e = parsePrimary();
  for (;;) {
    if (check(TokenType::Dot)) {
      pos_++;
      auto m = std::make_unique<MemberExpr>();
      m->line = e->line;
      m->object = std::move(e);
      m->member = expectIdentOrKeyword("nome do membro");
      e = std::move(m);
    } else if (check(TokenType::QuestionDot)) {
      // `e?.membro` / `e?.metodo(...)` — acesso condicional (spec §12)
      pos_++;
      auto m = std::make_unique<OptMemberExpr>();
      m->line = e->line;
      m->object = std::move(e);
      m->member = expectIdentOrKeyword("nome do membro após '?.'");
      e = std::move(m);
    } else if (check(TokenType::Lt) && e->kind == ExprKind::Ident) {
      // chamada genérica explícita: `F<int>(x)` — arg types antes de '('.
      // Amorfo: relacional `x < y > z` também começa com Ident+Lt. Validamos
      // varrendo até o '>' de fechamento: só é genérico se vier '(' logo após.
      bool isGeneric = false;
      if (!check(TokenType::IntLiteral)) {
        int depth = 1;
        size_t i = pos_ + 1;
        while (i < tokens_.size()) {
          if (tokens_[i].type == TokenType::Lt) { depth++; i++; continue; }
          if (tokens_[i].type == TokenType::Shr) {
            depth -= 2;
            i++;
            if (depth <= 0) break;
            continue;
          }
          if (tokens_[i].type == TokenType::Gt) {
            depth--;
            i++;
            if (depth <= 0) break;
            continue;
          }
          if (tokens_[i].type == TokenType::LParen) {
            i++;
            continue;
          }
          i++;
        }
        if (depth <= 0 && i < tokens_.size() && tokens_[i].type == TokenType::LParen)
          isGeneric = true;
      }
      if (!isGeneric) {
        // deixa os operadores `<` `>` para a expressão binária
        break;
      }
      pos_++; // '<'
      std::vector<Type> gargs;
      gargs.push_back(parseType());
      while (match(TokenType::Comma)) gargs.push_back(parseType());
      if (!match(TokenType::Gt)) {
        if (current().type == TokenType::Shr) splitShr();
        else error(current(), "'>' esperado para fechar 'F<'");
      }
      if (!check(TokenType::LParen)) {
        error(current(), "'(' esperado após argumentos de tipo");
      }
      pos_++; // consome '(' (parseArgs espera o primeiro argumento)
      if (e->kind == ExprKind::Ident && atIdent() &&
          peek(1).type == TokenType::Colon) {
        auto ident = static_cast<IdentExpr*>(e.get());
        auto ne = std::make_unique<NewExpr>();
        ne->line = e->line;
        ne->className = ident->name;
        ne->genericArgs = gargs;
        ne->args = parseArgs(&ne->argNames);
        e = std::move(ne);
      } else {
        auto c = std::make_unique<CallExpr>();
        c->line = e->line;
        c->callee = std::move(e);
        c->genericArgs = gargs;
        c->args = parseArgs();
        e = std::move(c);
      }
    } else if (check(TokenType::LParen)) {
      pos_++; // consome '(' (parseArgs espera o primeiro argumento)
      if (e->kind == ExprKind::Ident && atIdent() &&
          peek(1).type == TokenType::Colon) {
        // `LoadError(mensagem: "x", codigo: 12)` — construção de struct com
        // argumentos nomeados sem `new` (spec §12); a semântica valida a classe.
        auto ident = static_cast<IdentExpr*>(e.get());
        auto ne = std::make_unique<NewExpr>();
        ne->line = e->line;
        ne->className = ident->name;
        ne->args = parseArgs(&ne->argNames);
        e = std::move(ne);
      } else {
        auto c = std::make_unique<CallExpr>();
        c->line = e->line;
        c->callee = std::move(e);
        c->args = parseArgs();
        e = std::move(c);
      }
    } else if (check(TokenType::LBracket)) {
      pos_++;
      auto ix = std::make_unique<IndexExpr>();
      ix->line = e->line;
      ix->object = std::move(e);
      ix->index = parseExpression();
      expect(TokenType::RBracket, "']' após índice");
      e = std::move(ix);
    } else if (check(TokenType::QuestionBracket)) {
      pos_++;
      auto oi = std::make_unique<OptIndexExpr>();
      oi->line = e->line;
      oi->object = std::move(e);
      oi->index = parseExpression();
      expect(TokenType::RBracket, "']' esperado após índice em '?['");
      e = std::move(oi);
    } else if (check(TokenType::PlusPlus) || check(TokenType::MinusMinus)) {
      auto u = std::make_unique<UnaryExpr>();
      u->line = current().line;
      u->op = check(TokenType::PlusPlus) ? UnOp::PostInc : UnOp::PostDec;
      u->operand = std::move(e);
      pos_++;
      e = std::move(u);
    } else if (check(TokenType::Question)) {
      // `x?` (propagação de Option/Result) vs ternário `x ? a : b`:
      // o pós-fixo é escolhido quando o próximo token NÃO começa uma expressão
      // (o ternário sempre tem um then-expr após '?'; `x?` termina a expressão
      // com ';' ',' ')' ']' ou segue um operador binário).
      if (isPrimaryStart(peek(1).type)) break; // deixa para o ternário
      pos_++;
      auto t = std::make_unique<TryExpr>();
      t->line = current().line;
      t->operand = std::move(e);
      e = std::move(t);
    } else {
      break;
    }
  }
  return e;
}

// token que pode iniciar um primário (usado para distinguir `x?` de `x ? a : b`)
bool Parser::isPrimaryStart(TokenType t) const {
  switch (t) {
    case TokenType::Identifier:
    case TokenType::IntLiteral: case TokenType::FloatLiteral:
    case TokenType::StringLiteral: case TokenType::CharLiteral:
    case TokenType::KwTrue: case TokenType::KwFalse: case TokenType::KwNull:
    case TokenType::KwThis: case TokenType::KwNew: case TokenType::KwMatch:
    case TokenType::KwSome: case TokenType::KwNone:
    case TokenType::KwOk: case TokenType::KwErr:
    case TokenType::KwSpawn:
    case TokenType::LParen: case TokenType::LBrace:
    case TokenType::Minus: case TokenType::Bang: case TokenType::Tilde:
      return true;
    default:
      return false;
  }
}

// v0.95 (lambdas): `(x) => x + 1`, `(int x, string s) => ...`, `() => 42`,
// `(x) => { ... }`. Especulativo: só é lambda se `(params) =>` casar;
// caso contrário restaura pos_ e devolve nullptr (vira parêntese/cast).
// Erros DENTRO de params/corpo já confirmados propagam normalmente.
std::unique_ptr<Expr> Parser::tryParseLambda(const Token& lparen) {
  size_t save = pos_;
  pos_++; // '('
  auto lam = std::make_unique<LambdaExpr>();
  lam->line = lparen.line;
  // varredura de confirmação (sem consumir tipos): sequência de
  // `[tipo] nome [, ...]` seguida de `) =>`. Tipos com generics usam `<>`
  // balanceado; qualquer outro formato aborta para parêntese/cast.
  size_t i = pos_;
  auto isTypeKw = [](TokenType t) {
    switch (t) {
      case TokenType::KwInt: case TokenType::KwFloat: case TokenType::KwDouble:
      case TokenType::KwBool: case TokenType::KwChar: case TokenType::KwString:
      case TokenType::KwU8: case TokenType::KwU16: case TokenType::KwU32:
      case TokenType::KwU64: case TokenType::KwVoid:
        return true;
      default: return false;
    }
  };
  bool looksLambda = false;
  if (i < tokens_.size() && tokens_[i].type == TokenType::RParen) {
    looksLambda = (i + 1 < tokens_.size() &&
                   tokens_[i + 1].type == TokenType::FatArrow);
  } else {
    bool ok = true;
    while (ok) {
      // tipo opcional: keyword-tipo, Ident simples ou Ident com <> balanceado.
      // Se houver tipo, o nome vem após; senão o próprio Ident é o nome.
      if (i < tokens_.size() && isTypeKw(tokens_[i].type)) {
        i++; // consome o tipo (keyword); nome verificado abaixo
      } else if (i < tokens_.size() && tokens_[i].type == TokenType::Identifier) {
        size_t j = i + 1;
        if (j < tokens_.size() && tokens_[j].type == TokenType::Lt) {
          int depth = 0;
          while (j < tokens_.size()) {
            if (tokens_[j].type == TokenType::Lt) depth++;
            else if (tokens_[j].type == TokenType::Gt) {
              depth--;
              if (depth == 0) { j++; break; }
            } else if (tokens_[j].type == TokenType::Shr) {
              depth -= 2;
              if (depth <= 0) { j++; break; }
            }
            j++;
          }
          if (depth > 0) { ok = false; break; }
          // após genérico deve vir o nome: `list<int> xs`
          if (j >= tokens_.size() || tokens_[j].type != TokenType::Identifier) {
            ok = false;
            break;
          }
          i = j; // consome o tipo; nome verificado abaixo
        } else if (j < tokens_.size() && tokens_[j].type == TokenType::Identifier) {
          i = j; // consome 'Tipo'; nome verificado abaixo (`Ponto p`)
        }
        // senão: Ident solitário = parâmetro sem tipo (não consome aqui)
      } else {
        ok = false;
        break;
      }
      // nome do parâmetro
      if (i >= tokens_.size() || tokens_[i].type != TokenType::Identifier) {
        ok = false;
        break;
      }
      i++;
      if (i < tokens_.size() && tokens_[i].type == TokenType::Comma) {
        i++;
        continue;
      }
      break;
    }
    looksLambda = ok && i < tokens_.size() &&
                  tokens_[i].type == TokenType::RParen &&
                  i + 1 < tokens_.size() &&
                  tokens_[i + 1].type == TokenType::FatArrow;
  }
  if (!looksLambda) {
    pos_ = save;
    return nullptr;
  }
  // confirmado: parse real (erros aqui são erros de verdade)
  if (!check(TokenType::RParen)) {
    for (;;) {
      LambdaExpr::Param p;
      p.line = current().line;
      // parâmetro tipado? keyword-tipo, ou Ident seguido de Ident (Tipo nome)
      // / Lt (genérico). Ident solitário antes de ','/')' = sem tipo.
      bool typed = isTypeKw(current().type);
      if (!typed && atIdent()) {
        TokenType nxt =
            (pos_ + 1 < tokens_.size()) ? tokens_[pos_ + 1].type : TokenType::EndOfFile;
        typed = (nxt == TokenType::Identifier || nxt == TokenType::Lt);
      }
      if (typed) {
        p.type = parseType();
        p.hasType = true;
      }
      p.name = expectIdentifier("nome do parâmetro da lambda").text;
      lam->params.push_back(std::move(p));
      if (!match(TokenType::Comma)) break;
    }
  }
  expect(TokenType::RParen, "')' após parâmetros da lambda");
  expect(TokenType::FatArrow, "'=>' após parâmetros da lambda");
  if (check(TokenType::LBrace)) {
    lam->body = parseBlock();
  } else {
    auto bodyExpr = parseExpression();
    auto block = std::make_unique<BlockStmt>();
    block->line = bodyExpr->line;
    auto ret = std::make_unique<ReturnStmt>();
    ret->line = bodyExpr->line;
    ret->value = std::move(bodyExpr);
    block->stmts.push_back(std::move(ret));
    lam->body = std::move(block);
  }
  return lam;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
  const Token& tok = current();
  switch (tok.type) {
    case TokenType::IntLiteral: {
      auto e = std::make_unique<IntLitExpr>();
      e->line = tok.line;
      e->value = tok.intValue;
      pos_++;
      return e;
    }
    case TokenType::FloatLiteral: {
      auto e = std::make_unique<FloatLitExpr>();
      e->line = tok.line;
      e->value = tok.floatValue;
      pos_++;
      return e;
    }
    case TokenType::StringLiteral: {
      auto e = std::make_unique<StringLitExpr>();
      e->line = tok.line;
      e->value = tok.text;
      pos_++;
      return e;
    }
    case TokenType::CharLiteral: {
      auto e = std::make_unique<CharLitExpr>();
      e->line = tok.line;
      e->value = (int)tok.intValue;
      pos_++;
      return e;
    }
    case TokenType::KwTrue: case TokenType::KwFalse: {
      auto e = std::make_unique<BoolLitExpr>();
      e->line = tok.line;
      e->value = tok.type == TokenType::KwTrue;
      pos_++;
      return e;
    }
    case TokenType::KwNull: {
      pos_++;
      return std::make_unique<NullLitExpr>();
    }
    case TokenType::KwThis: {
      pos_++;
      return std::make_unique<ThisExpr>();
    }
    case TokenType::KwSpawn: {
      // `spawn [task] { return expr; }` — passa uma task<T>
      pos_++;
      if (check(TokenType::KwTask)) pos_++; // 'task' é opcional/aceno
      auto e = std::make_unique<SpawnExpr>();
      e->line = tok.line;
      e->body = parseBlock();
      return e;
    }
    case TokenType::KwNew: {
      pos_++;
      auto e = std::make_unique<NewExpr>();
      e->line = tok.line;
      if (check(TokenType::KwChannel)) {
        // `new channel<int>(cap)` — channel é builtin (keyword), não classe
        pos_++;
        e->className = "channel";
      } else if (isNumericTypeKeyword(current().type) ||
                 check(TokenType::KwBool) || check(TokenType::KwChar) ||
                 check(TokenType::KwString) || check(TokenType::KwVoid)) {
        // `new int(0)`, `new int[1000]`, `new string(...)` — tipo primitivo
        e->className = current().text;
        pos_++;
      } else {
        e->className = expectIdentifier("nome da classe").text;
        while (check(TokenType::Dot)) {
          pos_++;
          e->className += "." + expectIdentifier("nome da classe").text;
        }
      }
      // array: `new int[1000]` — aloca array na heap
      if (check(TokenType::LBracket)) {
        pos_++;
        e->isArrayNew = true;
        if (check(TokenType::IntLiteral)) {
          e->arraySize = current().intValue;
          pos_++;
        } else if (atIdent()) {
          // `new int[N]` — N pode ser uma constante/var local (não simbólico);
          // o size é avaliado em runtime, então marcamos como runtime-size
          e->arraySizeExpr = std::make_unique<IdentExpr>();
          static_cast<IdentExpr*>(e->arraySizeExpr.get())->name = current().text;
          static_cast<IdentExpr*>(e->arraySizeExpr.get())->line = current().line;
          pos_++;
        } else {
          error(current(), "tamanho do array esperado (ex.: new int[1000])");
        }
        expect(TokenType::RBracket, "']'");
        return e;
      }
      // genérico: `new Caixa<int>(...)` / `new list<int>(...)`
      if (check(TokenType::Lt) && !check(TokenType::IntLiteral)) {
        pos_++; // '<'
        e->genericArgs.push_back(parseType());
        while (match(TokenType::Comma)) e->genericArgs.push_back(parseType());
        if (!match(TokenType::Gt)) {
          if (current().type == TokenType::Shr) splitShr();
          else error(current(), "'>' esperado para fechar 'new Nome<'");
        }
      }
      expect(TokenType::LParen, "'('");
      e->args = parseArgs(&e->argNames);
      return e;
    }
    case TokenType::LBrace: {
      // literal de array: {1, 2, 3} (virgula final opcional)
      pos_++;
      auto a = std::make_unique<ArrayLitExpr>();
      a->line = tok.line;
      while (!check(TokenType::RBrace)) {
        a->elements.push_back(parseExpression());
        if (match(TokenType::Comma)) {
          if (check(TokenType::RBrace)) break; // vírgula final
        } else {
          break;
        }
      }
      expect(TokenType::RBrace, "'}' para fechar literal de array");
      return a;
    }
    case TokenType::KwMatch:
      return parseMatchExpr();
    case TokenType::KwSome: case TokenType::KwOk: case TokenType::KwErr: {
      auto o = std::make_unique<OptCtorExpr>();
      o->line = tok.line;
      o->variant = tok.type == TokenType::KwSome ? "Some"
                   : tok.type == TokenType::KwOk ? "Ok" : "Err";
      pos_++;
      // payload opcional: `Ok` / `Ok()` (Result<void, E>) ou `Some(x)`/`Ok(x)`
      if (check(TokenType::LParen)) {
        pos_++;
        if (!check(TokenType::RParen)) {
          o->arg = parseExpression();
          expect(TokenType::RParen, "')'");
        } else {
          pos_++; // `Ok()` — sem payload (Result<void, E>)
        }
      }
      return o;
    }
    case TokenType::KwNone: {
      auto o = std::make_unique<OptCtorExpr>();
      o->line = tok.line;
      o->variant = "None";
      pos_++;
      return o;
    }
    case TokenType::Identifier: {
      auto e = std::make_unique<IdentExpr>();
      e->line = tok.line;
      e->name = tok.text;
      pos_++;
      return e;
    }
    case TokenType::KwPool: {
      // contextual: `pool` vale como identificador fora de policy-decl
      auto e = std::make_unique<IdentExpr>();
      e->line = tok.line;
      e->name = tok.text;
      pos_++;
      return e;
    }
    case TokenType::LParen: {
      // v0.95 (lambdas): `(x) => x + 1`, `(x, y) => {...}`, `() => 42`
      if (auto lam = tryParseLambda(tok)) return lam;
      pos_++;
      // cast? (tipo) expr
      if (isTypeStart()) {
        size_t save = pos_;
        Type t = parseType();
        if (check(TokenType::RParen)) {
          pos_++;
          auto c = std::make_unique<CastExpr>();
          c->line = tok.line;
          c->target = t;
          c->operand = parseUnary();
          return c;
        }
        pos_ = save; // não é cast; volta e parseia como expr
      }
      auto e = parseExpression();
      expect(TokenType::RParen, "')'");
      return e;
    }
    case TokenType::KwBase: {
      // contextual: `base` sozinho e nome; `base.` continua base-call
      if (peek(1).type != TokenType::Dot) {
        auto be = std::make_unique<IdentExpr>();
        be->line = tok.line;
        be->name = tok.text;
        pos_++;
        return be;
      }
      // M10 (v0.44): `base.Metodo(...)` — chamada à implementação da base
      pos_++; // 'base'
      expect(TokenType::Dot, "'.' após 'base'");
      auto m = std::make_unique<MemberExpr>();
      m->line = tok.line;
      m->isBaseCall = true;
      m->member = expectIdentifier("nome do método da base").text;
      expect(TokenType::LParen, "'(' após o método da base");
      auto c = std::make_unique<CallExpr>();
      c->line = tok.line;
      c->callee = std::move(m);
      if (!check(TokenType::RParen)) {
        c->args.push_back(parseExpression());
        while (match(TokenType::Comma)) c->args.push_back(parseExpression());
      }
      expect(TokenType::RParen, "')' para fechar os argumentos");
      return c;
    }
    default:
      error(tok, "expressão esperada");
  }
}

// Padrão atômico: literal, faixa, binding, variante, struct ou lista.
std::unique_ptr<Pattern> Parser::parsePattern(bool neg) {
  if (check(TokenType::IntLiteral)) {
    auto p = std::make_unique<Pattern>();
    p->kind = Pattern::K::Const;
    p->constValue = neg ? -current().intValue : current().intValue;
    pos_++;
    return p;
  }
  if (check(TokenType::CharLiteral)) {
    auto p = std::make_unique<Pattern>();
    p->kind = Pattern::K::Const;
    p->constValue = (neg ? -1 : 1) * current().intValue;
    pos_++;
    return p;
  }
  if (check(TokenType::StringLiteral)) {
    if (neg) error(current(), "'-' antes de string inv�lido no padr�o do match");
    auto p = std::make_unique<Pattern>();
    p->kind = Pattern::K::StrConst;
    p->strValue = current().text;
    pos_++;
    return p;
  }
  if (atIdent() && current().text == "_") {
    if (neg) error(current(), "'-' antes de '_' inválido");
    auto p = std::make_unique<Pattern>();
    p->kind = Pattern::K::Wildcard;
    pos_++;
    return p;
  }
  if (check(TokenType::LBracket)) {
    if (neg) error(current(), "'-' antes de '[' inválido");
    return parsePatternAtom();
  }
  // binding simples: identificador sem '.' e sem '('
  if (atIdent() &&
      peek(1).type != TokenType::Dot && peek(1).type != TokenType::LParen) {
    if (neg) error(current(), "'-' antes de nome de binding inválido");
    auto p = std::make_unique<Pattern>();
    p->kind = Pattern::K::Bind;
    p->bindName = current().text;
    pos_++;
    return p;
  }
  return parsePatternAtom();
}

std::unique_ptr<Pattern> Parser::parsePatternAtom() {
  if (check(TokenType::LBracket)) {
    // desestruturação de coleção: `[]`, `[a, b]`, `[a, b, ..resto]`
    auto p = std::make_unique<Pattern>();
    p->kind = Pattern::K::List;
    pos_++;
    while (!check(TokenType::RBracket)) {
      if (match(TokenType::Range)) {
        // `..resto`
        if (p->hasRest) error(current(), "só um '..resto' por padrão de lista");
        p->hasRest = true;
        p->restName = expectIdentifier("nome do binding '..resto'").text;
        break;
      }
      p->add(parsePattern());
      if (match(TokenType::Range)) {
        if (p->hasRest) error(current(), "só um '..resto' por padrão de lista");
        p->hasRest = true;
        p->restName = expectIdentifier("nome do binding '..resto'").text;
        break;
      }
      if (!match(TokenType::Comma)) break;
    }
    expect(TokenType::RBracket, "']' para fechar padrão de lista");
    return p;
  }
  if (check(TokenType::KwNone) || check(TokenType::KwSome) ||
      check(TokenType::KwOk) || check(TokenType::KwErr)) {
    auto p = std::make_unique<Pattern>();
    p->kind = Pattern::K::Const;
    p->path = current().text;
    pos_++;
    if (check(TokenType::LParen)) {
      pos_++;
      p->kind = Pattern::K::Variant;
      while (!check(TokenType::RParen)) {
        p->add(parsePattern());
        if (!match(TokenType::Comma)) break;
      }
      expect(TokenType::RParen, "')' para fechar padrão de variante");
    }
    return p;
  }
  if (atIdent()) {
    std::string path = current().text;
    pos_++;
    while (check(TokenType::Dot)) {
      pos_++;
      path += "." + expectIdentifier("nome").text;
    }
    auto p = std::make_unique<Pattern>();
    if (check(TokenType::LParen)) {
      pos_++;
      // `T(...)` — struct (com `campo:`) ou variante
      if (atIdent() && peek(1).type == TokenType::Colon) {
        p->kind = Pattern::K::Struct;
        p->path = path;
        while (!check(TokenType::RParen)) {
          std::string fname = expectIdentifier("nome do campo do struct").text;
          expect(TokenType::Colon, "':' após nome do campo");
          p->subNames.push_back(fname);
          p->add(parsePattern());
          if (!match(TokenType::Comma)) break;
        }
        expect(TokenType::RParen, "')' para fechar padrão de struct");
      } else {
        p->kind = Pattern::K::Variant;
        p->path = path;
        while (!check(TokenType::RParen)) {
          p->add(parsePattern());
          if (!match(TokenType::Comma)) break;
        }
        expect(TokenType::RParen, "')' para fechar padrão de variante");
      }
      return p;
    }
    p->kind = Pattern::K::Const;
    p->path = path;
    return p;
  }
  error(current(), "padrão de match inválido (literal, '_', binding, variante, struct ou lista)");
}

// `match (sujeito) { padrão [when cond] => expr; | padrão [when cond] { ... } }`
std::unique_ptr<Expr> Parser::parseMatchExpr() {
  auto m = std::make_unique<MatchExpr>();
  m->line = current().line;
  expect(TokenType::KwMatch, "'match'");
  expect(TokenType::LParen, "'(' após 'match'");
  m->subject = parseExpression();
  expect(TokenType::RParen, "')' após o sujeito do match");
  expect(TokenType::LBrace, "'{' para abrir match");
  while (!check(TokenType::RBrace) && !check(TokenType::EndOfFile)) {
    MatchArm arm;
    arm.line = current().line;
    // binding do sujeito: `x @ padrão`
    if (!check(TokenType::KwMatch) &&
        (atIdent() || current().text == "_") &&
        peek(1).type == TokenType::At) {
      arm.hasSubjectBind = true;
      arm.subjectBind = current().text;
      pos_ += 2; // nome e '@'
    }
    bool neg = false;
    if (match(TokenType::Minus)) neg = true;
    arm.pattern = parsePattern(neg);
    // faixa: `lo..hi` (apenas literal)
    if (arm.pattern->kind == Pattern::K::Const && check(TokenType::Range)) {
      pos_++;
      bool negHi = false;
      if (match(TokenType::Minus)) negHi = true;
      if (!check(TokenType::IntLiteral) && !check(TokenType::CharLiteral))
        error(current(), "'..' exige um literal inteiro");
      long long hi = current().intValue;
      pos_++;
      arm.pattern->kind = Pattern::K::Range;
      arm.pattern->rangeLo = arm.pattern->constValue;
      arm.pattern->rangeHi = negHi ? -hi : hi;
      if (arm.pattern->rangeHi < arm.pattern->rangeLo)
        error(tokens_[pos_ - 1], "faixa invertida no padrão do match");
    }
    // guarda opcional: `when <expr>`
    if (match(TokenType::KwWhen)) {
      arm.guard = parseExpression();
    }
    if (match(TokenType::FatArrow)) {
      if (check(TokenType::LBrace)) {
        // match-instrução: `=> { ... }`
        auto block = parseBlock();
        for (auto& s : block->stmts) arm.body.push_back(std::move(s));
      } else {
        // match-expr: `=> expr;` (ou `=> expr,` entre braços)
        arm.yield = parseExpression();
        if (check(TokenType::Semicolon)) pos_++;
        else if (match(TokenType::Comma)) { /* separador entre braços */ }
      }
    } else {
      // match-instrução: bloco `{ ... }` sem seta
      auto block = parseBlock();
      for (auto& s : block->stmts) arm.body.push_back(std::move(s));
    }
    m->arms.push_back(std::move(arm));
    if (match(TokenType::Comma)) {
      // separador opcional entre braços (`... }` seguido de `,`)
    }
  }
  expect(TokenType::RBrace, "'}' para fechar match");
  return m;
}

std::vector<std::unique_ptr<Expr>> Parser::parseArgs(std::vector<std::string>* names) {
  std::vector<std::unique_ptr<Expr>> args;
  while (!check(TokenType::RParen)) {
    if (names && atIdent() && peek(1).type == TokenType::Colon) {
      // argumento nomeado (spec §12): `campo: expr`
      names->push_back(current().text);
      pos_ += 2; // nome e ':'
    } else {
      if (names) names->push_back("");
    }
    // ref/out/in: marcador de passagem por referência (spec §5) — sem
    // efeito no parse do argumento (a semântica exige lvalue quando preciso)
    while (check(TokenType::KwRef) || check(TokenType::KwOut) ||
           check(TokenType::KwIn)) {
      pos_++;
    }
    args.push_back(parseExpression());
    if (!match(TokenType::Comma)) break;
  }
  expect(TokenType::RParen, "')'");
  return args;
}

} // namespace hphl
