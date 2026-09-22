#pragma once
#include "types/types.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace hphl {

// ---------------------------------------------------------------------------
// Base
// ---------------------------------------------------------------------------
struct Node {
  int line = 0;
  virtual ~Node() = default;
};

// ---------------------------------------------------------------------------
// Declarações
// ---------------------------------------------------------------------------
struct Expr;
struct Stmt;
struct BlockStmt;
struct FunctionDecl;
struct VarDecl;
struct EnumDecl;
struct PropertyDecl;

enum class Access { Public, Private, Protected, Internal };

// ---------------------------------------------------------------------------
// Declarações de alto nível
// ---------------------------------------------------------------------------
enum class DeclKind { Function, Class, Enum, GlobalVar, Module, Import, Using,
                      Property, Specialize, Reflect };

struct Decl : Node {
  DeclKind kind;
  std::string moduleName;  // origem (definido pelo loader; 'main' se sem module)
  std::string filePath;    // arquivo de origem (visibilidade internal)
  bool isDeprecated = false;
  std::string deprecatedReason;
  explicit Decl(DeclKind k) : kind(k) {}
};

struct Param {
  Type type;
  std::string name;
  int line = 0;
  bool byRef = false;   // `ref` — leitura + escrita (endereço do caller)
  bool byOut = false;   // `out` — saída obrigatória (endereço do caller)
  bool byIn = false;    // `in` — somente leitura (endereço ou temporário)
  std::unique_ptr<Expr> defaultVal; // `int y = 0` → parâmetro opcional
  bool isByRef() const { return byRef || byOut || byIn; }
};

enum class Variance {
  Invariant = 0,
  Covariant,     // `out T`
  Contravariant  // `in T`
};

// parâmetro de tipo genérico: `T` com constraints `where T : class, NomeClasse`
struct TypeParam {
  std::string name;
  std::vector<std::string> constraints;
  bool isValue = false; // M10 (v0.45): parâmetro de valor `const int N`
  Variance variance = Variance::Invariant;
};

struct ClassDecl : Decl {
  bool isStruct = false;
  bool isInterface = false;
  bool isActor = false;   // `actor` (v0.23.0): estado próprio serializado por lock
  bool reflected = false; // v0.46: `reflect Nome;` — metadados habilitados
  Access access = Access::Public;
  std::string name;
  std::string base;
  std::vector<std::string> interfaces;
  // A2 (monomorfização): type args de cada interface genérica, paralelos a
  // `interfaces`. Vazio para interfaces não-genéricas. Ex.: `Repo<int>` →
  // interfaces=["Repo"], interfaceTypeArgs=[["int"]].
  std::vector<std::vector<Type>> interfaceTypeArgs;
  std::vector<TypeParam> typeParams; // genéricos: `class X<T, U>`
  std::vector<std::unique_ptr<VarDecl>> fields;
  std::vector<std::unique_ptr<PropertyDecl>> properties; // get/set
  std::vector<std::unique_ptr<FunctionDecl>> methods;
  std::vector<std::unique_ptr<EnumDecl>> enums;
  std::vector<std::string> derives;  // derive Equatable/Comparable/Hashable/Cloneable
  ClassDecl() : Decl(DeclKind::Class) {}
};

// `public int Vida { get { ... } set { ... } }` (spec §19/20, págs. 55-56).
// Corpos são blocos normais; o setter usa a variável implícita `value`.
struct PropertyDecl : Decl {
  Access access = Access::Public;
  Type type;
  std::string name;
  std::unique_ptr<BlockStmt> getBody; // null → sem leitura pública
  std::unique_ptr<BlockStmt> setBody; // null → somente leitura
  bool isStatic = false;        // M_RV1 A4: `public static int X { get { ... } }`
  PropertyDecl() : Decl(DeclKind::Property) {}
};

// ---------------------------------------------------------------------------
// Instruções
// ---------------------------------------------------------------------------
enum class StmtKind {
  Block, If, For, While, DoWhile, Break, Continue, Return,
  ExprStmt, VarDecl, Switch, Foreach, Panic, Assert, Try, Throw,
  Lock, Spawn, Parallel, Destructure,
};

struct Stmt : Node {
  StmtKind kind;
  explicit Stmt(StmtKind k) : kind(k) {}
};

struct BlockStmt : Stmt {
  std::vector<std::unique_ptr<Stmt>> stmts;
  bool suppressDeprecation = false;
  BlockStmt() : Stmt(StmtKind::Block) {}
};

struct IfStmt : Stmt {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> thenBranch;
  std::unique_ptr<Stmt> elseBranch;
  IfStmt() : Stmt(StmtKind::If) {}
};

struct ForStmt : Stmt {
  std::unique_ptr<Stmt> init;      // VarDecl ou ExprStmt
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Expr> step;
  std::unique_ptr<Stmt> body;
  ForStmt() : Stmt(StmtKind::For) {}
};

struct WhileStmt : Stmt {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> body;
  WhileStmt() : Stmt(StmtKind::While) {}
};

struct DoWhileStmt : Stmt {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Stmt> body;
  DoWhileStmt() : Stmt(StmtKind::DoWhile) {}
};

struct BreakStmt : Stmt {
  BreakStmt() : Stmt(StmtKind::Break) {}
};

struct ContinueStmt : Stmt {
  ContinueStmt() : Stmt(StmtKind::Continue) {}
};

struct ReturnStmt : Stmt {
  std::unique_ptr<Expr> value;
  ReturnStmt() : Stmt(StmtKind::Return) {}
};

struct ExprStmt : Stmt {
  std::unique_ptr<Expr> expr;
  ExprStmt() : Stmt(StmtKind::ExprStmt) {}
};

struct PanicStmt : Stmt {
  std::unique_ptr<Expr> message; // string
  PanicStmt() : Stmt(StmtKind::Panic) {}
};

struct AssertStmt : Stmt {
  std::unique_ptr<Expr> cond; // bool
  AssertStmt() : Stmt(StmtKind::Assert) {}
};

struct ThrowStmt : Stmt {
  std::unique_ptr<Expr> value; // o que está sendo lançado
  ThrowStmt() : Stmt(StmtKind::Throw) {}
};

struct TryStmt : Stmt {
  std::unique_ptr<Stmt> body;       // bloco try
  bool hasCatch = false;
  Type catchType;                    // tipo do parâmetro do catch
  std::string catchVar;              // nome do parâmetro
  std::unique_ptr<Stmt> catchBody;   // bloco catch
  TryStmt() : Stmt(StmtKind::Try) {}
};

// `lock (alvo) { ... }` (spec §10, M2): região crítica. Alvo avaliado por
// efeito colateral; a exclusão usa um spinlock global no runtime.
struct LockStmt : Stmt {
  std::unique_ptr<Expr> target; // avaliado (referência de classe)
  std::unique_ptr<BlockStmt> body;
  LockStmt() : Stmt(StmtKind::Lock) {}
};

// `spawn [task] { ... }` (spec §10, M2): corpo executado numa thread nova
// (sem espera). Variáveis locais do escopo de origem citadas no corpo são
// capturadas por valor (cópia no env da tarefa); o runtime junta todas as
// threads no fim de Main.
struct SpawnStmt : Stmt {
  std::unique_ptr<BlockStmt> body;
  // capturas (nome, tipo) — preenchido pela semântica; o codegen copia os
  // valores para o env da tarefa no ponto do spawn
  std::vector<std::pair<std::string, Type>> captures;
  SpawnStmt() : Stmt(StmtKind::Spawn) {}
};

// `spawn [task] { ... }` como EXPRESSÃO (spec §10): devolve `task<T>`; o
// corpo deve terminar com `return <expr>;` cujo tipo define T. O valor fica
// pronto após `t.Wait()`. Mesmas regras de captura do SpawnStmt.
// (definição completa na seção de Expressões)
struct SpawnExpr;

// `parallel { ... }` (spec §10, M2): cada instrução do bloco vira uma tarefa;
// o bloco termina quando TODAS terminam (barreira). A thread principal não
// executa parte alguma (documentado); divisão adaptativa fica para o runtime
// de tarefas com trabalho (work stealing) no M2+.
struct ParallelStmt : Stmt {
  std::vector<std::unique_ptr<Stmt>> parts; // 1 tarefa por instrução
  bool isDeterministic = false; // `parallel deterministic` (v0.23.0): inline
  // capturas POR VALOR por parte (v0.22.1): parte i → partCaptures[i] — cada
  // parte ganha um env próprio no site do spawn, como em `spawn`
  std::vector<std::vector<std::pair<std::string, Type>>> partCaptures;
  ParallelStmt() : Stmt(StmtKind::Parallel) {}
};

// declaração de variável local como instrução (VarDecl herda de Decl,
// pois também é usada em globals e campos; este wrapper a coloca em Stmt).
// M5: múltiplas variáveis na mesma linha (`int a = 1, b, c = 3;`) — um
// VarDecl por nome, todos com o mesmo tipo declarado.
struct StmtVarDecl : Stmt {
  std::vector<std::unique_ptr<VarDecl>> decls;
  StmtVarDecl() : Stmt(StmtKind::VarDecl) {}
};

// `var (a, b) = expr;` (M10.1b): destructuring de tuple — cada nome vira uma
// variável simples com o tipo do elemento correspondente.
struct StmtDestructure : Stmt {
  std::vector<std::string> names;
  bool isVar = true;
  Type tupleType;               // resolvido pela semântica
  std::unique_ptr<Expr> init;
  int line2 = 0;
  StmtDestructure() : Stmt(StmtKind::Destructure) {}
};

struct SwitchCase {
  long long value = 0;
  std::vector<std::unique_ptr<Stmt>> body;
};

struct SwitchStmt : Stmt {
  std::unique_ptr<Expr> subject;
  std::vector<SwitchCase> cases;
  std::vector<std::unique_ptr<Stmt>> defaultBody;
  SwitchStmt() : Stmt(StmtKind::Switch) {}
};

struct ForeachStmt : Stmt {
  std::string itemName;
  Type itemType;               // tipo explícito (se hasType)
  bool hasType = false;
  bool isVar = false;          // foreach (var x in ...)
  std::unique_ptr<Expr> collection;
  std::unique_ptr<Stmt> body;
  bool parallel = false;             // `parallel foreach` (v0.22.5)
  int batchSize = 0;        // `batch: N` (v0.23.2): chunks contíguos de N
  bool subjectIsGlobal = false;      // sujeito: array global (true) / list local (false)
  std::string collectionName;        // nome resolvido do sujeito
  ForeachStmt() : Stmt(StmtKind::Foreach) {}
};

// ---------------------------------------------------------------------------
// Expressões
// ---------------------------------------------------------------------------
enum class ExprKind {
  IntLit, FloatLit, StringLit, CharLit, BoolLit, NullLit,
  Ident, Member, OptMember, Index, OptIndex, ArrayLit, TupleLit, Call, Binary, Unary, Assign, Ternary, Cast, New, This,
  Match, OptCtor, Try, Coalesce, Spawn, Await, Lambda,
};

enum class SymbolKind {
  LocalVar, Param, GlobalVar, Field, EnumConst, Function, TypeName, ClassName,
  BuiltinPrint,
};

struct SymbolInfo {
  SymbolKind kind = SymbolKind::LocalVar;
  std::string name;
  int slotIndex = -1;          // locals/params: índice no frame (offset = -8*(slot+1))
  std::string label;           // globals: label no .data
  ClassDecl* ownerClass = nullptr; // campos
  long long constValue = 0;    // enum consts
  bool atomic = false;         // destino com acesso atômico
  bool isVolatile = false;   // destino com acesso volátil
};

struct Expr : Node {
  ExprKind kind;
  Type exprType;               // preenchido pela análise semântica
  explicit Expr(ExprKind k) : kind(k) {}
};

struct IntLitExpr : Expr {
  long long value = 0;
  bool isUnsigned = false;
  IntLitExpr() : Expr(ExprKind::IntLit) {}
};

// `spawn [task] { ... }` como expressão (spec §10): devolve `task<T>`; o
// corpo deve terminar com `return <expr>;` cujo tipo define T. O valor fica
// pronto após `t.Wait()`. Mesmas regras de captura do SpawnStmt.
struct SpawnExpr : Expr {
  std::unique_ptr<BlockStmt> body;   // o corpo (conclui com return do payload)
  std::vector<std::pair<std::string, Type>> captures;
  SpawnExpr() : Expr(ExprKind::Spawn) {}
};

// `await E` (spec §10, v0.22.3): espera a conclusão de E (uma `task<T>`) e
// devolve o payload. Só permitido dentro de função `async`.
struct AwaitExpr : Expr {
  std::unique_ptr<Expr> operand;
  AwaitExpr() : Expr(ExprKind::Await) {}
};

// v0.95: lambda `(params) => corpo` — função anônima de primeira classe.
// Captura por valor (cópia no env no ponto de criação, como `spawn`).
// O valor é um handle heap {code, env} de 16 bytes (tipo `func<R, P...>`).
struct LambdaExpr : Expr {
  struct Param {
    std::string name;
    Type type;          // vazio (Unknown) = a inferir pela semântica
    bool hasType = false;
    int line = 0;
  };
  std::vector<Param> params;
  std::unique_ptr<BlockStmt> body;  // corpo-expr vira bloco com Return
  Type returnType;                   // inferido (void = sem return)
  bool hasReturnType = false;
  // contexto: tipo declarado no VarDecl/param/retorno (ex.: `func<int,int> f = ...`)
  bool hasContextType = false;
  Type contextType;
  // capturas (nome, tipo) — preenchido pela semântica
  std::vector<std::pair<std::string, Type>> captures;
  // v0.95: auto-referência (`func<..> f = ... f ...`): a criação grava o
  // próprio box no slot do env desta captura (ponto fixo da recursão)
  std::string selfName;
  LambdaExpr() : Expr(ExprKind::Lambda) {}
};

struct FloatLitExpr : Expr {
  double value = 0.0;
  FloatLitExpr() : Expr(ExprKind::FloatLit) {}
};

struct StringLitExpr : Expr {
  std::string value;
  StringLitExpr() : Expr(ExprKind::StringLit) {}
};

struct CharLitExpr : Expr {
  int value = 0;
  CharLitExpr() : Expr(ExprKind::CharLit) {}
};

struct BoolLitExpr : Expr {
  bool value = false;
  BoolLitExpr() : Expr(ExprKind::BoolLit) {}
};

struct NullLitExpr : Expr {
  NullLitExpr() : Expr(ExprKind::NullLit) {}
};

struct IdentExpr : Expr {
  std::string name;
  SymbolInfo symbol;           // resolvido
  bool isProperty = false;     // propriedade sem qualificação (spec §19)
  Type propType;               // tipo da propriedade (resolvido)
  FunctionDecl* propGet = nullptr;
  FunctionDecl* propSet = nullptr;
  IdentExpr() : Expr(ExprKind::Ident) {}
};

struct MemberExpr : Expr {
  std::unique_ptr<Expr> object;
  std::string member;
  int fieldOffset = -1;        // resolvido (campos)
  long long enumValue = 0;     // resolvido (enum consts)
  bool isEnumConst = false;
  bool isArrayLength = false;  // arr.Length (tamanho constante em enumValue)
  bool isListLength = false;   // list.Length (tamanho em runtime)
  bool isMapLength = false;    // map.Length (tamanho em runtime)
  bool isGlobalRef = false;    // Mod.global — variável global de outro módulo
  VarDecl* resolvedGlobal = nullptr; // resolvido (isGlobalRef)
  bool isModuleTypeRef = false; // Mod.Classe/Mod.Enum como valor intermediário
  bool isEnumCtor = false;      // variante de enum rico (gera célula)
  std::string enumCtorEnum;     // enum canônico da construção
  int enumCtorIndex = -1;       // índice da variante (packedPayload)
  bool isProperty = false;      // Propriedade (spec §19): acesso via get/set
  Type propType;                // tipo da propriedade (resolvido)
  FunctionDecl* propGet = nullptr; // accessor get (null = somente escrita)
  FunctionDecl* propSet = nullptr; // accessor set (null = somente leitura)
  bool fieldAtomic = false;    // campo atomic (escrita atômica)
  bool isBaseCall = false;     // M10: `base.Metodo()` — resolve na classe base
  bool isWait = false;          // task.Wait() — espera e devolve the payload
  bool isTaskCancelled = false; // Task.IsCancelled — flag de cancelamento (v0.23)
  bool isResultIsOk = false;    // res.IsOk
  bool isResultIsError = false; // res.IsError / res.IsErr
  bool isResultValue = false;   // res.Value
  bool isResultError = false;   // res.Error
  bool isOptionHasValue = false;// opt.HasValue / opt.IsSome
  bool isOptionIsNone = false;  // opt.IsNone
  bool isOptionValue = false;   // opt.Value
  MemberExpr() : Expr(ExprKind::Member) {}
};

// `e?.membro` / `e?.metodo(args)` (spec §12): acesso condicional a referência
// de classe. Campo/método de classe ou string → referência (null quando e é null);
// tipo de valor → Option<T>; método void → skip (somente como instrução).
struct OptMemberExpr : Expr {
  std::unique_ptr<Expr> object;
  std::string member;
  bool isField = true;          // campo (false: método — CallExpr por cima)
  int fieldOffset = 0;          // resolvido (campo)
  FunctionDecl* resolved = nullptr; // resolvido (método)
  OptMemberExpr() : Expr(ExprKind::OptMember) {}
};

struct IndexExpr : Expr {
  std::unique_ptr<Expr> object; // array
  std::unique_ptr<Expr> index;  // inteiro
  IndexExpr() : Expr(ExprKind::Index) {}
};

// `e?[i]` (Item 2.16): indexação condicional/segura em Array, List ou String.
// Elemento de referência (Class/String) -> referência (null se fora dos limites);
// Tipo de valor -> Option<T> (None se fora dos limites).
struct OptIndexExpr : Expr {
  std::unique_ptr<Expr> object;
  std::unique_ptr<Expr> index;
  OptIndexExpr() : Expr(ExprKind::OptIndex) {}
};

struct ArrayLitExpr : Expr {
  std::vector<std::unique_ptr<Expr>> elements;
  ArrayLitExpr() : Expr(ExprKind::ArrayLit) {}
};

// `(e1, e2, ...)` — literal de tupla (M10.1b): só em contexto de return/
// inicializador de tuple; cada elemento ocupa um slot de 8 bytes.
struct TupleLitExpr : Expr {
  std::vector<std::unique_ptr<Expr>> elements;
  TupleLitExpr() : Expr(ExprKind::TupleLit) {}
};

struct CallExpr : Expr {
  std::unique_ptr<Expr> callee; // IdentExpr ou MemberExpr
  std::vector<std::unique_ptr<Expr>> args;
  std::vector<Type> genericArgs; // genérico explícito: `F<int>(x)`
  FunctionDecl* resolved = nullptr; // função resolvida (null = print builtin)
  bool isPrint = false;
  bool isClockNs = false; // clock_ns() — nanossegundos (QPC), builtin
  bool isSqrt = false;    // M11-bench: sqrt(x) — sqrtsd/libm
  bool isArenaReset = false; // M11.9: arena_reset() — recicla a frame-arena
  int stdBuiltin = -1;       // M12.0: índice no registro declarativo
  std::string stdSymbol;     // M12.0: símbolo runtime resolvido
  bool isListAdd = false; // list.Add(x) — runtime, não é método de classe
  bool isListNew = false; // B11: list<T>(n) — construtor com capacidade
  Type listElemType;      // B11: tipo do elemento (do generic arg)
  bool isMapPut = false;    // map.Put(k,v)
  bool isMapGet = false;    // map.Get(k)
  bool isMapContains = false; // map.Contains(k)
  bool isMapRemove = false; // map.Remove(k)
  bool isMapClear = false;  // map.Clear()
  bool isBaseCall = false; // M10: `base.Metodo()` — resolve na base (estático)
  bool isWait = false;    // t.Wait() — espera a task e devolve o payload
  bool isTaskCancel = false; // t.Cancel() — sinal cooperativo de cancelamento
  bool isActorCall = false;  // chamada externa a método de actor: lock no this
  // v0.46: chamada a função `compiletime` com args constantes — dobrada
  bool folded = false;
  long long foldValue = 0;
  bool isChannelSend = false;    // ch.Send(x) — envia valor ao channel (runtime)
  bool isChannelReceive = false; // ch.Receive() — bloqueia até haver valor
  bool isResultIsOk = false;     // res.is_ok()
  bool isResultIsErr = false;    // res.is_err()
  bool isResultUnwrap = false;   // res.unwrap()
  bool isResultUnwrapErr = false;// res.unwrap_err()
  bool isResultUnwrapOr = false; // res.unwrap_or(def)
  bool isOptionIsSome = false;   // opt.is_some()
  bool isOptionIsNone = false;   // opt.is_none()
  bool isOptionUnwrap = false;   // opt.unwrap()
  bool isOptionUnwrapOr = false; // opt.unwrap_or(def)
  // primitivas de sincronização (v0.24.0, spec §10): mutex/semaphore/event/barrier
  enum class PrimOp {
    None, MutexLock, MutexUnlock, SemaphoreWait, SemaphoreSignal,
    EventWait, EventSet, EventReset, BarrierWait
  };
  PrimOp primOp = PrimOp::None;
  bool isAsyncCall = false;  // F(...) com F async — spawna e devolve task<T>
  bool isModuleCall = false; // Mod.func(x) — função de outro módulo (sem this)
  bool isEnumCtor = false;    // NetworkState.Connected(...) — constrói célula
  std::string enumCtorEnum;   // enum canônico da construção
  int enumCtorIndex = -1;     // índice da variante
  bool isFromInt = false;     // Direction.FromInt(n) — enum simples → Option<Enum>
  std::string fromIntEnum;    // enum canônico
  bool isSerialize = false;   // e.Serialize() — enum com derive Serializable
  std::string serializeEnum;  // enum canônico
  bool isStrEq = false;  // __hphl_str_eq(a, b) — igualdade de strings (v0.25.0)
  bool isStrCmp = false; // __hphl_str_cmp(a, b) — ordem lexicográfica (v0.25.0)
  bool isToStr = false;  // __hphl_to_str(x) / toString(x) — escalar → string (M5)
  bool isAddrOf = false; // FFI v2: addr_of(x) — endereço do lvalue como ptr (void*)
  bool isFuncCall = false; // v0.95: callee é valor `func` (call indireto)
  Type funcType;           // v0.95: assinatura do callee quando isFuncCall
  CallExpr() : Expr(ExprKind::Call) {}
};

enum class BinOp {
  Add, Sub, Mul, Div, Mod,
  Eq, Ne, Lt, Gt, Le, Ge,
  And, Or,
  BitAnd, BitOr, BitXor, Shl, Shr,
};

struct BinaryExpr : Expr {
  BinOp op;
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  FunctionDecl* derivedEq = nullptr;  // ==/!= fieldwise via derive Equatable (v0.25.0)
  FunctionDecl* derivedCmp = nullptr; // </<=/>/>= fieldwise via derive Comparable
  bool strEqBin = false;              // M14.4: ==/!= entre strings via hphl_str_eq (conteúdo)
  BinaryExpr() : Expr(ExprKind::Binary) {}
};

enum class UnOp { Neg, Not, BitNot, PreInc, PreDec, PostInc, PostDec };

struct UnaryExpr : Expr {
  UnOp op;
  std::unique_ptr<Expr> operand;
  UnaryExpr() : Expr(ExprKind::Unary) {}
};

enum class AssignOp { Plain, Add, Sub, Mul, Div, Mod };

struct AssignExpr : Expr {
  AssignOp op;
  std::unique_ptr<Expr> target;  // IdentExpr ou MemberExpr
  std::unique_ptr<Expr> value;
  AssignExpr() : Expr(ExprKind::Assign) {}
};

struct TernaryExpr : Expr {
  std::unique_ptr<Expr> cond;
  std::unique_ptr<Expr> thenExpr;
  std::unique_ptr<Expr> elseExpr;
  TernaryExpr() : Expr(ExprKind::Ternary) {}
};

struct CastExpr : Expr {
  Type target;
  std::unique_ptr<Expr> operand;
  CastExpr() : Expr(ExprKind::Cast) {}
};

struct NewExpr : Expr {
  std::string className;
  std::vector<Type> genericArgs; // genérico: `new Caixa<int>(...)`
  std::vector<std::unique_ptr<Expr>> args;
  std::vector<std::string> argNames; // argName[i] != "" quando nomeado (spec 12)
  FunctionDecl* ctor = nullptr;      // resolvido
  bool resolved = false;             // checkNew já executado (var x = new ...)
  bool isChannel = false;       // `new channel<T>(cap)` — builtin do runtime
  bool isList = false;          // `new list<T>(...)` — builtin do runtime
  bool isArrayNew = false;      // `new int[N]` — aloca array na heap
  long long arraySize = 0;      // tamanho literal do array (isArrayNew)
  std::unique_ptr<Expr> arraySizeExpr; // tamanho runtime (isArrayNew)
  long long channelCapacity = 16; // capacidade da fila (default sem arg)
  NewExpr() : Expr(ExprKind::New) {}
};

// ---------------------------------------------------------------------------
// match / pattern matching (spec seção 13)
// ---------------------------------------------------------------------------
// Árvore recursiva de padrões — suporta padrões aninhados:
//  - Wildcard  `_`
//  - Const     5 / 'c' / Enum.Var
  //  - StrConst  "texto" (match sobre string)
//  - Range     lo..hi (só literal)
//  - Bind      `x`   (liga o valor)
//  - Variant   `V(p1, p2)`  (enum rico / Option / Result)
//  - Struct    `P(x: p1, y: p2)` (struct; campos em subNames)
//  - List      `[p1, p2, ..resto]` (list/array; hasRest + restName)
struct Pattern {
  enum class K { Wildcard, Const, StrConst, Range, Bind, Variant, Struct, List };
  K kind = K::Wildcard;
  long long constValue = 0;   // Const/Range: valores
  std::string strValue;       // StrConst: literal string do padrão
  long long rangeLo = 0;
  long long rangeHi = 0;
  std::string path;           // Variant/Struct: caminho nomeado (Enum.V, Mod.T)
  std::string bindName;       // Bind: nome
  Type bindType;              // Bind: tipo resolvido
  std::vector<std::string> subNames;   // Struct: campos (paralelo a subs)
  std::vector<std::unique_ptr<Pattern>> subs; // Variant payloads / Struct campos / List
  bool hasRest = false;       // List: `..resto`
  std::string restName;
  Type restType;              // List: tipo do rest
  void add(std::unique_ptr<Pattern> p) { subs.push_back(std::move(p)); }
};

// Um braço de `match`: padrão + guarda opcional (`when`) + corpo.
// Corpo pode ser bloco `{ ... }` (match-instrução) ou `=> expr;` (match-expr).
struct MatchArm {
  int line = 0;
  std::unique_ptr<Pattern> pattern;      // padrão do braço (raiz)
  bool hasSubjectBind = false;           // `nome @ padrão` — liga o sujeito a `nome`
  std::string subjectBind;
  Type subjectBindType;
  std::unique_ptr<Expr> guard;           // `when <expr>` (opcional)
  std::vector<std::unique_ptr<Stmt>> body; // match-instrução: bloco
  std::unique_ptr<Expr> yield;           // match-expr: `=> expr`
  Type yieldType;                        // tipo comum dos yields (match-expr)
};

struct MatchExpr : Expr {
  std::unique_ptr<Expr> subject;
  std::vector<MatchArm> arms;
  MatchExpr() : Expr(ExprKind::Match) {}
};

// ---------------------------------------------------------------------------
// Option/Result e propagação de erro (spec seção 12)
// ---------------------------------------------------------------------------
// `Some(x)` / `None` / `Ok(x)` / `Err(e)` — constroem célula [tag][payload]
struct OptCtorExpr : Expr {
  std::string variant;   // "Some" | "None" | "Ok" | "Err"
  std::unique_ptr<Expr> arg;  // Some/Ok/Err: payload (None: null)
  OptCtorExpr() : Expr(ExprKind::OptCtor) {}
};

// `x?` — desembrulha Option/Result; propaga None/Err para o retorno da função
struct TryExpr : Expr {
  std::unique_ptr<Expr> operand;
  TryExpr() : Expr(ExprKind::Try) {}
};

// `a ?? padrao` — valor do Some(a) ou o padrão
struct CoalesceExpr : Expr {
  std::unique_ptr<Expr> lhs;
  std::unique_ptr<Expr> rhs;
  CoalesceExpr() : Expr(ExprKind::Coalesce) {}
};

struct ThisExpr : Expr {
  ThisExpr() : Expr(ExprKind::This) {}
};

// ---------------------------------------------------------------------------
// Declarações de alto nível (cont.)
// ---------------------------------------------------------------------------
struct VarDecl : Decl {
  StoragePolicy storage = StoragePolicy::Auto;
  OverflowPolicy overflow = OverflowPolicy::Default;
  Type type;
  bool hasType = false;
  bool isVar = false;          // inferência
  std::string name;
  Access access = Access::Public;
  bool isConst = false;
  bool isReadonly = false;
  bool isGlobal = false;
  bool atomic = false;         // acesso atômico (apenas inteiros)
  bool isVolatile = false;   // acesso volátil (previne otimizações)
  std::unique_ptr<Expr> init;
  long long primitiveInit = -1; // primitivas (v0.24.0): `semaphore s(3)` — -1 = sem arg
  VarDecl() : Decl(DeclKind::GlobalVar) {}
};

struct FunctionDecl : Decl {
  Access access = Access::Public;
  bool isStatic = false;
  bool isInline = false;
  bool isConstructor = false;
  bool isMethod = false;
  bool isTask = false;             // função sintética de `spawn`/`parallel`
  bool isLambda = false;           // v0.95: função sintética de lambda `(p) => ...`
  bool isAsync = false;            // `async T F(...)`: roda numa thread; a
                                   // chamada devolve task<T>; `return` = payload
  std::string taskPayloadTypeName; // payload de task<T>: nome do label ("" se void)
  std::vector<std::pair<std::string, Type>> taskCaptures; // capturas por valor
  std::string ownerClass;      // se método
  bool isSynthetic = false;    // v0.46: gerada pelo compilador (derive, etc.)
  bool isCompiletime = false;  // v0.46: `compiletime int F(...)` — avaliada
                               // em compilação quando os args são constantes
  std::string name;
  Type returnType;             // Void se sem retorno
  bool hasReturnType = false;
  std::vector<std::unique_ptr<Param>> params;
  std::vector<TypeParam> typeParams; // genéricos: `class X<T, U>`
  // A4: contratos — `require cond` (pré, na entrada) e `ensure cond`
  // (pós, em cada return; pode usar `result` = valor retornado)
  std::vector<std::unique_ptr<Expr>> requires;
  std::vector<std::unique_ptr<Expr>> ensures;
  std::unique_ptr<BlockStmt> body;
  bool isEntryPoint = false;   // Main
  bool isExtern = false;       // função nativa FFI (sem corpo)
  std::string externLib;       // biblioteca associada (ex: "user32", "gdi32")
  // M10 (v0.45): parâmetros de valor da instância genérica (nome → constante)
  std::map<std::string, long long> litParams;
  FunctionDecl() : Decl(DeclKind::Function) {}
};

struct EnumParam {
  Type type;
  std::string name;
  int line = 0;
};

struct EnumEntry {
  std::string name;
  long long value = 0;
  bool hasPayload = false;           // variante com dados (tipos de soma)
  std::vector<EnumParam> params;     // parâmetros do payload
};

struct EnumDecl : Decl {
  Access access = Access::Public;
  std::string name;
  Type baseType = Type::makeInt(32);
  std::vector<EnumEntry> entries;
  bool anyPayload = false;           // enum com pelo menos uma variante com dados
  std::vector<std::string> derives;  // derive Equatable / derive Comparable
  int payloadBytes = 0;              // bytes do maior payload (alinhado a 8)
  EnumDecl() : Decl(DeclKind::Enum) {}
};

struct ModuleDecl : Decl {
  std::string name;
  std::vector<std::string> dependsOn; // `module X depends on A, B;`
  std::vector<std::unique_ptr<Decl>> decls;
  ModuleDecl() : Decl(DeclKind::Module) {}
};

// `using X = Mod.Tipo;` (alias local) ou `public use Mod.Nome;` (reexportação)
struct UsingDecl : Decl {
  std::string alias;       // nome local (no reexport = último segmento)
  Type target;             // tipo-alvo (o nome pode ser qualificado)
  bool isReexport = false; // `public use` (senão `using X = ...`)
  UsingDecl() : Decl(DeclKind::Using) {}
};

// M10 (v0.46): especialização manual — `specialize Buffer<int, 64>;`
// força a monomorfização (classe + métodos) mesmo sem uso posterior
struct SpecializeDecl : Decl {
  Type type;
  SpecializeDecl() : Decl(DeclKind::Specialize) {}
};

// M10.1c (v0.46): reflexão opt-in — `reflect Player;` (spec §40) habilita
// os builtins de metadados (SizeOf/FieldCount/HasField/FieldOffset)
struct ReflectDecl : Decl {
  std::string typeName;
  ReflectDecl() : Decl(DeclKind::Reflect) {}
};

// `import Modulo;` ou `import "caminho.hphl";` — resolvido pelo loader em main.cpp
struct ImportDecl : Decl {
  std::string name;   // módulo referenciado (pode ser 'A.B.C')
  std::string path;   // caminho explícito quando import "..." é usado
  std::string alias;  // `import X as Y;` — nome local opcional
  bool isPath = false;
  ImportDecl() : Decl(DeclKind::Import) {}
};

struct Program : Node {
  std::string moduleName;
  // nomes de módulo declarados: nome → arquivo (preenchido pelo loader)
  std::vector<std::pair<std::string, std::string>> modules;
  // aliases de import: alias → nome do módulo (preenchido pelo loader)
  std::vector<std::pair<std::string, std::string>> aliases;
  // dependências declaradas: módulo → módulos (preenchido pelo parser)
  std::vector<std::pair<std::string, std::vector<std::string>>> moduleDeps;
  std::vector<std::unique_ptr<Decl>> decls;
  // FFI nativo: bibliotecas externas (`extern "lib"`) para o linker (-l<lib>)
  std::vector<std::string> externLibs;
};

} // namespace hphl