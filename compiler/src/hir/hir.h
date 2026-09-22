#pragma once

#include "ast/ast.h"
#include "types/types.h"
#include <vector>
#include <map>
#include <string>

namespace hphl {

class Semantic;

// ---------------------------------------------------------------------------
// HIR com corpos (Milestone 3): representação sem açúcar sintático.
// Sugar removido no lowering: match (→ if-chain), switch (→ if-chain),
// foreach (→ for + índice), `?:` (→ if + temp), `??` (→ if + temp),
// `e?.membro` / `e?.M()` (→ if + temp), `x?` (→ if + return propagado).
// Mantidos como nós: try/catch, lock, spawn/parallel/await, new, chamadas
// especiais, construção de células Option/Result.
// ---------------------------------------------------------------------------

enum class HirExprKind {
  IntLit, FloatLit, StringLit, CharLit, BoolLit, NullLit,
  Var, Member, Index, ArrayLit, TupleLit, Call, Binary, Unary, Assign,
  Cast, New, This, CellTag, LoadAt, OptCtor, Spawn, Await, Lambda,
};

enum class HirStmtKind {
  Block, VarDecl, ExprStmt, Return, If, While, DoWhile, For,
  Break, Continue, Panic, Assert, Throw, Try, Lock, Spawn, Parallel,
  ParallelForeach,
};

enum class HirCallKind {
  Normal, Print, ClockNs, ListAdd, ListNew, MapPut, MapGet, MapContains, MapRemove, MapClear, MapLen, Wait, TaskCancel, ActorCall,
  ChannelSend, ChannelReceive, PrimOp, Async, ModuleCall,
  EnumCtor, FromInt, Serialize, StrEq, StrCmp, ToStr, AddrOf, Runtime, ArenaReset, Sqrt,
  ResultIsOk, ResultIsErr, ResultUnwrap, ResultUnwrapErr, ResultUnwrapOr,
  OptionIsSome, OptionIsNone, OptionUnwrap, OptionUnwrapOr, Indirect,
};

struct HirExpr {
  HirExprKind kind;
  Type type;  // tipo da expressão (pós-semântica; usado pelo codegen HIR)
  explicit HirExpr(HirExprKind k) : kind(k) {}
  virtual ~HirExpr() = default;
};

struct HirIntLit : HirExpr {
  long long value = 0;
  bool isUnsigned = false;
  HirIntLit() : HirExpr(HirExprKind::IntLit) {}
};

struct HirFloatLit : HirExpr {
  double value = 0.0;
  HirFloatLit() : HirExpr(HirExprKind::FloatLit) {}
};

struct HirStringLit : HirExpr {
  std::string value;
  HirStringLit() : HirExpr(HirExprKind::StringLit) {}
};

struct HirCharLit : HirExpr {
  int value = 0;
  HirCharLit() : HirExpr(HirExprKind::CharLit) {}
};

struct HirBoolLit : HirExpr {
  bool value = false;
  HirBoolLit() : HirExpr(HirExprKind::BoolLit) {}
};

struct HirNullLit : HirExpr {
  HirNullLit() : HirExpr(HirExprKind::NullLit) {}
};

struct HirVar : HirExpr {
  std::string name;
  bool isGlobal = false;   // variável global (label)
  std::string label;       // global: label no .data
  bool isConst = false;    // constante de enum (valor)
  long long constValue = 0;
  bool isProperty = false; // propriedade sem qualificação (this)
  FunctionDecl* propGet = nullptr;
  FunctionDecl* propSet = nullptr;
  bool isThisField = false; // campo do `this` sem qualificação
  long long thisFieldOffset = 0;
  bool atomic = false;      // destino atomic (xchg/lock xadd)
  HirVar() : HirExpr(HirExprKind::Var) {}
};

struct HirMember : HirExpr {
  std::unique_ptr<HirExpr> object;
  std::string member;
  int fieldOffset = -1;        // campo (resolvido)
  bool isEnumConst = false;    // Enum.C (valor em enumValue)
  long long enumValue = 0;
  bool isArrayLength = false;  // arr.Length (arraySize constante)
  long long arraySize = 0;
  bool isListLength = false;   // list.Length (runtime)
  bool isMapLength = false;    // map.Length (runtime)
  bool isGlobalRef = false;    // Mod.global (resolvedGlobal)
  VarDecl* resolvedGlobal = nullptr;
  bool isModuleTypeRef = false; // Mod.Classe/Mod.Enum como valor (0)
  bool isEnumCtor = false;     // variante de enum rico sem args
  std::string enumCtorEnum;
  int enumCtorIndex = -1;
  bool isProperty = false;     // propriedade (get/set — não desaçucarado ainda)
  Type propType;
  FunctionDecl* propGet = nullptr;
  FunctionDecl* propSet = nullptr;
  bool fieldAtomic = false;
  bool isWait = false;         // task.Wait()
  bool isTaskCancelled = false;
  bool isResultIsOk = false;
  bool isResultIsError = false;
  bool isResultValue = false;
  bool isResultError = false;
  bool isOptionHasValue = false;
  bool isOptionIsNone = false;
  bool isOptionValue = false;
  HirMember() : HirExpr(HirExprKind::Member) {}
};

struct HirIndex : HirExpr {
  std::unique_ptr<HirExpr> object;
  std::unique_ptr<HirExpr> index;
  bool isListBuffer = false;  // foreach: objeto = ponteiro do list (list_data)
  HirIndex() : HirExpr(HirExprKind::Index) {}
};

struct HirArrayLit : HirExpr {
  std::vector<std::unique_ptr<HirExpr>> elements;
  HirArrayLit() : HirExpr(HirExprKind::ArrayLit) {}
};

// M10.1b: `(e1, e2, ...)` — bloco heap de N×8 bytes; o valor é o handle
struct HirTupleLit : HirExpr {
  std::vector<std::unique_ptr<HirExpr>> elements;
  HirTupleLit() : HirExpr(HirExprKind::TupleLit) {}
};

struct HirCall : HirExpr {
  HirCallKind kind = HirCallKind::Normal;
  FunctionDecl* resolved = nullptr;  // Normal/Async/ModuleCall
  std::string runtimeName;           // Runtime: hphl_list_len etc.
  bool isBaseCall = false;           // M10: `base.M()` — estático na base
  std::string enumCanon;             // EnumCtor/FromInt/Serialize
  bool folded = false;               // v0.46: compiletime dobrada
  long long foldValue = 0;
  std::string entryName;             // EnumCtor: nome da variante
  int index = -1;                    // EnumCtor: variante
  int primOp = 0;                    // PrimOp: CallExpr::PrimOp
  std::vector<std::unique_ptr<HirExpr>> args;
  bool isResultIsOk = false;
  bool isResultIsErr = false;
  bool isResultUnwrap = false;
  bool isResultUnwrapErr = false;
  bool isResultUnwrapOr = false;
  bool isOptionIsSome = false;
  bool isOptionIsNone = false;
  bool isOptionUnwrap = false;
  bool isOptionUnwrapOr = false;
  std::unique_ptr<HirExpr> funcValue; // Indirect (v0.95): valor `func` chamado
  HirCall() : HirExpr(HirExprKind::Call) {}
};

struct HirBinary : HirExpr {
  BinOp op;
  std::unique_ptr<HirExpr> lhs;
  std::unique_ptr<HirExpr> rhs;
  FunctionDecl* derivedEq = nullptr;  // ==/!= fieldwise
  FunctionDecl* derivedCmp = nullptr; // </<=/>/>= fieldwise
  bool strEqBin = false;              // M14.4: ==/!= entre strings (conteúdo)
  HirBinary() : HirExpr(HirExprKind::Binary) {}
};

struct HirUnary : HirExpr {
  UnOp op;
  std::unique_ptr<HirExpr> operand;
  HirUnary() : HirExpr(HirExprKind::Unary) {}
};

struct HirAssign : HirExpr {
  AssignOp op;
  std::unique_ptr<HirExpr> target;
  std::unique_ptr<HirExpr> value;
  HirAssign() : HirExpr(HirExprKind::Assign) {}
};

struct HirCast : HirExpr {
  Type target;
  std::unique_ptr<HirExpr> operand;
  HirCast() : HirExpr(HirExprKind::Cast) {}
};

struct HirNew : HirExpr {
  std::string className;
  FunctionDecl* ctor = nullptr;
  std::vector<std::unique_ptr<HirExpr>> args;
  bool isChannel = false;
  bool isList = false;
  bool isArrayNew = false;
  long long arraySize = 0;
  long long channelCapacity = 16;
  bool hasNamedArgs = false;  // args nomeados (spec §12)
  HirNew() : HirExpr(HirExprKind::New) {}
};

struct HirThis : HirExpr {
  HirThis() : HirExpr(HirExprKind::This) {}
};

struct HirBlock;

// `tag(e)` — palavra de tag de uma célula (Option/Result/enum rico)
struct HirCellTag : HirExpr {
  std::unique_ptr<HirExpr> subject;
  HirCellTag() : HirExpr(HirExprKind::CellTag) {}
};

// `*(e + off)` — leitura no offset do valor do sujeito (payload de célula,
// campo de struct, elemento de array)
struct HirLoadAt : HirExpr {
  std::unique_ptr<HirExpr> subject;
  long long offset = 0;
  HirLoadAt() : HirExpr(HirExprKind::LoadAt) {}
};

struct HirOptCtor : HirExpr {
  std::string variant;  // Some | None | Ok | Err
  std::unique_ptr<HirExpr> arg;
  HirOptCtor() : HirExpr(HirExprKind::OptCtor) {}
};

struct HirSpawnExpr : HirExpr {
  std::unique_ptr<HirBlock> body;
  std::vector<std::pair<std::string, Type>> captures;
  HirSpawnExpr() : HirExpr(HirExprKind::Spawn) {}
};

struct HirAwaitExpr : HirExpr {
  std::unique_ptr<HirExpr> operand;
  HirAwaitExpr() : HirExpr(HirExprKind::Await) {}
};

// v0.95 (lambdas): `(params) => corpo` — corpo + capturas por valor + assinatura
// (params em `params`, retorno em `retType`). O codegen cria a função sintética
// `__lambda_N(env, p...)` e o valor é o handle heap {code, env}.
struct HirLambda : HirExpr {
  std::vector<std::pair<std::string, Type>> params;
  Type retType;
  std::unique_ptr<HirBlock> body;
  std::vector<std::pair<std::string, Type>> captures;
  std::string selfName; // v0.95: auto-referência (box gravado no env)
  HirLambda() : HirExpr(HirExprKind::Lambda) {}
};

// --- instruções ---

struct HirStmt {
  HirStmtKind kind;
  int line = 0;  // debugger (M7): linha de origem (0 = sintético)
  explicit HirStmt(HirStmtKind k) : kind(k) {}
  virtual ~HirStmt() = default;
};

struct HirBlock : HirStmt {
  std::vector<std::unique_ptr<HirStmt>> stmts;
  bool scope = true;  // false: bloco transparente (temps ficam no escopo do pai)
  HirBlock() : HirStmt(HirStmtKind::Block) {}
};

struct HirVarDecl : HirStmt {
  std::string name;
  Type type;
  std::unique_ptr<HirExpr> init;
  StoragePolicy storage = StoragePolicy::Stack;
  bool atomic = false;          // declaração `atomic`
  long long primitiveInit = 0;  // semaphore/barrier: contador inicial
  bool opaqueSlot = false;      // temp: slot guarda o valor, sem alocar/registrar
  HirVarDecl() : HirStmt(HirStmtKind::VarDecl) {}
};

struct HirExprStmt : HirStmt {
  std::unique_ptr<HirExpr> expr;
  HirExprStmt() : HirStmt(HirStmtKind::ExprStmt) {}
};

struct HirReturn : HirStmt {
  std::unique_ptr<HirExpr> value;
  HirReturn() : HirStmt(HirStmtKind::Return) {}
};

struct HirIf : HirStmt {
  std::unique_ptr<HirExpr> cond;
  std::unique_ptr<HirBlock> thenBranch;
  std::unique_ptr<HirStmt> elseBranch;  // HirBlock ou outro HirIf (else-if chain)
  HirIf() : HirStmt(HirStmtKind::If) {}
};

struct HirWhile : HirStmt {
  std::unique_ptr<HirExpr> cond;
  std::unique_ptr<HirBlock> body;
  HirWhile() : HirStmt(HirStmtKind::While) {}
};

struct HirDoWhile : HirStmt {
  std::unique_ptr<HirExpr> cond;
  std::unique_ptr<HirBlock> body;
  HirDoWhile() : HirStmt(HirStmtKind::DoWhile) {}
};

struct HirFor : HirStmt {
  std::unique_ptr<HirStmt> init;   // HirVarDecl ou HirExprStmt
  std::unique_ptr<HirExpr> cond;
  std::unique_ptr<HirExpr> step;
  std::unique_ptr<HirBlock> body;
  HirFor() : HirStmt(HirStmtKind::For) {}
};

struct HirBreak : HirStmt {
  HirBreak() : HirStmt(HirStmtKind::Break) {}
};

struct HirContinue : HirStmt {
  HirContinue() : HirStmt(HirStmtKind::Continue) {}
};

struct HirPanic : HirStmt {
  std::unique_ptr<HirExpr> message;
  HirPanic() : HirStmt(HirStmtKind::Panic) {}
};

struct HirAssert : HirStmt {
  std::unique_ptr<HirExpr> cond;
  int line = 0;  // mensagem de falha ("assert falhou (linha N)")
  HirAssert() : HirStmt(HirStmtKind::Assert) {}
};

struct HirThrow : HirStmt {
  std::unique_ptr<HirExpr> value;
  HirThrow() : HirStmt(HirStmtKind::Throw) {}
};

struct HirTry : HirStmt {
  std::unique_ptr<HirBlock> body;
  bool hasCatch = false;
  Type catchType;
  std::string catchVar;
  std::unique_ptr<HirBlock> catchBody;
  HirTry() : HirStmt(HirStmtKind::Try) {}
};

struct HirLock : HirStmt {
  std::unique_ptr<HirExpr> target;
  std::unique_ptr<HirBlock> body;
  HirLock() : HirStmt(HirStmtKind::Lock) {}
};

struct HirSpawn : HirStmt {
  std::unique_ptr<HirBlock> body;
  std::vector<std::pair<std::string, Type>> captures;
  HirSpawn() : HirStmt(HirStmtKind::Spawn) {}
};

struct HirParallel : HirStmt {
  std::vector<std::unique_ptr<HirBlock>> parts;
  bool isDeterministic = false;
  std::vector<std::vector<std::pair<std::string, Type>>> partCaptures;
  HirParallel() : HirStmt(HirStmtKind::Parallel) {}
};

struct HirParallelForeach : HirStmt {
  std::string itemName;
  Type itemType;
  std::unique_ptr<HirExpr> collection;
  std::unique_ptr<HirBlock> body;
  int batchSize = 0;
  HirParallelForeach() : HirStmt(HirStmtKind::ParallelForeach) {}
};

struct HirFunction {
  std::string name;
  FunctionDecl* decl = nullptr;  // nó AST de origem (corpo emitido dele no M3-lite)
  bool isMethod = false;
  std::string ownerClass;
  Type returnType;
  bool hasReturnType = false;
  std::vector<std::pair<std::string, Type>> params;
  std::string moduleName;
  std::string filePath;
  std::unique_ptr<HirBlock> body;  // corpo desaçucarado (M3)
};

struct HirProgram {
  std::vector<HirFunction> functions;
  std::map<std::string, FunctionDecl*> declMap;
};

// lowering completo: assinaturas + corpos desaçucarados (precisa do Semantic
// para layouts de struct/enum)
bool loweringToHir(HirProgram& hir, const Program* prog, const Semantic& sem);

// representação textual do HIR (debug/`--dump-hir`)
std::string hirToString(const HirProgram& hir);

} // namespace hphl