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
//   alinhamento:        rsp Γëí 0 (mod 16) no ponto de chamada
class Codegen {
public:
  Codegen(Semantic& sem, const std::string& filename, bool debug = false);

  // gera o assembly completo
  std::string generate();

  // Verifica se o programa tem HIR dispon├¡vel
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
    bool byRef = false; // par├ómetro ref/out/in: o slot guarda o endere├ºo
    bool atomic = false; // declara├º├úo `atomic`: escrita via lock xchg/xadd
  };

  std::vector<std::map<std::string, Local>> scopes_;
  std::map<FunctionDecl*, std::string> fnLabels_;
  std::map<std::string, std::string> stringPool_;
  std::map<std::string, int> tlsOffsets_; // global threadlocal ΓåÆ offset (8*i)
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
  std::vector<HeapSlotInfo> heapSlots_; // slots a liberar no retorno da fun├º├úo
  int arenaHandleSlot_ = -1;   // M10.2: handle da arena da fun├º├úo (bump alloc)
  std::vector<int> listSlots_; // slots de list a liberar (hphl_list_free)
  std::vector<int> mapSlots_;  // slots de map a liberar (hphl_map_free)
  // M28 28.1: slots que guardam refer├¬ncias GC-managed (class/list/map/tuple
  // locals). O GC precisa desses slots como roots para alcan├ºar objetos.
  std::vector<int> gcRefSlots_;
  // M20-A 4.1: tracking de locks ativos para cleanup em exception path
  std::vector<int> lockStack_;  // slots onde guardamos ptrs de objetos locados
  int retSlot_ = -1;           // slot tempor├írio com o valor de retorno
  bool retIsFloat_ = false;    // retorno ├⌐ float (xmm0)
  FunctionDecl* curFn_ = nullptr;
  bool isFloatFn_ = false;
  bool needExc_ = false;        // true se o programa usa try/catch ou throw
  std::map<int, std::string> regForSlot_;
  std::vector<std::string> calleeSaved_ = {"%rbx","%r13","%r14","%r15"};
  size_t nextCalleeIdx_ = 0;

  struct LoopCtx {
    std::string breakLabel;
    std::string continueLabel;
  };
  std::vector<LoopCtx> loopStack_;
  std::map<std::string, long long> loopUpperBounds_; // M13.3 P8: var -> upper bound exclusive
  std::set<std::string> loopVarNames_; // M13.3: vars de indu├º├úo (para n├úo alocar em reg)
  std::set<std::string> addrTakenVars_; // vars cujo endere├ºo ├⌐ tomado (++/--/ref/out/in)
  bool rcxStaged_ = false; // M14.4: lhs do concat estagiado em %rcx ΓÇö fast paths imediatos desligados
  int r10Live_ = 0;        // M14.4: profundidade de est├ígio em %r10 ΓÇö aninhados usam shadow na stack
  long long getLoopBound(const std::string& var) const;
  long long maxIndexForHir(HirExpr* e) const;
  bool canElideHirBoundsCheck(HirExpr* index, long long arraySize) const;
  bool extractLoopBound(HirFor* f, std::string& var, long long& bound) const;

  // ---- debugger (M7): metadados emitidos com --debug ----
  struct DbgVarMeta {
    std::string name;
    Type type;
    int slot = -1;   // endere├ºo = rbp - 8*(slot+1)
    bool byRef = false;
    bool isArg = false;
    bool stackArray = false; // array inline no frame: base = endere├ºo do slot
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
  int dbgFnId_ = -1;       // fun├º├úo sendo gerada (├¡ndice em dbgFns_)
  int lastTrapLine_ = -1;  // dedup de traps consecutivos na mesma linha
  static int dbgTypeCode(const Type& t);
  static int dbgElemCode(const Type& t); // 0 se sem elemento
  static std::string dbgTypeName(const Type& t); // tipo can├┤nico (F1.2)
  static bool isGcPointer(const Type& t); // M21.1: true se slot ├⌐ ponteiro GC

  /* M21.1 1.2: descri├º├úo de classe para GC scan preciso */
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
  std::ostringstream classInitText_;
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
           t.kind == Type::Kind::Ptr || // FFI v2: ponteiro opaco = inteiro 64-bit
           t.kind == Type::Kind::Class;
  }

  // pol├¡tica: slot guarda um ponteiro (heap, arena, pool, shared).
  // No M1/M2 o codegen trabalha diretamente da AST sem├óntica; HIR/MIR
  // ficam para o Milestone 3+ (backend LLVM/Cranelift e an├ílise de lifetimes).
  bool isPointerPolicy(StoragePolicy p) const {
    return p == StoragePolicy::Heap || p == StoragePolicy::Arena ||
           p == StoragePolicy::Pool || p == StoragePolicy::Shared;
  }

  // arrays: "valor" de um array = endere├ºo da base (stack: leaq do slot;
  // heap/pool/...: ponteiro do slot; global: leaq do label)
  void emitArrayBaseLoad(const Local& l);
  // grava os elementos de um literal {..} (recursivo p/ multidimensionais;
  // arrayType ├⌐ o tipo do array sendo gravado ΓÇö o sub-array na recurs├úo)
  void storeArrayLit(const Local& l, const Type& arrayType, ArrayLitExpr* al, long long off);
  // coleta valores constantes de um literal para a se├º├úo .data (globais)
  void collectArrayValues(Expr* el, std::vector<std::string>& vals);

  // fun├º├úo: label de uma vari├ível local/global
  Local* findLocal(const std::string& name);
  std::string slotRef(int slot) const {
    auto it = regForSlot_.find(slot);
    if (it != regForSlot_.end()) return it->second;
    return "-" + std::to_string(8 * (slot + 1)) + "(%rbp)";
  }
  // Bug 1.3: referência de MEMÓRIA do slot (nunca o registrador). Usado onde
  // `lea` precisa do endereço (ref/out/in, endereços) — um slot reg-backed não
  // é endereçável, então sempre apontamos para o slot na frame.
  std::string slotRefMem(int slot) const {
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
  // lhs float em xmm1 através do rhs: true se avaliar rhs pode destruir
  // xmm1 (binária/unária float, call/await, index com bounds-check, etc.)
  bool hirClobbersXmm1(HirExpr* e) const;
  std::string fnLabel(FunctionDecl* fn);

  // gera├º├úo
  void generateFunctions();
  void genFunction(FunctionDecl* fn);
  void genParamStore(const std::string& src, const Type& t);

  // HIR support (Milestone 3): gera├º├úo de fun├º├╡es com corpo HIR
  void generateFunctionsFromHir();
  void generateFunctionsFromAst();
  void generateFunctionFromHir(HirFunction* hf);
  // corpo HIR da fun├º├úo `fn` (genFunctionHir): pr├│logo/ep├¡logo id├¬nticos ├á AST
  void genFunctionHir(FunctionDecl* fn, HirBlock* body);
  // stmt HIR ΓåÆ asm (espelha genStmt sobre AST desa├ºucarada)
  void genHirStmt(HirStmt* s);
  void genHirVarDecl(HirVarDecl* vd);
  void genHirExpr(HirExpr* e); // valor: rax (int) ou xmm0 (float)
  void genHirAddr(HirExpr* e); // endere├ºo em rax (lvalue)
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
  // Análise de address-taken (regalloc): marca variáveis locais cujo endereço
  // é tomado (++/--, ref/out/in, endereço) para que não sejam alocadas em reg.
  void hirMarkAddrTaken(HirBlock* b);
  void hirMarkAddrTakenExpr(HirExpr* e);
  // M10.1b: tuple ΓÇö literal (handle fresh), destructuring e atribui├º├úo-clone
  void genHirTupleLit(HirTupleLit* tl);
  void genDestructure(StmtDestructure* st);
  void genHirPatternAt(Pattern* p, const Type& t, const std::string& addrExpr,
                       const std::string& nextL);
  // fun├º├úo sint├⌐tica de tarefa a partir de corpo HIR (spawn/parallel); a
  // emiss├úo do corpo ├⌐ adiada para depois da fun├º├úo corrente
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
  // M28 32.1: TCO self-tail-call helpers (TCO.txt ┬º5, ┬º21)
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
  // cria a fun├º├úo sint├⌐tica de uma tarefa (`spawn`/`parallel`); a emiss├úo do
  // corpo ├⌐ adiada para depois da fun├º├úo corrente (frames n├úo se intercalam)
  FunctionDecl* makeTaskFunction(std::unique_ptr<BlockStmt> body,
                                 const std::vector<std::pair<std::string, Type>>& captures,
                                 const std::string& payloadTypeName);
  // emiss├úo do site de spawn: aloca o env (copiando os valores das capturas
  // atuais), carrega a fun├º├úo da tarefa e chama hphl_spawn_task_ex
  void emitSpawnSite(FunctionDecl* fn,
                     const std::vector<std::pair<std::string, Type>>& captures);
  void genPendingTasks();
  int taskCounter_ = 0;
  bool programParallel_ = false; // programa usa spawn ΓåÆ join no fim de Main
  // v0.95 (lambdas)
  void genHirLambda(HirLambda* l);
  void genHirIndirectCall(HirCall* c);
  FunctionDecl* makeHirLambdaFunction(std::unique_ptr<HirBlock> body,
                                      const std::vector<std::pair<std::string, Type>>& params,
                                      const Type& ret,
                                      const std::vector<std::pair<std::string, Type>>& captures);
  int lambdaCounter_ = 0;
  int lambdaEnvSlot_ = -1;
  int taskEnvSlot_ = -1;         // fun├º├úo de tarefa: env recebido (rcx)
  int taskResSlot_ = -1;         // fun├º├úo de tarefa: slot com o ptr de resultado
  std::vector<FunctionDecl*> pendingTasks_;         // a emitir ap├│s generateFunctions()
  std::vector<std::unique_ptr<FunctionDecl>> taskDecls_; // dono dos FunctionDecl
  void genExpr(Expr* e);      // valor: rax (int) ou xmm0 (float)
  std::string optimizeAsm(const std::string& text); // peephole sobre o .text
  void genAddr(Expr* e);      // endere├ºo em rax (lvalue)
  void genCall(CallExpr* c);
  void genAsyncCall(CallExpr* c);
  void genCallInternal(FunctionDecl* fn, std::vector<Expr*>& args, Expr* thisArg,
                       bool forceStatic = false);
  void emitActorCall(CallExpr* c); // chamada externa a actor: lock+unlock no this
  void genPrint(CallExpr* c);
  void genBinary(BinaryExpr* b);
  bool isPureValueExpr(Expr* e); // rhs sem calls p/ atalho de atribui├º├úo direta
  bool isSimpleValExpr(Expr* e); // rhs sem calls e sem %r10 p/ caminho r├ípido
  bool needsOverflowCode(const Type& res); // applyOverflowPolicy emite algo?
  std::string foldSetccJump(const std::string& text); // cmpq+setcc+movzbl+testq+jcc ΓåÆ jcc
  void genUnary(UnaryExpr* u);
  void genCast(CastExpr* c);
  void genNew(NewExpr* n);
  void genInitClassFields(const std::string& className);
  void genClassDestructor(const std::string& className);
  // M31: registra globals string (escalar + arrays) como roots permanentes
  // do GC no Main (strings são GC-gerenciadas; .bss é invisível ao mark).
  void emitGlobalStringRoots();
  void genMatch(MatchExpr* m);
  // casamento recursivo de padr├╡es do match: emite compares/binds do padr├úo `p`
  // contra o sujeito cujo VALOR (8 bytes: escalar, ponteiro de c├⌐lula/struct/
  // list ou base de array) est├í no endere├ºo `addrExpr`; se o padr├úo n├úo casar,
  // salta para `nextL`. Declara binds de `p` no escopo do bra├ºo (scopes_.back()).
  void genPatternAt(Pattern* p, const Type& t, const std::string& addrExpr,
                    const std::string& nextL);
  void genEnumCtor(const std::string& enumCanon, int entryIndex,
                   const std::vector<Expr*>& args);
  void genFromInt(const std::string& enumCanon, Expr* arg);
  void genOptCtor(OptCtorExpr* o);
  void genTry(TryExpr* t);
  void genOptMember(OptMemberExpr* om);   // campo: `e?.campo`
  void genOptIndex(OptIndexExpr* oi);     // indexação: `e?[i]`
  void genOptCall(CallExpr* c);           // m├⌐todo: `e?.metodo(args)`
  void genCoalesce(CoalesceExpr* c);
  void genCond(Expr* e, const std::string& falseLabel);
  void genCondTrue(Expr* e, const std::string& trueLabel);
  void genGlobal(VarDecl* v);
  std::string internString(const std::string& value);

  void genCallMalloc(long long size);
  void genCallCalloc(long long size);
  void genPolicyAlloc(StoragePolicy pol, long long size); // M10.2
  void emitRuntimeCall(const std::string& name);
  // bounds check de indexa├º├úo: ├¡ndice em %rax; a base do array j├í est├í em
  // (%rsp) (16 bytes reservados). Panic em runtime se fora de [0, size).
  void emitBoundsCheck(long long arraySize);
  // endere├ºo do elemento de list em %rax (d├⌐j├í valida o ├¡ndice; reserva de
  // 16 bytes preserva o ponteiro do list e o ├¡ndice durante as chamadas)
  void genListIndexAddr(IndexExpr* ex);
  // list.Add(x): objeto e valor avaliados, chamada de runtime apropriada
  void genListAdd(CallExpr* c);
  // remove slot das listas de libera├º├úo (objetos/lista que "escapam" no return)
  void releaseEscapeSlot(int slot);
  // aplica a overflow policy do tipo de resultado (em %rax):
  // wrap = trunca para a largura; checked = trunca + panic se fora da faixa;
  // saturate = clamp na faixa; promote/default = nada
  void applyOverflowPolicy(const Type& res);
  void applyFloatOverflowPolicy(const Type& res, int W);

  int classSize(const std::string& name);
  int fieldOffset(const std::string& className, const std::string& field);
  bool isStructType(const Type& t) const;
  // A2 (interface como tipo): tipo Class cujo decl é interface (template ou
  // instância monomorfizada). Representação = ponteiro único (vtable vem do
  // objeto concreto); chamadas sempre despacham via vptr@0.
  bool isInterfaceType(const Type& t) const;
  // M24 10.2: AArch64 native emitter stub (cross-compile check)
  bool isAArch64Target() const { return false; } // true quando --target aarch64*
  std::string emitAArch64() { return "// AArch64 native: via LLVM backend (cross)\n"; }
  void genStructCopy(const Type& t); // c├│pia por valor: rax = ponteiro ΓåÆ novo bloco
  bool isFreshAllocExpr(const Expr* e) const;
};

} // namespace hphl
