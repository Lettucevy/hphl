#pragma once
#include "ast/ast.h"
#include "semantic/semantic.h"
#include "hir/hir.h"
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>
#include <cstdint>

namespace hphl {

// Gera LLVM IR textual a partir do HIR (Milestone 3+).
// O IR gerado pode ser compilado com `llc` ou `clang -x ir` para
// Windows x64, WebAssembly, ARM64, etc.
class Irgen {
public:
  Irgen(Semantic& sem, const std::string& filename, bool debug = false);

  // gera o módulo LLVM IR completo
  std::string generate();

  // M14.3: EH por flag de propagação (wasm32 não tem returns_twice/naked asm).
  // try/catch viram arestas de CFG estáticas: cada chamada de usuário verifica
  // @hphl_exc_flag e desvia para o catch BB mais interno (ou para um bloco
  // epílogo que retorna default, propagando através dos frames via flag).
  void setWasmEh(bool w) { wasmEh_ = w; }

private:
  Semantic& sem_;
  std::string filename_;
  bool debug_ = false; // F2.4: instrumentação do debugger (IR/LLVM)

  // F2.4 debug IR/LLVM: os locais de usuário são espelhados num bloco
  // [N x i64] por função (hphl_dbg_enter_frame recebe o ponteiro do bloco;
  // o runtime lê via offset negativo). O corpo usa os allocas reais; o bloco
  // é atualizado a cada escrita (dbgMirrorStore).
  struct DbgSlot {
    std::string name;
    Type type;
    bool byRef = false, isArg = false;
    int index = -1; // índice no bloco [N x i64]
  };
  std::vector<DbgSlot> dbgSlots_;              // slots da função corrente
  std::map<std::string, int> dbgSlotIdx_;      // nome → índice (função corrente)
  int dbgSlotCount_ = 0;
  std::string dbgFrame_;                       // SSA do alloca do bloco de debug
  int lastTrapLine_ = 0;                       // dedup de traps por linha
  int dbgFnId_ = -1;                           // fnId da função corrente
  int dbgFnCounter_ = 0;                       // fnIds atribuídos em ordem de geração
  struct DbgFnMeta {
    std::string name, file;
    struct DbgLocMeta { std::string name, type; long long offset = 0, flags = 0; };
    std::vector<DbgLocMeta> locs;
    std::vector<long long> traps;
  };
  std::vector<DbgFnMeta> dbgFnsMeta_;          // meta por função (ordem = fnId)
  // true se o nome é de variável de usuário (não sintético "__")
  static bool isUserSlotName(const std::string& n);
  // registra o slot (nome → índice); usado por emitAllocaSlot e pelo prólogo
  int dbgReserveSlot(const std::string& name, const Type& type, bool byRef,
                     bool isArg);
  // ptr (SSA) do campo do bloco de debug para o local (ou "" se não debug)
  std::string dbgFieldPtr(const std::string& name) const;
  // espelha uma escrita de local de usuário no bloco de debug
  void dbgMirrorStore(const std::string& name, const std::string& val,
                      const Type& t);
  // trap por linha executável (dedup; só quando debug_)
  void dbgEmitTrap(int line);
  // tipo canônico para a meta (mesmo formato do codegen x64)
  std::string dbgTypeName(const Type& t) const;
  // emite @hphl_dbg_meta + tabelas em IR textual (layout idêntico ao x64)
  void emitDebugMeta();

  std::ostringstream out_;
  // allocas de variáveis locais vão para o PRÓLOGO (bloco de entrada): emitir
  // `alloca` no ponto de declaração (dentro de loops) faz o codegen -O0
  // crescer a stack a cada iteração (chkstk + sub rsp) → stack overflow
  // (medido: mandelbrot -O0 0xC00000FD com 8M iterações × 16 bytes)
  std::ostringstream prologue_;
  int labelCounter_ = 0;
  int tempCounter_ = 0;
  int taskCounter_ = 0;        // nomes de tarefas sintéticas (__task.N)
  bool returned_ = false;  // track implicit return suppression
  bool inTask_ = false;    // função corrente é tarefa sintética (spawn/parallel)
  bool spawnedAny_ = false;  // algum spawn emitido → join no fim de Main

  // M14.3: EH por flag (wasm) — ver setWasmEh
  bool wasmEh_ = false;
  std::vector<std::string> wasmCatchStack_; // catch BBs aninhados (topo = mais interno)
  std::string wasmPropLabel_;               // bloco de propagação da função corrente
  void emitWasmExcGuard();                  // flag check + br catch/prop + cont label
  std::string ensureWasmPropLabel();
  // M22 10.1: cleanup de locks/shared no catch para WASM
  struct WasmCleanup {
    std::vector<std::pair<std::string, std::string>> locks; // (target, type)
  };
  std::vector<WasmCleanup> wasmCleanupStack_;
  void emitWasmCleanup(const WasmCleanup& scope);

  // threadlocal: label → offset no bloco da thread (hphl_tls_block)
  std::map<std::string, int> tlsOffsets_;
  int tlsSizeBytes_ = 0;
  std::string tlsAddr(const std::string& label);

  // pilha de loops ativos (breakLabel, continueLabel)
  struct LoopCtx { std::string breakLabel, continueLabel; };
  std::vector<LoopCtx> loopStack_;

  // tarefas sintéticas pendentes (spawn/parallel): corpo emitido depois
  struct PendingTask {
    std::string fnLabel;
    std::vector<std::pair<std::string, Type>> captures;
    std::unique_ptr<HirBlock> body;
  };
  std::vector<PendingTask> pendingTasks_;
  // v0.95 (lambdas): funções sintéticas `__lambda.N(env, params...)`
  struct PendingLambda {
    std::string fnLabel;
    std::vector<std::pair<std::string, Type>> params;
    Type retType;
    std::vector<std::pair<std::string, Type>> captures;
    std::unique_ptr<HirBlock> body;
  };
  std::vector<PendingLambda> pendingLambdas_;
  int lambdaCounter_ = 0;

  // wrappers de chamadas async: leem o env (parâmetros), chamam F, gravam o
  // payload em res (o backend IR emite funções async com params ABI)
  struct AsyncWrapper {
    std::string fnLabel;
    FunctionDecl* fn;
  };
  std::vector<AsyncWrapper> asyncWrappers_;

  // maps FunctionDecl* → nome de label IR (mangled)
  std::map<FunctionDecl*, std::string> fnLabels_;

  // string literal constants (emitted at module scope)
  std::ostringstream rodata_;
  std::map<std::string, std::string> stringLiterals_; // raw string → global name
  std::set<std::string> paramNames_; // tracks which names are function parameters
  // parâmetros ref/out: nome -> aloca (alloca) inicializado no prólogo
  std::map<std::string, std::string> refSlotNames_;
  std::map<std::string, std::string> refSlotTypes_;
  bool entryPointFn_ = false;        // dentro do entry point (main: i32)
  Type curFnRetType_;                // tipo de retorno da função corrente
  FunctionDecl* curFnDecl_ = nullptr; // M10: decl corrente (litParams)

  // locais: slots (allocas) ÚNICOS por declaração (nomes SSA não podem ser
  // redefinidos — dois `for (var i...)` no mesmo escopo colidiriam). Pilha de
  // escopos por bloco: nome → slot corrente
  std::vector<std::map<std::string, std::string>> localSlots_;
  int localCounter_ = 0;
  // registros LIFO por nome (slot, tipo do slot): sobrevive ao pop do escopo
  // (o HIR usa temps do lowering declarados em bloco aninhado DEPOIS do bloco
  // — ex. `return __h3`); o TIPO registrado é o tipo real do alloca
  std::map<std::string, std::vector<std::pair<std::string, Type>>> slotStack_;
  std::unordered_set<std::string> globalNames_; // nomes de variáveis globais
  std::unordered_set<std::string> definedFns_;  // funções já emitidas (evita redefinição)
  // devolve o slot corrente do local (fallback: "%.nome" ou "@nome" para global)
  std::string findLocalSlot(const std::string& name);
  // registra um slot novo para o local e devolve o nome
  std::string declareLocalSlot(const std::string& name, const Type& type = Type::makeInt(64));
  // declara o slot E emite o `alloca` no PRÓLOGO (bloco de entrada da função)
  // — nunca no ponto de declaração (loops: stack cresce por iteração)
  std::string emitAllocaSlot(const std::string& name, const Type& type);
  // slot para ESCRITA: devolve o slot corrente (parâmetros com slot de spill do
  // prólogo entram aqui; idêntico ao findLocalSlot — mantido para documentar
  // que ESCRITA sempre exige slot real, nunca o fallback "%.nome")
  std::string ensureLocalSlot(const std::string& name, const Type& type);
  // tipo LLVM real do valor de um expr (slots têm o tipo do alloca; o tipo
  // declarado no nó HIR pode divergir — ex. temps do match)
  Type actualTypeOf(const HirExpr* e);

  std::string newLabel(const std::string& base);
  std::string newTemp();
  // true se o bloco corrente não tem terminator (última linha é rótulo) —
  // usada na epílogo para emitir o ret implícito mesmo com return condicional
  bool lastLineIsLabel();
  bool curBlockTerminated(); // M11-prep: bloco corrente já tem terminator?
  void emitBrIfOpen(const std::string& label);

  // tipo HP-HL → tipo LLVM IR
  std::string llvmType(const Type& t);
  // tipo de retorno (void → "void", etc.)
  std::string llvmReturnType(const Type& t);

  // função: label
  std::string fnLabel(FunctionDecl* fn);
  // nome canônico da classe (ownerClass simples → "main.Ponto")
  std::string canonClassName(const std::string& ownerClass);

  // corpo HIR → IR
  void genFunction(const HirFunction& hf);
  void genBlock(const HirStmt* s);

  // stmt HIR → IR
  void genHirStmt(const HirStmt* s);
  void genHirVarDecl(const HirVarDecl* vd);
  void genGlobal(VarDecl* v);
  void genHirReturn(const HirReturn* r);
  void genHirIf(const HirIf* ifs);
  void genHirFor(const HirFor* f);
  void genHirWhile(const HirWhile* w);
  void genHirDoWhile(const HirDoWhile* dw);
  void genHirExprStmt(const HirExprStmt* es);
  void genHirPanic(const HirPanic* p);
  void genHirAssert(const HirAssert* a);
  void genHirThrow(const HirThrow* t);
  void genHirTry(const HirTry* t);
  void genHirLock(const HirLock* l);
  void genHirSpawnStmt(const HirSpawn* s);
  void genHirParallel(const HirParallel* p);
  void genHirParallelForeach(const HirParallelForeach* pf);
  // normaliza uma condição (i1/i8/iN) → i1, devolve o nome SSA
  std::string genHirCondI1(const HirExpr* cond);

  // helpers para cond i1 e br condicional
  void emitCondBranch(const HirExpr* cond, const std::string& trueLabel,
                      const std::string& falseLabel);

  // tarefas sintéticas
  std::string makeTaskLabel();
  void queueTask(std::unique_ptr<HirBlock> body,
                 const std::vector<std::pair<std::string, Type>>& captures);
  void genPendingTasks();
  // site de spawn: monta o env (malloc + capturas) e chama hphl_spawn_task_ex;
  // devolve o handle (HphlTask*, i8*) como SSA
  std::string emitSpawnSite(const std::string& taskLabel,
                            const std::vector<std::pair<std::string, Type>>& captures);
  // descarta capturas que não existem no frame do caller
  std::vector<std::pair<std::string, Type>> filterCaptures(
      const std::vector<std::pair<std::string, Type>>& caps);
  // payload i64 (result da task / canal) → tipo LLVM final
  std::string castPayloadFromI64(const std::string& v, const Type& t);
  // escalar → i64 (slot de env/payload de 8 bytes)
  std::string extendToI64(const std::string& v, const Type& t);
  // coleta bytes de um ArrayLitExpr para inicialização de globals
  void collectArrayBytes(Expr* el, const Type& arrayType, std::vector<uint8_t>& bytes);
  // HirVar sintético para ler o local capturado no site do spawn
  std::unique_ptr<HirExpr> hirLocal(const std::string& name, const Type& t);

  // expr HIR → IR: valor em nome temporário (%tmpN)
  std::string genHirExpr(const HirExpr* e);
  std::string genHirIntLit(const HirIntLit* l);
  std::string genHirFloatLit(const HirFloatLit* l);
  std::string genHirBoolLit(const HirBoolLit* l);
  std::string genHirStringLit(const HirStringLit* l);
  std::string genHirVar(const HirVar* v);
  std::string genHirThis(const HirThis* t);
  // endereço de um lvalue (args ref/out)
  std::string genLValueAddress(const HirExpr* e);
  std::string genHirMember(const HirMember* m);
  std::string genHirIndex(const HirIndex* i);
  std::string genHirArrayLit(const HirArrayLit* a);
std::string genHirTupleLit(const HirTupleLit* a);
  // grava os elementos inline no buffer (sub-arrays inline, stride 8/typeSize)
  void genHirArrayLitBody(const HirArrayLit* a, const Type& arrayType,
                          const std::string& base, long long off);
  // ponteiro i8* de campo do `this` (deslocamento em bytes)
  std::string genThisFieldPtr(long long off);
  std::string genHirCall(const HirCall* c);
  std::string genHirBinary(const HirBinary* b);
  std::string genHirUnary(const HirUnary* u);
  // endereço (slot ou i8*) de alvo de ++/-- (Var/Member/Index)
  std::string genHirIncAddr(const HirExpr* t);
  std::string genHirAssign(const HirAssign* a);
  std::string genHirCast(const HirCast* cs);
  std::string genHirNew(const HirNew* n);
  std::string genHirCellTag(const HirCellTag* ct);
  std::string genHirLoadAt(const HirLoadAt* la);
  std::string genHirOptCtor(const HirOptCtor* oc);
  std::string genHirSpawn(const HirSpawnExpr* s);
  std::string genHirAwait(const HirAwaitExpr* a);
  // v0.95 (lambdas)
  std::string makeLambdaLabel();
  void queueLambda(std::unique_ptr<HirBlock> body,
                   const std::vector<std::pair<std::string, Type>>& params,
                   const Type& ret,
                   const std::vector<std::pair<std::string, Type>>& captures);
  void genPendingLambdas();
  std::string genHirLambda(const HirLambda* l);
  std::string genHirIndirectCall(const HirCall* c);

  // utilities
  std::string convertForStore(const std::string& v, const Type& srcT, const std::string& dstTy);
  std::string toI8Store(const std::string& val, const HirExpr* src, const Type& dstT);
  // estende um valor ao tipo do destino (compostos; constantes direto)
  std::string extendToMatch(const std::string& v, const Type& srcT, const std::string& dstTy);
  bool isPointerType(const Type& t);
  std::string llvmBinOp(BinOp op, const Type& t);
  std::string llvmUnaryOp(UnOp op);
  // overflow policy: wrap/saturate/checked para int<w> (valor em tmp, retorna tmp)
  std::string applyOverflowPolicy(const std::string& val, const Type& t);
  // struct: classe com semântica de valor — cópia nas fronteiras de passagem
  bool isStructType(const Type& t) const;
  // A2 (interface como tipo): Class cujo decl é interface (ponteiro único)
  bool isInterfaceType(const Type& t) const;
  // envolve o valor (ponteiro do bloco) em hphl_struct_copy → bloco independente
  std::string genStructCopy(const Type& t, const std::string& val);
  // expr que já produz bloco fresco (New / chamada de ctor) — não aliam
  bool isFreshAlloc(const HirExpr* e) const;
};

} // namespace hphl
