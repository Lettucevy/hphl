#pragma once
#include "ast/ast.h"
#include "semantic/semantic.h"
#include "hir/hir.h"
#include "stdlib/stdbuiltins.h"
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace hphl {

// Gera assembly x64 (sintaxe AT&T, GNU as) usando a Windows x64 ABI:
//   inteiros/ponteiros: rcx, rdx, r8, r9 (4 registradores)
//   floats:             xmm0..xmm3
//   retorno:            rax (inteiro) / xmm0 (float)
//   stack args:         [rbp + 16 + 8*(k-4)] para argumento k >= 4
//   alinhamento:        rsp ≡ 0 (mod 16) no ponto de chamada
class Codegen {
public:
  Codegen(Semantic& sem, const std::string& filename, bool debug = false);

  // gera o assembly completo
  std::string generate();

  // Verifica se o programa tem HIR disponível
  bool hasHir() const;

private:
  Semantic& sem_;
  std::string filename_;

  struct Local {
    Type type;
    StoragePolicy policy = StoragePolicy::Stack;
    int slot = -1;
    bool isGlobal = false;
    std::string globalLabel;
    int tlsOffset = -1; // global `threadlocal`: offset no bloco por thread (8*i)
    bool byRef = false; // parâmetro ref/out/in: o slot guarda o endereço
    bool atomic = false; // declaração `atomic`: escrita via lock xchg/xadd
  };

  std::vector<std::map<std::string, Local>> scopes_;
  std::map<FunctionDecl*, std::string> fnLabels_;
  std::map<std::string, std::string> stringPool_;
  std::map<std::string, int> tlsOffsets_; // global threadlocal → offset (8*i)
  int tlsSizeBytes_ = 0;         // bloco por thread (globais threadlocal, 8*i)
  int stringCounter_ = 0;
  int labelCounter_ = 0;
  int nextSlot_ = 0;
  struct HeapSlotInfo {
    int slot;
    StoragePolicy policy = StoragePolicy::Heap;
    long long size = 0; // pool: classe de tamanho no free
    std::string typeName; // M20.1.3 4.3: nome do tipo se for classe (para destrutor)
  };
  std::vector<HeapSlotInfo> heapSlots_; // slots a liberar no retorno da função
  int arenaHandleSlot_ = -1;   // M10.2: handle da arena da função (bump alloc)
  std::vector<int> listSlots_; // slots de list a liberar (hphl_list_free)
  std::vector<int> mapSlots_;  // slots de map a liberar (hphl_map_free)
  // M28 28.1: slots que guardam referências GC-managed (class/list/map/tuple
  // locals). O GC precisa desses slots como roots para alcançar objetos.
  std::vector<int> gcRefSlots_;
  // M20-A 4.1: tracking de locks ativos para cleanup em exception path
  std::vector<int> lockStack_;  // slots onde guardamos ptrs de objetos locados
  int retSlot_ = -1;           // slot temporário com o valor de retorno
  bool retIsFloat_ = false;    // retorno é float (xmm0)
  FunctionDecl* curFn_ = nullptr;
  bool isFloatFn_ = false;
  bool needExc_ = false;        // true se o programa usa try/catch ou throw
  std::map<int, std::string> regForSlot_;
  std::vector<std::string> calleeSaved_ = {"%rbx","%r12","%r13","%r14","%r15"};
  size_t nextCalleeIdx_ = 0;

  struct LoopCtx {
    std::string breakLabel;
    std::string continueLabel;
  };
  std::vector<LoopCtx> loopStack_;
  std::map<std::string, long long> loopUpperBounds_; // M13.3 P8: var -> upper bound exclusive
  std::set<std::string> loopVarNames_; // M13.3: vars de indução (para não alocar em reg)
  bool rcxStaged_ = false; // M14.4: lhs do concat estagiado em %rcx — fast paths imediatos desligados
  int r10Live_ = 0;        // M14.4: profundidade de estágio em %r10 — aninhados usam shadow na stack
  long long getLoopBound(const std::string& var) const;
  long long maxIndexForHir(HirExpr* e) const;
  bool canElideHirBoundsCheck(HirExpr* index, long long arraySize) const;
  bool extractLoopBound(HirFor* f, std::string& var, long long& bound) const;

  // ---- debugger (M7): metadados emitidos com --debug ----
  struct DbgVarMeta {
    std::string name;
    Type type;
    int slot = -1;   // endereço = rbp - 8*(slot+1)
    bool byRef = false;
    bool isArg = false;
    bool stackArray = false; // array inline no frame: base = endereço do slot
  };
  struct DbgFnMeta {
    FunctionDecl* fn = nullptr;
    std::string file;
    std::vector<DbgVarMeta> locals;
    std::vector<int> trapLines;
  };
  bool debug_ = false;
  std::vector<DbgFnMeta> dbgFns_;
  // M20-A 4.4: tabela de globais para debug (mapeia nome->label .data)
  struct DbgGlobalMeta {
    std::string name;
    Type type;
    std::string label; // ".Lg_<name>"
  };
  std::vector<DbgGlobalMeta> dbgGlobals_;
  int dbgFnId_ = -1;       // função sendo gerada (índice em dbgFns_)
  int lastTrapLine_ = -1;  // dedup de traps consecutivos na mesma linha
  static int dbgTypeCode(const Type& t);
  static int dbgElemCode(const Type& t); // 0 se sem elemento
  static std::string dbgTypeName(const Type& t); // tipo canônico (F1.2)
  static bool isGcPointer(const Type& t); // M21.1: true se slot é ponteiro GC

  /* M21.1 1.2: descrição de classe para GC scan preciso */
  struct ClassDescInfo {
    std::string canon;
    int size = 0;
    std::vector<unsigned char> bitmap; // 1 bit por slot (1=ponteiro GC)
  };
  void emitClassDesc(const std::string& canon, const ClassInfo& ci); // gera .rodata
  std::string emitClassDescs(); // chama emitClassDesc para cada classe
  std::unordered_map<std::string, int> classIdMap_; // canon -> classId (1-based)
  int nextClassId_ = 1;
  void dbgStartFunction(FunctionDecl* fn);
  void dbgEndFunction();
  void dbgAddLocal(const std::string& name, const Type& t, int slot, bool byRef,
                   bool isArg, bool stackArray = false);
  void dbgAddGlobal(const std::string& name, const Type& t, const std::string& label);
  void emitDebugTrap(int line);
  void emitDebugPrologue();
  void emitDebugEpilogue();
  std::string buildDbgMeta(); // tabelas .data (chamado por generate())

  std::ostringstream text_;
  std::ostringstream data_;
  std::ostringstream rodata_;
  std::ostringstream bss_;

  std::string newLabel(const std::string& base);
  void emit(std::ostringstream& out, const std::string& line);
  void emitText(const std::string& line) { emit(text_, line); }
  void emitData(const std::string& line) { emit(data_, line); }
  void emitRodata(const std::string& line) { emit(rodata_, line); }
  void emitBss(const std::string& line) { emit(bss_, line); }

  bool isFloatType(const Type& t) const {
    return t.kind == Type::Kind::Float;
  }
  bool isIntType(const Type& t) const {
    return t.isInteger() || t.kind == Type::Kind::Bool ||
           t.kind == Type::Kind::Char || t.kind == Type::Kind::String ||
           t.kind == Type::Kind::Class;
  }

  // política: slot guarda um ponteiro (heap, arena, pool, shared).
  // No M1/M2 o codegen trabalha diretamente da AST semântica; HIR/MIR
  // ficam para o Milestone 3+ (backend LLVM/Cranelift e análise de lifetimes).
  bool isPointerPolicy(StoragePolicy p) const {
    return p == StoragePolicy::Heap || p == StoragePolicy::Arena ||
           p == StoragePolicy::Pool || p == StoragePolicy::Shared;
  }

  // arrays: "valor" de um array = endereço da base (stack: leaq do slot;
  // heap/pool/...: ponteiro do slot; global: leaq do label)
  void emitArrayBaseLoad(const Local& l);
  // grava os elementos de um literal {..} (recursivo p/ multidimensionais;
  // arrayType é o tipo do array sendo gravado — o sub-array na recursão)
  void storeArrayLit(const Local& l, const Type& arrayType, ArrayLitExpr* al, long long off);
  // coleta valores constantes de um literal para a seção .data (globais)
  void collectArrayValues(Expr* el, std::vector<std::string>& vals);

  // função: label de uma variável local/global
  Local* findLocal(const std::string& name);
  std::string slotRef(int slot) const {
    auto it = regForSlot_.find(slot);
    if (it != regForSlot_.end()) return it->second;
    return "-" + std::to_string(8 * (slot + 1)) + "(%rbp)";
  }
  bool isRegSlot(int slot) const { return regForSlot_.count(slot) != 0; }
  std::string regForSlot(int slot) const {
    auto it = regForSlot_.find(slot);
    return it == regForSlot_.end() ? "" : it->second;
  }
  bool tryAllocReg(int slot, const Type& t, StoragePolicy p, bool byRef);
  bool isAddressable(Expr* e) const; // lvalue potencial p/ ref/out/in (by value)
  bool hirHasCall(HirExpr* e) const;
  std::string fnLabel(FunctionDecl* fn);

  // geração
  void generateFunctions();
  void genFunction(FunctionDecl* fn);
  void genParamStore(const std::string& src, const Type& t);

  // HIR support (Milestone 3): geração de funções com corpo HIR
  void generateFunctionsFromHir();
  void generateFunctionsFromAst();
  void generateFunctionFromHir(HirFunction* hf);
  // corpo HIR da função `fn` (genFunctionHir): prólogo/epílogo idênticos à AST
  void genFunctionHir(FunctionDecl* fn, HirBlock* body);
  // stmt HIR → asm (espelha genStmt sobre AST desaçucarada)
  void genHirStmt(HirStmt* s);
  void genHirVarDecl(HirVarDecl* vd);
  void genHirExpr(HirExpr* e); // valor: rax (int) ou xmm0 (float)
  void genHirAddr(HirExpr* e); // endereço em rax (lvalue)
  void genHirCall(HirCall* c);
  void genHirCallInternal(FunctionDecl* fn, std::vector<HirExpr*>& args,
                          HirExpr* thisArg, bool forceStaticHir = false);
  void genHirPrint(HirCall* c);
  void genHirBinary(HirBinary* b);
  void genHirUnary(HirUnary* u);
  void genHirCast(HirCast* c);
  void genHirNew(HirNew* n);
  void genHirOptCtor(HirOptCtor* o);
  void genHirEnumCtor(const std::string& enumCanon, int entryIndex,
                      const std::vector<HirExpr*>& args);
  void genHirSpawn(HirSpawnExpr* sp);
  void genHirAwait(HirAwaitExpr* a);
  void genHirCond(HirExpr* e, const std::string& falseLabel);
  void genHirCondTrue(HirExpr* e, const std::string& trueLabel);
  void genHirRuntimeCall(HirCall* c);
  void genHirStdBuiltinCall(HirCall* c, const StdBuiltin& sb);
  void genHirListIndexAddr(HirExpr* object, HirExpr* index);
  void genHirStoreArrayLit(const Local& l, const Type& arrayType,
                           HirArrayLit* al, long long off);
  bool hirIsAddressable(HirExpr* e) const;
  bool hirIsFreshAlloc(HirExpr* e) const;
  void genHirParallelForeach(HirParallelForeach* pf);
  // M10.1b: tuple — literal (handle fresh), destructuring e atribuição-clone
  void genHirTupleLit(HirTupleLit* tl);
  void genDestructure(StmtDestructure* st);
  void genHirPatternAt(Pattern* p, const Type& t, const std::string& addrExpr,
                       const std::string& nextL);
  // função sintética de tarefa a partir de corpo HIR (spawn/parallel); a
  // emissão do corpo é adiada para depois da função corrente
  struct PendingHirTask {
    FunctionDecl* fn = nullptr;
    std::unique_ptr<HirBlock> body;
  };
  FunctionDecl* makeHirTaskFunction(std::unique_ptr<HirBlock> body,
                                    const std::vector<std::pair<std::string, Type>>& captures,
                                    const std::string& payloadTypeName);
  std::vector<PendingHirTask> pendingHirTasks_;
  void genPendingHirTasks();

  void genBlock(BlockStmt* b);
  void genStmt(Stmt* s);
  // M28 32.1: TCO self-tail-call helpers (TCO.txt §5, §21)
  bool canSelfTailCall(const CallExpr* c) const;
  void emitSelfTailCall(CallExpr* c);
  bool canSelfTailCallHir(const HirCall* c) const;
  void emitSelfTailCallHir(const HirCall* c);
  void genTryStmt(TryStmt* st);
  void genThrowStmt(ThrowStmt* st);
  void genVarDecl(VarDecl* v);
  void genLockStmt(LockStmt* st);
  void genSpawnStmt(SpawnStmt* st);
  void genSpawnExpr(SpawnExpr* ex);
  void genParallelStmt(ParallelStmt* st);
  void genParallelForeach(ForeachStmt* st); // `parallel foreach` (v0.22.5)
  std::unique_ptr<BlockStmt> makeParallelForeachBody(ForeachStmt* st,
                                                     bool isList, long long n,
                                                     bool isBatch = false);
  // cria a função sintética de uma tarefa (`spawn`/`parallel`); a emissão do
  // corpo é adiada para depois da função corrente (frames não se intercalam)
  FunctionDecl* makeTaskFunction(std::unique_ptr<BlockStmt> body,
                                 const std::vector<std::pair<std::string, Type>>& captures,
                                 const std::string& payloadTypeName);
  // emissão do site de spawn: aloca o env (copiando os valores das capturas
  // atuais), carrega a função da tarefa e chama hphl_spawn_task_ex
  void emitSpawnSite(FunctionDecl* fn,
                     const std::vector<std::pair<std::string, Type>>& captures);
  void genPendingTasks();
  int taskCounter_ = 0;
  bool programParallel_ = false; // programa usa spawn → join no fim de Main
  int taskEnvSlot_ = -1;         // função de tarefa: env recebido (rcx)
  int taskResSlot_ = -1;         // função de tarefa: slot com o ptr de resultado
  std::vector<FunctionDecl*> pendingTasks_;         // a emitir após generateFunctions()
  std::vector<std::unique_ptr<FunctionDecl>> taskDecls_; // dono dos FunctionDecl
  void genExpr(Expr* e);      // valor: rax (int) ou xmm0 (float)
  std::string optimizeAsm(const std::string& text); // peephole sobre o .text
  void genAddr(Expr* e);      // endereço em rax (lvalue)
  void genCall(CallExpr* c);
  void genAsyncCall(CallExpr* c);
  void genCallInternal(FunctionDecl* fn, std::vector<Expr*>& args, Expr* thisArg,
                       bool forceStatic = false);
  void emitActorCall(CallExpr* c); // chamada externa a actor: lock+unlock no this
  void genPrint(CallExpr* c);
  void genBinary(BinaryExpr* b);
  bool isPureValueExpr(Expr* e); // rhs sem calls p/ atalho de atribuição direta
  bool isSimpleValExpr(Expr* e); // rhs sem calls e sem %r10 p/ caminho rápido
  bool needsOverflowCode(const Type& res); // applyOverflowPolicy emite algo?
  std::string foldSetccJump(const std::string& text); // cmpq+setcc+movzbl+testq+jcc → jcc
  void genUnary(UnaryExpr* u);
  void genCast(CastExpr* c);
  void genNew(NewExpr* n);
  void genInitClassFields(const std::string& className);
  void genClassDestructor(const std::string& className);
  void genMatch(MatchExpr* m);
  // casamento recursivo de padrões do match: emite compares/binds do padrão `p`
  // contra o sujeito cujo VALOR (8 bytes: escalar, ponteiro de célula/struct/
  // list ou base de array) está no endereço `addrExpr`; se o padrão não casar,
  // salta para `nextL`. Declara binds de `p` no escopo do braço (scopes_.back()).
  void genPatternAt(Pattern* p, const Type& t, const std::string& addrExpr,
                    const std::string& nextL);
  void genEnumCtor(const std::string& enumCanon, int entryIndex,
                   const std::vector<Expr*>& args);
  void genFromInt(const std::string& enumCanon, Expr* arg);
  void genOptCtor(OptCtorExpr* o);
  void genTry(TryExpr* t);
  void genOptMember(OptMemberExpr* om);   // campo: `e?.campo`
  void genOptIndex(OptIndexExpr* oi);     // indexação: `e?[i]`
  void genOptCall(CallExpr* c);           // método: `e?.metodo(args)`
  void genCoalesce(CoalesceExpr* c);
  void genCond(Expr* e, const std::string& falseLabel);
  void genCondTrue(Expr* e, const std::string& trueLabel);
  void genGlobal(VarDecl* v);
  std::string internString(const std::string& value);

  void genCallMalloc(long long size);
  void genCallCalloc(long long size);
  void genPolicyAlloc(StoragePolicy pol, long long size); // M10.2
  void emitRuntimeCall(const std::string& name);
  // bounds check de indexação: índice em %rax; a base do array já está em
  // (%rsp) (16 bytes reservados). Panic em runtime se fora de [0, size).
  void emitBoundsCheck(long long arraySize);
  // endereço do elemento de list em %rax (déjá valida o índice; reserva de
  // 16 bytes preserva o ponteiro do list e o índice durante as chamadas)
  void genListIndexAddr(IndexExpr* ex);
  // list.Add(x): objeto e valor avaliados, chamada de runtime apropriada
  void genListAdd(CallExpr* c);
  // remove slot das listas de liberação (objetos/lista que "escapam" no return)
  void releaseEscapeSlot(int slot);
  // aplica a overflow policy do tipo de resultado (em %rax):
  // wrap = trunca para a largura; checked = trunca + panic se fora da faixa;
  // saturate = clamp na faixa; promote/default = nada
  void applyOverflowPolicy(const Type& res);
  void applyFloatOverflowPolicy(const Type& res, int W);

  int classSize(const std::string& name);
  int fieldOffset(const std::string& className, const std::string& field);
  bool isStructType(const Type& t) const;
  // M24 10.2: AArch64 native emitter stub (cross-compile check)
  bool isAArch64Target() const { return false; } // true quando --target aarch64*
  std::string emitAArch64() { return "// AArch64 native: via LLVM backend (cross)\n"; }
  void genStructCopy(const Type& t); // cópia por valor: rax = ponteiro → novo bloco
  bool isFreshAllocExpr(const Expr* e) const;
};

} // namespace hphl