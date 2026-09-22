#pragma once
#include "ast/ast.h"
#include "hir/hir.h"
#include "frontend/lexer.h"
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <algorithm>

namespace hphl {

struct PropInfo {
  Type type;                 // tipo da propriedade (canônico)
  FunctionDecl* getter = nullptr; // null → sem leitura
  FunctionDecl* setter = nullptr; // null → somente leitura
  Access access = Access::Public; // v0.46: visibilidade da propriedade
};

struct ClassInfo {
  ClassDecl* decl = nullptr;
  int size = 0;                        // tamanho em bytes (inclui vptr e base)
  bool hasVptr = false;                // M10: classe (não struct) tem vptr @0
  bool layoutDone = false;             // M10: layout recursivo já calculado
  bool inLayout = false;               // Proteção contra ciclos de herança
  std::map<std::string, std::pair<Type, int>> fields; // nome → (tipo, offset)
  std::map<std::string, bool> fieldAtomic;          // nome → acesso atômico
  std::map<std::string, Access> fieldAccess;        // v0.46: visibilidade
  std::map<std::string, PropInfo> properties;        // nome → accessors
  std::vector<FunctionDecl*> methods;
  // overload de construtores: todos os ctors declarados (seleção por arity
  // no checkNew; mesma arity declarada é rejeitada na coleta)
  std::vector<FunctionDecl*> ctors;
  std::string base;
  // derive (v0.25.0): helpers sintetizadas p/ class/struct
  std::vector<std::string> derives;      // traços derivados (Equatable, ...)
  FunctionDecl* equFn = nullptr;         // equ_<classe>(a, b) → bool
  FunctionDecl* cmpFn = nullptr;         // cmp_<classe>(a, b) → int<64> -1/0/1
  FunctionDecl* hashFn = nullptr;        // hash_<classe>(a) → int<64>
  FunctionDecl* cloneFn = nullptr;       // clone_<classe>(a) → <classe>
  // genéricos (monomorfização)
  bool isTemplate = false;             // declaração genérica `class X<T>`
  bool isInstance = false;             // instância concretizada `X<int>`
  std::vector<std::string> typeParamNames; // 'T','U' (template)
  std::vector<Type> typeArgs;          // instância: tipos concretos
  std::string templateFrom;            // instância: canônico do template
  std::vector<std::vector<Type>> interfaceTypeArgs; // type args por interface (instanciados)
};

struct FunctionInfo {
  FunctionDecl* decl = nullptr;
  std::string label;                  // label de assembly
  std::string moduleName;             // módulo de origem
};

class Semantic {
public:
  Semantic(Program* prog, const std::string& filename);

  // executa a análise completa; lança CompileError em caso de erro
  void analyze();

  // M4 Fase 2: otimizações do HIR (constant folding, dead branch, DCE de
  // locais) — roda após analyze(), antes do codegen (beneficia os 3 backends)
  void optimizeHir();

  const std::map<std::string, ClassInfo>& classes() const { return classes_; }
  // M10 (v0.44): acesso para o codegen (vtables/dispatch)
  const std::map<std::string, int>& vslots() const { return vslots_; }
  const std::map<std::string, EnumDecl*>& enums() const { return enums_; }
  const std::vector<FunctionInfo>& functions() const { return functions_; }
  const std::vector<std::string>& externLibs() const { return externLibs_; }
  const std::vector<VarDecl*>& globals() const { return globals_; }
  const HirProgram& hir() const { return hirProgram; }
  bool isRichEnum(const std::string& canon) const {
    auto it = enums_.find(canon);
    return it != enums_.end() && it->second->anyPayload;
  }
  bool derived(const std::string& canon, const std::string& trait) const {
    auto it = enums_.find(canon);
    if (it != enums_.end())
      return std::find(it->second->derives.begin(), it->second->derives.end(),
                       trait) != it->second->derives.end();
    auto ic = classes_.find(canon);
    if (ic != classes_.end())
      return std::find(ic->second.derives.begin(), ic->second.derives.end(),
                       trait) != ic->second.derives.end();
    return false;
  }

private:
  Program* prog_;
  HirProgram hirProgram;
  std::string filename_;
  bool failed_ = false;
  bool inAssignLhs_ = false; // o próx. checkExpr é alvo de `=` (get não exigido)

  // --- atribuição definida para 'out' (SPEC §5.2) ---
  std::set<std::string> outAssigned_; // parâmetros 'out' atribuídos em TODOS os
                                      // caminhos até o ponto atual do fluxo
  std::string outWriteName_;          // nome em escrita SEM leitura prévia (LHS de
                                      // `=` puro ou argumento `out`): o checkExpr
                                      // do Ident correspondente não verifica leitura

  std::map<std::string, ClassInfo> classes_;
  // M10 (v0.44): slots virtuais globais (name/arity → índice) e contagem de
  // classes na hierarquia que declaram o slot (≥2 ⇒ há override)
  std::map<std::string, int> vslots_;
  std::map<int, int> vslotDeclCount_;
  std::map<std::string, EnumDecl*> enums_;
  std::vector<FunctionInfo> functions_;
  std::vector<VarDecl*> globals_;
  std::vector<std::string> externLibs_; // FFI: libs acumuladas de `extern "lib"`

  // --- módulos e visibilidade ---
  std::set<std::string> modules_;              // módulos válidos (declarados + aliases)
  std::map<std::string, std::string> aliases_; // alias → módulo canônico
  std::map<std::string, std::vector<Decl*>> modDecls_; // módulo → decls do arquivo
  std::map<std::string, std::vector<std::string>> simpleClasses_; // simples → canônicos
  std::map<std::string, std::vector<std::string>> simpleEnums_;
  std::map<std::string, Type> typeAliases_;     // `using X = Tipo;` (alvo não-resolvido)
  std::map<std::string, std::string> reexports_; // 'Mod.membro' → alvo 'A.Nome' (public use)
  std::vector<std::string> aliasStack_;          // guarda anti-ciclo de aliases
  std::string curFile_;   // arquivo em análise (visibilidade internal)
  std::string curModule_; // módulo em análise

  // --- escopos ---
  struct Scope {
    std::map<std::string, SymbolInfo> symbols;
    std::map<std::string, Type> varTypes; // tipo de cada variável
    std::map<std::string, StoragePolicy> policies;
    std::map<std::string, bool> isConst;
  };
  std::vector<Scope> scopes_;
  FunctionDecl* currentFunction_ = nullptr;
  ClassDecl* currentClass_ = nullptr;
  std::string currentClassBase_;
  std::vector<Type> catchTypes_; // pilha de tipos dos catch ativos (try/catch)
  std::set<std::string> movedVars_; // M10.4: posse transferida (use after move)
  int suppressDeprecationDepth_ = 0;
  bool isDeprecationSuppressed() const { return suppressDeprecationDepth_ > 0; }

  // helpers
  [[noreturn]] void error(int line, const std::string& msg);
  void warn(int line, const std::string& msg); // A10: warnings não-paramount
  void pushScope();
  void popScope();
  Scope& topScope() { return scopes_.back(); }
  bool lookup(const std::string& name, SymbolInfo& out, Type& typeOut,
              StoragePolicy* policy = nullptr, bool* isConst = nullptr,
              size_t* foundScope = nullptr);
SymbolInfo declareVar(const std::string& name, const Type& t, StoragePolicy policy,
                      bool isConst, int line, SymbolKind kind = SymbolKind::LocalVar,
                      bool atomic = false, bool isVolatile = false);

  // passadas
  void registerDeclarations();
  void checkFunctions();
  void checkFunction(FunctionDecl* fn);
  void checkStatement(Stmt* s, const Type& returnType);
  void checkBlock(BlockStmt* b, const Type& returnType);
  Type checkExpr(Expr* e, const Type& expected = Type());
  Type checkCall(CallExpr* c);
  Type checkFuncCall(CallExpr* c, const Type& funcType); // v0.95: callee é valor func
  Type checkLambda(LambdaExpr* l, const Type& expected); // v0.95: `(p) => corpo`
  // v0.95: coleta restrições de tipo p/ inferência de parâmetros (retorna por
  // parâmetro: lista de tipos candidatos). Parcial por desenho: tipos sem
  // restrição exigem anotação.
  void lambdaConstraints(Expr* e, const Expr* parent,
                         const std::map<std::string, size_t>& paramIdx,
                         const std::set<std::string>& shadow,
                         std::vector<std::vector<Type>>& out);
  void lambdaConstraintsStmt(Stmt* s,
                             const std::map<std::string, size_t>& paramIdx,
                             const std::set<std::string>& shadow,
                             std::vector<std::vector<Type>>& out);
  // v0.95: nomes de parâmetros referenciados numa subárvore (p/ propagação)
  void collectLambdaParams(Expr* e,
                           const std::map<std::string, size_t>& paramIdx,
                           const std::set<std::string>& shadow,
                           std::set<std::string>& out);
  Type checkAssign(AssignExpr* a);
  void checkNestedPattern(Pattern* p, const Type& want, int line);
  void checkNew(NewExpr* n);
  void checkArrayInit(Expr* init, const Type& target);
  Type checkMatch(MatchExpr* m, const Type& expected = Type());
  Type checkOptCtor(OptCtorExpr* o, const Type& expected);
  Type checkTry(TryExpr* t);
  Type checkOptMember(OptMemberExpr* om);
  Type checkOptIndex(OptIndexExpr* oi);
  Type checkCoalesce(CoalesceExpr* c);
  int findEnumEntry(const EnumDecl* en, const std::string& path) const;
  Type resolveType(const Type& t, int line);
  // A2 (interface como tipo): classe implementa a interface indicada pelo
  // nome canônico (template ou instância monomorfizada, ex. "main.Repo[i32]"),
  // seguindo a cadeia de herança. Exige type args resolvidos na declaração.
  bool implementsInterface(const std::string& clsCanon, const std::string& ifaceMangled) const;
  Type typeOfMember(ClassInfo& ci, const std::string& member, int* offsetOut);
  bool isFieldAtomic(ClassInfo& ci, const std::string& member); // + base
  // v0.46: enforcement de private/protected em membros de classe
  bool canAccessMember(const ClassInfo& ownerCi, Access acc);
  ClassInfo* findFieldOwner(ClassInfo& ci, const std::string& name); // + base
  bool isRefPolicyLocal(const std::string& name); // M10.4: move/double-free
  void checkGlobalRaceWrite(int line, const Expr* target); // M11.1: thread race
  // M11.7: NULL DEREF — estado possivelmente-null por local (Class/String)
  std::set<std::string> maybeNull_;
  void nullNoteAssign(const std::string& name, const Type& t, const Expr* value);
  int nullCondPolarity(const Expr* cond, std::string& name); // 0/1(!=)/2(==)
  void checkNullDeref(const Expr* obj, const Type& objType, int line);
  // M11.8: LOCK DISCIPLINE / TOCTOU
  std::string lockObjName_;        // alvo Ident do lock corrente ("" se nenhum)
  bool lockBodyRead_ = false;      // corpo leu/escreveu campos do alvo?
  std::set<std::string> staleAfterLock_; // objetos com leitura sob lock vencida
  void noteLockAccess(const Expr* obj, int line);
  // M11.9: arena resetável — locais 'arena' da função + estado pós-reset
  std::set<std::string> arenaLocals_;
  std::set<std::string> arenaFreed_;
  void checkArenaUaf(const Expr* obj, int line);
  // v0.46: funções `compiletime` — interpretador do subconjunto puro
  std::optional<long long> evalCompiletime(FunctionDecl* fn,
                                           const std::vector<long long>& args,
                                           int depth);
  std::optional<long long>
  evalCtExpr(Expr* e, std::map<std::string, long long>& env,
             FunctionDecl* fn, int depth);
  bool evalCtBlock(BlockStmt* b, std::map<std::string, long long>& env,
                   std::optional<long long>* result, FunctionDecl* fn,
                   int depth);
  void validateAtomicDecl(int line, const Type& t, bool atomic);
  PropInfo* findProperty(ClassInfo& ci, const std::string& name); // + base
  FunctionDecl* findMethod(ClassInfo& ci, const std::string& name, size_t argc,
                           int line = 0, const std::vector<Type>* explicitArgs = nullptr);
  FunctionDecl* findFunction(const std::string& name, size_t argc, int line);
  // aridade com parâmetros opcionais: `argc` entre obrigatórios e total
  bool arityMatches(const FunctionDecl* fn, size_t argc) const;
  size_t requiredParams(const FunctionDecl* fn) const;
  void requireLvalue(Expr* e, const std::string& what);
  // --- atribuição definida para 'out' (SPEC §5.2) ---
  bool isOutParam(const SymbolInfo& si) const;
  std::string outDirectName(Expr* arg); // arg é Ident do `out` do fn atual ('' se não)
  void outReadCheck(int line, const std::string& name);
  void outMarkAssigned(const std::string& name);
  std::set<std::string> outIntersect(const std::set<std::string>& a,
                                     const std::set<std::string>& b) const;
  bool isAssignable(const Type& target, const Type& value) const;
  bool isNumericBinary(const Type& a, const Type& b);
  Type commonNumeric(const Type& a, const Type& b);

  // --- genéricos: monomorfização ---
  std::string typeKey(const Type& t) const;
  std::string argsKey(const std::vector<Type>& args) const;
  std::string mangleClassName(const std::string& canon, const std::vector<Type>& args) const;
  ClassInfo& instantiateClassInfo(const std::string& templateCanon,
                                  const std::vector<Type>& args, int line);
  FunctionDecl* genericFunctionInstance(FunctionDecl* tmpl,
                                        const std::vector<Type>& ownArgs, int line);
  FunctionDecl* methodInstance(FunctionDecl* tmpl, const std::string& ownerMangled,
                               const std::vector<std::pair<std::string, Type>>& classBinding,
                               const std::vector<Type>& ownArgs, int line);
  std::vector<Type> inferTypeArgs(FunctionDecl* tmpl,
                                  const std::vector<std::unique_ptr<Expr>>& args, int line);
  void checkConstraints(FunctionDecl* tmpl, const std::vector<Type>& args, int line);
  bool satisfiesConstraint(const Type& arg, const std::string& constraint);
  bool isSendableType(const Type& t); // M11.3: atravessa threads?
  // M10 (v0.44): polimorfismo — subsumption + dispatch virtual (público p/ codegen)
public:
  bool derivesFrom(const std::string& derivedCanon, const std::string& baseCanon) const;
  int vslotOf(const std::string& name, size_t arity) const;   // -1 se não virtual
  bool slotOverridden(int slot) const;                        // override em alguma derivada
  const ClassInfo* classInfo(const std::string& canon) const;
  FunctionDecl* findOverrideIn(const std::string& startCanon, int slot) const;
  void layoutClass(ClassInfo& ci);
  // derive em class/struct (v0.25.0): valida os traços e sintetiza os helpers
  // (equ_/cmp_/hash_/clone_ + métodos Equals/CompareTo/Hash/Clone se não-
  // genérica); chamada no layout (classes comuns) e em instantiateClassInfo
  void synthDerivedHelpers(ClassInfo& ci, bool toInstances);
  std::map<std::string, std::vector<FunctionDecl*>> genericFuncs_; // nome → templates
  std::map<std::string, FunctionDecl*> genericInstances_;          // chave → instância
  std::vector<FunctionDecl*> instancesPending_;                    // esperando checagem
  std::set<FunctionDecl*> instancesChecked_;
  int inLockDepth_ = 0; // `lock { }`: impede aninhamento e return/break/continue
  int inSpawnDepth_ = 0; // `spawn`/`parallel`: sem captura de locals, sem exc/return
  bool inParallelForeach_ = false; // corpo de `parallel foreach`: vetos próprios
  size_t spawnBaseScope_ = 0; // escopo onde o spawn começou (captura = fora dele)
  // corpo de spawn-expressão (`task<T> t = spawn { return ...; }`):
  int inSpawnExpr_ = 0;        // dentro do corpo da task (return vira payload)
  Type* spawnPayload_ = nullptr; // lista o tipo inferido do `return`
  bool spawnPayloadSet_ = false; // já houve return neste corpo
  std::vector<std::pair<std::string, Type>>* spawnCaptureSink_ = nullptr; // nó do spawn que coleta capturas
  // v0.95 (lambdas): corpo de `(params) => ...` — captura por valor, return
  // vira o tipo de retorno inferido (mesmo desenho do spawn-expressão)
  int inLambdaDepth_ = 0;      // >0 dentro de corpo de lambda
  Type* lambdaReturn_ = nullptr;
  bool lambdaReturnSet_ = false;
  bool lambdaHadReturn_ = false; // corpo executou `return <valor>`
  size_t lambdaBaseScope_ = 0; // escopo onde a lambda começou (captura = fora dele)
  std::vector<std::pair<std::string, Type>>* lambdaCaptureSink_ = nullptr;
  std::vector<std::unique_ptr<Decl>> instancesKeep_;               // dono dos clones

  // diagnóstico "variável de OUTRA parte do parallel" (v0.22.8): cada parte é
  // uma tarefa com escopo próprio — uma pilha por nível de aninhamento guarda
  // os nomes declarados em cada parte (nome → índice da parte) para gerar a
  // dica amigável quando uma parte referencia a variável de uma irmã
  std::vector<std::map<std::string, int>> parallelSiblings_;
  std::vector<size_t> parallelPartStack_; // índice da parte em análise, por nível

  // módulos: qualificação e visibilidade
  std::string moduleKey(const std::string& name) const;   // alias→módulo canônico ('' inválido)
  bool isModulePrefix(const std::string& name) const;     // 'A' é prefixo de 'A.B'
  bool visibleFrom(const std::string& declModule, const std::string& declFile, Access access) const;
  std::string canonicalType(const std::string& dotted, int line); // 'A.B.Type'→canônico
  Type moduleTypeMember(const std::string& modKey, const std::string& member, int line);
  VarDecl* moduleGlobalVar(const std::string& modKey, const std::string& name, int line);
  FunctionDecl* moduleFunction(const std::string& modKey, const std::string& name,
                               size_t argc, int line);
  void validateDeps(); // 'depends on': dependências conhecidas e sem ciclos
};

} // namespace hphl