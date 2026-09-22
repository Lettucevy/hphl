#include "irgen.h"
#include "types/types.h"
#include "stdlib/stdbuiltins.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <limits>
#include <set>

namespace hphl {

Irgen::Irgen(Semantic& sem, const std::string& filename, bool debug)
    : sem_(sem), filename_(filename), debug_(debug) {}

// ---------------------------------------------------------------------------
// Helper utilities
// ---------------------------------------------------------------------------
std::string Irgen::newLabel(const std::string& base) {
  return base + "." + std::to_string(labelCounter_++);
}

std::string Irgen::newTemp() {
  return "%t" + std::to_string(tempCounter_++);
}

// ---------------------------------------------------------------------------
// Tipo HP-HL → tipo LLVM IR
// ---------------------------------------------------------------------------
std::string Irgen::llvmType(const Type& t) {
  switch (t.kind) {
    case Type::Kind::Void:    return "void";
    case Type::Kind::Int:
    case Type::Kind::UInt: {
      int bits = t.bits;
      if (bits == 0) bits = 64; // default for untyped integer literals
      if (bits <= 32) bits = 64; // x64 backend uses .quad (8-byte) for all int slots
      return "i" + std::to_string(bits);
    }
    case Type::Kind::Float: {
      if (t.bits == 16) return "half";
      if (t.bits == 32) return "float";
      if (t.bits >= 128) return "fp128";
      return "double";
    }
    case Type::Kind::Bool: {
      if (t.bits == 1) return "i1";
      return "i8";
    }
    case Type::Kind::Char: {
      if (t.bits == 16) return "i16";
      if (t.bits == 32) return "i32";
      return "i8";
    }
    case Type::Kind::String:  return "ptr";
    case Type::Kind::Class:   return "ptr";
    case Type::Kind::Enum:    return "i64";
    case Type::Kind::Array:
    case Type::Kind::List:
    case Type::Kind::Map:
    case Type::Kind::Tuple:   return "ptr";
    case Type::Kind::Option:
    case Type::Kind::Result:  return "ptr";
    case Type::Kind::Task:    return "ptr";
    case Type::Kind::Channel: return "ptr";
    case Type::Kind::Func:    return "ptr"; // v0.95: closure box {code, env}
    case Type::Kind::Mutex:
    case Type::Kind::Semaphore:
    case Type::Kind::Event:
    case Type::Kind::Barrier: return "ptr";
    default:                  return "ptr";
  }
}

std::string Irgen::llvmReturnType(const Type& t) {
  return llvmType(t);
}

// Returns the LLVM type string for a pointer to the given type string.
// For opaque pointer mode: "ptr" -> "ptr" (not "ptr*"), "i64" -> "i64*"
static std::string ptrTo(const std::string& ty) {
  return (ty == "ptr") ? "ptr" : (ty + "*");
}

// true se o tipo LLVM é um ponteiro (opaque "ptr" ou legacy "i8*"/"%X*")
static bool isPtrStr(const std::string& ty) {
  return ty == "ptr" || ty.find('*') != std::string::npos;
}
// tamanho em bytes do espaço ocupado por um global (array: bytes totais)
static long long globalSizeBytes(const Type& t) {
  return typeSize(t);
}

// true se o expr indexado é um ARRAY GLOBAL: os globais são blocos
// empacotados ([N x i8]) — o elemento de sub-array é contíguo (endereço),
// diferente dos arrays locais/heap (elementos são ponteiros de 8 bytes)
static bool isGlobalArrayObject(const HirExpr* e) {
  if (!e) return false;
  if (e->kind == HirExprKind::Var) {
    const HirVar* v = static_cast<const HirVar*>(e);
    return v->isGlobal && v->type.kind == Type::Kind::Array;
  }
  if (e->kind == HirExprKind::Member) {
    const HirMember* m = static_cast<const HirMember*>(e);
    return m->isGlobalRef && m->resolvedGlobal &&
           m->resolvedGlobal->type.kind == Type::Kind::Array;
  }
  return false;
}

// stride (em bytes) do elemento num array: sub-array local/heap = ponteiro
// (8), sub-array global = linha empacotada; escalar = size do tipo
static long long arrayElementStride(const HirExpr* object, const Type& elemType) {
  if (elemType.kind == Type::Kind::Array)
    return isGlobalArrayObject(object) ? globalSizeBytes(elemType) : 8;
  return typeSize(elemType);
}

// resolve o nome canônico da classe (ownerClass pode vir como nome simples,
// ex. "Ponto"; o canônico é "main.Ponto")
std::string Irgen::canonClassName(const std::string& ownerClass) {
  const auto& cls = sem_.classes();
  if (cls.count(ownerClass)) return ownerClass;
  for (const auto& [k, ci] : cls) {
    if (ci.decl && ci.decl->name == ownerClass) return k;
  }
  return ownerClass;
}

std::string Irgen::fnLabel(FunctionDecl* fn) {
  if (fnLabels_.count(fn)) return fnLabels_[fn];
  if (fn->isExtern) {
    // FFI: símbolo bruto (ex: @MessageBoxA)
    std::string mangled = "@" + fn->name;
    fnLabels_[fn] = mangled;
    return mangled;
  }
  // overload por arity: aridade declarada no label (ver Codegen::fnLabel)
  std::string name = fn->name + "." + std::to_string(fn->params.size());
  if (fn->isMethod) name = "method." + fn->ownerClass + "." + name;
  if (fn->isEntryPoint) name = "main";
  std::string mangled = "@" + name;
  fnLabels_[fn] = mangled;
  return mangled;
}

bool Irgen::isPointerType(const Type& t) {
  return t.isPointer() ||
         t.kind == Type::Kind::List ||
         t.kind == Type::Kind::Array ||
         t.kind == Type::Kind::Option ||
         t.kind == Type::Kind::Result ||
         t.kind == Type::Kind::Task ||
         t.kind == Type::Kind::Channel ||
         t.kind == Type::Kind::Mutex ||
         t.kind == Type::Kind::Semaphore ||
         t.kind == Type::Kind::Event ||
         t.kind == Type::Kind::Barrier;
}

std::string Irgen::llvmBinOp(BinOp op, const Type& t) {
  bool isFloat = t.kind == Type::Kind::Float;
  bool isUnsigned = t.kind == Type::Kind::UInt;
  switch (op) {
    case BinOp::Add: return isFloat ? "fadd" : "add";
    case BinOp::Sub: return isFloat ? "fsub" : "sub";
    case BinOp::Mul: return isFloat ? "fmul" : "mul";
    case BinOp::Div:
      if (isFloat) return "fdiv";
      return isUnsigned ? "udiv" : "sdiv";
    case BinOp::Mod:
      return isUnsigned ? "urem" : "srem";
    case BinOp::BitAnd: return "and";
    case BinOp::BitOr:  return "or";
    case BinOp::BitXor: return "xor";
    case BinOp::Shl:    return "shl";
    case BinOp::Shr:    return isUnsigned ? "lshr" : "ashr";
    default: return "";
  }
}

// Returns true if a string is an LLVM IR constant literal (not an SSA register)
inline bool isConstantOperand(const std::string& s) {
  // SSA values start with '%'
  if (!s.empty() && s[0] == '%') return false;
  // null pointer
  if (s == "null") return true;
  // endereço de global: constante de ponteiro
  if (!s.empty() && s[0] == '@') return true;
  // numeric constants: "42", "0x1p+0", etc.
  if (!s.empty() && (isdigit(s[0]) || s.find("0x") == 0)) return true;
  // typed constants: "i32 42", "float 0x..."
  if (s.size() >= 2 && s[0] == 'i') return true;
  if (s.find("float") == 0) return true;
  if (s.find("double") == 0) return true;
  if (s.find("true") == 0 || s.find("false") == 0) return true;
  // call sites (e.g. "call i64 @hphl_clock_ns()")
  if (s.find("call") == 0) return true;
  return false;
}

// Format an LLVM IR instruction operand:
// - For instructions like add/sub/icmp: registers use "%reg", constants use "type value"
// - For call args: always include type prefix
inline std::string llvmInstrOperand(const std::string& value, const std::string& type) {
  if (isConstantOperand(value)) return type + " " + value;
  return value;
}

// true se a expressão HIR produz i1 (comparações, !, and/or mistos)
static bool exprProducesI1(const HirExpr* e) {
  if (!e) return false;
  if (e->kind == HirExprKind::Unary)
    return static_cast<const HirUnary*>(e)->op == UnOp::Not;
  if (e->kind == HirExprKind::Binary) {
    auto* b = static_cast<const HirBinary*>(e);
    if (b->op == BinOp::Eq || b->op == BinOp::Ne || b->op == BinOp::Lt ||
        b->op == BinOp::Gt || b->op == BinOp::Le || b->op == BinOp::Ge)
      return true;
    // M11-prep: And/Or SEMPRE produz i8 no irgen (o emissor normaliza os
    // operandos para i8 antes do and/or) — antes retornava true quando
    // algum operando era comparação, gerando zext i1 sobre valor i8
    // (`a || b || c`). Descoberto pelo demo Snake no wasm32.
  }
  return false;
}

// Always include type (for call arguments)
inline std::string llvmCallOperand(const std::string& value, const std::string& type) {
  return type + " " + value;
}

// ---------------------------------------------------------------------------
// Top-level generation
// ---------------------------------------------------------------------------
std::string Irgen::generate() {
  out_ << "; ModuleID = '" << filename_ << "'\n";
  out_ << "source_filename = \"" << filename_ << "\"\n";
  out_ << "target datalayout = \"e-m:w\"\n";
  out_ << "target triple = \"x86_64-pc-windows-msvc\"\n\n";

  // declare external runtime functions we use
  out_ << "; external runtime declarations\n";
  out_ << "declare void @hphl_print_int(i64)\n";
  out_ << "declare void @hphl_print_uint(i64)\n";
  out_ << "declare void @hphl_print_float(double)\n";
  out_ << "declare void @hphl_print_bool(i64)\n";
  out_ << "declare void @hphl_print_char(i64)\n";
  out_ << "declare void @hphl_print_string(ptr)\n";
  out_ << "declare void @hphl_panic(ptr)\n";
  out_ << "declare i64 @hphl_clock_ns()\n";
  // M12.0: declares da biblioteca padrão geradas do registro
  {
    std::set<std::string> declared;
    for (auto& sb : stdBuiltinTable()) {
      if (!declared.insert(sb.symbol).second) continue;  // dedup (char_at/charAt → mesmo símbolo)
      std::string sig = sb.symbol;
      sig += "(";
      for (size_t i = 0; i < sb.params.size(); i++) {
        if (i) sig += ", ";
        switch (sb.params[i]) {
          case SBType::Int: sig += "i64"; break;
          case SBType::Float: sig += "double"; break;
          case SBType::Str: sig += "ptr"; break;
          case SBType::Bool: sig += "i64"; break;
          case SBType::List: sig += "ptr"; break;
          case SBType::ListInt: sig += "ptr"; break;
          case SBType::Ptr: sig += "ptr"; break;
          case SBType::Void: sig += "void"; break;
        }
      }
      sig += ")";
      std::string retTy;
      switch (sb.ret) {
        case SBType::Int: retTy = "i64"; break;
        case SBType::Float: retTy = "double"; break;
        case SBType::Str: retTy = "ptr"; break;
        case SBType::Bool: retTy = "i64"; break;
        case SBType::List: retTy = "ptr"; break;
        case SBType::ListInt: retTy = "ptr"; break;
        case SBType::Ptr: retTy = "ptr"; break;
        case SBType::Void: retTy = "void"; break;
      }
      out_ << "declare " << retTy << " @" << sig << "\n";
    }
  }
  out_ << "declare double @sqrt(double)\n";
  out_ << "declare void @hphl_arena_reset(ptr)\n";
  out_ << "declare noalias ptr @malloc(i64)\n";
  out_ << "declare noalias ptr @calloc(i64, i64)\n";
  out_ << "declare noalias ptr @hphl_channel_new(i64)\n";
  out_ << "declare i64 @hphl_bounds_check(i64, i64)\n";
  out_ << "declare void @hphl_overflow_check(i64, i64, i64)\n";
  // strings
  out_ << "declare i64 @hphl_str_char_index(ptr, i64)\n";
  out_ << "declare i64 @hphl_str_eq(ptr, ptr)\n";
  out_ << "declare i64 @hphl_str_cmp(ptr, ptr)\n";
  out_ << "declare ptr @hphl_str_concat(ptr, ptr)\n";
  out_ << "declare ptr @hphl_str_from_int(i64)\n";
  out_ << "declare ptr @hphl_str_from_uint(i64)\n";
  out_ << "declare ptr @hphl_str_from_float(double)\n";
  out_ << "declare ptr @hphl_str_from_bool(i64)\n";
  out_ << "declare ptr @hphl_str_from_char(i64)\n";
  out_ << "declare void @hphl_str_free(ptr)\n";
  // listas
  out_ << "declare noalias ptr @hphl_list_new()\n";
  out_ << "declare noalias ptr @hphl_list_with_cap(i64)\n";
  out_ << "declare void @hphl_list_add_i(ptr, i64)\n";
  out_ << "declare void @hphl_list_add_f(ptr, double)\n";
  out_ << "declare i64 @hphl_list_len(ptr)\n";
  out_ << "declare void @hphl_list_check(i64, ptr)\n";
  out_ << "declare ptr @hphl_list_data(ptr)\n";
  out_ << "declare ptr @hphl_list_slice(ptr, i64, i64, i64)\n";
  // map (M10.1)
  out_ << "declare noalias ptr @hphl_map_new(i64)\n";
  out_ << "declare void @hphl_map_free(ptr)\n";
  out_ << "declare void @hphl_map_put(ptr, i64, i64)\n";
  out_ << "declare i64 @hphl_map_get(ptr, i64)\n";
  out_ << "declare i64 @hphl_map_contains(ptr, i64)\n";
  out_ << "declare i64 @hphl_map_remove(ptr, i64)\n";
  out_ << "declare void @hphl_map_clear(ptr)\n";
  out_ << "declare i64 @hphl_map_len(ptr)\n";
  // tupla (M10.1b)
  out_ << "declare noalias ptr @hphl_tuple_new(i64)\n";
  out_ << "declare ptr @hphl_tuple_clone(ptr, i64)\n";
  out_ << "declare void @hphl_match_fail()\n";
  // assert / lock (hphl_assert já vem do stdBuiltinTable, não duplicar)
  out_ << "declare void @hphl_lock_begin(ptr)\n";
  out_ << "declare void @hphl_lock_end(ptr)\n";
  // exceções (modelo CONTEXT/RtlRestoreContext — runtime.c)
  if (!wasmEh_) {
    out_ << "declare noalias ptr @hphl_exc_new()\n";
    out_ << "declare void @hphl_exc_free(ptr)\n";
    out_ << "declare void @hphl_exc_push(ptr)\n";
    // captura no frame do caller (try): helper com setjmp do CRT + longjmp
    // posterior quebra no MinGW-w64 e o shadow space do clang corrompe o RSP;
    // hphl_exc_begin captura o CONTEXT e ajusta Rip/Rsp para o call site.
    // returns_twice (como setjmp): o LLVM mantém em MEMÓRIA todo valor vivo
    // através do call — com -O1..-O3, valores em registradores callee-saved
    // seriam corrompidos pelo hphl_throw (que salta sem restaurá-los) →
    // STATUS_HEAP_CORRUPTION no trycatch com -O2.
    out_ << "declare i32 @hphl_exc_begin(ptr) returns_twice\n";
    out_ << "declare void @hphl_exc_end(ptr)\n";
    out_ << "declare ptr @hphl_exc_payload(ptr)\n";
    out_ << "declare void @hphl_throw(ptr)\n";
  } else {
    // M14.3: EH por flag — estado 100% interno ao módulo (sem imports JS):
    // throw grava payload+flag; cada chamada de usuário verifica a flag e
    // desvia por CFG estático (aresta direta para o catch BB / propagação)
    out_ << "@hphl_exc_flag = global i32 0\n";
    out_ << "@hphl_exc_payload = global i64 0\n";
  }
  out_ << "declare noalias ptr @hphl_box_i64(i64)\n";
  out_ << "declare i64 @hphl_unbox_i64(ptr)\n";
  // tarefas / canais / primitivas de sincronização
  out_ << "declare ptr @hphl_spawn_task_ex(void (ptr, ptr)*, ptr)\n";
  out_ << "declare i64 @hphl_wait_task(ptr)\n";
  out_ << "declare void @hphl_cancel_task(ptr)\n";
  out_ << "declare i64 @hphl_task_iscancelled()\n";
  out_ << "declare void @hphl_join_tasks()\n";
  out_ << "declare i64 @hphl_worker_count()\n";
  out_ << "declare void @hphl_region_begin()\n";
  out_ << "declare void @hphl_region_join()\n";
  out_ << "declare void @hphl_channel_send(ptr, i64)\n";
  out_ << "declare i64 @hphl_channel_receive(ptr)\n";
  out_ << "declare noalias ptr @hphl_semaphore_new(i64)\n";
  out_ << "declare noalias ptr @hphl_event_new()\n";
  out_ << "declare noalias ptr @hphl_barrier_new(i64)\n";
  out_ << "declare void @hphl_semaphore_wait(ptr)\n";
  out_ << "declare void @hphl_semaphore_signal(ptr)\n";
  out_ << "declare void @hphl_event_wait(ptr)\n";
  out_ << "declare void @hphl_event_set(ptr)\n";
  out_ << "declare void @hphl_event_reset(ptr)\n";
  out_ << "declare void @hphl_barrier_wait(ptr)\n";
  out_ << "declare void @hphl_tls_setup(i32)\n";
  out_ << "declare ptr @hphl_tls_block()\n";
  out_ << "declare ptr @hphl_struct_copy(ptr, i64)\n\n";
  if (debug_) {
    out_ << "declare void @hphl_dbg_trap(i64)\n";
    out_ << "declare void @hphl_dbg_enter_frame(i64, ptr)\n";
    out_ << "declare void @hphl_dbg_leave()\n\n";
  }

  // threadlocal: offsets dos slots no bloco por thread (8 bytes por global)
  {
    int off = 0;
    for (auto* g : sem_.globals()) {
      if (g->storage == StoragePolicy::ThreadLocal) {
        tlsOffsets_[".Lg_" + g->name] = off;
        off += 8;
      }
    }
    tlsSizeBytes_ = off;
  }

  // global variables (.data)
  for (auto* g : sem_.globals()) {
    if (g->storage == StoragePolicy::ThreadLocal) continue; // slot no bloco TLS
    std::string gname = "@.Lg_" + g->name;
    if (g->type.kind == Type::Kind::Array) {
      long long total = globalSizeBytes(g->type);
      if (g->init && g->init->kind == ExprKind::ArrayLit) {
        std::vector<uint8_t> bytes;
        collectArrayBytes(g->init.get(), g->type, bytes);
        out_ << gname << " = global [" << total << " x i8] [";
        for (size_t bi = 0; bi < bytes.size(); bi++) {
          if (bi > 0) out_ << ", ";
          out_ << "i8 " << (int)bytes[bi];
        }
        out_ << "]\n";
      } else {
        out_ << gname << " = global [" << total << " x i8] zeroinitializer\n";
      }
    } else {
      std::string initVal = "zeroinitializer";
      if (g->init) {
        if (auto* il = dynamic_cast<IntLitExpr*>(g->init.get()))
          initVal = std::to_string(il->value);
        else if (auto* bl = dynamic_cast<BoolLitExpr*>(g->init.get()))
          initVal = bl->value ? "true" : "false";
        else if (auto* fl = dynamic_cast<FloatLitExpr*>(g->init.get()))
          initVal = std::to_string(fl->value);
      }
      out_ << gname << " = global " << llvmType(g->type) << " " << initVal << "\n";
    }
  }

  // classes: tipos opacos (sempre usadas como ponteiro)
  for (const auto& [canon, ci] : sem_.classes()) {
    (void)ci;
    out_ << "%class." << canon << " = type opaque\n";
  }

  const HirProgram& hir = sem_.hir();

  // FFI nativo: `declare` para cada extern (sem corpo no HIR)
  {
    std::set<std::string> declared;
    for (const auto& hf : hir.functions) {
      if (!hf.decl || !hf.decl->isExtern) continue;
      if (!declared.insert(hf.decl->name).second) continue;
      Type rt = hf.decl->hasReturnType ? hf.decl->returnType : Type::makeVoid();
      std::string sig = "@" + hf.decl->name + "(";
      for (size_t i = 0; i < hf.params.size(); i++) {
        if (i) sig += ", ";
        sig += llvmType(hf.params[i].second);
      }
      sig += ")";
      out_ << "declare " << llvmReturnType(rt) << " " << sig << "\n";
    }
  }

  // define all functions
  for (const auto& hf : hir.functions) {
    if (!hf.body) continue;
    // modelos genéricos: só as instâncias monomorfizadas são emitidas
    // (métodos de classe-template também — o dono é o canônico do template)
    if (hf.decl && !hf.decl->typeParams.empty()) continue;
    if (hf.decl && hf.decl->isMethod && !hf.decl->ownerClass.empty()) {
      auto cit = sem_.classes().find(hf.decl->ownerClass);
      if (cit != sem_.classes().end() && cit->second.isTemplate) continue;
    }
    genFunction(hf);
  }

  // tarefas sintéticas adiadas (spawn/parallel) — depois das funções
  // regulares; podem criar outras tarefas (paralelismo aninhado)
  genPendingTasks();
  // v0.95 (lambdas): funções sintéticas `__lambda.N` (podem aninhar outras)
  genPendingLambdas();

  // string literal constants (collected during function gen)
  if (stringLiterals_.size()) {
    out_ << "\n; string literals\n";
    for (auto& sl : stringLiterals_) {
      out_ << sl.second << "\n";
    }
  }

  // F2.4 debug: meta do debugger no final do módulo. O stub SEM debug é
  // necessário porque o runtime.c sempre referencia @hphl_dbg_meta (extern);
  // o codegen x64 emite o mesmo stub (.quad 0, 0) sem --debug.
  if (debug_) emitDebugMeta();
  else out_ << "@hphl_dbg_meta = global [2 x i64] zeroinitializer\n";

  // M10 (v0.44): vtables por classe — slot i = implementação mais derivada
  if (!sem_.vslots().empty()) {
    out_ << "\n; vtables (dispatch virtual)\n";
    for (auto& [canon, ci] : sem_.classes()) {
      if (!ci.hasVptr || ci.isTemplate || ci.decl->isInterface) continue;
      int nSlots = (int)sem_.vslots().size();
      out_ << "@\"Lvt_" << canon << "\" = global [" << nSlots
           << " x ptr] [";
      for (int s = 0; s < nSlots; s++) {
        if (s) out_ << ", ";
        FunctionDecl* impl = sem_.findOverrideIn(canon, s);
        out_ << "ptr " << (impl ? fnLabel(impl) : std::string("null"));
      }
      out_ << "]\n";
    }
  }

  return out_.str();
}

// ---------------------------------------------------------------------------
// Function generation
// ---------------------------------------------------------------------------
void Irgen::genFunction(const HirFunction& hf) {
  std::string retType = llvmReturnType(hf.returnType);
  // M15.1: entry point vira `i32 @main` — `define void @main` deixava o CRT
  // lendo eax lixo como exit code (hello/arrays com rc=1 no backend llvm)
  if (hf.decl && hf.decl->isEntryPoint) retType = "i32";
  curFnRetType_ = hf.returnType;
  curFnDecl_ = hf.decl; // M10: para litParams (genéricos com valor)
  std::string fname = fnLabel(hf.decl);
  if (definedFns_.count(fname)) return;   // dedup: já emitimos esta função
  definedFns_.insert(fname);
  returned_ = false;

  // slots são por função (nomes .lN não podem vazar entre funções)
  localSlots_.clear();
  slotStack_.clear();
  localCounter_ = 0;
  refSlotNames_.clear();
  refSlotTypes_.clear();
  wasmCatchStack_.clear();
  wasmPropLabel_.clear();
  prologue_.str("");
  prologue_.clear();

  // track parameter names (they use SSA values, not allocas)
  paramNames_.clear();
  // métodos de instância: `this` é o primeiro parâmetro ABI (espelha o x64,
  // que recebe o objeto em %rcx)
  bool isMethod = hf.isMethod && hf.decl && !hf.decl->isStatic;
  if (isMethod) paramNames_.insert("this");
  std::vector<std::pair<std::string, int>> refParams;
  for (size_t i = 0; i < hf.params.size(); i++) {
    bool isRef = hf.decl && i < hf.decl->params.size() &&
                 hf.decl->params[i] &&
                 (hf.decl->params[i]->byRef || hf.decl->params[i]->byOut);
    if (isRef) {
      refParams.emplace_back(hf.params[i].first, i);
    } else {
      paramNames_.insert(hf.params[i].first);
    }
  }

  // F2.4 debug: registra this/params como locais (espelhos no bloco de debug)
  if (debug_) {
    dbgSlots_.clear();
    dbgSlotIdx_.clear();
    dbgSlotCount_ = 0;
    dbgFrame_.clear();
    lastTrapLine_ = 0;
    DbgFnMeta m;
    m.name = hf.decl ? hf.decl->name : hf.name;
    m.file = (hf.decl && !hf.decl->filePath.empty()) ? hf.decl->filePath
                                                     : filename_;
    dbgFnsMeta_.push_back(std::move(m));
    dbgFnId_ = dbgFnCounter_++;
    if (isMethod) dbgReserveSlot("this", Type::makeClass(hf.ownerClass), false, true);
    for (size_t i = 0; i < hf.params.size(); i++) {
      bool isRef = false;
      for (auto& rp : refParams) { if (rp.second == (int)i) { isRef = true; break; } }
      dbgReserveSlot(hf.params[i].first, hf.params[i].second, isRef, true);
    }
  }

  std::string params;
  if (isMethod) params = llvmType(Type::makeClass(canonClassName(hf.ownerClass))) + " %.this";
  for (size_t i = 0; i < hf.params.size(); i++) {
    if (!params.empty()) params += ", ";
    bool isRef = false;
    for (auto& rp : refParams) { if (rp.second == (int)i) { isRef = true; break; } }
    std::string pType = isRef ? ptrTo(llvmType(hf.params[i].second)) : llvmType(hf.params[i].second);
    params += pType + " %." + hf.params[i].first;
  }

  out_ << "\n; Function: " << hf.name << "\n";
  out_ << "define " << retType << " " << fname << "(" << params << ") {\n";

  // o corpo é gerado num buffer separado: as allocas das variáveis locais só
  // são conhecidas DURANTE a geração do corpo (genHirVarDecl dentro de loops
  // e blocos), mas precisam sair no bloco de entrada — a montagem final é
  // [define][entry+spills+tls][ALLOCAS hoisted][corpo]
  std::ostringstream bodyBuf;
  out_.swap(bodyBuf);
  std::ostringstream headBuf;
  std::string entryLabel = newLabel("entry");
  headBuf << entryLabel << ":\n";

  for (auto& rp : refParams) {
    const std::string& pName = rp.first;
    Type slotT = hf.params[rp.second].second;
    std::string slot = "%.ls" + std::to_string(rp.second) + "." + pName;
    headBuf << "  " << slot << " = alloca ptr, align 8\n";
    headBuf << "  store ptr %." << pName << ", ptr " << slot << ", align 8\n";
    slotStack_[pName].push_back({slot, slotT});
  }

  // parâmetros NÃO-ref: slot de spill criado no bloco de entrada e registrado
  // no slotStack_ — TODAS as leituras passam pelo slot (escrever o parâmetro
  // dentro de um loop invalidaria leituras SSA emitidas antes; o mem2reg do
  // -O2 promove de volta para SSA). `this` (métodos) continua como valor SSA.
  for (size_t i = 0; i < hf.params.size(); i++) {
    bool isRef = false;
    for (auto& rp : refParams) { if (rp.second == (int)i) { isRef = true; break; } }
    if (isRef) continue;
    const std::string& pName = hf.params[i].first;
    Type pT = hf.params[i].second;
    std::string slot = "%.lp" + std::to_string(i) + "." + pName;
    std::string llvmTy = llvmType(pT);
    headBuf << "  " << slot << " = alloca " << llvmTy << ", align 8\n";
    headBuf << "  store " << llvmTy << " %." << pName << ", " << ptrTo(llvmTy) << " "
         << slot << ", align 8\n";
    slotStack_[pName].push_back({slot, pT});
  }

  if (hf.body) {
    // entrada: registra o tamanho do bloco threadlocal (hphl_tls_setup)
    if (hf.decl->isEntryPoint && tlsSizeBytes_ > 0)
      headBuf << "  call void @hphl_tls_setup(i32 " << tlsSizeBytes_ << ")\n";
    genBlock(hf.body.get());
  }

  // programa com spawn/parallel: antes de sair de Main, aguarda as threads
  // (mesmo desenho do codegen x64: join global drena a fila inteira)
  if (hf.decl->isEntryPoint && spawnedAny_)
    out_ << "  call void @hphl_join_tasks()\n";

  // implicit return: emite sempre que o bloco corrente ainda não tem
  // terminator (fim natural da função OU join de if/while cujos ramos
  // fizeram return). Antes usava a flag returned_, que suprimia o ret
  // mesmo com o bloco de join aberto → IR inválido no LLVM 22.
  if (!curBlockTerminated()) {
    if (debug_) out_ << "  call void @hphl_dbg_leave()\n";
    if (retType == "void") {
      out_ << "  ret void\n";
    } else if (retType == "ptr") {
      out_ << "  ret " << retType << " null\n";
    } else {
      out_ << "  ret " << retType << " 0\n";
    }
  }

  // M14.3: bloco de propagação do EH por flag (wasm) — desvios de chamadas
  // sem try envolvente pousam aqui; a flag continua acesa para os frames
  // superiores decidirem (catch ou nova propagação)
  if (!wasmPropLabel_.empty()) {
    out_ << wasmPropLabel_ << ":\n";
    if (debug_) out_ << "  call void @hphl_dbg_leave()\n";
    if (retType == "void") {
      out_ << "  ret void\n";
    } else if (retType == "ptr") {
      out_ << "  ret ptr null\n";
    } else {
      out_ << "  ret " << retType << " 0\n";
    }
  }

  out_ << "}\n";

  // F2.4 debug: bloco de espelho [N x i64] + GEPs + espelhos de this/params +
  // hphl_dbg_enter_frame — tudo no prólogo (montado antes do corpo)
  if (debug_) {
    dbgFrame_ = "%.dbgframe" + std::to_string(dbgFnId_);
    prologue_ << "  " << dbgFrame_ << " = alloca [" << dbgSlotCount_
              << " x i64], align 8\n";
    for (const auto& s : dbgSlots_) {
      prologue_ << "  %.dbg" << s.index << "." << s.name
                << " = getelementptr inbounds [" << dbgSlotCount_ << " x i64], ptr "
                << dbgFrame_ << ", i64 0, i64 " << s.index << "\n";
    }
    if (isMethod) {
      std::string v = newTemp();
      prologue_ << "  " << v << " = ptrtoint ptr %.this to i64\n";
      prologue_ << "  store i64 " << v << ", ptr %.dbg" << dbgSlotIdx_["this"]
                << ".this, align 8\n";
    }
    for (auto& rp : refParams) {
      const std::string& pName = rp.first;
      prologue_ << "  store ptr %." << pName << ", ptr %.dbg" << dbgSlotIdx_[pName]
                << "." << pName << ", align 8\n";
    }
    for (size_t i = 0; i < hf.params.size(); i++) {
      bool isRef = false;
      for (auto& rp : refParams) { if (rp.second == (int)i) { isRef = true; break; } }
      if (isRef) continue;
      const std::string& pName = hf.params[i].first;
      Type pT = hf.params[i].second;
      std::string llvmTy = llvmType(pT);
      std::string v;
      if (llvmTy == "i64") {
        v = "%." + pName;
      } else if (llvmTy == "ptr") {
        v = newTemp();
        prologue_ << "  " << v << " = ptrtoint ptr %." << pName << " to i64\n";
      } else if (pT.kind == Type::Kind::UInt) {
        v = newTemp();
        prologue_ << "  " << v << " = zext " << llvmTy << " %." << pName << " to i64\n";
      } else if (pT.kind == Type::Kind::Int || pT.kind == Type::Kind::Bool ||
                 pT.kind == Type::Kind::Char) {
        v = newTemp();
        prologue_ << "  " << v << " = sext " << llvmTy << " %." << pName << " to i64\n";
      } else {
        v = newTemp();
        prologue_ << "  " << v << " = bitcast " << llvmTy << " %." << pName << " to i64\n";
      }
      prologue_ << "  store i64 " << v << ", ptr %.dbg" << dbgSlotIdx_[pName]
                << "." << pName << ", align 8\n";
    }
    prologue_ << "  call void @hphl_dbg_enter_frame(i64 " << dbgFnId_
              << ", ptr " << dbgFrame_ << ")\n";
    // meta: locais da função (offset negativo: rbp - offset = bloco + 8*idx)
    DbgFnMeta& fm = dbgFnsMeta_.back();
    for (const auto& s : dbgSlots_) {
      DbgFnMeta::DbgLocMeta lm;
      lm.name = s.name;
      lm.type = dbgTypeName(s.type);
      lm.offset = -(8 * (long long)s.index);
      lm.flags = (s.byRef ? 1 : 0) | (s.isArg ? 2 : 0);
      fm.locs.push_back(std::move(lm));
    }
  }

  // monta o módulo: [define][entry+spills+tls][ALLOCAS hoisted][corpo] — as
  // allocas das variáveis locais só existem após a geração do corpo
  std::string finalIr = bodyBuf.str() + headBuf.str() + prologue_.str() + out_.str();
  out_.str("");
  out_ << finalIr;
}

// ---------------------------------------------------------------------------
// HIR statement generation
// ---------------------------------------------------------------------------
void Irgen::genBlock(const HirStmt* s) {
  if (!s) return;
  switch (s->kind) {
    case HirStmtKind::Block: {
      localSlots_.push_back({});
      for (auto& st : static_cast<const HirBlock*>(s)->stmts)
        genHirStmt(st.get());
      // desregistra slots deste escopo no slotStack_ APENAS se o nome também
      // existia num escopo externo (restaura o shadowing). Registros originais
      // ficam: o lowering usa temps de blocos aninhados DEPOIS do bloco
      // (ex. `return __h3` após o bloco do match)
      for (auto& [name, slot] : localSlots_.back()) {
        auto it = slotStack_.find(name);
        if (it != slotStack_.end() && it->second.size() > 1 && it->second.back().first == slot)
          it->second.pop_back();
      }
      localSlots_.pop_back();
      break;
    }
    default:
      genHirStmt(s);
      break;
  }
}

void Irgen::genHirStmt(const HirStmt* s) {
  if (!s) return;
  // F2.4 debug: trap por linha executável (0 = sintético; dedup por linha)
  dbgEmitTrap(s->line);
  switch (s->kind) {
    case HirStmtKind::Block:
      genBlock(s);
      break;
    case HirStmtKind::VarDecl:         genHirVarDecl(static_cast<const HirVarDecl*>(s)); break;
    case HirStmtKind::ExprStmt:        genHirExprStmt(static_cast<const HirExprStmt*>(s)); break;
    case HirStmtKind::Return:         genHirReturn(static_cast<const HirReturn*>(s)); break;
    case HirStmtKind::If:             genHirIf(static_cast<const HirIf*>(s)); break;
    case HirStmtKind::While:          genHirWhile(static_cast<const HirWhile*>(s)); break;
    case HirStmtKind::DoWhile:        genHirDoWhile(static_cast<const HirDoWhile*>(s)); break;
    case HirStmtKind::For:            genHirFor(static_cast<const HirFor*>(s)); break;
    case HirStmtKind::Break: {
      // M11-prep: bloco já terminado (ex.: `return` antes) não emite br morto
      if (!loopStack_.empty()) {
        if (!curBlockTerminated())
          out_ << "  br label %" << loopStack_.back().breakLabel << "\n";
      } else
        out_ << "  unreachable ; break fora de loop\n";
      break;
    }
    case HirStmtKind::Continue: {
      if (!loopStack_.empty()) {
        if (!curBlockTerminated())
          out_ << "  br label %" << loopStack_.back().continueLabel << "\n";
      } else
        out_ << "  unreachable ; continue fora de loop\n";
      break;
    }
    case HirStmtKind::Panic:          genHirPanic(static_cast<const HirPanic*>(s)); break;
    case HirStmtKind::Assert:        genHirAssert(static_cast<const HirAssert*>(s)); break;
    case HirStmtKind::Throw:         genHirThrow(static_cast<const HirThrow*>(s)); break;
    case HirStmtKind::Try:           genHirTry(static_cast<const HirTry*>(s)); break;
    case HirStmtKind::Lock:          genHirLock(static_cast<const HirLock*>(s)); break;
    case HirStmtKind::Spawn:         genHirSpawnStmt(static_cast<const HirSpawn*>(s)); break;
    case HirStmtKind::Parallel:      genHirParallel(static_cast<const HirParallel*>(s)); break;
    case HirStmtKind::ParallelForeach:
      genHirParallelForeach(static_cast<const HirParallelForeach*>(s)); break;
    default:
      // M20-D 5.1: stmt kind não suportado - emite mensagem mas não pânico
      out_ << "  ; stmt kind " << static_cast<int>(s->kind)
           << " não implementado no backend IR (M20 deferred)\n";
      // continua (sem pânico) para permitir múltiplos stmts
      break;
  }
}

std::string Irgen::findLocalSlot(const std::string& name) {
  auto it = slotStack_.find(name);
  if (it != slotStack_.end() && !it->second.empty()) return it->second.back().first;
  return "%." + name;
}

std::string Irgen::ensureLocalSlot(const std::string& name, const Type& type) {
  (void)type;
  auto it = slotStack_.find(name);
  if (it != slotStack_.end() && !it->second.empty()) return it->second.back().first;
  return "%." + name;
}

std::string Irgen::declareLocalSlot(const std::string& name, const Type& type) {
  if (localSlots_.empty()) localSlots_.push_back({});
  std::string slot = "%.l" + std::to_string(localCounter_++) + "." + name;
  localSlots_.back()[name] = slot;
  slotStack_[name].push_back({slot, type});
  return slot;
}

// declara o slot e emite o `alloca` no PRÓLOGO (bloco de entrada da função):
// allocas no ponto de declaração dentro de loops crescem a stack por iteração
// no codegen -O0 (chkstk + sub rsp por iteração → stack overflow); o mem2reg
// do -O2 promove de volta a SSA de qualquer forma
std::string Irgen::emitAllocaSlot(const std::string& name, const Type& type) {
  std::string slot = declareLocalSlot(name, type);
  // F2.4 debug: locais de usuário entram no bloco de espelho
  if (debug_ && isUserSlotName(name)) dbgReserveSlot(name, type, false, false);
  prologue_ << "  " << slot << " = alloca " << llvmType(type) << ", align 8\n";
  return slot;
}

// ---------------------------------------------------------------------------
// F2.4: debug IR/LLVM — espelho dos locais de usuário num bloco [N x i64]
// ---------------------------------------------------------------------------
bool Irgen::isUserSlotName(const std::string& n) {
  return !(n.size() >= 2 && n[0] == '_' && n[1] == '_');
}

int Irgen::dbgReserveSlot(const std::string& name, const Type& type, bool byRef,
                          bool isArg) {
  if (!debug_) return -1;
  auto it = dbgSlotIdx_.find(name);
  if (it != dbgSlotIdx_.end()) return it->second;
  int idx = dbgSlotCount_++;
  DbgSlot s;
  s.name = name;
  s.type = type;
  s.byRef = byRef;
  s.isArg = isArg;
  s.index = idx;
  dbgSlots_.push_back(std::move(s));
  dbgSlotIdx_[name] = idx;
  return idx;
}

std::string Irgen::dbgFieldPtr(const std::string& name) const {
  if (!debug_) return "";
  auto it = dbgSlotIdx_.find(name);
  if (it == dbgSlotIdx_.end()) return "";
  return "%.dbg" + std::to_string(it->second) + "." + name;
}

void Irgen::dbgMirrorStore(const std::string& name, const std::string& val,
                           const Type& t) {
  if (!debug_) return;
  std::string field = dbgFieldPtr(name);
  if (field.empty()) return;
  std::string v64;
  if (llvmType(t) == "ptr") {
    v64 = newTemp();
    out_ << "  " << v64 << " = ptrtoint ptr " << val << " to i64\n";
  } else {
    v64 = extendToI64(val, t);
  }
  out_ << "  store i64 " << v64 << ", ptr " << field << ", align 8\n";
}

void Irgen::dbgEmitTrap(int line) {
  if (!debug_ || line <= 0 || line == lastTrapLine_) return;
  lastTrapLine_ = line;
  out_ << "  call void @hphl_dbg_trap(i64 " << line << ")\n";
  if (!dbgFnsMeta_.empty()) dbgFnsMeta_.back().traps.push_back(line);
}

std::string Irgen::dbgTypeName(const Type& t) const {
  switch (t.kind) {
    case Type::Kind::Int: return "int";
    case Type::Kind::UInt: return "uint";
    case Type::Kind::Bool: return "bool";
    case Type::Kind::Float: return "float";
    case Type::Kind::Char: return "char";
    case Type::Kind::String: return "string";
    case Type::Kind::List:
      return t.elem ? "list:" + dbgTypeName(*t.elem) : std::string("list");
    case Type::Kind::Class: return "class:" + t.name;
    case Type::Kind::Enum: return "enum:" + t.name;
    case Type::Kind::Array: {
      const Type* p = &t;
      std::string dims;
      while (p && p->kind == Type::Kind::Array) {
        dims += "[" + std::to_string(p->arraySize) + "]";
        p = p->elem.get();
      }
      return (p ? dbgTypeName(*p) : std::string("ptr")) + dims;
    }
    default: return "ptr";
  }
}

// escapa uma string para o formato c"..." do LLVM IR
static std::string irEscape(const std::string& s) {
  std::string out;
  char buf[8];
  for (unsigned char c : s) {
    if (c == '\\' || c == '"') {
      out += '\\';
      out += (char)c;
    } else if (c < 0x20 || c > 0x7E) {
      snprintf(buf, sizeof buf, "\\%02X", (int)c);
      out += buf;
    } else {
      out += (char)c;
    }
  }
  return out;
}

// F2.4 debug: emite @hphl_dbg_meta + tabelas em IR (layout idêntico ao x64:
// fn = {name,file,0,nLocals,locs,nTraps,traps}; loc = {name,type,offset,flags};
// gbl = {name,type,tls,addr,isArray}; cls = {name,nFields,fields};
// fld = {name,type,offset})
void Irgen::emitDebugMeta() {
  int strId = 0;
  auto newStr = [&](const std::string& s) {
    std::string g = "@hphl.dbg.s" + std::to_string(strId++);
    out_ << g << " = private unnamed_addr constant [" << (s.size() + 1)
         << " x i8] c\"" << irEscape(s) << "\\00\"\n";
    return g;
  };
  auto dummy = [&]() {
    std::string g = "@hphl.dbg.s" + std::to_string(strId++);
    out_ << g << " = private unnamed_addr constant [1 x i8] c\"\\00\"\n";
    return g;
  };

  size_t fnCount = dbgFnsMeta_.size();
  std::vector<std::string> fnNames(fnCount), fnFiles(fnCount);
  for (size_t i = 0; i < fnCount; i++) {
    fnNames[i] = newStr(dbgFnsMeta_[i].name);
    fnFiles[i] = newStr(dbgFnsMeta_[i].file);
  }
  size_t gblCount = sem_.globals().size();
  std::vector<std::string> gblName(gblCount), gblType(gblCount), gblAddr(gblCount);
  std::vector<long long> gblTls(gblCount), gblArray(gblCount);
  for (size_t i = 0; i < gblCount; i++) {
    const auto* g = sem_.globals()[i];
    bool tls = g->storage == StoragePolicy::ThreadLocal;
    gblName[i] = newStr(g->name);
    gblType[i] = newStr(dbgTypeName(g->type));
    gblTls[i] = tls ? (tlsOffsets_.count(".Lg_" + g->name) ? tlsOffsets_[".Lg_" + g->name] : 0) : -1;
    gblAddr[i] = tls ? dummy() : "@.Lg_" + g->name;
    gblArray[i] = g->type.kind == Type::Kind::Array ? 1 : 0;
  }
  size_t clsCount = sem_.classes().size();
  std::vector<std::string> clsName(clsCount);
  std::vector<std::vector<std::string>> clsFldName(clsCount), clsFldType(clsCount);
  std::vector<std::vector<long long>> clsFldOff(clsCount);
  size_t ci = 0;
  for (const auto& [canon, cinfo] : sem_.classes()) {
    clsName[ci] = newStr(canon);
    for (const auto& [fname, fmeta] : cinfo.fields) {
      clsFldName[ci].push_back(newStr(fname));
      clsFldType[ci].push_back(newStr(dbgTypeName(fmeta.first)));
      clsFldOff[ci].push_back(fmeta.second);
    }
    ci++;
  }

  out_ << "\n; F2.4: meta do debugger\n";
  // funções
  out_ << "@hphl.dbg.fns = global [" << fnCount << " x i64] [";
  for (size_t i = 0; i < fnCount; i++) {
    if (i) out_ << ", ";
    out_ << "i64 ptrtoint (ptr @hphl.dbg.fn" << i << " to i64)";
  }
  out_ << "]\n";
  for (size_t i = 0; i < fnCount; i++) {
    const auto& m = dbgFnsMeta_[i];
    size_t L = m.locs.size(), T = m.traps.size();
    std::vector<std::string> locName(L), locType(L);
    for (size_t k = 0; k < L; k++) {
      locName[k] = newStr(m.locs[k].name);
      locType[k] = newStr(m.locs[k].type);
    }
    std::string locsG = "@hphl.dbg.fn" + std::to_string(i) + ".locs";
    if (L == 0) {
      out_ << locsG << " = global [1 x i64] [i64 ptrtoint (ptr " << dummy()
           << " to i64)]\n";
    } else {
      out_ << locsG << " = global [" << L << " x i64] [";
      for (size_t k = 0; k < L; k++) {
        if (k) out_ << ", ";
        out_ << "i64 ptrtoint (ptr @hphl.dbg.fn" << i << ".loc" << k << " to i64)";
      }
      out_ << "]\n";
    }
    for (size_t k = 0; k < L; k++) {
      out_ << "@hphl.dbg.fn" << i << ".loc" << k << " = global [4 x i64] [i64 ptrtoint (ptr "
           << locName[k] << " to i64), i64 ptrtoint (ptr " << locType[k]
           << " to i64), i64 " << m.locs[k].offset << ", i64 " << m.locs[k].flags << "]\n";
    }
    std::string trapsG = "@hphl.dbg.fn" + std::to_string(i) + ".traps";
    if (T == 0) {
      out_ << trapsG << " = global [1 x i64] [i64 0]\n";
    } else {
      out_ << trapsG << " = global [" << T << " x i64] [";
      for (size_t k = 0; k < T; k++) {
        if (k) out_ << ", ";
        out_ << "i64 " << m.traps[k];
      }
      out_ << "]\n";
    }
    out_ << "@hphl.dbg.fn" << i << " = global [7 x i64] [i64 ptrtoint (ptr "
         << fnNames[i] << " to i64), i64 ptrtoint (ptr " << fnFiles[i]
         << " to i64), i64 0, i64 " << L << ", i64 ptrtoint (ptr " << locsG
         << " to i64), i64 " << T << ", i64 ptrtoint (ptr " << trapsG
         << " to i64)]\n";
  }
  // globais
  out_ << "@hphl.dbg.globs = global [" << gblCount << " x i64] [";
  for (size_t i = 0; i < gblCount; i++) {
    if (i) out_ << ", ";
    out_ << "i64 ptrtoint (ptr @hphl.dbg.gbl" << i << " to i64)";
  }
  out_ << "]\n";
  for (size_t i = 0; i < gblCount; i++) {
    out_ << "@hphl.dbg.gbl" << i << " = global [5 x i64] [i64 ptrtoint (ptr "
         << gblName[i] << " to i64), i64 ptrtoint (ptr " << gblType[i]
         << " to i64), i64 " << gblTls[i] << ", i64 ptrtoint (ptr " << gblAddr[i]
         << " to i64), i64 " << gblArray[i] << "]\n";
  }
  // classes
  out_ << "@hphl.dbg.clss = global [" << clsCount << " x i64] [";
  for (size_t i = 0; i < clsCount; i++) {
    if (i) out_ << ", ";
    out_ << "i64 ptrtoint (ptr @hphl.dbg.cls" << i << " to i64)";
  }
  out_ << "]\n";
  for (size_t i = 0; i < clsCount; i++) {
    size_t F = clsFldName[i].size();
    std::string fldsG = "@hphl.dbg.cls" + std::to_string(i) + ".flds";
    if (F == 0) {
      out_ << fldsG << " = global [1 x i64] [i64 0]\n";
    } else {
      out_ << fldsG << " = global [" << F << " x i64] [";
      for (size_t k = 0; k < F; k++) {
        if (k) out_ << ", ";
        out_ << "i64 ptrtoint (ptr @hphl.dbg.cls" << i << ".fld" << k << " to i64)";
      }
      out_ << "]\n";
    }
    for (size_t k = 0; k < F; k++) {
      out_ << "@hphl.dbg.cls" << i << ".fld" << k << " = global [3 x i64] [i64 ptrtoint (ptr "
           << clsFldName[i][k] << " to i64), i64 ptrtoint (ptr " << clsFldType[i][k]
           << " to i64), i64 " << clsFldOff[i][k] << "]\n";
    }
    out_ << "@hphl.dbg.cls" << i << " = global [3 x i64] [i64 ptrtoint (ptr "
         << clsName[i] << " to i64), i64 " << F << ", i64 ptrtoint (ptr " << fldsG
         << " to i64)]\n";
  }
  // cabeçalho
  out_ << "@hphl_dbg_meta = global [6 x i64] [i64 " << fnCount
       << ", i64 ptrtoint (ptr @hphl.dbg.fns to i64), i64 " << gblCount
       << ", i64 ptrtoint (ptr @hphl.dbg.globs to i64), i64 " << clsCount
       << ", i64 ptrtoint (ptr @hphl.dbg.clss to i64)]\n";
}

// endereço do slot threadlocal no bloco da thread corrente
// (hphl_tls_block + offset; espelha o modelo do codegen x64)
std::string Irgen::tlsAddr(const std::string& label) {
  auto it = tlsOffsets_.find(label);
  int off = (it != tlsOffsets_.end()) ? it->second : 0;
  std::string block = newTemp();
  out_ << "  " << block << " = call ptr @hphl_tls_block()\n";
  std::string addr = newTemp();
  out_ << "  " << addr << " = getelementptr inbounds i8, ptr " << block
       << ", i64 " << off << "\n";
  return addr;
}

Type Irgen::actualTypeOf(const HirExpr* e) {
  if (e && e->kind == HirExprKind::Var) {
    auto it = slotStack_.find(static_cast<const HirVar*>(e)->name);
    if (it != slotStack_.end() && !it->second.empty()) return it->second.back().second;
  }
  return e ? e->type : Type::makeInt(64);
}

void Irgen::genHirVarDecl(const HirVarDecl* vd) {
  if (!vd) return;
  std::string allocaName = emitAllocaSlot(vd->name, vd->type);
  std::string llvmTy = llvmType(vd->type);
  if (vd->type.kind == Type::Kind::List) {
    // list: init com valor avalia e guarda; sem init cria vazia (ver x64)
    if (vd->init) {
      std::string val = toI8Store(genHirExpr(vd->init.get()), vd->init.get(), vd->type);
      std::string converted = convertForStore(val, actualTypeOf(vd->init.get()), llvmTy);
      out_ << "  store " << llvmTy << " " << converted << ", ptr " << allocaName << ", align 8\n";
      dbgMirrorStore(vd->name, converted, vd->type);
    } else {
      // list: objeto criado na declaração (o epílogo só libera criados)
      std::string nt = newTemp();
      out_ << "  " << nt << " = call noalias ptr @hphl_list_new()\n";
      out_ << "  store ptr " << nt << ", ptr " << allocaName << ", align 8\n";
      dbgMirrorStore(vd->name, nt, vd->type);
    }
  }
  if (vd->type.kind == Type::Kind::Map) {
    int tag = 0;
    if (vd->type.elem && vd->type.elem->kind == Type::Kind::String) tag = 1;
    else if (vd->type.elem && vd->type.elem->kind == Type::Kind::Bool) tag = 2;
    else if (vd->type.elem && vd->type.elem->kind == Type::Kind::Char) tag = 3;
    std::string nt = newTemp();
    out_ << "  " << nt << " = call noalias ptr @hphl_map_new(i64 " << tag << ")\n";
    out_ << "  store ptr " << nt << ", ptr " << allocaName << ", align 8\n";
    dbgMirrorStore(vd->name, nt, vd->type);
  }
  if (vd->type.kind == Type::Kind::Tuple) {
    // M10.1b: handle de bloco heap; literal já aloca, origem não-fresh clona
    if (vd->init && vd->init->kind == HirExprKind::TupleLit) {
      std::string v = genHirExpr(vd->init.get());
      out_ << "  store ptr " << v << ", ptr " << allocaName << ", align 8\n";
      dbgMirrorStore(vd->name, v, vd->type);
    } else {
      int n = (int)vd->type.tupleElems.size();
      std::string nt = newTemp();
      out_ << "  " << nt << " = call noalias ptr @hphl_tuple_new(i64 " << n << ")\n";
      if (vd->init) {
        std::string src = genHirExpr(vd->init.get());
        std::string cl = newTemp();
        out_ << "  " << cl << " = call ptr @hphl_tuple_clone("
             << llvmCallOperand(src, "ptr") << ", i64 " << n << ")\n";
        out_ << "  store ptr " << cl << ", ptr " << allocaName << ", align 8\n";
      } else {
        out_ << "  store ptr " << nt << ", ptr " << allocaName << ", align 8\n";
      }
      dbgMirrorStore(vd->name, nt, vd->type);
    }
  }
  if (vd->type.kind == Type::Kind::Mutex || vd->type.kind == Type::Kind::Semaphore ||
      vd->type.kind == Type::Kind::Event || vd->type.kind == Type::Kind::Barrier) {
    // primitivas de sincronização: o handle nasce na declaração
    // (semaphore/barrier recebem o contador; mutex/event não)
    std::string h;
    if (vd->type.kind == Type::Kind::Semaphore) {
      h = newTemp();
      out_ << "  " << h << " = call noalias ptr @hphl_semaphore_new(i64 " << vd->primitiveInit << ")\n";
    } else if (vd->type.kind == Type::Kind::Barrier) {
      h = newTemp();
      out_ << "  " << h << " = call noalias ptr @hphl_barrier_new(i64 " << vd->primitiveInit << ")\n";
    } else if (vd->type.kind == Type::Kind::Mutex) {
      h = newTemp();
      out_ << "  " << h << " = call noalias ptr @hphl_mutex_new()\n";
    } else {
      h = newTemp();
      out_ << "  " << h << " = call noalias ptr @hphl_event_new()\n";
    }
    out_ << "  store ptr " << h << ", ptr " << allocaName << ", align 8\n";
    dbgMirrorStore(vd->name, h, vd->type);
  }
  if (vd->type.kind == Type::Kind::Class && !vd->init) {
    // A2 (interface como tipo): sem init nasce null (sem ctor/default;
    // `@Lvt_<iface>` sequer existe)
    if (isInterfaceType(vd->type)) {
      out_ << "  store ptr null, ptr " << allocaName << ", align 8\n";
      dbgMirrorStore(vd->name, "null", vd->type);
    } else {
      // class/struct sem inicializador: aloca do heap com calloc(1, classSize)
      auto& ci = sem_.classes().at(vd->type.name);
      std::string ptr = newTemp();
      out_ << "  " << ptr << " = call noalias ptr @calloc(i64 1, i64 " << ci.size << ")\n";
      if (ci.hasVptr && !sem_.vslots().empty())
        out_ << "  store ptr @\"Lvt_" << vd->type.name << "\", ptr " << ptr << ", align 8\n"; // M10: vptr
      out_ << "  store ptr " << ptr << ", ptr " << allocaName << ", align 8\n";
      dbgMirrorStore(vd->name, ptr, vd->type);
    }
  }
  if (vd->type.kind == Type::Kind::Array && !vd->init) {
    // v0.46: array local sem inicializador — aloca bloco zerado (calloc);
    // antes o slot ficava SEM bloco nenhum e indexar era ponteiro selvagem
    std::string p2 = newTemp();
    out_ << "  " << p2 << " = call noalias ptr @calloc(i64 1, i64 "
         << typeSize(vd->type) << ")\n";
    out_ << "  store ptr " << p2 << ", ptr " << allocaName << ", align 8\n";
    dbgMirrorStore(vd->name, p2, vd->type);
  }
  if (vd->init) {
    std::string val = toI8Store(genHirExpr(vd->init.get()), vd->init.get(), vd->type);
    // struct: cópia por valor, exceto quando o init já é bloco fresco
    // (new / chamada de ctor) — como o codegen x64
    if (isStructType(vd->type) && !isFreshAlloc(vd->init.get()))
      val = genStructCopy(vd->type, val);
    std::string converted = convertForStore(val, actualTypeOf(vd->init.get()), llvmTy);
    std::string slotTy = (llvmTy == "ptr") ? "ptr" : (llvmTy + "*");
    out_ << "  store " << llvmTy << " " << converted << ", " << slotTy << " " << allocaName << ", align 8\n";
    dbgMirrorStore(vd->name, converted, vd->type);
  }
}

void Irgen::genHirExprStmt(const HirExprStmt* es) {
  genHirExpr(es->expr.get());
}

// Normaliza uma condição HIR (i1 de comparação, i8 de bool, iN de inteiro)
// para i1. Devolve o nome SSA de tipo i1 a usar em `br i1`.
std::string Irgen::genHirCondI1(const HirExpr* cond) {
  if (!cond) return "0";
  std::string val = genHirExpr(cond);
  if (cond->kind == HirExprKind::Binary && cond->type.kind == Type::Kind::Bool) {
    const HirBinary* b = static_cast<const HirBinary*>(cond);
    if (b->op != BinOp::And && b->op != BinOp::Or) {
      // comparações já devolvem i1
      return val;
    }
    // and/or com operandos i8 (bools): resultado é i8 → truncar para i1
    std::string cmpTmp = newTemp();
    out_ << "  " << cmpTmp << " = trunc i8 " << val << " to i1\n";
    return cmpTmp;
  }
  if (cond->kind == HirExprKind::Unary && cond->type.kind == Type::Kind::Bool) {
    // ! bool já devolve i1
    return val;
  }
  std::string condType;
  if (cond->type.kind == Type::Kind::Bool) {
    condType = "i8";
  } else {
    condType = llvmType(cond->type);
  }
  std::string cmpTmp = newTemp();
  if (condType == "i1") {
    cmpTmp = val;
  } else if (condType == "i8") {
    out_ << "  " << cmpTmp << " = trunc i8 " << val << " to i1\n";
  } else {
    out_ << "  " << cmpTmp << " = icmp ne " << condType << " " << val << ", 0\n";
  }
  return cmpTmp;
}

// true se a última linha emitida é um rótulo de bloco (o bloco corrente não
// tem terminator — ex.: join `if.end:` de um `if` cujo ramo faz return)
bool Irgen::lastLineIsLabel() {
  std::string s = out_.str();
  size_t pos = s.find_last_not_of(" \t\r\n");
  if (pos == std::string::npos) return false;
  size_t eol = s.rfind('\n', pos);
  size_t lineStart = (eol == std::string::npos) ? 0 : eol + 1;
  std::string line = s.substr(lineStart, pos - lineStart + 1);
  return !line.empty() && line.back() == ':';
}

// M11-prep: true se o bloco corrente JÁ tem terminator (ret/br/unreachable).
// O LLVM 22 rejeita terminador duplicado (`ret void` seguido de `br`) — o
// padrão `if (c) { return; } ...` gerava br morto após o ret. Descoberto
// pelo demo Snake no wasm32.
bool Irgen::curBlockTerminated() {
  std::string s = out_.str();
  size_t pos = s.find_last_not_of(" \t\r\n");
  if (pos == std::string::npos) return false;
  size_t eol = s.rfind('\n', pos);
  size_t lineStart = (eol == std::string::npos) ? 0 : eol + 1;
  std::string line = s.substr(lineStart, pos - lineStart + 1);
  if (!line.empty() && line[0] == ' ') {
    size_t ns = line.find_first_not_of(" \t");
    if (ns != std::string::npos) line = line.substr(ns);
  }
  return line.rfind("ret ", 0) == 0 || line.rfind("br ", 0) == 0 ||
         line.rfind("unreachable", 0) == 0 || line.rfind("switch ", 0) == 0;
}

// emite `br label %X` apenas se o bloco corrente ainda não tem terminator
void Irgen::emitBrIfOpen(const std::string& label) {
  if (!curBlockTerminated()) out_ << "  br label %" << label << "\n";
}

// Emite `br i1 <cond>, label %<true>, label %<false>` normalizando a condição.
void Irgen::emitCondBranch(const HirExpr* cond,
                           const std::string& trueLabel,
                           const std::string& falseLabel) {
  std::string c = genHirCondI1(cond);
  out_ << "  br i1 " << c << ", label %" << trueLabel << ", label %" << falseLabel << "\n";
}

void Irgen::genHirReturn(const HirReturn* r) {
  returned_ = true;
  if (debug_) out_ << "  call void @hphl_dbg_leave()\n";
  if (inTask_) {
    // return da tarefa sintética: grava o payload no slot res (8 bytes) e
    // retorna; o runtime guarda t->result para o Wait()/await
    if (r->value) {
      std::string val = genHirExpr(r->value.get());
      std::string slot = newTemp();
      out_ << "  " << slot << " = bitcast ptr %.res to ptr\n";
      if (isPointerType(r->value->type)) {
        std::string p = newTemp();
        out_ << "  " << p << " = bitcast " << llvmType(r->value->type) << " " << val
             << " to ptr\n";
        out_ << "  store ptr " << p << ", ptr " << slot << ", align 8\n";
      } else {
        std::string s64 = newTemp();
        out_ << "  " << s64 << " = bitcast ptr %.res to i64*\n";
        std::string v64 = extendToI64(val, r->value->type);
        out_ << "  store i64 " << v64 << ", i64* " << s64 << ", align 8\n";
      }
    }
    out_ << "  ret void\n";
    return;
  }
  if (r->value) {
    std::string val = genHirExpr(r->value.get());
    // M15.1: entry point SEMPRE retorna i32 (independente do tipo declarado)
    bool entryFix = curFnDecl_ && curFnDecl_->isEntryPoint;
    std::string retTy =
        entryFix ? std::string("i32") : llvmType(curFnRetType_);
    if ((curFnRetType_.kind == Type::Kind::Bool || curFnRetType_.kind == Type::Kind::Char) &&
        exprProducesI1(r->value.get())) {
      std::string ext = newTemp();
      out_ << "  " << ext << " = zext i1 " << val << " to i8\n";
      val = ext;
    }
    std::string converted = convertForStore(val, actualTypeOf(r->value.get()), retTy);
    out_ << "  ret " << retTy << " " << converted << "\n";
  } else {
    // M15.1: return vazio no entry point → `ret i32 0` (main é i32)
    if (curFnDecl_ && curFnDecl_->isEntryPoint) {
      out_ << "  ret i32 0\n";
    } else {
      out_ << "  ret void\n";
    }
  }}

void Irgen::genHirIf(const HirIf* ifs) {
  std::string thenLabel = newLabel("if.then");
  std::string elseLabel = newLabel("if.else");
  std::string endLabel = newLabel("if.end");

  emitCondBranch(ifs->cond.get(), thenLabel, ifs->elseBranch ? elseLabel : endLabel);

  out_ << thenLabel << ":\n";
  if (ifs->thenBranch) genBlock(ifs->thenBranch.get());
  emitBrIfOpen(endLabel);

  if (ifs->elseBranch) {
    out_ << elseLabel << ":\n";
    genBlock(ifs->elseBranch.get());
    emitBrIfOpen(endLabel);
  }

  out_ << endLabel << ":\n";
}

void Irgen::genHirWhile(const HirWhile* w) {
  std::string condLabel = newLabel("while.cond");
  std::string bodyLabel = newLabel("while.body");
  std::string endLabel = newLabel("while.end");

  out_ << "  br label %" << condLabel << "\n";
  out_ << condLabel << ":\n";
  emitCondBranch(w->cond.get(), bodyLabel, endLabel);

  out_ << bodyLabel << ":\n";
  loopStack_.push_back({endLabel, condLabel});
  if (w->body) genBlock(w->body.get());
  loopStack_.pop_back();
  emitBrIfOpen(condLabel);

  out_ << endLabel << ":\n";
}

void Irgen::genHirDoWhile(const HirDoWhile* dw) {
  std::string bodyLabel = newLabel("dowhile.body");
  std::string condLabel = newLabel("dowhile.cond");
  std::string endLabel = newLabel("dowhile.end");

  out_ << "  br label %" << bodyLabel << "\n";
  out_ << bodyLabel << ":\n";
  loopStack_.push_back({endLabel, condLabel});
  if (dw->body) genBlock(dw->body.get());
  loopStack_.pop_back();
  emitBrIfOpen(condLabel);

  out_ << condLabel << ":\n";
  emitCondBranch(dw->cond.get(), bodyLabel, endLabel);

  out_ << endLabel << ":\n";
}

void Irgen::genHirFor(const HirFor* f) {
  std::string condLabel = newLabel("for.cond");
  std::string bodyLabel = newLabel("for.body");
  std::string stepLabel = newLabel("for.step");
  std::string endLabel = newLabel("for.end");

  if (f->init) genHirStmt(f->init.get());
  out_ << "  br label %" << condLabel << "\n";
  out_ << condLabel << ":\n";
  if (f->cond) {
    emitCondBranch(f->cond.get(), bodyLabel, endLabel);
  } else {
    out_ << "  br label %" << bodyLabel << "\n";
  }
  out_ << bodyLabel << ":\n";
  loopStack_.push_back({endLabel, stepLabel});
  if (f->body) genBlock(f->body.get());
  loopStack_.pop_back();
  emitBrIfOpen(stepLabel);

  out_ << stepLabel << ":\n";
  if (f->step) genHirExpr(f->step.get());
  emitBrIfOpen(condLabel);
  out_ << endLabel << ":\n";
}

void Irgen::genHirPanic(const HirPanic* p) {
  std::string msg = genHirExpr(p->message.get());
  out_ << "  call void @hphl_panic(" << llvmCallOperand(msg, llvmType(p->message->type)) << ")\n";
  out_ << "  unreachable\n";
}

void Irgen::genHirAssert(const HirAssert* a) {
  // assert(cond) — se cond verdadeira, segue; senão chama hphl_assert(0, msg)
  // e marca unreachable (o runtime aborta).
  std::string okLabel = newLabel("assert.ok");
  std::string failLabel = newLabel("assert.fail");
  emitCondBranch(a->cond.get(), okLabel, failLabel);

  out_ << failLabel << ":\n";
  // mensagem "assert falhou (linha N)"
  std::string msgStr = "assert falhou (linha " + std::to_string(a->line) + ")";
  // reusa o mecanismo de genHirStringLit (interns no mapa de string literals)
  auto fakeStrLit = std::make_unique<HirStringLit>();
  fakeStrLit->value = msgStr;
  fakeStrLit->type = Type::makeString();
  std::string msgPtr = genHirStringLit(fakeStrLit.get());
  out_ << "  call void @hphl_assert(i64 0, ptr " << msgPtr << ")\n";
  out_ << "  unreachable\n";

  out_ << okLabel << ":\n";
}

void Irgen::genHirThrow(const HirThrow* t) {
  std::string val = genHirExpr(t->value.get());
  const Type& vt = t->value->type;
  if (wasmEh_) {
    // M14.3: payload cru como i64 no global interno + flag; o desvio é CFG
    // estático — br direto para o catch BB mais interno ou para o bloco de
    // propagação (ret default), que mantém a flag acesa para os frames de cima
    std::string bits = extendToI64(val, vt);
    out_ << "  store i64 " << bits << ", ptr @hphl_exc_payload, align 8\n";
    out_ << "  store i32 1, ptr @hphl_exc_flag, align 4\n";
    // M22 10.1: emit cleanup for the current try scope before branching to its
    // catch. Only the innermost level's locks are released here; outer scopes
    // are released when their own catch consumes the propagated exception.
    // (Do NOT pop the stack — genHirTry owns the balanced pop after the catch.)
    if (!wasmCleanupStack_.empty())
      emitWasmCleanup(wasmCleanupStack_.back());
    if (!wasmCatchStack_.empty())
      out_ << "  br label %" << wasmCatchStack_.back() << "\n";
    else
      out_ << "  br label %" << ensureWasmPropLabel() << "\n";
    return;
  }
  // throw E: converte para call @hphl_throw(ptr payload) + unreachable.
  // O runtime hphl_throw guarda o payload no registro do handler ativo
  // (cadeia setjmp/longjmp, veja runtime.c) e longjmp para o `try` mais
  // interno; sem handler ativo o runtime aborta (mesma semântica do modelo
  // de exceção custom do codegen x64: "throw sem 'catch' ativo").
  std::string payload;
  if (vt.kind == Type::Kind::Float) {
    // double: os bits viram i64 e o box guarda os 8 bytes
    std::string bits = newTemp();
    out_ << "  " << bits << " = bitcast double " << val << " to i64\n";
    payload = newTemp();
    out_ << "  " << payload << " = call ptr @hphl_box_i64(i64 " << bits << ")\n";
  } else if (vt.kind == Type::Kind::Int || vt.kind == Type::Kind::UInt ||
             vt.kind == Type::Kind::Bool || vt.kind == Type::Kind::Char) {
    // escalar inteiro: estende para i64 e boxa (payload = ponteiro de 8 bytes)
    std::string ext = val;
    if (llvmType(vt) != "i64") {
      ext = newTemp();
      if (vt.kind == Type::Kind::UInt)
        out_ << "  " << ext << " = zext " << llvmType(vt) << " " << val << " to i64\n";
      else
        out_ << "  " << ext << " = sext " << llvmType(vt) << " " << val << " to i64\n";
    }
    payload = newTemp();
    out_ << "  " << payload << " = call ptr @hphl_box_i64(i64 " << ext << ")\n";
  } else {
    // ponteiro/objeto (string, classe, list, ...): o valor já é um ponteiro
    std::string ptr = newTemp();
    out_ << "  " << ptr << " = bitcast " << llvmType(vt) << " " << val << " to ptr\n";
    payload = ptr;
  }
  out_ << "  call void @hphl_throw(ptr " << payload << ")\n";
  out_ << "  unreachable\n";
}

void Irgen::genHirTry(const HirTry* t) {
  std::string bodyLabel = newLabel("try.body");
  std::string catchLabel = newLabel("try.catch");
  std::string endLabel = newLabel("try.end");
  if (wasmEh_) {
    // M14.3: EH por flag — sem registro de runtime. O corpo roda num bloco
    // próprio com o catch BB empilhado; toda chamada de usuário dentro do
    // corpo verifica @hphl_exc_flag e desvia DIRETO para o catch (aresta de
    // CFG estática). O catch consome: carrega o payload, apaga a flag.
    out_ << "  br label %" << bodyLabel << "\n";
    wasmCatchStack_.push_back(catchLabel);
    wasmCleanupStack_.push_back(WasmCleanup()); // M22 10.1: cleanup context for this try
    out_ << bodyLabel << ":\n";
    if (t->body) genBlock(t->body.get());
    // Fecha o escopo do try ANTES de gerar o catch: um `throw`/guard dentro do
    // catch deve propagar para o try MAIS EXTERNO (não para este catch). O
    // `genHirThrow` só emite o cleanup do escopo corrente (sem desempilhar);
    // este pop equilibra o push de cima.
    wasmCleanupStack_.pop_back();
    wasmCatchStack_.pop_back();
    emitBrIfOpen(endLabel);
    out_ << catchLabel << ":\n";
    // M22 10.1: o throw/guard já emitiu o cleanup do escopo corrente antes de
    // desviar para este catch. O catch consome o payload e apaga a flag.
    std::string payloadBits = newTemp();
    out_ << "  " << payloadBits << " = load i64, ptr @hphl_exc_payload, align 8\n";
    out_ << "  store i32 0, ptr @hphl_exc_flag, align 4\n";
    if (t->hasCatch) {
      // decodifica o i64 cru para o tipo da variável de catch
      const Type& ct = t->catchType;
      std::string catchSlot = emitAllocaSlot(t->catchVar, ct);
      std::string cty = llvmType(ct);
      std::string v = payloadBits;
      if (ct.kind == Type::Kind::Float) {
        std::string f = newTemp();
        out_ << "  " << f << " = bitcast i64 " << v << " to double\n";
        v = f;
        cty = "double";
      } else if (ct.isPointer()) {
        std::string p = newTemp();
        out_ << "  " << p << " = inttoptr i64 " << v << " to " << cty << "\n";
        v = p;
      } else if (cty != "i64") {
        std::string tr = newTemp();
        out_ << "  " << tr << " = trunc i64 " << v << " to " << cty << "\n";
        v = tr;
      }
      out_ << "  store " << cty << " " << v << ", " << ptrTo(cty) << " "
           << catchSlot << ", align 8\n";
      paramNames_.erase(t->catchVar);
      if (t->catchBody) genBlock(t->catchBody.get());
      emitBrIfOpen(endLabel);
    } else {
      // try sem catch: rethrow — flag continua acesa, propaga para fora
      if (!wasmCatchStack_.empty())
        out_ << "  br label %" << wasmCatchStack_.back() << "\n";
      else
        out_ << "  br label %" << ensureWasmPropLabel() << "\n";
    }
    out_ << endLabel << ":\n";
    return;
  }
  // Modelo CONTEXT (runtime hphl_exc_*): o `try` aloca o registro, registra na
  // cadeia de handlers (hphl_exc_push) e chama hphl_exc_begin, que captura o
  // contexto do caller (RtlCaptureContext) e ajusta Rip/Rsp para o call site;
  // um throw restaura o contexto (RtlRestoreContext) e faz o begin "retornar"
  // 1 → catch. O corpo roda em bloco próprio. Espelha a semântica do modelo
  // custom do codegen x64 (registro por handler, payload no registro, rethrow
  // para o handler externo após o desempilhar).
  std::string excRec = newTemp();
  out_ << "  " << excRec << " = call ptr @hphl_exc_new()\n";
  out_ << "  call void @hphl_exc_push(ptr " << excRec << ")\n";
  std::string r = newTemp();
  out_ << "  " << r << " = call i32 @hphl_exc_begin(ptr " << excRec << ")\n";
  std::string isExc = newTemp();
  out_ << "  " << isExc << " = icmp ne i32 " << r << ", 0\n";

  out_ << "  br i1 " << isExc << ", label %" << catchLabel << ", label %" << bodyLabel << "\n";

  // caminho de sucesso
  out_ << bodyLabel << ":\n";
  if (t->body) genBlock(t->body.get());
  out_ << "  call void @hphl_exc_end(ptr " << excRec << ")\n";
  out_ << "  call void @hphl_exc_free(ptr " << excRec << ")\n";
  out_ << "  br label %" << endLabel << "\n";

  // caminho da exceção
  out_ << catchLabel << ":\n";
  std::string payload = newTemp();
  out_ << "  " << payload << " = call ptr @hphl_exc_payload(ptr " << excRec << ")\n";
  out_ << "  call void @hphl_exc_free(ptr " << excRec << ")\n";
  if (t->hasCatch) {
    // armazena o payload no slot da variável de catch (desembala escalares)
    std::string catchTy = llvmType(t->catchType);
    std::string catchSlot = emitAllocaSlot(t->catchVar, t->catchType);
    if (t->catchType.kind == Type::Kind::Float) {
      std::string unboxed = newTemp();
      out_ << "  " << unboxed << " = call i64 @hphl_unbox_i64(ptr " << payload
           << ")\n";
      std::string ftmp = newTemp();
      out_ << "  " << ftmp << " = bitcast i64 " << unboxed << " to double\n";
      out_ << "  store double " << ftmp << ", double* " << catchSlot << ", align 8\n";
    } else if (t->catchType.kind == Type::Kind::Int ||
               t->catchType.kind == Type::Kind::UInt ||
               t->catchType.kind == Type::Kind::Bool ||
               t->catchType.kind == Type::Kind::Char) {
      std::string unboxed = newTemp();
      out_ << "  " << unboxed << " = call i64 @hphl_unbox_i64(ptr " << payload
           << ")\n";
      std::string castTmp = newTemp();
      if (catchTy != "i64" && t->catchType.kind != Type::Kind::Float) {
        out_ << "  " << castTmp << " = trunc i64 " << unboxed << " to " << catchTy
             << "\n";
      } else {
        castTmp = unboxed;
      }
      out_ << "  store " << catchTy << " " << castTmp << ", " << ptrTo(catchTy) << " "
           << catchSlot << ", align 8\n";
    } else {
      // ponteiro/objeto: o payload já é o ponteiro
      std::string ptr = newTemp();
      out_ << "  " << ptr << " = bitcast ptr " << payload << " to " << catchTy << "\n";
      std::string catchPtrTy = (catchTy == "ptr") ? "ptr" : (catchTy + "*");
      out_ << "  store " << catchTy << " " << ptr << ", " << catchPtrTy << " "
           << catchSlot << ", align 8\n";
    }
    paramNames_.erase(t->catchVar);
    if (t->catchBody) genBlock(t->catchBody.get());
    out_ << "  br label %" << endLabel << "\n";
  } else {
    // try sem catch: rethrow para o handler externo (inalcançável na prática:
    // a semântica exige catch)
    out_ << "  call void @hphl_throw(ptr " << payload << ")\n";
    out_ << "  unreachable\n";
  }

  out_ << endLabel << ":\n";
}

// ---------------------------------------------------------------------------
// M22 10.1: EH por flag de propagação (alvo wasm32) — cleanup completo de destructors/finally via guard
// M14.3 base + M22 estende para incluir destrutores de Heap/Shared no bloco de propagação
// -------------------------------------------------------------------------
std::string Irgen::ensureWasmPropLabel() {
  if (wasmPropLabel_.empty()) wasmPropLabel_ = newLabel("exc.prop");
  return wasmPropLabel_;
}

void Irgen::emitWasmExcGuard() {
  std::string f = newTemp();
  out_ << "  " << f << " = load i32, ptr @hphl_exc_flag, align 4\n";
  std::string isx = newTemp();
  out_ << "  " << isx << " = icmp ne i32 " << f << ", 0\n";
  std::string cont = newLabel("exc.cont");
  std::string target =
      !wasmCatchStack_.empty() ? wasmCatchStack_.back() : ensureWasmPropLabel();
  out_ << "  br i1 " << isx << ", label %" << target << ", label %" << cont
       << "\n";
  out_ << cont << ":\n";
}

void Irgen::emitWasmCleanup(const WasmCleanup& scope) {
  // M22 10.1: emite cleanup de locks de UM escopo (ordem reversa/LIFO)
  for (auto lit = scope.locks.rbegin(); lit != scope.locks.rend(); ++lit) {
    out_ << "  call void @hphl_lock_end(" << llvmCallOperand(lit->first, lit->second) << ")\n";
  }
}

void Irgen::genHirLock(const HirLock* l) {
  // lock (target) { body } → hphl_lock_begin(target); body; hphl_lock_end(target)
  std::string target = genHirExpr(l->target.get());
  std::string targetTy = llvmType(l->target->type);
  out_ << "  call void @hphl_lock_begin(" << llvmCallOperand(target, targetTy)
       << ")\n";
  // M22 10.1: se dentro de try WASM, registra lock para cleanup no catch
  if (wasmEh_ && !wasmCleanupStack_.empty()) {
    wasmCleanupStack_.back().locks.push_back({target, targetTy});
  }
  if (l->body) genBlock(l->body.get());
  // re-avalia o target (policy: alvo puro / sem efeito colateral no M2)
  std::string target2 = genHirExpr(l->target.get());
  out_ << "  call void @hphl_lock_end(" << llvmCallOperand(target2, targetTy)
       << ")\n";
}

void Irgen::genHirSpawnStmt(const HirSpawn* s) {
  // spawn { body }: enfileira tarefa sintética e emite o site de spawn.
  spawnedAny_ = true;
  auto caps = filterCaptures(s->captures);
  queueTask(std::move(const_cast<HirSpawn*>(s)->body), caps);
  std::string taskLbl = pendingTasks_.back().fnLabel;
  emitSpawnSite(taskLbl, caps);
}

// descarta capturas que não existem no frame do caller (varíaveis declaradas
// DENTRO do corpo da task não podem ser capturadas; o semantic não cria escopo
// para o corpo do spawn, então nomes locais da task vazam para a lista)
std::vector<std::pair<std::string, Type>> Irgen::filterCaptures(
    const std::vector<std::pair<std::string, Type>>& caps) {
  std::vector<std::pair<std::string, Type>> out;
  for (const auto& c : caps) {
    if (paramNames_.count(c.first)) { out.push_back(c); continue; }
    bool found = false;
    for (auto it = localSlots_.rbegin(); it != localSlots_.rend() && !found; ++it)
      if (it->count(c.first)) found = true;
    if (found) out.push_back(c);
  }
  return out;
}

void Irgen::genHirParallel(const HirParallel* p) {
  // `parallel deterministic`: partes inline em ordem (sem spawn/barreira).
  if (p->isDeterministic) {
    for (auto& part : const_cast<HirParallel*>(p)->parts) genBlock(part.get());
    return;
  }
  // `parallel`: spawn cada parte com seu env, depois join. Dentro de uma
  // tarefa (inTask_), abre uma REGIÃO própria (a barreira espera só as tarefas
  // da região, sem deadlock) — mesmo desenho do codegen x64.
  bool nested = inTask_;
  if (nested) out_ << "  call void @hphl_region_begin()\n";
  const std::vector<std::pair<std::string, Type>> emptyCaps;
  for (size_t i = 0; i < p->parts.size(); i++) {
    std::unique_ptr<HirBlock> part =
        std::move(const_cast<HirParallel*>(p)->parts[i]);
    const auto& capsAll =
        i < p->partCaptures.size() ? p->partCaptures[i] : emptyCaps;
    auto caps = filterCaptures(capsAll);
    queueTask(std::move(part), caps);
    std::string taskLbl = pendingTasks_.back().fnLabel;
    emitSpawnSite(taskLbl, caps);
  }
  out_ << "  call void @hphl_" << (nested ? "region_join" : "join_tasks")
       << "()\n";
}

void Irgen::genHirParallelForeach(const HirParallelForeach* pf) {
  // `parallel foreach (x in coll) { ... }`: particionamento strided/batch,
  // uma task por parte. Corpo sintético = for (j; j<n; j+=step) { var x =
  // coll[j]; ... corpo }. Cada parte recebe __coll (+ __p/__step ou
  // __start/__count). Espelha genHirParallelForeach do codegen x64.
  bool isList = pf->collection->type.kind == Type::Kind::List;
  long long n = isList ? 0 : pf->collection->type.arraySize;
  int B = pf->batchSize;
  bool isBatch = B > 0;
  bool nested = inTask_;
  if (nested) out_ << "  call void @hphl_region_begin()\n";

  // N total (list: runtime; array: constante)
  std::string nTmp;
  if (isList) {
    std::string coll = genHirExpr(pf->collection.get());
    nTmp = newTemp();
    out_ << "  " << nTmp << " = call i64 @hphl_list_len("
         << llvmCallOperand(coll, llvmType(pf->collection->type)) << ")\n";
  } else {
    nTmp = std::to_string(n);
  }
  // P = nº de partes (batch: ceil(N/B); adaptativo: min(workers, N))
  std::string pTmp = newTemp();
  if (isBatch) {
    out_ << "  " << pTmp << " = add i64 " << nTmp << ", " << (B - 1) << "\n";
    std::string d = newTemp();
    out_ << "  " << d << " = sdiv i64 " << pTmp << ", " << B << "\n";
    pTmp = d;
  } else {
    std::string w = newTemp();
    out_ << "  " << w << " = call i64 @hphl_worker_count()\n";
    // P = max(1, min(w, N))
    std::string m1 = newTemp();
    out_ << "  ; P = min(workers, N)\n";
    std::string cmp = newTemp();
    out_ << "  " << cmp << " = icmp sgt i64 " << w << ", " << nTmp << "\n";
    out_ << "  " << m1 << " = select i1 " << cmp << ", i64 " << nTmp
         << ", i64 " << w << "\n";
    std::string cmp2 = newTemp();
    out_ << "  " << cmp2 << " = icmp sgt i64 " << m1 << ", 0\n";
    out_ << "  " << pTmp << " = select i1 " << cmp2 << ", i64 " << m1
         << ", i64 1\n";
  }

  // corpo sintético da task (mesmo desenho do codegen)
  auto body = std::make_unique<HirBlock>();
  Type tInt = Type::makeInt(64);
  auto mkVar = [](const std::string& name, const Type& t) {
    auto v = std::make_unique<HirVar>();
    v->name = name;
    v->type = t;
    return v;
  };
  auto mkDecl = [](const std::string& name, const Type& t,
                   std::unique_ptr<HirExpr> init) {
    auto d = std::make_unique<HirVarDecl>();
    d->name = name;
    d->type = t;
    d->init = std::move(init);
    return d;
  };
  if (isList && !isBatch) {
    auto len = std::make_unique<HirMember>();
    len->object = mkVar("__coll", pf->collection->type);
    len->member = "Length";
    len->isListLength = true;
    len->type = tInt;
    body->stmts.push_back(mkDecl("__n", tInt, std::move(len)));
  }
  auto loop = std::make_unique<HirFor>();
  loop->init = mkDecl("__j", tInt, mkVar(isBatch ? "__start" : "__p", tInt));
  auto lt = std::make_unique<HirBinary>();
  lt->op = BinOp::Lt;
  lt->lhs = mkVar("__j", tInt);
  lt->type = Type::makeBool();
  if (isBatch) {
    auto up = std::make_unique<HirBinary>();
    up->op = BinOp::Add;
    up->lhs = mkVar("__start", tInt);
    up->rhs = mkVar("__count", tInt);
    up->type = tInt;
    lt->rhs = std::move(up);
  } else if (isList) {
    lt->rhs = mkVar("__n", tInt);
  } else {
    auto nn = std::make_unique<HirIntLit>();
    nn->value = n;
    nn->type = tInt;
    lt->rhs = std::move(nn);
  }
  loop->cond = std::move(lt);
  if (isBatch) {
    auto inc = std::make_unique<HirUnary>();
    inc->op = UnOp::PostInc;
    inc->operand = mkVar("__j", tInt);
    inc->type = tInt;
    loop->step = std::move(inc);
  } else {
    auto assign = std::make_unique<HirAssign>();
    assign->op = AssignOp::Plain;
    assign->target = mkVar("__j", tInt);
    assign->type = tInt;
    auto add = std::make_unique<HirBinary>();
    add->op = BinOp::Add;
    add->lhs = mkVar("__j", tInt);
    add->rhs = mkVar("__step", tInt);
    add->type = tInt;
    assign->value = std::move(add);
    loop->step = std::move(assign);
  }
  auto inner = std::make_unique<HirBlock>();
  auto iidx = std::make_unique<HirIndex>();
  iidx->object = mkVar("__coll", pf->collection->type);
  iidx->index = mkVar("__j", tInt);
  iidx->type = pf->itemType;
  inner->stmts.push_back(mkDecl(pf->itemName, pf->itemType, std::move(iidx)));
  for (auto& s : const_cast<HirParallelForeach*>(pf)->body->stmts)
    inner->stmts.push_back(std::move(s));
  loop->body = std::move(inner);
  body->stmts.push_back(std::move(loop));

  std::vector<std::pair<std::string, Type>> caps;
  caps.push_back({"__coll", pf->collection->type});
  if (isBatch) {
    caps.push_back({"__start", tInt});
    caps.push_back({"__count", tInt});
  } else {
    caps.push_back({"__p", tInt});
    caps.push_back({"__step", tInt});
  }
  queueTask(std::move(body), caps);

  // loop de spawns: i = 0; while (i < P) { emit spawn site with env; i++ }
  std::string iTmp = emitAllocaSlot("__i", tInt);
  out_ << "  store i64 0, i64* " << iTmp << ", align 8\n";

  std::string collPtr = emitAllocaSlot("__coll", Type::makePtr());
  {
    std::string collVal = genHirExpr(pf->collection.get());
    out_ << "  store ptr " << llvmInstrOperand(collVal, llvmType(pf->collection->type))
         << ", ptr " << collPtr << ", align 8\n";
  }

  std::string spLabel = newLabel("pf.spawn.cond");
  std::string bodyLabel = newLabel("pf.spawn.body");
  std::string spEnd = newLabel("pf.spawn.end");
  out_ << "  br label %" << spLabel << "\n";
  out_ << spLabel << ":\n";
  std::string iv = newTemp();
  out_ << "  " << iv << " = load i64, i64* " << iTmp << ", align 8\n";
  std::string cmp = newTemp();
  out_ << "  " << cmp << " = icmp slt i64 " << iv << ", " << pTmp << "\n";
  out_ << "  br i1 " << cmp << ", label %" << bodyLabel << ", label %" << spEnd << "\n";

  out_ << bodyLabel << ":\n";
  // monta o env: __coll + __p (i) ou __coll + __start (i*B) + __count (min(B,N-start))
  std::vector<std::pair<std::string, Type>> envCaps;
  envCaps.push_back({"__coll", pf->collection->type});
  if (isBatch) {
    envCaps.push_back({"__start", tInt});
    envCaps.push_back({"__count", tInt});
  } else {
    envCaps.push_back({"__p", tInt});
    envCaps.push_back({"__step", tInt});
  }

  std::string envTmp = newTemp();
  out_ << "  " << envTmp << " = call noalias ptr @malloc(i64 "
       << (envCaps.size() * 8) << ")\n";
  // __coll = load do ponteiro guardado (mesmo objeto por todas as partes)
  {
    std::string gep = newTemp();
    out_ << "  " << gep << " = getelementptr inbounds i8, ptr " << envTmp
         << ", i64 0\n";
    std::string gepPtr = newTemp();
    out_ << "  " << gepPtr << " = bitcast ptr " << gep << " to ptr\n";
    std::string collLoad = newTemp();
    out_ << "  " << collLoad << " = load ptr, ptr " << collPtr << ", align 8\n";
    out_ << "  store ptr " << collLoad << ", ptr " << gepPtr << ", align 8\n";
  }
  if (isBatch) {
    // __start = i * B
    std::string startGep = newTemp();
    out_ << "  " << startGep << " = getelementptr inbounds i8, ptr " << envTmp
         << ", i64 8\n";
    std::string startPtr = newTemp();
    out_ << "  " << startPtr << " = bitcast ptr " << startGep << " to i64*\n";
    std::string mul = newTemp();
    out_ << "  " << mul << " = mul i64 " << iv << ", " << B << "\n";
    out_ << "  store i64 " << mul << ", i64* " << startPtr << ", align 8\n";

    // __count = min(B, N - start)
    std::string countGep = newTemp();
    out_ << "  " << countGep << " = getelementptr inbounds i8, ptr " << envTmp
         << ", i64 16\n";
    std::string countPtr = newTemp();
    out_ << "  " << countPtr << " = bitcast ptr " << countGep << " to i64*\n";
    std::string rem = newTemp();
    out_ << "  " << rem << " = sub i64 " << nTmp << ", " << mul << "\n";
    std::string ccmp = newTemp();
    out_ << "  " << ccmp << " = icmp slt i64 " << rem << ", " << B << "\n";
    std::string cnt = newTemp();
    out_ << "  " << cnt << " = select i1 " << ccmp << ", i64 " << rem << ", i64 "
         << B << "\n";
    out_ << "  store i64 " << cnt << ", i64* " << countPtr << ", align 8\n";
  } else {
    // __p = i (parte corrente)
    std::string pGep = newTemp();
    out_ << "  " << pGep << " = getelementptr inbounds i8, ptr " << envTmp
         << ", i64 8\n";
    std::string pPtr = newTemp();
    out_ << "  " << pPtr << " = bitcast ptr " << pGep << " to i64*\n";
    out_ << "  store i64 " << iv << ", i64* " << pPtr << ", align 8\n";
    // __step = P
    std::string sGep = newTemp();
    out_ << "  " << sGep << " = getelementptr inbounds i8, ptr " << envTmp
         << ", i64 16\n";
    std::string sPtr = newTemp();
    out_ << "  " << sPtr << " = bitcast ptr " << sGep << " to i64*\n";
    out_ << "  store i64 " << pTmp << ", i64* " << sPtr << ", align 8\n";
  }

  // chama hphl_spawn_task_ex(fn, env) — task fn tem assinatura (ptr, ptr)
  out_ << "  call void @hphl_spawn_task_ex(ptr bitcast (void (ptr, ptr)* "
       << pendingTasks_.back().fnLabel << " to ptr), ptr " << envTmp << ")\n";

  // i++
  std::string incLoad = newTemp();
  out_ << "  " << incLoad << " = load i64, i64* " << iTmp << ", align 8\n";
  std::string incVal = newTemp();
  out_ << "  " << incVal << " = add i64 " << incLoad << ", 1\n";
  out_ << "  store i64 " << incVal << ", i64* " << iTmp << ", align 8\n";
  out_ << "  br label %" << spLabel << "\n";
  out_ << spEnd << ":\n";
  out_ << "  call void @hphl_" << (nested ? "region_join" : "join_tasks")
       << "()\n";
}

// ---------------------------------------------------------------------------
// HIR expression generation → returns SSA value identifier (operand string)
// ---------------------------------------------------------------------------
// M15: normaliza valor bool/char vindo de comparação (i1) para i8 quando o
// destino é um store tipado (vardecl/assign/arg). Sem isso, `bool b = a == b;`
// armazenava o SSA i1 direto no slot i8 — o x64 não tipa e aceitava, mas o
// verificador LLVM rejeita.
std::string Irgen::toI8Store(const std::string& val, const HirExpr* src,
                             const Type& dstT) {
  if ((dstT.kind == Type::Kind::Bool || dstT.kind == Type::Kind::Char) &&
      src && exprProducesI1(src)) {
    std::string ext = newTemp();
    out_ << "  " << ext << " = zext i1 " << val << " to i8\n";
    return ext;
  }
  return val;
}

std::string Irgen::genHirExpr(const HirExpr* e) {
  if (!e) return "0";
  switch (e->kind) {
    case HirExprKind::IntLit:    return genHirIntLit(static_cast<const HirIntLit*>(e));
    case HirExprKind::FloatLit:  return genHirFloatLit(static_cast<const HirFloatLit*>(e));
    case HirExprKind::BoolLit:   return genHirBoolLit(static_cast<const HirBoolLit*>(e));
    case HirExprKind::StringLit: return genHirStringLit(static_cast<const HirStringLit*>(e));
    case HirExprKind::Var:       return genHirVar(static_cast<const HirVar*>(e));
    case HirExprKind::This:      return genHirThis(static_cast<const HirThis*>(e));
    case HirExprKind::Member:    return genHirMember(static_cast<const HirMember*>(e));
    case HirExprKind::Index:     return genHirIndex(static_cast<const HirIndex*>(e));
    case HirExprKind::ArrayLit:  return genHirArrayLit(static_cast<const HirArrayLit*>(e));
    case HirExprKind::TupleLit:  return genHirTupleLit(static_cast<const HirTupleLit*>(e));
    case HirExprKind::Call:      return genHirCall(static_cast<const HirCall*>(e));    case HirExprKind::Binary:    return genHirBinary(static_cast<const HirBinary*>(e));
    case HirExprKind::Unary:     return genHirUnary(static_cast<const HirUnary*>(e));
    case HirExprKind::Assign:    return genHirAssign(static_cast<const HirAssign*>(e));
    case HirExprKind::Cast:      return genHirCast(static_cast<const HirCast*>(e));
    case HirExprKind::New:       return genHirNew(static_cast<const HirNew*>(e));
    case HirExprKind::CellTag:   return genHirCellTag(static_cast<const HirCellTag*>(e));
    case HirExprKind::LoadAt:    return genHirLoadAt(static_cast<const HirLoadAt*>(e));
    case HirExprKind::OptCtor:   return genHirOptCtor(static_cast<const HirOptCtor*>(e));
    case HirExprKind::Spawn:     return genHirSpawn(static_cast<const HirSpawnExpr*>(e));
    case HirExprKind::Await:     return genHirAwait(static_cast<const HirAwaitExpr*>(e));
    case HirExprKind::Lambda:    return genHirLambda(static_cast<const HirLambda*>(e));
    case HirExprKind::CharLit:   return std::to_string(static_cast<const HirCharLit*>(e)->value);
    case HirExprKind::NullLit:   return "null";
    default:
      return "0";
  }
}
std::string Irgen::genHirIntLit(const HirIntLit* l) {
  return std::to_string(l->value);
}

std::string Irgen::genHirFloatLit(const HirFloatLit* l) {
  std::string ty = llvmType(l->type);
  if (ty == "float") {
    union { float f; uint32_t i; } u;
    u.f = (float)l->value;
    std::string tmp = newTemp();
    out_ << "  " << tmp << " = bitcast i32 " << u.i << " to float\n";
    return tmp;
  } else {
    union { double d; uint64_t i; } u;
    u.d = l->value;
    std::string tmp = newTemp();
    out_ << "  " << tmp << " = bitcast i64 " << u.i << " to double\n";
    return tmp;
  }
}

std::string Irgen::genHirBoolLit(const HirBoolLit* l) {
  return l->value ? "1" : "0";
}

std::string Irgen::genHirStringLit(const HirStringLit* l) {
  // check if we already have this string literal
  if (stringLiterals_.count(l->value)) {
    // extract the global name from the stored declaration
    const std::string& decl = stringLiterals_[l->value];
    size_t nameEnd = decl.find(' ');
    return decl.substr(0, nameEnd);
  }

  // escape special chars for LLVM IR string literal (c"..." format uses \HH hex escapes)
  std::string escaped;
  for (char c : l->value) {
    switch (c) {
      case '\n':  escaped += "\\0a"; break;
      case '\r':  escaped += "\\0d"; break;
      case '\t':  escaped += "\\09"; break;
      case '\"':  escaped += "\\22"; break;
      case '\\':  escaped += "\\\\"; break;
      default:
        if ((unsigned char)c < 0x20 || (unsigned char)c >= 0x7f) {
          char buf[5];
          snprintf(buf, sizeof(buf), "\\%02x", (unsigned char)c);
          escaped += buf;
        } else {
          escaped += c;
        }
    }
  }

  std::string globalName = "@.str." + std::to_string(tempCounter_++);
  int totalLen = (int)(l->value.size() + 1);
  std::string constDecl = globalName + " = private unnamed_addr constant [" +
                          std::to_string(totalLen) + " x i8] c\"" + escaped + "\\00\"";

  // store declaration for module-level emission
  stringLiterals_[l->value] = constDecl;

  // generate GEP to get pointer to string data
  std::string gepTmp = newTemp();
  out_ << "  " << gepTmp << " = getelementptr inbounds [" << totalLen << " x i8], [" << totalLen
       << " x i8]* " << globalName << ", i64 0, i64 0\n";
  return gepTmp;
}

std::string Irgen::genHirVar(const HirVar* v) {
  // M10 (v0.45): parâmetro de valor da instância genérica → literal inteiro
  if (curFnDecl_ && !curFnDecl_->litParams.empty()) {
    auto lit = curFnDecl_->litParams.find(v->name);
    if (lit != curFnDecl_->litParams.end()) return std::to_string(lit->second);
  }
  if (v->isProperty && v->propGet) {
    std::string fname = fnLabel(v->propGet);
    HirThis thObj;
    std::string thisArg = genHirThis(&thObj);
    std::string thisTy = v->propGet->params.empty() ? llvmType(thObj.type) : llvmType(v->propGet->params[0]->type);
    std::string args = llvmCallOperand(thisArg, thisTy);
    if (v->type.kind == Type::Kind::Void) {
      out_ << "  call void " << fname << "(" << args << ")\n";
      return "";
    } else {
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call " << llvmReturnType(v->type) << " " << fname << "(" << args << ")\n";
      return tmp;
    }
  }
  if (v->isConst) {
    // enum constant
    return std::to_string(v->constValue);
  }
  if (v->isThisField) {
    std::string bytePtr = genThisFieldPtr(v->thisFieldOffset);
    // M10 (v0.45.2): campo-array FIXO é inline no objeto — devolver o
    // ENDEREÇO (indexar/Length fazem gep a partir dele); carregar como
    // escalar leria um ponteiro falso
    if (v->type.kind == Type::Kind::Array) return bytePtr;
    std::string llvmTy = llvmType(v->type);
    // leitura: o slot contém o valor (ponteiro da string/objeto incluso)
    std::string tmp = newTemp();
    out_ << "  " << tmp << " = load " << llvmTy << ", ptr " << bytePtr << ", align 8\n";
    return tmp;
  }
  // parâmetros SSA (sem slot): só `this` (métodos) e params ref chegam aqui
  // sem slot próprio... na prática apenas `this` (nunca escrito; leitura via
  // valor). Parâmetros normais têm slot de spill no slotStack_.
  if (paramNames_.count(v->name) && slotStack_.find(v->name) == slotStack_.end()) {
    return "%." + v->name;
  }
  // global variable: use @<label>
  if (v->isGlobal) {
    std::string gname = v->label.empty() ? v->name : v->label;
    std::string globalName = "@" + gname;
    std::string llvmTy = llvmType(v->type);
    if (tlsOffsets_.count(gname)) {
      // threadlocal: slot da thread corrente (bloco zerado por thread)
      std::string addr = tlsAddr(gname);
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = load " << llvmTy << ", ptr " << addr << ", align 8\n";
      return tmp;
    }
    if (v->type.kind == Type::Kind::Array) {
      // array global: ponteiro ptr para o bloco de dados ([N x i8])
      long long total = globalSizeBytes(v->type);
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = getelementptr [" << total << " x i8], [" << total << " x i8]* "
           << globalName << ", i64 0, i64 0\n";
      return tmp;
    }
std::string tmp = newTemp();
    std::string globalPtrTy = (llvmTy == "ptr") ? "ptr" : (llvmTy + "*");
    out_ << "  " << tmp << " = load " << llvmTy << ", " << globalPtrTy << " " << globalName << ", align 8\n";
    return tmp;
  }
  // local variable: load from alloca slot or return the slot address (ref vars)
  {
    std::string tmp = newTemp();
    Type slotT = actualTypeOf(v);
    std::string slotTy = llvmType(slotT);
    std::string slotPtrTy = (slotTy == "ptr") ? "ptr" : (slotTy + "*");
    std::string slotName = findLocalSlot(v->name);
    bool slotIsRef = false;
    if (!slotName.empty() && slotName[0]=='%') {
      size_t dot = slotName.find('.');
      if (dot != std::string::npos && dot+1 < slotName.size() &&
          slotName[dot+1]=='l') {
        char c = slotName[dot+2];
        if (c >= '0' && c <= '9') {
          // not a ref slot (%.lN.name = declareLocalSlot output)
        } else if (c == 's') {
          slotIsRef = true;  // %.lsN.name = ref slot from ref-param prologue
        }
      }
    }
    if (slotIsRef) {
      std::string inner = newTemp();
      out_ << "  " << inner << " = load ptr, ptr " << slotName << ", align 8\n";
      std::string val = newTemp();
      out_ << "  " << val << " = load " << slotTy << ", ptr " << inner << ", align 8\n";
      return val;
    }
    if (llvmType(v->type).find('*') != std::string::npos) return slotName;
    out_ << "  " << tmp << " = load " << slotTy << ", " << slotPtrTy << " "
         << slotName << ", align 8\n";
    return tmp;
  }
}

// endereço de um lvalue (args ref/out): devolve o ponteiro da célula
std::string Irgen::genLValueAddress(const HirExpr* e) {
  switch (e->kind) {
    case HirExprKind::Var: {
      const HirVar* v = static_cast<const HirVar*>(e);
      if (v->isThisField) return genThisFieldPtr(v->thisFieldOffset);
      if (v->isGlobal) {
        std::string gname = v->label.empty() ? v->name : v->label;
        if (tlsOffsets_.count(gname)) return tlsAddr(gname); // threadlocal
        std::string globalName = "@" + gname;
        if (v->type.kind == Type::Kind::Array) {
          long long total = globalSizeBytes(v->type);
          std::string tmp = newTemp();
          out_ << "  " << tmp << " = getelementptr [" << total << " x i8], ["
               << total << " x i8]* " << globalName << ", i64 0, i64 0\n";
          return tmp;
        }
        return globalName;
      }
      std::string slotName = findLocalSlot(v->name);
      // ref-slot (%.lsN.name): o slot guarda o endereço → carrega o ponteiro
      if (slotName.find(".ls") != std::string::npos) {
        std::string inner = newTemp();
        out_ << "  " << inner << " = load ptr, ptr " << slotName << ", align 8\n";
        return inner;
      }
      return slotName;
    }
    case HirExprKind::Index: {
      const HirIndex* ix = static_cast<const HirIndex*>(e);
      std::string obj = genHirExpr(ix->object.get());
      std::string objTy = llvmType(ix->object->type);
      std::string idxv = genHirExpr(ix->index.get());
      std::string idx64 = idxv;
      if (llvmType(ix->index->type) != "i64") {
        idx64 = newTemp();
        out_ << "  " << idx64 << " = sext " << llvmType(ix->index->type) << " "
             << idxv << " to i64\n";
      }
      std::string base = obj;
      if (ix->object->type.kind == Type::Kind::List) {
        out_ << "  call void @hphl_list_check(i64 " << idx64 << ", "
             << llvmCallOperand(obj, objTy) << ")\n";
        std::string dataTmp = newTemp();
        out_ << "  " << dataTmp << " = call ptr @hphl_list_data("
             << llvmCallOperand(obj, objTy) << ")\n";
        base = dataTmp;
      }
      // sub-array: local/heap = ponteiro (8 bytes); global = linha contígua
      // (packed [N x i8]) — endereço da linha, sem load (valor inexistente)
      long long stride = arrayElementStride(ix->object.get(), ix->type);
      std::string scaled = newTemp();
      out_ << "  " << scaled << " = mul i64 " << idx64 << ", " << stride << "\n";
      std::string addr = newTemp();
      out_ << "  " << addr << " = getelementptr inbounds i8, ptr "
           << llvmInstrOperand(base, "ptr") << ", i64 " << scaled << "\n";
      return addr;
    }
    case HirExprKind::Member: {
      const HirMember* m = static_cast<const HirMember*>(e);
      std::string obj = genHirExpr(m->object.get());
      std::string objTy = llvmType(m->object->type);
      std::string ptrTmp = newTemp();
      out_ << "  " << ptrTmp << " = getelementptr inbounds i8, ptr "
           << llvmInstrOperand(obj, objTy) << ", i64 " << m->fieldOffset << "\n";
      return ptrTmp;
    }
    default:
      return "0";
  }
}

std::string Irgen::genHirThis(const HirThis* t) {
  // método de instância: `this` é parâmetro SSA (nenhum alloca/load)
  if (paramNames_.count("this")) return "%.this";
  std::string tmp = newTemp();
  std::string llvmTy = llvmType(t->type);
  std::string thisPtrTy = (llvmTy == "ptr") ? "ptr" : (llvmTy + "*");
  out_ << "  " << tmp << " = load " << llvmTy << ", " << thisPtrTy << " %.this, align 8\n";
  return tmp;
}

std::string Irgen::genHirMember(const HirMember* m) {
  if (m->isProperty && m->propGet) {
    std::string fname = fnLabel(m->propGet);
    std::string objArg = genHirExpr(m->object.get());
    std::string objTy = m->propGet->params.empty() ? llvmType(m->object->type) : llvmType(m->propGet->params[0]->type);
    std::string args = llvmCallOperand(objArg, objTy);
    if (m->type.kind == Type::Kind::Void) {
      out_ << "  call void " << fname << "(" << args << ")\n";
      return "";
    } else {
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call " << llvmReturnType(m->type) << " " << fname << "(" << args << ")\n";
      return tmp;
    }
  }
  if (m->isEnumCtor) {
    // variante de enum rico (célula: tag i64 em +0)
    long long tag = 0;
    auto it = sem_.enums().find(m->enumCtorEnum);
    if (it != sem_.enums().end() && m->enumCtorIndex >= 0 &&
        m->enumCtorIndex < (int)it->second->entries.size())
      tag = it->second->entries[m->enumCtorIndex].value;
    std::string h = newTemp();
    out_ << "  " << h << " = call noalias ptr @malloc(i64 16)\n";
    std::string p0 = newTemp();
    out_ << "  " << p0 << " = bitcast ptr " << h << " to i64*\n";
    out_ << "  store i64 " << tag << ", i64* " << p0 << ", align 8\n";
    std::string pi = newTemp();
    out_ << "  " << pi << " = ptrtoint ptr " << h << " to i64\n";
    return pi;
  }
  if (m->isEnumConst) {
    return std::to_string(m->enumValue);
  }
  if (m->isArrayLength) {
    return std::to_string(m->arraySize);
  }
  if (m->isListLength) {
    std::string obj = genHirExpr(m->object.get());
    std::string tmp = newTemp();
    out_ << "  " << tmp << " = call i64 @hphl_list_len(" << llvmCallOperand(obj, llvmType(m->object->type)) << ")\n";
    return tmp;
  }
  if (m->isMapLength) {
    std::string obj = genHirExpr(m->object.get());
    std::string tmp = newTemp();
    out_ << "  " << tmp << " = call i64 @hphl_map_len(" << llvmCallOperand(obj, llvmType(m->object->type)) << ")\n";
    return tmp;
  }
  if (m->isTaskCancelled) {
    // Task.IsCancelled: flag de cancelamento da tarefa corrente (TLS)
    std::string tmp = newTemp();
    out_ << "  " << tmp << " = call i64 @hphl_task_iscancelled()\n";
    return tmp;
  }
  if (m->isResultIsOk) {
    std::string obj = genHirExpr(m->object.get());
    std::string tag = newTemp();
    out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
    std::string cmp = newTemp();
    out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 0\n";
    std::string res = newTemp();
    out_ << "  " << res << " = zext i1 " << cmp << " to i8\n";
    return res;
  }
  if (m->isResultIsError) {
    std::string obj = genHirExpr(m->object.get());
    std::string tag = newTemp();
    out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
    std::string cmp = newTemp();
    out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 1\n";
    std::string res = newTemp();
    out_ << "  " << res << " = zext i1 " << cmp << " to i8\n";
    return res;
  }
  if (m->isOptionHasValue) {
    std::string obj = genHirExpr(m->object.get());
    std::string tag = newTemp();
    out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
    std::string cmp = newTemp();
    out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 1\n";
    std::string res = newTemp();
    out_ << "  " << res << " = zext i1 " << cmp << " to i8\n";
    return res;
  }
  if (m->isOptionIsNone) {
    std::string obj = genHirExpr(m->object.get());
    std::string tag = newTemp();
    out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
    std::string cmp = newTemp();
    out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 0\n";
    std::string res = newTemp();
    out_ << "  " << res << " = zext i1 " << cmp << " to i8\n";
    return res;
  }
  if (m->isResultValue) {
    std::string obj = genHirExpr(m->object.get());
    std::string tag = newTemp();
    out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
    std::string cmp = newTemp();
    out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 0\n";
    std::string okL = newLabel("res_val.ok");
    std::string failL = newLabel("res_val.fail");
    out_ << "  br i1 " << cmp << ", label %" << okL << ", label %" << failL << "\n";
    out_ << failL << ":\n";
    auto fakeStrLit = std::make_unique<HirStringLit>();
    fakeStrLit->value = "Result.Value called on Err";
    fakeStrLit->type = Type::makeString();
    std::string msgPtr = genHirStringLit(fakeStrLit.get());
    out_ << "  call void @hphl_panic(ptr " << msgPtr << ")\n";
    out_ << "  unreachable\n";
    out_ << okL << ":\n";
    std::string p8 = newTemp();
    out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << obj << ", i64 8\n";
    std::string val = newTemp();
    out_ << "  " << val << " = load " << llvmType(m->type) << ", ptr " << p8 << ", align 8\n";
    return val;
  }
  if (m->isResultError) {
    std::string obj = genHirExpr(m->object.get());
    std::string tag = newTemp();
    out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
    std::string cmp = newTemp();
    out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 1\n";
    std::string okL = newLabel("res_err.ok");
    std::string failL = newLabel("res_err.fail");
    out_ << "  br i1 " << cmp << ", label %" << okL << ", label %" << failL << "\n";
    out_ << failL << ":\n";
    auto fakeStrLit = std::make_unique<HirStringLit>();
    fakeStrLit->value = "Result.Error called on Ok";
    fakeStrLit->type = Type::makeString();
    std::string msgPtr = genHirStringLit(fakeStrLit.get());
    out_ << "  call void @hphl_panic(ptr " << msgPtr << ")\n";
    out_ << "  unreachable\n";
    out_ << okL << ":\n";
    std::string p8 = newTemp();
    out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << obj << ", i64 8\n";
    std::string val = newTemp();
    out_ << "  " << val << " = load " << llvmType(m->type) << ", ptr " << p8 << ", align 8\n";
    return val;
  }
  if (m->isOptionValue) {
    std::string obj = genHirExpr(m->object.get());
    std::string tag = newTemp();
    out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
    std::string cmp = newTemp();
    out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 1\n";
    std::string okL = newLabel("opt_val.ok");
    std::string failL = newLabel("opt_val.fail");
    out_ << "  br i1 " << cmp << ", label %" << okL << ", label %" << failL << "\n";
    out_ << failL << ":\n";
    auto fakeStrLit = std::make_unique<HirStringLit>();
    fakeStrLit->value = "Option.Value called on None";
    fakeStrLit->type = Type::makeString();
    std::string msgPtr = genHirStringLit(fakeStrLit.get());
    out_ << "  call void @hphl_panic(ptr " << msgPtr << ")\n";
    out_ << "  unreachable\n";
    out_ << okL << ":\n";
    std::string p8 = newTemp();
    out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << obj << ", i64 8\n";
    std::string val = newTemp();
    out_ << "  " << val << " = load " << llvmType(m->type) << ", ptr " << p8 << ", align 8\n";
    return val;
  }
if (m->isGlobalRef && m->resolvedGlobal) {
    // Mod.global: global de outro módulo (array → ptr, escalar → load)
    const Type& gt = m->resolvedGlobal->type;
    std::string gname = ".Lg_" + m->resolvedGlobal->name;
    if (tlsOffsets_.count(gname)) {
      // threadlocal de outro módulo: slot da thread corrente
      std::string addr = tlsAddr(gname);
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = load " << llvmType(gt) << ", ptr " << addr << ", align 8\n";
      return tmp;
    }
    std::string gfull = "@" + gname;
    if (gt.kind == Type::Kind::Array) {
      long long total = globalSizeBytes(gt);
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = getelementptr [" << total << " x i8], ["
           << total << " x i8]* " << gfull << ", i64 0, i64 0\n";
      return tmp;
    }
    std::string tmp = newTemp();
    std::string gtSr = llvmType(gt);
    std::string gPtrTy = (gtSr == "ptr") ? "ptr" : (gtSr + "*");
    out_ << "  " << tmp << " = load " << gtSr << ", " << gPtrTy << " " << gfull << ", align 8\n";
    return tmp;
  }
  std::string obj = genHirExpr(m->object.get());
  std::string objType = llvmType(m->object->type);
  int fieldOffset = m->fieldOffset;
  if (fieldOffset < 0) fieldOffset = 0;
  std::string ptrTmp = newTemp();
  out_ << "  " << ptrTmp << " = getelementptr inbounds i8, ptr " << llvmInstrOperand(obj, objType)
       << ", i64 " << fieldOffset << "\n";
  // M10 (v0.45.2): campo-array FIXO é inline no objeto — devolver o ENDEREÇO
  // (como globais acima); carregar como escalar leria um ponteiro falso
  if (m->type.kind == Type::Kind::Array) return ptrTmp;
  std::string valTmp = newTemp();
  std::string memberType = llvmType(m->type);
  out_ << "  " << valTmp << " = load " << memberType << ", ptr " << ptrTmp << ", align 8\n";
  return valTmp;
}

std::string Irgen::genHirIndex(const HirIndex* i) {
  std::string obj = genHirExpr(i->object.get());
  std::string idx = genHirExpr(i->index.get());
  std::string objType = llvmType(i->object->type);
  std::string idx64 = idx;
  if (llvmType(i->index->type) != "i64") {
    idx64 = newTemp();
    out_ << "  " << idx64 << " = sext " << llvmType(i->index->type) << " " << idx << " to i64\n";
  }
  if (!i->isListBuffer && i->object->type.kind == Type::Kind::String) {
    // s[i] — byte stride + OOB devolve 0, via runtime (igual ao x64 —
    // GEP manual usaria stride 8 errado)
    std::string ch64 = newTemp();
    out_ << "  " << ch64 << " = call i64 @hphl_str_char_index(ptr "
         << llvmInstrOperand(obj, "ptr") << ", i64 " << idx64 << ")\n";
    std::string ch = newTemp();
    out_ << "  " << ch << " = trunc i64 " << ch64 << " to i8\n";
    return ch;
  }
  std::string base = obj;
  if (i->object->type.kind == Type::Kind::List) {
    // list dinâmico: valida o índice e resolve o buffer de elementos
    // (HphlList = {count, capacity, items}; indexar o struct direto lê count)
    out_ << "  call void @hphl_list_check(i64 " << idx64 << ", "
         << llvmCallOperand(obj, objType) << ")\n";
    std::string dataTmp = newTemp();
    out_ << "  " << dataTmp << " = call ptr @hphl_list_data("
         << llvmCallOperand(obj, objType) << ")\n";
    base = dataTmp;
  }
  // sub-array (int[2][2]): local/heap = ponteiro (8 bytes); global = linha
  // contígua (packed [N x i8]) — neste caso o resultado é o ENDEREÇO da
  // linha (sem load; o valor só existe como elemento)
  bool isGlobalArr = isGlobalArrayObject(i->object.get());
  long long stride = arrayElementStride(i->object.get(), i->type);
  std::string scaledIdx = newTemp();
  out_ << "  " << scaledIdx << " = mul i64 " << idx64 << ", " << stride << "\n";
  std::string ptrTmp = newTemp();
  out_ << "  " << ptrTmp << " = getelementptr inbounds i8, ptr " << llvmInstrOperand(base, "ptr")
       << ", i64 " << scaledIdx << "\n";
  if (i->type.kind == Type::Kind::Array && isGlobalArr) return ptrTmp;
  std::string valTmp = newTemp();
  std::string elemType = llvmType(i->type);
  out_ << "  " << valTmp << " = load " << elemType << ", ptr " << ptrTmp << ", align 8\n";
  return valTmp;
}

std::string Irgen::genHirArrayLit(const HirArrayLit* a) {
  int count = (int)a->elements.size();
  std::string mallocTmp = newTemp();
  out_ << "  " << mallocTmp << " = call noalias ptr @malloc(i64 " << (count * 8) << ")\n";
  if (count == 0) return mallocTmp;
  std::string tmp = newTemp();
  out_ << "  " << tmp << " = bitcast ptr " << mallocTmp << " to " << llvmType(a->type) << "\n";
  for (int i = 0; i < count; i++) {
    std::string val = genHirExpr(a->elements[i].get());
    std::string elemType = llvmType(a->elements[i]->type);
    std::string gep = newTemp();
    std::string elemPtrTy = (elemType == "ptr") ? "ptr" : (elemType + "*");
    std::string byteOff = newTemp();
    out_ << "  " << byteOff << " = mul i64 " << i << ", 8\n";
    out_ << "  " << gep << " = getelementptr inbounds i8, ptr " << tmp << ", i64 " << byteOff << "\n";
    std::string addr = newTemp();
    out_ << "  " << addr << " = bitcast ptr " << gep << " to " << elemPtrTy << "\n";
    std::string casted = convertForStore(val, a->elements[i]->type, elemType);
    out_ << "  store " << elemType << " " << casted << ", " << elemPtrTy << " " << addr << ", align 8\n";
  }
  return tmp;
}

std::string Irgen::genHirTupleLit(const HirTupleLit* a) {
  // M10.1b: bloco heap de N×8; cada elemento em seu slot
  int count = (int)a->elements.size();
  std::string base = newTemp();
  out_ << "  " << base << " = call noalias ptr @hphl_tuple_new(i64 " << count << ")\n";
  for (int i = 0; i < count; i++) {
    std::string val = genHirExpr(a->elements[i].get());
    Type et = a->elements[i]->type;
    std::string gep = newTemp();
    std::string byteOff = newTemp();
    out_ << "  " << byteOff << " = mul i64 " << i << ", 8\n";
    out_ << "  " << gep << " = getelementptr inbounds i8, ptr " << base
         << ", i64 " << byteOff << "\n";
    if (et.kind == Type::Kind::Float) {
      // double: bitcast para i64 e grava (slot de 8 bytes, como o x64)
      std::string bc = newTemp();
      std::string addr = newTemp();
      out_ << "  " << addr << " = bitcast ptr " << gep << " to ptr\n";
      out_ << "  " << bc << " = bitcast double " << val << " to i64\n";
      out_ << "  store i64 " << bc << ", ptr " << addr << ", align 8\n";
    } else if (isPtrStr(llvmType(et))) {
      std::string addr = newTemp();
      out_ << "  " << addr << " = bitcast ptr " << gep << " to ptr\n";
      out_ << "  store ptr " << val << ", ptr " << addr << ", align 8\n";
    } else {
      std::string v64 = extendToI64(val, et);
      out_ << "  store i64 " << v64 << ", ptr " << gep << ", align 8\n";
    }
  }
  return base;
}

std::string Irgen::genHirCall(const HirCall* c) {
  // v0.46: chamada `compiletime` dobrada na semântica
  if (c->folded) return std::to_string(c->foldValue);
  switch (c->kind) {
    case HirCallKind::Print: {
      std::string lastResult;
      for (auto& a : c->args) {
        Type t = a->type;
        lastResult = genHirExpr(a.get());
        std::string argType = llvmType(t);
        if (t.kind == Type::Kind::Float) {
          // extend float to double for runtime
          if (argType == "float") {
            std::string extTmp = newTemp();
            out_ << "  " << extTmp << " = fpext " << argType << " " << lastResult << " to double\n";
            lastResult = extTmp;
            argType = "double";
          }
          out_ << "  call void @hphl_print_float(" << llvmCallOperand(lastResult, argType) << ")\n";
        } else if (t.kind == Type::Kind::String) {
          // M5 (v0.36.0): libera apenas strings na heap — concat (Binary) e
          // ToStr. Chamadas que retornam string podem devolver literal
          // estático; free nelas corrompe a heap.
          out_ << "  call void @hphl_print_string(" << llvmCallOperand(lastResult, argType) << ")\n";
          bool fresh =
              a->kind == HirExprKind::Binary ||
              (a->kind == HirExprKind::Call &&
               static_cast<HirCall*>(a.get())->kind == HirCallKind::ToStr);
          if (fresh) {
            out_ << "  call void @hphl_str_free(" << llvmCallOperand(lastResult, argType) << ")\n";
          }
        } else if (t.kind == Type::Kind::Bool) {
          std::string zArg = lastResult;
          std::string zTy = argType;
          if (exprProducesI1(a.get())) {
            std::string extTmp = newTemp();
            out_ << "  " << extTmp << " = zext i1 " << lastResult << " to i8\n";
            zArg = extTmp;
            zTy = "i8";
          }
          std::string z64 = newTemp();
          out_ << "  " << z64 << " = zext " << zTy << " " << zArg << " to i64\n";
          out_ << "  call void @hphl_print_bool(i64 " << z64 << ")\n";
        } else if (t.kind == Type::Kind::Char) {
          std::string z64 = newTemp();
          out_ << "  " << z64 << " = zext " << argType << " " << lastResult << " to i64\n";
          out_ << "  call void @hphl_print_char(i64 " << z64 << ")\n";
        } else if (t.kind == Type::Kind::UInt) {
          // int<128>+ estreita com trunc (extendToI64 decide sext/zext/trunc)
          lastResult = extendToI64(lastResult, t);
          argType = "i64";
          out_ << "  call void @hphl_print_uint(" << llvmCallOperand(lastResult, argType) << ")\n";
        } else {
          // int<128>+ estreita com trunc (extendToI64 decide sext/zext/trunc)
          lastResult = extendToI64(lastResult, t);
          argType = "i64";
          out_ << "  call void @hphl_print_int(" << llvmCallOperand(lastResult, argType) << ")\n";
        }
      }
      return lastResult;
    }
    case HirCallKind::ClockNs: {
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call i64 @hphl_clock_ns()\n";
      return tmp;
    }
    case HirCallKind::Sqrt: {
      // M11-bench: sqrt(x) via libm — arg convertido para double
      std::string v = genHirExpr(c->args[0].get());
      std::string dv = convertForStore(v, actualTypeOf(c->args[0].get()), "double");
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call double @sqrt(double " << dv << ")\n";
      return tmp;
    }
    case HirCallKind::ArenaReset: {
      // M11.9: no backend LLVM não há frame-arena (objetos de política
      // 'arena' vivem no heap comum) — o reset é um no-op seguro
      out_ << "  call void @hphl_arena_reset(ptr null)\n";
      return "";
    }    case HirCallKind::StrEq:
    case HirCallKind::StrCmp: {
      std::string a = genHirExpr(c->args[0].get());
      std::string b = genHirExpr(c->args[1].get());
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call i64 @"
           << (c->kind == HirCallKind::StrEq ? "hphl_str_eq" : "hphl_str_cmp")
           << "(" << llvmCallOperand(a, llvmType(c->args[0]->type)) << ", "
           << llvmCallOperand(b, llvmType(c->args[1]->type)) << ")\n";
      return castPayloadFromI64(tmp, c->type);
    }
    case HirCallKind::ToStr: {
      // M5: toString(x)/__hphl_to_str(x) — escalar → string heap
      Type at = c->args[0]->type;
      std::string v = genHirExpr(c->args[0].get());
      std::string arg = llvmCallOperand(v, llvmType(at));
      std::string tmp = newTemp();
      if (at.kind == Type::Kind::Bool) {
        std::string bArg = v;
        std::string bTy = llvmType(at);
        if (exprProducesI1(c->args[0].get())) {
          std::string e = newTemp();
          out_ << "  " << e << " = zext i1 " << bArg << " to i8\n";
          bArg = e;
          bTy = "i8";
        }
        std::string z64 = newTemp();
        out_ << "  " << z64 << " = zext " << bTy << " " << bArg << " to i64\n";
        out_ << "  " << tmp << " = call ptr @hphl_str_from_bool(i64 " << z64 << ")\n";
      } else if (at.kind == Type::Kind::Char) {
        // v eh i8 puro (literal "34" ou SSA); arg ja vem tipado ("i8 ...")
        std::string z64 = newTemp();
        out_ << "  " << z64 << " = zext i8 " << v << " to i64\n";
        out_ << "  " << tmp << " = call ptr @hphl_str_from_char(i64 " << z64 << ")\n";
      } else if (at.kind == Type::Kind::Float) {
        // f32 precisa fpext p/ double; f64 já é double (arg já vem "double v")
        if (at.bits < 64) {
          std::string d = newTemp();
          out_ << "  " << d << " = fpext float " << arg << " to double\n";
          out_ << "  " << tmp << " = call ptr @hphl_str_from_float(double " << d << ")\n";
        } else {
          out_ << "  " << tmp << " = call ptr @hphl_str_from_float(" << arg << ")\n";
        }
      } else if (at.kind == Type::Kind::UInt) {
        std::string v64 = extendToI64(v, at);
        out_ << "  " << tmp << " = call ptr @hphl_str_from_uint(i64 " << v64 << ")\n";
      } else {
        std::string v64 = extendToI64(v, at);
        out_ << "  " << tmp << " = call ptr @hphl_str_from_int(i64 " << v64 << ")\n";
      }
      return tmp;
    }
    case HirCallKind::AddrOf: {
      // FFI v2: addr_of(x) — endereço do lvalue como `ptr` (void*)
      return genLValueAddress(c->args[0].get());
    }
    case HirCallKind::ListAdd: {
      // list.Add(v): elemento em 8 bytes (float → double, escalar → i64)
      Type et = *c->args[0]->type.elem;
      std::string list = genHirExpr(c->args[0].get());
      std::string v = genHirExpr(c->args[1].get());
      if (et.kind == Type::Kind::Float) {
        std::string arg = convertForStore(v, actualTypeOf(c->args[1].get()), "double");
        out_ << "  call void @hphl_list_add_f(" << llvmCallOperand(list, "ptr")
             << ", double " << arg << ")\n";
      } else {
        // extendToI64 emite instrução: materializar ANTES de escrever a call
        std::string v64 = extendToI64(v, et);
        out_ << "  call void @hphl_list_add_i(" << llvmCallOperand(list, "ptr")
             << ", i64 " << v64 << ")\n";
      }
      return "";
    }
    case HirCallKind::MapPut: {
      std::string m = genHirExpr(c->args[0].get());
      // chave/valor: ponteiro → i64 (ptrtoint), float → double bits, resto → i64
      std::string kraw = genHirExpr(c->args[1].get());
      std::string k;
      if (isPtrStr(llvmType(*c->args[0]->type.elem))) {
        std::string pi = newTemp();
        out_ << "  " << pi << " = ptrtoint ptr " << kraw << " to i64\n";
        k = pi;
      } else {
        k = extendToI64(kraw, *c->args[0]->type.elem);
      }
      std::string vraw = genHirExpr(c->args[2].get());
      std::string v;
      Type vt = *c->args[0]->type.elem2;
      if (vt.kind == Type::Kind::Float) {
        // double bits → i64 (bitcast)
        std::string bc = newTemp();
        out_ << "  " << bc << " = bitcast " << llvmType(vt) << " " << vraw << " to i64\n";
        v = bc;
      } else if (isPtrStr(llvmType(vt))) {
        std::string pi = newTemp();
        out_ << "  " << pi << " = ptrtoint ptr " << vraw << " to i64\n";
        v = pi;
      } else {
        v = extendToI64(vraw, vt);
      }
      out_ << "  call void @hphl_map_put(" << llvmCallOperand(m,"ptr") << ", i64 " << k << ", i64 " << v << ")\n";
      return "";
    }
    case HirCallKind::MapGet: {
      std::string m = genHirExpr(c->args[0].get());
      std::string k = extendToI64(genHirExpr(c->args[1].get()), *c->args[0]->type.elem);
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call i64 @hphl_map_get(" << llvmCallOperand(m,"ptr") << ", i64 " << k << ")\n";
      if (isPtrStr(llvmType(c->type))) {
        // valor ponteiro (string/classe): inttoptr
        std::string pc = newTemp();
        out_ << "  " << pc << " = inttoptr i64 " << tmp << " to ptr\n";
        return pc;
      }
      if (c->type.kind == Type::Kind::Float) {
        std::string fc = newTemp();
        out_ << "  " << fc << " = bitcast i64 " << tmp << " to double\n";
        return fc;
      }
      return tmp;
    }
    case HirCallKind::MapContains: {
      std::string m = genHirExpr(c->args[0].get());
      std::string k = extendToI64(genHirExpr(c->args[1].get()), *c->args[0]->type.elem);
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call i64 @hphl_map_contains(" << llvmCallOperand(m,"ptr") << ", i64 " << k << ")\n";
      // o tipo da expressão é Bool (i8): trunca para o consumidor
      std::string b = newTemp();
      out_ << "  " << b << " = trunc i64 " << tmp << " to i8\n";
      return b;
    }
    case HirCallKind::MapRemove: {
      std::string m = genHirExpr(c->args[0].get());
      std::string k = extendToI64(genHirExpr(c->args[1].get()), *c->args[0]->type.elem);
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call i64 @hphl_map_remove(" << llvmCallOperand(m,"ptr") << ", i64 " << k << ")\n";
      std::string b = newTemp();
      out_ << "  " << b << " = trunc i64 " << tmp << " to i8\n";
      return b;
    }
    case HirCallKind::MapClear: {
      std::string m = genHirExpr(c->args[0].get());
      out_ << "  call void @hphl_map_clear(" << llvmCallOperand(m,"ptr") << ")\n";
      return "";
    }
    case HirCallKind::Wait: {
      // t.Wait(): espera a conclusão e devolve o payload (task.remove)
      std::string h = genHirExpr(c->args[0].get());
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call i64 @hphl_wait_task("
           << llvmCallOperand(h, "ptr") << ")\n";
      return castPayloadFromI64(tmp, c->type);
    }
    case HirCallKind::TaskCancel: {
      // t.Cancel(): sinal cooperativo de cancelamento (não preempta)
      std::string h = genHirExpr(c->args[0].get());
      out_ << "  call void @hphl_cancel_task(" << llvmCallOperand(h, "ptr") << ")\n";
      return "";
    }
    case HirCallKind::ChannelSend: {
      // ch.Send(x): payload em 8 bytes (float passa os bits)
      std::string ch = genHirExpr(c->args[0].get());
      std::string v = genHirExpr(c->args[1].get());
      std::string v64;
      const Type& vt = c->args[1]->type;
      if (vt.kind == Type::Kind::Float) {
        v64 = newTemp();
        out_ << "  " << v64 << " = bitcast double " << v << " to i64\n";
      } else if (isPointerType(vt)) {
        v64 = newTemp();
        out_ << "  " << v64 << " = ptrtoint " << llvmType(vt) << " " << v
             << " to i64\n";
      } else {
        v64 = extendToI64(v, vt);
      }
      out_ << "  call void @hphl_channel_send(" << llvmCallOperand(ch, "ptr")
           << ", i64 " << v64 << ")\n";
      return "";
    }
    case HirCallKind::ChannelReceive: {
      // ch.Receive(): bloqueia até haver valor; devolve o payload
      std::string ch = genHirExpr(c->args[0].get());
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call i64 @hphl_channel_receive("
           << llvmCallOperand(ch, "ptr") << ")\n";
      return castPayloadFromI64(tmp, c->type);
    }
    case HirCallKind::ResultIsOk: {
      std::string obj = genHirExpr(c->args[0].get());
      std::string tag = newTemp();
      out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
      std::string cmp = newTemp();
      out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 0\n";
      std::string res = newTemp();
      out_ << "  " << res << " = zext i1 " << cmp << " to i8\n";
      return res;
    }
    case HirCallKind::ResultIsErr: {
      std::string obj = genHirExpr(c->args[0].get());
      std::string tag = newTemp();
      out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
      std::string cmp = newTemp();
      out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 1\n";
      std::string res = newTemp();
      out_ << "  " << res << " = zext i1 " << cmp << " to i8\n";
      return res;
    }
    case HirCallKind::OptionIsSome: {
      std::string obj = genHirExpr(c->args[0].get());
      std::string tag = newTemp();
      out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
      std::string cmp = newTemp();
      out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 1\n";
      std::string res = newTemp();
      out_ << "  " << res << " = zext i1 " << cmp << " to i8\n";
      return res;
    }
    case HirCallKind::OptionIsNone: {
      std::string obj = genHirExpr(c->args[0].get());
      std::string tag = newTemp();
      out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
      std::string cmp = newTemp();
      out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 0\n";
      std::string res = newTemp();
      out_ << "  " << res << " = zext i1 " << cmp << " to i8\n";
      return res;
    }
    case HirCallKind::ResultUnwrap: {
      std::string obj = genHirExpr(c->args[0].get());
      std::string tag = newTemp();
      out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
      std::string cmp = newTemp();
      out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 0\n";
      std::string okL = newLabel("res_unw.ok");
      std::string failL = newLabel("res_unw.fail");
      out_ << "  br i1 " << cmp << ", label %" << okL << ", label %" << failL << "\n";
      out_ << failL << ":\n";
      auto fakeStrLit = std::make_unique<HirStringLit>();
      fakeStrLit->value = "unwrap called on Err";
      fakeStrLit->type = Type::makeString();
      std::string msgPtr = genHirStringLit(fakeStrLit.get());
      out_ << "  call void @hphl_panic(ptr " << msgPtr << ")\n";
      out_ << "  unreachable\n";
      out_ << okL << ":\n";
      std::string p8 = newTemp();
      out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << obj << ", i64 8\n";
      std::string val = newTemp();
      out_ << "  " << val << " = load " << llvmType(c->type) << ", ptr " << p8 << ", align 8\n";
      return val;
    }
    case HirCallKind::OptionUnwrap: {
      std::string obj = genHirExpr(c->args[0].get());
      std::string tag = newTemp();
      out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
      std::string cmp = newTemp();
      out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 1\n";
      std::string okL = newLabel("opt_unw.ok");
      std::string failL = newLabel("opt_unw.fail");
      out_ << "  br i1 " << cmp << ", label %" << okL << ", label %" << failL << "\n";
      out_ << failL << ":\n";
      auto fakeStrLit = std::make_unique<HirStringLit>();
      fakeStrLit->value = "unwrap called on None";
      fakeStrLit->type = Type::makeString();
      std::string msgPtr = genHirStringLit(fakeStrLit.get());
      out_ << "  call void @hphl_panic(ptr " << msgPtr << ")\n";
      out_ << "  unreachable\n";
      out_ << okL << ":\n";
      std::string p8 = newTemp();
      out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << obj << ", i64 8\n";
      std::string val = newTemp();
      out_ << "  " << val << " = load " << llvmType(c->type) << ", ptr " << p8 << ", align 8\n";
      return val;
    }
    case HirCallKind::ResultUnwrapErr: {
      std::string obj = genHirExpr(c->args[0].get());
      std::string tag = newTemp();
      out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
      std::string cmp = newTemp();
      out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 1\n";
      std::string okL = newLabel("res_unw_err.ok");
      std::string failL = newLabel("res_unw_err.fail");
      out_ << "  br i1 " << cmp << ", label %" << okL << ", label %" << failL << "\n";
      out_ << failL << ":\n";
      auto fakeStrLit = std::make_unique<HirStringLit>();
      fakeStrLit->value = "unwrap_err called on Ok";
      fakeStrLit->type = Type::makeString();
      std::string msgPtr = genHirStringLit(fakeStrLit.get());
      out_ << "  call void @hphl_panic(ptr " << msgPtr << ")\n";
      out_ << "  unreachable\n";
      out_ << okL << ":\n";
      std::string p8 = newTemp();
      out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << obj << ", i64 8\n";
      std::string val = newTemp();
      out_ << "  " << val << " = load " << llvmType(c->type) << ", ptr " << p8 << ", align 8\n";
      return val;
    }
    case HirCallKind::ResultUnwrapOr: {
      std::string obj = genHirExpr(c->args[0].get());
      std::string tag = newTemp();
      out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
      std::string cmp = newTemp();
      out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 0\n";
      std::string okL = newLabel("res_unw_or.ok");
      std::string elseL = newLabel("res_unw_or.else");
      std::string endL = newLabel("res_unw_or.end");
      std::string resSlot = emitAllocaSlot(newLabel("__res_unw_or"), c->type);
      std::string ty = llvmType(c->type);
      out_ << "  br i1 " << cmp << ", label %" << okL << ", label %" << elseL << "\n";
      out_ << okL << ":\n";
      std::string p8 = newTemp();
      out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << obj << ", i64 8\n";
      std::string okVal = newTemp();
      out_ << "  " << okVal << " = load " << ty << ", ptr " << p8 << ", align 8\n";
      out_ << "  store " << ty << " " << okVal << ", ptr " << resSlot << ", align 8\n";
      out_ << "  br label %" << endL << "\n";
      out_ << elseL << ":\n";
      std::string elseVal = genHirExpr(c->args[1].get());
      std::string converted = convertForStore(elseVal, actualTypeOf(c->args[1].get()), ty);
      out_ << "  store " << ty << " " << converted << ", ptr " << resSlot << ", align 8\n";
      out_ << "  br label %" << endL << "\n";
      out_ << endL << ":\n";
      std::string resVal = newTemp();
      out_ << "  " << resVal << " = load " << ty << ", ptr " << resSlot << ", align 8\n";
      return resVal;
    }
    case HirCallKind::OptionUnwrapOr: {
      std::string obj = genHirExpr(c->args[0].get());
      std::string tag = newTemp();
      out_ << "  " << tag << " = load i64, ptr " << obj << ", align 8\n";
      std::string cmp = newTemp();
      out_ << "  " << cmp << " = icmp eq i64 " << tag << ", 1\n";
      std::string okL = newLabel("opt_unw_or.ok");
      std::string elseL = newLabel("opt_unw_or.else");
      std::string endL = newLabel("opt_unw_or.end");
      std::string resSlot = emitAllocaSlot(newLabel("__opt_unw_or"), c->type);
      std::string ty = llvmType(c->type);
      out_ << "  br i1 " << cmp << ", label %" << okL << ", label %" << elseL << "\n";
      out_ << okL << ":\n";
      std::string p8 = newTemp();
      out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << obj << ", i64 8\n";
      std::string okVal = newTemp();
      out_ << "  " << okVal << " = load " << ty << ", ptr " << p8 << ", align 8\n";
      out_ << "  store " << ty << " " << okVal << ", ptr " << resSlot << ", align 8\n";
      out_ << "  br label %" << endL << "\n";
      out_ << elseL << ":\n";
      std::string elseVal = genHirExpr(c->args[1].get());
      std::string converted = convertForStore(elseVal, actualTypeOf(c->args[1].get()), ty);
      out_ << "  store " << ty << " " << converted << ", ptr " << resSlot << ", align 8\n";
      out_ << "  br label %" << endL << "\n";
      out_ << endL << ":\n";
      std::string resVal = newTemp();
      out_ << "  " << resVal << " = load " << ty << ", ptr " << resSlot << ", align 8\n";
      return resVal;
    }
    case HirCallKind::PrimOp: {
      // mutex/semaphore/event/barrier: lock/wait/signal/reset — um argumento
      if (c->args.empty()) return "";
      std::string obj = genHirExpr(c->args[0].get());
      const char* fn = "hphl_mutex_lock";
      switch ((CallExpr::PrimOp)c->primOp) {
        case CallExpr::PrimOp::MutexLock:     fn = "hphl_mutex_lock"; break;
        case CallExpr::PrimOp::MutexUnlock:   fn = "hphl_mutex_unlock"; break;
        case CallExpr::PrimOp::SemaphoreWait: fn = "hphl_semaphore_wait"; break;
        case CallExpr::PrimOp::SemaphoreSignal: fn = "hphl_semaphore_signal"; break;
        case CallExpr::PrimOp::EventWait:     fn = "hphl_event_wait"; break;
        case CallExpr::PrimOp::EventSet:      fn = "hphl_event_set"; break;
        case CallExpr::PrimOp::EventReset:    fn = "hphl_event_reset"; break;
        case CallExpr::PrimOp::BarrierWait:   fn = "hphl_barrier_wait"; break;
        default: break;
      }
      out_ << "  call void @" << fn << "(" << llvmCallOperand(obj, "ptr") << ")\n";
      return "";
    }
    case HirCallKind::Runtime: {
      // M12.0: builtin da std? chamada tipada pela tabela (doubles reais)
      const StdBuiltin* sb = nullptr;
      for (auto& b : stdBuiltinTable())
        if (c->runtimeName == b.symbol &&
            b.params.size() == c->args.size()) {
          sb = &b;
          break;
        }
      if (sb) {
        std::string args;
        for (size_t i = 0; i < c->args.size(); i++) {
          std::string v = genHirExpr(c->args[i].get());
          if (!args.empty()) args += ", ";
          switch (sb->params[i]) {
            case SBType::Float: {
              std::string dv =
                  convertForStore(v, actualTypeOf(c->args[i].get()), "double");
              args += "double " + dv;
              break;
            }
            case SBType::Str:
            case SBType::List:
              args += "ptr " + v;
              break;
            default:
              args += "i64 " + extendToI64(v, c->args[i]->type);
              break;
          }
        }
        if (sb->ret == SBType::Float) {
          std::string tmp = newTemp();
          out_ << "  " << tmp << " = call double @" << sb->symbol << " ("
               << args << ")\n";
          return tmp;
        }
        if (sb->ret == SBType::Str || sb->ret == SBType::List) {
          std::string tmp = newTemp();
          out_ << "  " << tmp << " = call ptr @" << sb->symbol << " ("
               << args << ")\n";
          return tmp;
        }
        std::string tmp = newTemp();
        out_ << "  " << tmp << " = call i64 @" << sb->symbol << " (" << args
             << ")\n";
        return castPayloadFromI64(tmp, c->type);
      }
      // chamadas de runtime do lowering (list_len, list_data, match_fail, ...):
      // argumentos em 8 bytes; retorno conforme o nome conhecido
      std::string args;
      for (auto& a : c->args) {
        std::string v = genHirExpr(a.get());
        if (!args.empty()) args += ", ";
        if (isPointerType(a->type)) {
          std::string p = newTemp();
          out_ << "  " << p << " = bitcast " << llvmType(a->type) << " " << v
               << " to ptr\n";
          args += "ptr " + p;
        } else {
          args += "i64 " + extendToI64(v, a->type);
        }
      }
      const std::string& name = c->runtimeName;
      if (name == "hphl_match_fail") {
        out_ << "  call void @" << name << "()\n";
        return "";
      }
      if (name == "hphl_list_data" || name == "hphl_list_slice") {
        std::string tmp = newTemp();
        out_ << "  " << tmp << " = call ptr @" << name << "(" << args << ")\n";
        return tmp;
      }
      std::string tmp = newTemp();
      out_ << "  " << tmp << " = call i64 @" << name << "(" << args << ")\n";
      return castPayloadFromI64(tmp, c->type);
    }
    case HirCallKind::Async: {
      // F(...) async: env com os parâmetros (por valor) + tarefa sintética
      // wrapper que lê o env, chama F e grava o payload em res
      spawnedAny_ = true;
      std::string env;
      if (!c->args.empty()) {
        env = newTemp();
        out_ << "  " << env << " = call noalias ptr @malloc(i64 "
             << (c->args.size() * 8) << ")\n";
        for (size_t i = 0; i < c->args.size(); i++) {
          std::string v = genHirExpr(c->args[i].get());
          std::string slot = newTemp();
          out_ << "  " << slot << " = getelementptr inbounds i8, ptr " << env
               << ", i64 " << (i * 8) << "\n";
          if (isPointerType(c->args[i]->type)) {
            std::string sp = newTemp();
            out_ << "  " << sp << " = bitcast ptr " << slot << " to ptr\n";
            std::string p = newTemp();
            out_ << "  " << p << " = bitcast " << llvmType(c->args[i]->type)
                 << " " << v << " to ptr\n";
            out_ << "  store ptr " << p << ", ptr " << sp << ", align 8\n";
          } else {
            std::string s64 = newTemp();
            out_ << "  " << s64 << " = bitcast ptr " << slot << " to i64*\n";
            std::string v64 = extendToI64(v, c->args[i]->type);
            out_ << "  store i64 " << v64 << ", i64* " << s64 << ", align 8\n";
          }
        }
      } else {
        env = "null";
      }
      std::string wrapper = makeTaskLabel();
      // corpo do wrapper adiado: precisa do resolved + params (tipo do retorno)
      asyncWrappers_.push_back({wrapper, c->resolved});
      std::string h = newTemp();
      out_ << "  " << h << " = call ptr @hphl_spawn_task_ex(void (ptr, ptr)* "
           << wrapper << ", ptr " << env << ")\n";
      return h;
    }
    case HirCallKind::EnumCtor: {
      // variante de enum com dados: aloca célula (tag i64 em +0, payload em +8..+8*n)
      std::string h = newTemp();
      out_ << "  " << h << " = call noalias ptr @malloc(i64 16)\n";
      std::string p0 = newTemp();
      out_ << "  " << p0 << " = bitcast ptr " << h << " to i64*\n";
      out_ << "  store i64 " << c->index << ", i64* " << p0 << ", align 8\n";
      for (size_t i = 0; i < c->args.size(); i++) {
        std::string val = genHirExpr(c->args[i].get());
        std::string argTy = llvmType(c->args[i]->type);
        long long offset = 8LL * (i + 1);
        std::string p8 = newTemp();
        out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << h << ", i64 " << offset << "\n";
        if (argTy == "float") {
          std::string ext = newTemp();
          out_ << "  " << ext << " = fpext float " << val << " to double\n";
          out_ << "  store double " << ext << ", ptr " << p8 << ", align 8\n";
        } else {
          out_ << "  store " << argTy << " " << val << ", ptr " << p8 << ", align 8\n";
        }
      }
      std::string pi = newTemp();
      out_ << "  " << pi << " = ptrtoint ptr " << h << " to i64\n";
      return pi;
    }
    case HirCallKind::Normal:
    case HirCallKind::ModuleCall: {
      std::string fname = fnLabel(c->resolved);
      // métodos de instância: o receiver (this) é args[0] e vira o PRIMEIRO
      // argumento da chamada (parâmetro %.this na função)
      bool isMethod = c->resolved && c->resolved->isMethod && !c->resolved->isStatic;
      std::string args;
      size_t start = 0;
      std::string thisVal; // M10: receiver para dispatch virtual
      Type thisTyV = Type::makeVoid();
      if (isMethod) {
        std::string thisArg = genHirExpr(c->args[0].get());
        thisVal = thisArg;
        thisTyV = c->args[0]->type;
        args += llvmCallOperand(thisArg, llvmType(c->args[0]->type));
        start = 1;
      }
      for (size_t i = start; i < c->args.size(); i++) {
        size_t p = isMethod ? i - 1 : i;
        bool argIsRef = c->resolved && p < c->resolved->params.size() &&
                        c->resolved->params[p] &&
                        (c->resolved->params[p]->byRef || c->resolved->params[p]->byOut);
        std::string arg;
        if (argIsRef) {
          arg = genLValueAddress(c->args[i].get());
        } else {
          arg = genHirExpr(c->args[i].get());
          // struct por valor: cópia independente na callee
          if (c->resolved && p < c->resolved->params.size() &&
              isStructType(c->resolved->params[p]->type))
            arg = genStructCopy(c->resolved->params[p]->type, arg);
          // FFI v2 fix: arg int p/ param float — converte p/ o tipo do param
          if (c->resolved && p < c->resolved->params.size() &&
              c->resolved->params[p]->type.kind == Type::Kind::Float &&
              c->args[i]->type.kind != Type::Kind::Float)
            arg = convertForStore(arg, c->args[i]->type,
                                  llvmType(c->resolved->params[p]->type));
        }
        if (!args.empty()) args += ", ";
        // use the parameter type from the function signature for type matching
        // (params de função não incluem o receiver → ajuste de índice)
        std::string argType;
        if (c->resolved && p < c->resolved->params.size()) {
          argType = llvmType(c->resolved->params[p]->type);
        } else {
          argType = llvmType(c->args[i]->type);
        }
        args += llvmCallOperand(arg, argIsRef ? ptrTo(argType) : argType);
      }
      if (c->type.kind == Type::Kind::Void) {
        // M10 (v0.44): dispatch virtual — slot com override em alguma derivada
        // A2 (interface como tipo): receiver interface SEMPRE via vtable
        bool recvIface = isInterfaceType(thisTyV);
        bool virt = isMethod && !c->isBaseCall &&
                    ((thisTyV.kind == Type::Kind::Class && !isStructType(thisTyV)) || recvIface);
        int vslot = -1;
        if (virt) {
          vslot = sem_.vslotOf(c->resolved->name, c->resolved->params.size());
          if (vslot < 0 || (!recvIface && !sem_.slotOverridden(vslot))) virt = false;
        }
        if (virt) {
          std::string vt = newTemp();
          out_ << "  " << vt << " = load ptr, ptr " << thisVal << ", align 8\n";
          std::string slotp = newTemp();
          // M10.5c: slot da vtable é PTR (stride do alvo), não i64 — o global
          // é `[N x ptr]`; no wasm32 o elemento tem 4 bytes e o acesso como
          // i64 lia fora dos limites (call_indirect com índice lixo)
          out_ << "  " << slotp << " = getelementptr inbounds ptr, ptr " << vt
               << ", i64 " << vslot << "\n";
          std::string fp = newTemp();
          out_ << "  " << fp << " = load ptr, ptr " << slotp << ", align 8\n";
          out_ << "  call void " << fp << "(" << args << ")\n";
          if (wasmEh_) emitWasmExcGuard();
        } else {
          out_ << "  call void " << fname << "(" << args << ")\n";
          if (wasmEh_) emitWasmExcGuard();
        }
        return "";
      } else {
        // M10: dispatch virtual também para chamadas com retorno
        // A2 (interface como tipo): receiver interface SEMPRE via vtable
        bool recvIface = isInterfaceType(thisTyV);
        bool virt = isMethod && !c->isBaseCall &&
                    ((thisTyV.kind == Type::Kind::Class && !isStructType(thisTyV)) || recvIface);
        int vslot = -1;
        if (virt) {
          vslot = sem_.vslotOf(c->resolved->name, c->resolved->params.size());
          if (vslot < 0 || (!recvIface && !sem_.slotOverridden(vslot))) virt = false;
        }
        std::string tmp = newTemp();
        // M10.5: o tipo de RETORNO vem sempre da função resolvida — o tipo
        // da expressão pode divergir (ex.: `p?.V()` tem tipo Option<int>,
        // mas V retorna int); no wasm32 ptr≠i64 e a divergência quebra o
        // backend (chamadas desviadas para stubs "_bitcast_invalid")
        std::string retTy =
            (c->resolved && c->resolved->returnType.kind != Type::Kind::Void)
                ? llvmReturnType(c->resolved->returnType)
                : llvmReturnType(c->type);
        if (virt) {
          std::string vt = newTemp();
          out_ << "  " << vt << " = load ptr, ptr " << thisVal << ", align 8\n";
          std::string slotp = newTemp();
          // M10.5c: slot PTR (ver nota no dispatch void acima)
          out_ << "  " << slotp << " = getelementptr inbounds ptr, ptr " << vt
               << ", i64 " << vslot << "\n";
          std::string fp = newTemp();
          out_ << "  " << fp << " = load ptr, ptr " << slotp << ", align 8\n";
          out_ << "  " << tmp << " = call " << retTy << " "
               << fp << "(" << args << ")\n";
          if (wasmEh_) emitWasmExcGuard();
        } else {
          out_ << "  " << tmp << " = call " << retTy << " " << fname << "(" << args << ")\n";
          if (wasmEh_) emitWasmExcGuard();
        }
        return tmp;
      }
    }
    case HirCallKind::ActorCall: {
      // chamada EXTERNA a método de actor: lock_begin(this), call, lock_end(this)
      std::string thisObj = genHirExpr(c->args[0].get());
      std::string thisTy = llvmType(c->args[0]->type);
      // bitcast class ptr → ptr for lock
      std::string lockH = newTemp();
      out_ << "  " << lockH << " = bitcast " << thisTy << " " << thisObj << " to ptr\n";
      out_ << "  call void @hphl_lock_begin(ptr " << lockH << ")\n";
      // call method (args[0] = this, args[1..] = method args)
      std::string fname = fnLabel(c->resolved);
      bool isMethod = c->resolved && c->resolved->isMethod && !c->resolved->isStatic;
      std::string callArgs;
      size_t start = 0;
      if (isMethod) {
        callArgs += llvmCallOperand(thisObj, llvmType(c->args[0]->type));
        start = 1;
      }
      for (size_t i = start; i < c->args.size(); i++) {
        std::string arg = genHirExpr(c->args[i].get());
        // struct por valor: cópia independente na callee (params não incluem this)
        size_t pp = isMethod ? i - 1 : i;
        if (c->resolved && pp < c->resolved->params.size() &&
            isStructType(c->resolved->params[pp]->type))
          arg = genStructCopy(c->resolved->params[pp]->type, arg);
        if (!callArgs.empty()) callArgs += ", ";
        callArgs += llvmCallOperand(arg, llvmType(c->args[i]->type));
      }
      std::string retTy = llvmReturnType(c->type);
      std::string callH;
      if (retTy != "void") {
        callH = newTemp();
        out_ << "  " << callH << " = call " << retTy << " " << fname << "(" << callArgs << ")\n";
      } else {
        out_ << "  call void " << fname << "(" << callArgs << ")\n";
      }
      // lock_end
      out_ << "  call void @hphl_lock_end(ptr " << lockH << ")\n";
      if (retTy != "void") return callH;
      return "";
    }
    case HirCallKind::FromInt: {
      // Enum.FromInt(n): Option<Enum> — célula malloc(16): tag i64 em +0, payload em +8
      // (mesmo layout do EnumCtor e do match Some/None)
      std::string val = genHirExpr(c->args[0].get());
      std::string valTy = llvmType(c->args[0]->type);
      if (valTy != "i64") {
        std::string ext = newTemp();
        out_ << "  " << ext << " = sext " << valTy << " " << val << " to i64\n";
        val = ext;
      }
      std::string h = newTemp();
      out_ << "  " << h << " = call noalias ptr @malloc(i64 16)\n";
      const EnumDecl* en = sem_.enums().at(c->enumCanon);
      std::string tag = "0";
      for (auto& e : en->entries) {
        std::string cmp = newTemp();
        std::string sel = newTemp();
        out_ << "  " << cmp << " = icmp eq i64 " << val << ", " << e.value << "\n";
        out_ << "  " << sel << " = select i1 " << cmp << ", i64 1, i64 " << tag << "\n";
        tag = sel;
      }
      out_ << "  store i64 " << tag << ", ptr " << h << ", align 8\n";
      std::string p8 = newTemp();
      out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << h << ", i64 8\n";
      out_ << "  store i64 " << val << ", ptr " << p8 << ", align 8\n";
      return h;
    }
    case HirCallKind::Serialize: {
      // e.Serialize(): valor inteiro da variante; enum rico: tag em [0] da célula
      std::string eobj = genHirExpr(c->args[0].get());
      if (sem_.isRichEnum(c->enumCanon)) {
        std::string t = newTemp();
        out_ << "  " << t << " = load i64, ptr " << eobj << ", align 8\n";
        return t;
      }
      return eobj;
    }
    case HirCallKind::Indirect:
      return genHirIndirectCall(c);
    default:
      return "0";
  }
}

std::string Irgen::genHirBinary(const HirBinary* b) {
  std::string lhs = genHirExpr(b->lhs.get());
  std::string rhs = genHirExpr(b->rhs.get());
  Type t = b->type;
  Type lhsT = b->lhs->type;
  Type rhsT = b->rhs->type;

  // comparison ops return bool
  if (t.kind == Type::Kind::Bool) {
    // M14.4: ==/!= entre strings por CONTEÚDO (hphl_str_eq)
    if ((b->op == BinOp::Eq || b->op == BinOp::Ne) && b->strEqBin) {
      std::string aop = llvmCallOperand(lhs, llvmType(b->lhs->type));
      std::string bop = llvmCallOperand(rhs, llvmType(b->rhs->type));
      std::string r = newTemp();
      out_ << "  " << r << " = call i64 @hphl_str_eq(" << aop << ", " << bop
           << ")\n";
      std::string i1r = newTemp();
      out_ << "  " << i1r << " = icmp ne i64 " << r << ", 0\n";
      if (b->op == BinOp::Ne) {
        std::string inv = newTemp();
        out_ << "  " << inv << " = xor i1 " << i1r << ", true\n";
        return inv;
      }
      return i1r;
    }
    // derived Eq/Ne: call equ_<canon>(a, b) -> bool (struct fieldwise comparison)
    if (b->op == BinOp::Eq || b->op == BinOp::Ne) {
      if (b->derivedEq) {
        std::string fname = fnLabel(b->derivedEq);
        std::string a = genHirExpr(b->lhs.get());
        std::string b2 = genHirExpr(b->rhs.get());
        std::string aty = llvmType(b->lhs->type);
        std::string bty = llvmType(b->rhs->type);
         std::string tmp = newTemp();
        out_ << "  " << tmp << " = call i8 " << fname << "("
             << llvmCallOperand(a, aty) << ", "
             << llvmCallOperand(b2, bty) << ")\n";
        std::string i1res = newTemp();
        out_ << "  " << i1res << " = trunc i8 " << tmp << " to i1\n";
if (b->op == BinOp::Ne) {
          std::string inv = newTemp();
          out_ << "  " << inv << " = xor i1 " << i1res << ", true\n";
          return inv;
        }
        return i1res;
      }
    }
    // derived Cmp (Comparable): cmp_<canon>(a, b) -> i64 (-1/0/1); o
    // resultado é comparado com 0 pelo predicado da operação
    if (b->derivedCmp) {
      std::string fname = fnLabel(b->derivedCmp);
      std::string a = genHirExpr(b->lhs.get());
      std::string b2 = genHirExpr(b->rhs.get());
      std::string aty = llvmType(b->lhs->type);
      std::string bty = llvmType(b->rhs->type);
      std::string c = newTemp();
      out_ << "  " << c << " = call i64 " << fname << "("
           << llvmCallOperand(a, aty) << ", "
           << llvmCallOperand(b2, bty) << ")\n";
      std::string pred;
      switch (b->op) {
        case BinOp::Lt: pred = "slt"; break;
        case BinOp::Le: pred = "sle"; break;
        case BinOp::Gt: pred = "sgt"; break;
        case BinOp::Ge: pred = "sge"; break;
        case BinOp::Eq: pred = "eq"; break;
        case BinOp::Ne: pred = "ne"; break;
        default: pred = "eq"; break;
      }
      std::string i1res = newTemp();
      out_ << "  " << i1res << " = icmp " << pred << " i64 " << c << ", 0\n";
      return i1res;
    }
    Type lhsTFixed = lhsT;
    if (lhsTFixed.kind == Type::Kind::Int && lhsTFixed.bits == 0) lhsTFixed.bits = b->rhs->type.bits;
    if (lhsTFixed.bits == 0) lhsTFixed.bits = 64;
    Type rhsTFixed = rhsT;
    if (rhsTFixed.kind == Type::Kind::Int && rhsTFixed.bits == 0) rhsTFixed.bits = b->lhs->type.bits;
    if (rhsTFixed.bits == 0) rhsTFixed.bits = 64;
    // if types differ, use the larger one for comparison
    int cmpBits = std::max(lhsTFixed.bits, rhsTFixed.bits);
    if (lhsTFixed.bits < cmpBits) lhsTFixed.bits = cmpBits;
    if (rhsTFixed.bits < cmpBits) rhsTFixed.bits = cmpBits;
    std::string lhsTy = llvmType(lhsTFixed);
    std::string rhsTy = llvmType(rhsTFixed);
    // comparacao com float: unifica em float/double (fpext/fptrunc/sitofp);
    // sem isso `float == double` emite fcmp com tipos diferentes
    if (lhsT.kind == Type::Kind::Float || rhsT.kind == Type::Kind::Float) {
      std::string fty = (lhsTy == "double" || rhsTy == "double") ? "double" : "float";
      lhs = convertForStore(lhs, lhsT, fty);
      rhs = convertForStore(rhs, rhsT, fty);
      lhsTy = fty;
      rhsTy = fty;
    }
    std::string tmp = newTemp();
    bool isUnsigned = lhsT.kind == Type::Kind::UInt;

    // operandos de larguras diferentes: estende o menor (ex. i32 vs i64)
    if (lhsTFixed.bits != rhsTFixed.bits) {
      lhs = extendToMatch(lhs, b->lhs->type, lhsTy);
      rhs = extendToMatch(rhs, b->rhs->type, rhsTy);
    }

    if (b->op == BinOp::And || b->op == BinOp::Or) {
      // operands podem ser i8 (bools) ou i1 (comparações); normaliza para i8
      if (lhsTy == "i8" || exprProducesI1(b->lhs.get()) || exprProducesI1(b->rhs.get())) {
        if (exprProducesI1(b->lhs.get())) {
          std::string ext = newTemp();
          out_ << "  " << ext << " = zext i1 " << lhs << " to i8\n";
          lhs = ext;
        }
        if (exprProducesI1(b->rhs.get())) {
          std::string ext = newTemp();
          out_ << "  " << ext << " = zext i1 " << rhs << " to i8\n";
          rhs = ext;
        }
        out_ << "  " << tmp << " = " << (b->op == BinOp::And ? "and" : "or")
             << " i8 " << lhs << ", " << rhs << "\n";
        return tmp;
      }
    }

    bool isFloatCmp = lhsT.kind == Type::Kind::Float || rhsT.kind == Type::Kind::Float;
    std::string pred;
    switch (b->op) {
      case BinOp::Eq: pred = isFloatCmp ? "oeq" : "eq"; break;
      case BinOp::Ne: pred = isFloatCmp ? "one" : "ne"; break;
      case BinOp::Lt: pred = isFloatCmp ? "olt" : (isUnsigned ? "ult" : "slt"); break;
      case BinOp::Gt: pred = isFloatCmp ? "ogt" : (isUnsigned ? "ugt" : "sgt"); break;
      case BinOp::Le: pred = isFloatCmp ? "ole" : (isUnsigned ? "ule" : "sle"); break;
      case BinOp::Ge: pred = isFloatCmp ? "oge" : (isUnsigned ? "uge" : "sge"); break;
      default: pred = isFloatCmp ? "oeq" : "eq"; break;
    }
    std::string cmpInst = isFloatCmp ? "fcmp" : "icmp";
    out_ << "  " << tmp << " = " << cmpInst << " " << pred << " " << lhsTy << " " << lhs << ", " << rhs << "\n";
    // return i1 directly (the if/while code will use it as branch condition)
    return tmp;
  }

  // String concatenation: use runtime hphl_str_concat instead of add
  if (b->op == BinOp::Add && (lhsT.kind == Type::Kind::String || rhsT.kind == Type::Kind::String ||
      t.kind == Type::Kind::String)) {
    std::string a = genHirExpr(b->lhs.get());
    std::string b2 = genHirExpr(b->rhs.get());
    std::string tmp = newTemp();
    out_ << "  " << tmp << " = call ptr @hphl_str_concat(" << llvmCallOperand(a, "ptr") << ", " << llvmCallOperand(b2, "ptr") << ")\n";
    return tmp;
  }

  // arithmetic ops
  std::string opStr = llvmBinOp(b->op, t.kind != Type::Kind::Unknown ? t : lhsT);
  std::string lhsTy = llvmType(lhsT);
  std::string rhsTy = llvmType(rhsT);
  std::string tmp = newTemp();
  std::string resultTypeStr = llvmType(t);

  // Fix: IntLits with bits=0 should be treated as matching the result type
  Type lhsTFixed = lhsT;
  if (lhsTFixed.kind == Type::Kind::Int && lhsTFixed.bits == 0) {
    lhsTFixed.bits = t.bits;
  }
  Type rhsTFixed = rhsT;
  if (rhsTFixed.kind == Type::Kind::Int && rhsTFixed.bits == 0) {
    rhsTFixed.bits = t.bits;
  }
  lhsTy = llvmType(lhsTFixed);
  rhsTy = llvmType(rhsTFixed);

  if (b->op == BinOp::Shl || b->op == BinOp::Shr) {
    // LLVM 22: o deslocamento NÃO repete o tipo do operando — constante vai
    // direto (`shl i64 %x, 3`) e registrador entra como valor (`shl i64
    // %x, %n`). Antes emitia ", i8 N"/", i64 N", rejeitado pelo parser.
    std::string rhsShift = rhs;
    if (!isConstantOperand(rhs) && rhsTy != lhsTy) {
      bool narrow = (rhsTy == "i8" || rhsTy == "i16" || rhsTy == "i32");
      std::string ext = newTemp();
      if (narrow)
        out_ << "  " << ext << " = zext " << rhsTy << " " << rhs << " to "
             << lhsTy << "\n";
      else
        out_ << "  " << ext << " = trunc " << rhsTy << " " << rhs << " to "
             << lhsTy << "\n";
      rhsShift = ext;
    }
    out_ << "  " << tmp << " = " << opStr << " " << lhsTy << " " << lhs << ", "
         << rhsShift << "\n";
  } else if (lhsTFixed.kind != t.kind || lhsTFixed.bits != t.bits ||
             rhsTFixed.kind != t.kind || rhsTFixed.bits != t.bits) {
    // type promotion needed - the operands and result have different types
    // promote both operands to the result type, then emit the operation
    std::string lhsProm = newTemp();
    std::string rhsProm = newTemp();
    // promote lhs
    if (lhsTFixed.kind == Type::Kind::Float && t.kind != Type::Kind::Float) {
      out_ << "  " << lhsProm << " = fptosi " << lhsTy << " " << lhs << " to " << resultTypeStr << "\n";
    } else if (lhsTFixed.kind != Type::Kind::Float && t.kind == Type::Kind::Float) {
      if (lhsTFixed.kind == Type::Kind::UInt)
        out_ << "  " << lhsProm << " = uitofp " << lhsTy << " " << lhs << " to " << resultTypeStr << "\n";
      else
        out_ << "  " << lhsProm << " = sitofp " << lhsTy << " " << lhs << " to " << resultTypeStr << "\n";
    } else if (lhsTFixed.isPointer() && t.isPointer()) {
      out_ << "  " << lhsProm << " = bitcast " << lhsTy << " " << lhs << " to " << resultTypeStr << "\n";
    } else if (lhsTFixed.kind == Type::Kind::Float && t.kind == Type::Kind::Float) {
      // float<->float nunca usa sext/trunc: fpext/fptrunc
      if (lhsTy != resultTypeStr)
        out_ << "  " << lhsProm << " = " << (resultTypeStr == "double" ? "fpext" : "fptrunc") << " " << lhsTy << " " << lhs << " to " << resultTypeStr << "\n";
      else lhsProm = lhs;
    } else if (lhsTy != resultTypeStr && t.bits > lhsTFixed.bits) {
      out_ << "  " << lhsProm << " = sext " << lhsTy << " " << lhs << " to " << resultTypeStr << "\n";
    } else if (lhsTy != resultTypeStr) {
      out_ << "  " << lhsProm << " = trunc " << lhsTy << " " << lhs << " to " << resultTypeStr << "\n";
    } else {
      lhsProm = lhs;
    }
    // promote rhs
    std::string rhsPromType = resultTypeStr;
    if (rhsTFixed.kind == Type::Kind::Float && t.kind != Type::Kind::Float) {
      out_ << "  " << rhsProm << " = fptosi " << rhsTy << " " << rhs << " to " << rhsPromType << "\n";
    } else if (rhsTFixed.kind != Type::Kind::Float && t.kind == Type::Kind::Float) {
      if (rhsTFixed.kind == Type::Kind::UInt)
        out_ << "  " << rhsProm << " = uitofp " << rhsTy << " " << rhs << " to " << rhsPromType << "\n";
      else
        out_ << "  " << rhsProm << " = sitofp " << rhsTy << " " << rhs << " to " << rhsPromType << "\n";
    } else if (rhsTFixed.isPointer() && t.isPointer()) {
      out_ << "  " << rhsProm << " = bitcast " << rhsTy << " " << rhs << " to " << rhsPromType << "\n";
    } else if (rhsTFixed.kind == Type::Kind::Float && t.kind == Type::Kind::Float) {
      // float<->float nunca usa sext/trunc: fpext/fptrunc
      if (rhsTy != rhsPromType)
        out_ << "  " << rhsProm << " = " << (rhsPromType == "double" ? "fpext" : "fptrunc") << " " << rhsTy << " " << rhs << " to " << rhsPromType << "\n";
      else rhsProm = rhs;
    } else if (resultTypeStr != rhsTy) {
      if (t.bits > rhsTFixed.bits)
        out_ << "  " << rhsProm << " = sext " << rhsTy << " " << rhs << " to " << rhsPromType << "\n";
      else
        out_ << "  " << rhsProm << " = trunc " << rhsTy << " " << rhs << " to " << rhsPromType << "\n";
    } else {
      rhsProm = rhs;
    }
    out_ << "  " << tmp << " = " << opStr << " " << resultTypeStr << " " << lhsProm << ", " << rhsProm << "\n";
  } else {
    // same types: just emit the operation (instruction type already specified)
    out_ << "  " << tmp << " = " << opStr << " " << lhsTy << " " << lhs << ", " << rhs << "\n";
  }
  return tmp;
}


std::string Irgen::genHirUnary(const HirUnary* u) {
  std::string operand = genHirExpr(u->operand.get());
  std::string opType = llvmType(u->operand->type);
  std::string tmp = newTemp();
  switch (u->op) {
    case UnOp::Not:
      out_ << "  " << tmp << " = icmp eq " << opType << " " << operand << ", 0\n";
      break;
    case UnOp::Neg:
      if (u->operand->type.kind == Type::Kind::Float) {
        out_ << "  " << tmp << " = fneg " << opType << " " << operand << "\n";
      } else if (u->operand->kind == HirExprKind::IntLit) {
        // -literal: dobra em constante tipada pelo contexto (evita `sub i64` com
        // icmp de outra largura, ex.: `x != -1` com x i32)
        long long v = static_cast<const HirIntLit*>(u->operand.get())->value;
        return "-" + std::to_string(v);
      } else {
        out_ << "  " << tmp << " = sub " << opType << " 0, " << operand << "\n";
      }
      break;
    case UnOp::BitNot:
      out_ << "  " << tmp << " = xor " << opType << " " << operand << ", -1\n";
      break;
    case UnOp::PreInc:
    case UnOp::PostInc:
    case UnOp::PreDec:
    case UnOp::PostDec: {
      // lvalue: load, ±1, store de volta; devolve novo (pre) ou antigo (post)
      bool inc = (u->op == UnOp::PreInc || u->op == UnOp::PostInc);

      // detect atomic target
      bool uAtomic = false;
      if (u->operand->kind == HirExprKind::Var)
        uAtomic = static_cast<const HirVar*>(u->operand.get())->atomic;
      else if (u->operand->kind == HirExprKind::Member)
        uAtomic = static_cast<const HirMember*>(u->operand.get())->fieldAtomic;

      if (uAtomic) {
        // atomicrmw for atomic i32 ++/--
        std::string addr;
        if (u->operand->kind == HirExprKind::Var) {
          const HirVar* v = static_cast<const HirVar*>(u->operand.get());
          if (v->isGlobal) {
            std::string gname = v->label.empty() ? v->name : v->label;
            if (tlsOffsets_.count(gname)) addr = tlsAddr(gname); // threadlocal
            else addr = "@" + gname;
          } else {
            addr = ensureLocalSlot(v->name, v->type);
          }
        } else if (u->operand->kind == HirExprKind::Member) {
          const HirMember* m = static_cast<const HirMember*>(u->operand.get());
          std::string obj = genHirExpr(m->object.get());
          std::string objTy = llvmType(m->object->type);
          int off = m->fieldOffset;
          if (off < 0) off = 0;
          std::string ptr = newTemp();
          out_ << "  " << ptr << " = getelementptr inbounds i8, ptr " << llvmInstrOperand(obj, objTy) << ", i64 " << off << "\n";
          addr = ptr;
        }
        std::string atomicOp = inc ? "add" : "sub";
        std::string rmw = newTemp();
        out_ << "  " << rmw << " = atomicrmw " << atomicOp << " " << ptrTo(opType) << " " << addr << ", " << opType << " 1 seq_cst\n";
        if (u->op == UnOp::PreInc || u->op == UnOp::PreDec) {
          std::string adj = newTemp();
          out_ << "  " << adj << " = " << (inc ? "add" : "sub") << " " << opType << " " << rmw << ", 1\n";
          return adj;
        }
        // post: atomicrmw already returned old value
        return rmw;
      }

      std::string delta = inc ? "1" : "-1";
      std::string newVal = newTemp();
      out_ << "  " << newVal << " = add " << opType << " " << operand << ", " << delta << "\n";
      newVal = applyOverflowPolicy(newVal, u->operand->type);
      const HirExpr* tgt = u->operand.get();
      bool stored = false;
      if (tgt->kind == HirExprKind::Var) {
        const HirVar* v = static_cast<const HirVar*>(tgt);
        if (v->isThisField) {
          std::string bytePtr = genThisFieldPtr(v->thisFieldOffset);
          if (isPtrStr(opType)) {
            std::string pc = newTemp();
            out_ << "  " << pc << " = bitcast ptr " << bytePtr << " to " << opType << "\n";
            out_ << "  store " << opType << " " << newVal << ", " << ptrTo(opType) << " " << pc << ", align 8\n";
          } else {
            out_ << "  store " << opType << " " << newVal << ", ptr " << bytePtr << ", align 8\n";
          }
          stored = true;
        } else if (v->isGlobal) {
          std::string gname = v->label.empty() ? v->name : v->label;
          if (tlsOffsets_.count(gname)) {
            std::string addr = tlsAddr(gname);
            out_ << "  store " << opType << " " << newVal << ", ptr " << addr << ", align 8\n";
          } else {
            out_ << "  store " << opType << " " << newVal << ", " << ptrTo(opType) << " @" << gname
                 << ", align 8\n";
          }
          stored = true;
        } else {
          std::string slot = ensureLocalSlot(v->name, v->type);
          out_ << "  store " << opType << " " << newVal << ", " << ptrTo(opType) << " "
               << slot << ", align 8\n";
          stored = true;
          // F2.4 debug: espelho do local (ref slots já espelhados no prólogo)
          if (slot.find(".ls") == std::string::npos)
            dbgMirrorStore(v->name, newVal, u->operand->type);
        }
      } else if (tgt->kind == HirExprKind::Member &&
                 static_cast<const HirMember*>(tgt)->object->kind == HirExprKind::This) {
        // this.X++
        std::string bytePtr = genThisFieldPtr(static_cast<const HirMember*>(tgt)->fieldOffset);
        out_ << "  store " << opType << " " << newVal << ", ptr " << bytePtr << ", align 8\n";
        stored = true;
      } else if (tgt->kind == HirExprKind::Index) {
        // nums[i]++ / m[r][c]++: recalcula o endereço e grava de volta
        const HirIndex* ix = static_cast<const HirIndex*>(tgt);
        std::string obj = genHirExpr(ix->object.get());
        std::string objTy = llvmType(ix->object->type);
        std::string idxv = genHirExpr(ix->index.get());
        std::string idx64 = idxv;
        if (llvmType(ix->index->type) != "i64") {
          idx64 = newTemp();
          out_ << "  " << idx64 << " = sext " << llvmType(ix->index->type) << " " << idxv << " to i64\n";
        }
        std::string base = obj;
        if (ix->object->type.kind == Type::Kind::List) {
          out_ << "  call void @hphl_list_check(i64 " << idx64 << ", ptr "
               << llvmCallOperand(obj, objTy) << ")\n";
          std::string dataTmp = newTemp();
          out_ << "  " << dataTmp << " = call ptr @hphl_list_data(ptr "
               << llvmCallOperand(obj, objTy) << ")\n";
          base = dataTmp;
        }
        // sub-array (int[2][2]): local/heap = ponteiro (8 bytes); global = linha
        // contígua (packed [N x i8])
        long long stride = arrayElementStride(ix->object.get(), ix->type);
        std::string scaled = newTemp();
        out_ << "  " << scaled << " = mul i64 " << idx64 << ", " << stride << "\n";
        std::string addr = newTemp();
        out_ << "  " << addr << " = getelementptr inbounds i8, ptr " << llvmInstrOperand(base, "ptr")
             << ", i64 " << scaled << "\n";
        out_ << "  store " << opType << " " << newVal << ", ptr " << addr << ", align 8\n";
        stored = true;
      }
      if (!stored) return (u->op == UnOp::PreInc || u->op == UnOp::PreDec) ? newVal : operand;
      if (u->op == UnOp::PreInc || u->op == UnOp::PreDec) return newVal;
      return operand;
    }
    default:
      out_ << "  " << tmp << " = sub " << opType << " 0, " << operand << "\n";
      break;
  }
  return tmp;
}

std::string Irgen::genHirAssign(const HirAssign* a) {
  // For assignment, we need to store the value to the target's slot.
  // In HIR, the target is typically an addressable expression.
  // For simplicity in this minimal backend, we handle:
  // 1. Simple assignment: store the value
  // 2. Compound assignment: load target, compute, store

  if (a->op == AssignOp::Plain) {
    std::string value = toI8Store(genHirExpr(a->value.get()), a->value.get(), a->type);
    // propriedade set: chama set_X(receiver, value) em vez de gravar o campo
    // (espelha o codegen x64 — genHirAssign/genHirMember fazem o get)
    if (a->target->kind == HirExprKind::Member) {
      const HirMember* m = static_cast<const HirMember*>(a->target.get());
      if (m->isProperty && m->propSet) {
        std::string obj = genHirExpr(m->object.get());
        std::string objTy = llvmType(m->object->type);
        std::string vTy = llvmType(m->propType);
        std::string conv = convertForStore(value, actualTypeOf(a->value.get()), vTy);
        out_ << "  call void " << fnLabel(m->propSet) << "("
             << llvmCallOperand(obj, objTy) << ", " << vTy << " " << conv << ")\n";
        return value;
      }
    } else if (a->target->kind == HirExprKind::Var) {
      const HirVar* v = static_cast<const HirVar*>(a->target.get());
      if (v->isProperty && v->propSet) {
        HirThis thObj;
        std::string thisArg = genHirThis(&thObj);
        std::string thisTy = llvmType(thObj.type);
        std::string vTy = llvmType(v->type);
        std::string conv = convertForStore(value, actualTypeOf(a->value.get()), vTy);
        out_ << "  call void " << fnLabel(v->propSet) << "("
             << llvmCallOperand(thisArg, thisTy) << ", " << vTy << " " << conv << ")\n";
        return value;
      }
    }
    // struct: cópia por valor (semântica de struct — bloco independente)
    if (isStructType(a->value->type)) value = genStructCopy(a->value->type, value);
    // M10.1b: atribuição a variável tuple clona (semântica de valor)
    if (a->type.kind == Type::Kind::Tuple && a->target->kind == HirExprKind::Var &&
        a->value->kind != HirExprKind::TupleLit) {
      const HirVar* tv = static_cast<const HirVar*>(a->target.get());
      int n = (int)a->type.tupleElems.size();
      std::string slot = ensureLocalSlot(tv->name, tv->type);
      std::string cl = newTemp();
      out_ << "  " << cl << " = call ptr @hphl_tuple_clone("
           << llvmCallOperand(value, "ptr") << ", i64 " << n << ")\n";
      out_ << "  store ptr " << cl << ", ptr " << slot << ", align 8\n";
      return cl;
    }
    // The target is addressable (a Var expression)
    if (a->target->kind == HirExprKind::Var) {
      const HirVar* v = static_cast<const HirVar*>(a->target.get());
      std::string targetType = llvmType(v->type);
      // convertForStore emite instruções: materializar ANTES do out_ << (a
      // avaliação no meio da cadeia colocaria o sext dentro do store)
      std::string storeVal = convertForStore(value, actualTypeOf(a->value.get()), targetType);
      if (v->isThisField) {
        std::string bytePtr = genThisFieldPtr(v->thisFieldOffset);
        if (isPtrStr(targetType)) {
          std::string pc = newTemp();
          out_ << "  " << pc << " = bitcast ptr " << bytePtr << " to " << targetType << "\n";
          out_ << "  store " << targetType << " " << storeVal << ", " << ptrTo(targetType)
               << " " << pc << ", align 8\n";
        } else {
          out_ << "  store " << targetType << " " << storeVal << ", ptr " << bytePtr << ", align 8\n";
        }
      } else if (v->isGlobal) {
        std::string gname = v->label.empty() ? v->name : v->label;
        if (tlsOffsets_.count(gname)) {
          std::string addr = tlsAddr(gname);
          out_ << "  store " << targetType << " " << storeVal << ", ptr " << addr << ", align 8\n";
        } else {
          out_ << "  store " << targetType << " " << storeVal << ", " << ptrTo(targetType)
               << " @" << gname << ", align 8\n";
        }
      } else {
        std::string slot = ensureLocalSlot(v->name, v->type);
        std::string slotPtrTy = ptrTo(targetType);
        bool isRefSlot = false;
        if (slot.size() > 2 && slot[0] == '%') {
          auto d = slot.find('.');
          if (d != std::string::npos && d + 2 < slot.size() && slot[d+2] == 's') {
            isRefSlot = true;
          }
        }
        if (isRefSlot && targetType != "ptr") {
          std::string innerPtr = newTemp();
          out_ << "  " << innerPtr << " = load ptr, ptr " << slot << ", align 8\n";
          out_ << "  store " << targetType << " " << storeVal
               << ", ptr " << innerPtr << ", align 8\n";
        } else {
          out_ << "  store " << targetType << " " << storeVal << ", " << slotPtrTy
               << " " << slot << ", align 8\n";
          // F2.4 debug: espelho do local (ref slots já espelhados no prólogo)
          dbgMirrorStore(v->name, storeVal, v->type);
        }
      }
    } else if (a->target->kind == HirExprKind::Member) {
      // For member targets (this.field), compute the address directly
      const HirMember* m = static_cast<const HirMember*>(a->target.get());
      std::string targetType = llvmType(m->type);
      std::string obj = genHirExpr(m->object.get());
      std::string objType = llvmType(m->object->type);
      int fieldOffset = m->fieldOffset;
      if (fieldOffset < 0) fieldOffset = 0;
      std::string ptrTmp = newTemp();
      out_ << "  " << ptrTmp << " = getelementptr inbounds i8, ptr " << llvmInstrOperand(obj, objType)
           << ", i64 " << fieldOffset << "\n";
      std::string storeVal = convertForStore(value, actualTypeOf(a->value.get()), targetType);
      if (isPtrStr(targetType)) {
        std::string pc = newTemp();
        out_ << "  " << pc << " = bitcast ptr " << ptrTmp << " to " << targetType << "\n";
        out_ << "  store " << targetType << " " << storeVal << ", " << ptrTo(targetType) << " " << pc << ", align 8\n";
      } else {
        out_ << "  store " << targetType << " " << storeVal << ", ptr " << ptrTmp << ", align 8\n";
      }
    } else if (a->target->kind == HirExprKind::Index) {
      // For index targets (arr[i]), compute the address directly
      const HirIndex* idx = static_cast<const HirIndex*>(a->target.get());
      std::string targetType = llvmType(idx->type);
      std::string obj = genHirExpr(idx->object.get());
      std::string objType = llvmType(idx->object->type);
      std::string index = genHirExpr(idx->index.get());
      std::string index64 = index;
      if (llvmType(idx->index->type) != "i64") {
        index64 = newTemp();
        out_ << "  " << index64 << " = sext " << llvmType(idx->index->type) << " " << index << " to i64\n";
      }
      std::string base = obj;
      if (idx->object->type.kind == Type::Kind::List) {
        out_ << "  call void @hphl_list_check(i64 " << index64 << ", "
             << llvmCallOperand(obj, objType) << ")\n";
        std::string dataTmp = newTemp();
        out_ << "  " << dataTmp << " = call ptr @hphl_list_data("
             << llvmCallOperand(obj, objType) << ")\n";
        base = dataTmp;
      }
      // sub-array (int[2][2]): local/heap = ponteiro (8 bytes); global = linha
      // contígua (packed [N x i8])
      long long stride = arrayElementStride(idx->object.get(), idx->type);
      std::string scaledIdx = newTemp();
      out_ << "  " << scaledIdx << " = mul i64 " << index64 << ", " << stride << "\n";
      std::string ptrTmp = newTemp();
      out_ << "  " << ptrTmp << " = getelementptr inbounds i8, ptr " << llvmInstrOperand(base, "ptr")
           << ", i64 " << scaledIdx << "\n";
      std::string storeVal = convertForStore(value, actualTypeOf(a->value.get()), targetType);
      if (isPtrStr(targetType)) {
        std::string pc = newTemp();
        out_ << "  " << pc << " = bitcast ptr " << ptrTmp << " to " << targetType << "\n";
        out_ << "  store " << targetType << " " << storeVal << ", " << ptrTo(targetType) << " " << pc << ", align 8\n";
      } else {
        out_ << "  store " << targetType << " " << storeVal << ", ptr " << ptrTmp << ", align 8\n";
      }
    } else {
      std::string targetType = llvmType(a->target->type);
      std::string addr = genHirExpr(a->target.get());
      std::string storeVal = convertForStore(value, actualTypeOf(a->value.get()), targetType);
      out_ << "  store " << targetType << " " << storeVal << ", " << ptrTo(targetType)
           << " " << addr << ", align 8\n";
    }
    return value;
  }

  // compound assignment
  std::string targetVal = genHirExpr(a->target.get());
  std::string rhs = genHirExpr(a->value.get());
  std::string lType = llvmType(a->target->type);

  // detect atomic target
  bool tAtomic = false;
  if (a->target->kind == HirExprKind::Var)
    tAtomic = static_cast<const HirVar*>(a->target.get())->atomic;
  else if (a->target->kind == HirExprKind::Member)
    tAtomic = static_cast<const HirMember*>(a->target.get())->fieldAtomic;

  std::string atomicOp;
  switch (a->op) {
    case AssignOp::Add: atomicOp = "add"; break;
    case AssignOp::Sub: atomicOp = "add"; break;
    default: break;
  }

  if (tAtomic && !atomicOp.empty()) {
    // atomicrmw for i32 atomics (add/sub/and/or/xor)
    std::string addr;
    if (a->target->kind == HirExprKind::Var) {
      const HirVar* v = static_cast<const HirVar*>(a->target.get());
      if (v->isGlobal) {
        std::string gname = v->label.empty() ? v->name : v->label;
        if (tlsOffsets_.count(gname)) addr = tlsAddr(gname); // threadlocal
        else addr = "@" + gname;
      } else {
        addr = ensureLocalSlot(v->name, v->type);
      }
    } else if (a->target->kind == HirExprKind::Member) {
      const HirMember* m = static_cast<const HirMember*>(a->target.get());
      std::string obj = genHirExpr(m->object.get());
      std::string objTy = llvmType(m->object->type);
      int off = m->fieldOffset;
      if (off < 0) off = 0;
      std::string ptr = newTemp();
      out_ << "  " << ptr << " = getelementptr inbounds i8, ptr " << llvmInstrOperand(obj, objTy) << ", i64 " << off << "\n";
      addr = ptr;
    }
    std::string rhsAdj = rhs;
    if (a->op == AssignOp::Sub) {
      std::string neg = newTemp();
      out_ << "  " << neg << " = sub " << lType << " 0, " << extendToMatch(rhs, actualTypeOf(a->value.get()), lType) << "\n";
      rhsAdj = neg;
    }
    std::string rmw = newTemp();
    out_ << "  " << rmw << " = atomicrmw " << atomicOp << " " << ptrTo(lType) << " " << addr << ", " << lType << " " << rhsAdj << " seq_cst\n";
    return rmw;
  }

  BinOp binOp = BinOp::Add;
  switch (a->op) {
    case AssignOp::Add: binOp = BinOp::Add; break;
    case AssignOp::Sub: binOp = BinOp::Sub; break;
    case AssignOp::Mul: binOp = BinOp::Mul; break;
    case AssignOp::Div: binOp = BinOp::Div; break;
    case AssignOp::Mod: binOp = BinOp::Mod; break;
    default: binOp = BinOp::Add; break;
  }
  std::string opStr = llvmBinOp(binOp, a->target->type);
  std::string result = newTemp();
  std::string rhsFixed = extendToMatch(rhs, actualTypeOf(a->value.get()), lType);
  out_ << "  " << result << " = " << opStr << " " << lType << " " << targetVal << ", " << rhsFixed << "\n";
  result = applyOverflowPolicy(result, a->target->type);

  // store result to target
  if (a->target->kind == HirExprKind::Var) {
    const HirVar* v = static_cast<const HirVar*>(a->target.get());
    if (v->isThisField) {
      std::string bytePtr = genThisFieldPtr(v->thisFieldOffset);
      if (isPtrStr(lType)) {
        std::string pc = newTemp();
        out_ << "  " << pc << " = bitcast ptr " << bytePtr << " to " << lType << "\n";
        out_ << "  store " << lType << " " << result << ", " << ptrTo(lType) << " " << pc << ", align 8\n";
      } else {
        out_ << "  store " << lType << " " << result << ", ptr " << bytePtr << ", align 8\n";
      }
    } else if (v->isGlobal) {
      std::string gname = v->label.empty() ? v->name : v->label;
      if (tlsOffsets_.count(gname)) {
        std::string addr = tlsAddr(gname);
        out_ << "  store " << lType << " " << result << ", ptr " << addr << ", align 8\n";
      } else {
        out_ << "  store " << lType << " " << result << ", " << ptrTo(lType) << " @"
             << gname << ", align 8\n";
      }
    } else {
      std::string slot = ensureLocalSlot(v->name, v->type);
      bool isRefSlot = false;
      if (slot.size() > 2 && slot[0] == '%') {
        auto d = slot.find('.');
        if (d != std::string::npos && d + 2 < slot.size() && slot[d + 2] == 's') {
          isRefSlot = true;
        }
      }
      if (isRefSlot) {
        std::string innerPtr = newTemp();
        out_ << "  " << innerPtr << " = load ptr, ptr " << slot << ", align 8\n";
        out_ << "  store " << lType << " " << result << ", ptr " << innerPtr << ", align 8\n";
      } else {
        out_ << "  store " << lType << " " << result << ", " << ptrTo(lType) << " "
             << slot << ", align 8\n";
        // F2.4 debug: espelho do local (ref slots já espelhados no prólogo)
        dbgMirrorStore(v->name, result, a->target->type);
      }
    }
  } else if (a->target->kind == HirExprKind::Member ||
             a->target->kind == HirExprKind::Index) {
    std::string addr = genLValueAddress(a->target.get());
    out_ << "  store " << lType << " " << result << ", ptr " << addr << ", align 8\n";
  }
  return result;
}

std::string Irgen::genHirCast(const HirCast* cs) {
  std::string src = genHirExpr(cs->operand.get());
  std::string srcTy = llvmType(cs->operand->type);
  std::string dstTy = llvmType(cs->target);
  Type srcT = cs->operand->type;
  Type dstT = cs->target;

  if (srcTy == dstTy) return src;

  std::string tmp = newTemp();
  // int to int
  if (srcT.kind == Type::Kind::Int || srcT.kind == Type::Kind::UInt || srcT.kind == Type::Kind::Bool || srcT.kind == Type::Kind::Char) {
    if (dstT.kind == Type::Kind::Int || dstT.kind == Type::Kind::UInt || dstT.kind == Type::Kind::Bool || dstT.kind == Type::Kind::Char) {
      if (dstT.bits > srcT.bits)
        out_ << "  " << tmp << " = sext " << srcTy << " " << src << " to " << dstTy << "\n";
      else if (dstT.bits < srcT.bits)
        out_ << "  " << tmp << " = trunc " << srcTy << " " << src << " to " << dstTy << "\n";
      else
        out_ << "  " << tmp << " = bitcast " << srcTy << " " << src << " to " << dstTy << "\n";
      return tmp;
    }
  }
  // float to float
  if (srcT.kind == Type::Kind::Float && dstT.kind == Type::Kind::Float) {
    if (dstT.bits > srcT.bits)
      out_ << "  " << tmp << " = fpext " << srcTy << " " << src << " to " << dstTy << "\n";
    else if (dstT.bits < srcT.bits)
      out_ << "  " << tmp << " = fptrunc " << srcTy << " " << src << " to " << dstTy << "\n";
    else
      out_ << "  " << tmp << " = bitcast " << srcTy << " " << src << " to " << dstTy << "\n";
    return tmp;
  }
  // int to float
  if ((srcT.kind == Type::Kind::Int || srcT.kind == Type::Kind::UInt) && dstT.kind == Type::Kind::Float) {
    if (srcT.kind == Type::Kind::UInt)
      out_ << "  " << tmp << " = uitofp " << srcTy << " " << src << " to " << dstTy << "\n";
    else
      out_ << "  " << tmp << " = sitofp " << srcTy << " " << src << " to " << dstTy << "\n";
    return tmp;
  }
  // float to int
  if (srcT.kind == Type::Kind::Float && (dstT.kind == Type::Kind::Int || dstT.kind == Type::Kind::UInt)) {
    std::string instr = (dstT.kind == Type::Kind::UInt) ? "fptoui" : "fptosi";
    out_ << "  " << tmp << " = " << instr << " " << srcTy << " " << src << " to " << dstTy << "\n";
    return tmp;
  }
  // pointer to int
  if (srcT.isPointer() && (dstT.kind == Type::Kind::Int || dstT.kind == Type::Kind::UInt)) {
    out_ << "  " << tmp << " = ptrtoint " << srcTy << " " << src << " to " << dstTy << "\n";
    return tmp;
  }
  // int to pointer
  if ((srcT.kind == Type::Kind::Int || srcT.kind == Type::Kind::UInt) && dstT.isPointer()) {
    out_ << "  " << tmp << " = inttoptr " << srcTy << " " << src << " to " << dstTy << "\n";
    return tmp;
  }
  // pointer to pointer, or fallback
  out_ << "  " << tmp << " = bitcast " << srcTy << " " << src << " to " << dstTy << "\n";
  return tmp;
}


std::string Irgen::convertForStore(const std::string& v, const Type& srcT, const std::string& dstTy) {
std::string srcTy = llvmType(srcT);
  if (isConstantOperand(v)) {
    bool isPtrDst = isPtrStr(dstTy);
    // M11-prep: constante em destino FLOAT — o literal precisa de forma de
    // ponto flutuante (`store double 0.0`, não `store double 0`)
    if (dstTy == "double" || dstTy == "float") {
      std::string num = v;
      if (num.find(' ') != std::string::npos)
        num = num.substr(num.find(' ') + 1);
      if (num == "null") return "null";
      if (num.find('.') == std::string::npos && num.find('e') == std::string::npos)
        num += ".0";
      return num;
    }
    if (v.find(' ') != std::string::npos) {
      std::string num = v.substr(v.find(' ') + 1);
      if (isPtrDst && (num == "0" || num == "-0")) return "null";
      return num;
    }
    if (isPtrDst && (v == "0" || v == "-0")) return "null";
    return v;
  }
  if (srcTy == dstTy) return v;
  std::string tmp = newTemp();
  bool srcPtr = isPtrStr(srcTy);
  bool dstPtr = isPtrStr(dstTy);
  if (srcPtr && dstPtr) return v;  // ponteiros opacos (LLVM 22): sem cast
  if (srcPtr && !dstPtr) {
    out_ << "  " << tmp << " = ptrtoint " << srcTy << " " << v << " to " << dstTy << "\n";
    return tmp;
  }
  if (!srcPtr && dstPtr) {
    out_ << "  " << tmp << " = inttoptr " << srcTy << " " << v << " to " << dstTy << "\n";
    return tmp;
  }
  if (srcT.kind == Type::Kind::Float) {
    if (dstTy == "float" || dstTy == "double") {
      if ((dstTy == "double" && srcTy == "float") || (dstTy == "float" && srcTy == "double")) {
        std::string instr = (dstTy == "double") ? "fpext" : "fptrunc";
        out_ << "  " << tmp << " = " << instr << " " << srcTy << " " << v << " to " << dstTy << "\n";
      } else {
        out_ << "  " << tmp << " = bitcast " << srcTy << " " << v << " to " << dstTy << "\n";
      }
      return tmp;
    }
    out_ << "  " << tmp << " = fptosi " << srcTy << " " << v << " to " << dstTy << "\n";
    return tmp;
  }
  bool srcIs64 = (srcTy == "i64");
  bool dstIs32 = (dstTy == "i32");
  bool dstIs16 = (dstTy == "i16");
  bool dstIs8  = (dstTy == "i8");
  // M11-bench: inteiro → float NUNCA é trunc — é sitofp/uitofp
  if ((dstTy == "double" || dstTy == "float") && srcT.isInteger() &&
      !isConstantOperand(v)) {
    std::string instr = (srcT.kind == Type::Kind::UInt) ? "uitofp" : "sitofp";
    out_ << "  " << tmp << " = " << instr << " " << srcTy << " " << v << " to "
         << dstTy << "\n";
    return tmp;
  }
  if (srcIs64 && (dstIs32 || dstIs16 || dstIs8)) {
    out_ << "  " << tmp << " = trunc " << srcTy << " " << v << " to " << dstTy << "\n";
    return tmp;
  }
  bool dstIsInt = dstTy == "i64" || dstTy == "i32" || dstTy == "i16" || dstTy == "i8";
  if (dstIsInt && !srcPtr && (srcT.kind == Type::Kind::Int || srcT.kind == Type::Kind::UInt ||
      srcT.kind == Type::Kind::Bool || srcT.kind == Type::Kind::Char)) {
    std::string instr = (srcT.kind == Type::Kind::UInt) ? "zext" : "sext";
    out_ << "  " << tmp << " = " << instr << " " << srcTy << " " << v << " to " << dstTy << "\n";
    return tmp;
  }
  out_ << "  " << tmp << " = trunc " << srcTy << " " << v << " to " << dstTy << "\n";
  return tmp;
}

// estende um valor para o tipo LLVM do destino (compostos/atribuições):
// constantes são aceitas direto (LLVM infere o tipo); registradores menores
// são sext/zext; ponteiros opacos não precisam de cast
std::string Irgen::extendToMatch(const std::string& v, const Type& srcT,
                                 const std::string& dstTy) {
  std::string srcTy = llvmType(srcT);
  if (srcTy == dstTy) return v;
  if (isConstantOperand(v)) return v;
  bool srcPtr = isPtrStr(srcTy);
  bool dstPtr = isPtrStr(dstTy);
  if (srcPtr || dstPtr) return v;
  std::string tmp = newTemp();
  if (srcT.kind == Type::Kind::Float) {
    if (dstTy == "double" && srcTy == "float") {
      out_ << "  " << tmp << " = fpext " << srcTy << " " << v << " to " << dstTy << "\n";
    } else {
      out_ << "  " << tmp << " = fptrunc " << srcTy << " " << v << " to " << dstTy << "\n";
    }
    return tmp;
  }
  if (srcT.kind == Type::Kind::Int || srcT.kind == Type::Kind::UInt ||
      srcT.kind == Type::Kind::Bool || srcT.kind == Type::Kind::Char) {
    std::string instr = srcT.kind == Type::Kind::UInt ? "zext" : "sext";
    out_ << "  " << tmp << " = " << instr << " " << srcTy << " " << v << " to " << dstTy << "\n";
    return tmp;
  }
  return v;
}


std::string Irgen::applyOverflowPolicy(const std::string& val, const Type& t) {
  if (t.kind != Type::Kind::Int && t.kind != Type::Kind::UInt) return val;
  int W = t.bits == 0 ? 64 : t.bits;
  if (W >= 64) return val;
  if (t.policy == OverflowPolicy::Default || t.policy == OverflowPolicy::Fixed ||
      t.policy == OverflowPolicy::Promote) {
    return val;
  }
  bool uns = t.kind == Type::Kind::UInt;
  std::string instr = uns ? "zext" : "sext";
  if (t.policy == OverflowPolicy::Wrap) {
    std::string truncated = newTemp();
    std::string extended = newTemp();
    out_ << "  " << truncated << " = trunc i64 " << val << " to i" << W << "\n";
    out_ << "  " << extended << " = " << instr << " i" << W << " " << truncated
         << " to i64\n";
    return extended;
  }
  if (t.policy == OverflowPolicy::Checked) {
    long long minV = uns ? 0 : -(1LL << (W - 1));
    long long maxV = uns ? (1LL << W) - 1 : (1LL << (W - 1)) - 1;
    // hphl_overflow_check é void: panica se fora da faixa; o valor passa
    out_ << "  call void @hphl_overflow_check(i64 " << val << ", i64 "
         << minV << ", i64 " << maxV << ")\n";
    return val;
  }
  // saturate: clamp no intervalo [minV, maxV]
  long long minV = uns ? 0 : -(1LL << (W - 1));
  long long maxV = uns ? (1LL << W) - 1 : (1LL << (W - 1)) - 1;
  std::string clamped = newTemp();
  out_ << "  " << clamped << " = icmp " << (uns ? "ult" : "slt") << " i64 " << val
       << ", " << minV << "\n";
  std::string selMin = newTemp();
  out_ << "  " << selMin << " = select i1 " << clamped << ", i64 " << minV << ", i64 "
       << val << "\n";
  std::string clamped2 = newTemp();
  out_ << "  " << clamped2 << " = icmp " << (uns ? "ugt" : "sgt") << " i64 " << val
       << ", " << maxV << "\n";
  std::string result = newTemp();
  out_ << "  " << result << " = select i1 " << clamped2 << ", i64 " << maxV
       << ", i64 " << selMin << "\n";
  return result;
}


std::string Irgen::genHirNew(const HirNew* n) {
  if (n->isChannel) {
    // `new channel<T>(cap)` — builtin do runtime (espelha genHirNew do x64)
    std::string tmp = newTemp();
    out_ << "  " << tmp << " = call noalias ptr @hphl_channel_new(i64 " << n->channelCapacity << ")\n";
    return tmp;
  }
  if (n->isList) {
    // `new list<T>()` / `new list<T>(cap)` — espelha genHirNew do x64
    if (n->args.empty()) {
      std::string h = newTemp();
      out_ << "  " << h << " = call noalias ptr @hphl_list_new()\n";
      return h;
    }
    std::string cap = genHirExpr(n->args[0].get());
    std::string cap64 = extendToI64(cap, n->args[0]->type);
    std::string h = newTemp();
    out_ << "  " << h << " = call noalias ptr @hphl_list_with_cap(i64 "
         << cap64 << ")\n";
    return h;
  }
  if (n->isArrayNew) {
    // `new T[N]` — bloco heap zerado (x64 usa hphl_shared_alloc; aqui calloc
    // como nos objetos — ver abaixo)
    Type et = (n->type.elem) ? *n->type.elem : Type::makeInt(32);
    long long bytes = n->arraySize * (long long)typeSize(et);
    std::string h = newTemp();
    out_ << "  " << h << " = call noalias ptr @calloc(i64 1, i64 " << bytes << ")\n";
    return h;
  }
  // aloca o bloco do objeto (classSize do layout da semântica — como o x64
  // usa calloc; campos não inicializados ficam zerados) e chama o ctor
  // (método: `this` é o primeiro argumento ABI, ver genFunction)
  auto& ci = sem_.classes().at(n->className);
  std::string raw = newTemp();
  out_ << "  " << raw << " = call noalias ptr @calloc(i64 1, i64 " << ci.size << ")\n";
  if (ci.hasVptr && !sem_.vslots().empty())
    out_ << "  store ptr @\"Lvt_" << n->className << "\", ptr " << raw << ", align 8\n"; // M10: vptr
  std::string obj = newTemp();
    out_ << "  " << obj << " = bitcast ptr " << raw << " to ptr\n";
  if (n->ctor) {
    std::string ctorName = fnLabel(n->ctor);
    std::string args;
    args += llvmCallOperand(obj, llvmType(Type::makeClass(n->className)));
    for (size_t i = 0; i < n->args.size(); i++) {
      std::string arg = genHirExpr(n->args[i].get());
      if (!args.empty()) args += ", ";
      std::string argType;
      if (i < n->ctor->params.size()) {
        argType = llvmType(n->ctor->params[i]->type);
        // FFI v2 fix: arg int p/ param float do ctor — converte
        if (n->ctor->params[i]->type.kind == Type::Kind::Float &&
            n->args[i]->type.kind != Type::Kind::Float)
          arg = convertForStore(arg, n->args[i]->type, argType);
      } else {
        argType = llvmType(n->args[i]->type);
      }
      args += llvmCallOperand(arg, argType);
    }
    out_ << "  call void " << ctorName << "(" << args << ")\n";
  }
  return obj;
}

std::string Irgen::genHirCellTag(const HirCellTag* ct) {
  std::string subject = genHirExpr(ct->subject.get());
  std::string subjectTy = llvmType(ct->subject->type);
  bool isPtr = isPointerType(ct->subject->type);
  std::string objPtr;
  if (isPtr) {
    objPtr = subject;
  } else {
    std::string pTmp = newTemp();
    out_ << "  " << pTmp << " = inttoptr " << subjectTy << " " << subject << " to ptr\n";
    objPtr = pTmp;
  }
  std::string tagTmp = newTemp();
  out_ << "  " << tagTmp << " = load i64, ptr " << objPtr << ", align 8\n";
  return tagTmp;
}

std::string Irgen::genHirLoadAt(const HirLoadAt* la) {
  std::string subject = genHirExpr(la->subject.get());
  int offsetBytes = (int)la->offset;
  // célula/ponteiro inteiro (variante rico por valor): inttoptr antes do GEP
  std::string subjectTy = llvmType(la->subject->type);
  std::string base = subject;
  if (!isPointerType(la->subject->type)) {
    std::string pTmp = newTemp();
    out_ << "  " << pTmp << " = inttoptr " << subjectTy << " " << llvmInstrOperand(subject, subjectTy)
         << " to ptr\n";
    base = pTmp;
  }
  std::string ptrTmp = newTemp();
  out_ << "  " << ptrTmp << " = getelementptr inbounds i8, ptr " << base
       << ", i64 " << offsetBytes << "\n";
  std::string valTmp = newTemp();
  std::string memberType = llvmType(la->type);
  out_ << "  " << valTmp << " = load " << memberType << ", ptr " << ptrTmp << ", align 8\n";
  return valTmp;
}

std::string Irgen::genHirOptCtor(const HirOptCtor* oc) {
  // espelha o x64: célula de 16 bytes (tag i64 em +0, payload em +8)
  long long tag = (oc->variant == "Some" || oc->variant == "Err") ? 1 : 0;
  std::string h = newTemp();
  out_ << "  " << h << " = call noalias ptr @malloc(i64 16)\n";
  std::string p0 = newTemp();
  out_ << "  " << p0 << " = bitcast ptr " << h << " to i64*\n";
  out_ << "  store i64 " << tag << ", i64* " << p0 << ", align 8\n";
  if (oc->arg) {
    std::string val = genHirExpr(oc->arg.get());
    std::string argTy = llvmType(oc->arg->type);
    std::string p8 = newTemp();
    out_ << "  " << p8 << " = getelementptr inbounds i8, ptr " << h << ", i64 8\n";
    if (argTy == "float") {
      std::string ext = newTemp();
      out_ << "  " << ext << " = fpext float " << val << " to double\n";
      out_ << "  store double " << ext << ", ptr " << p8 << ", align 8\n";
    } else {
      out_ << "  store " << argTy << " " << val << ", ptr " << p8 << ", align 8\n";
    }
  }
  return h;
}

std::string Irgen::genThisFieldPtr(long long off) {
  std::string tmp = newTemp();
  out_ << "  " << tmp << " = getelementptr inbounds i8, ptr %.this, i64 " << off << "\n";
  return tmp;
}

std::string Irgen::genHirSpawn(const HirSpawnExpr* s) {
  // `task<T> t = spawn { return ...; }` — task sintética com env; o handle
  // (HphlTask*) é o valor da expressão. O `return` do corpo vira o payload
  // (escrito via res, ver genHirReturn).
  spawnedAny_ = true;
  auto caps = filterCaptures(s->captures);
  queueTask(std::move(const_cast<HirSpawnExpr*>(s)->body), caps);
  return emitSpawnSite(pendingTasks_.back().fnLabel, caps);
}

std::string Irgen::genHirAwait(const HirAwaitExpr* a) {
  // `await E`: espera a task E e devolve o payload; `await ch.Receive()` é
  // açúcar — o Receive já bloqueia (nó ChannelReceive no HIR)
  if (a->operand->kind == HirExprKind::Call &&
      static_cast<HirCall*>(a->operand.get())->kind == HirCallKind::ChannelReceive) {
    return genHirExpr(a->operand.get());
  }
  std::string h = genHirExpr(a->operand.get());
  std::string tmp = newTemp();
  out_ << "  " << tmp << " = call i64 @hphl_wait_task(" << llvmCallOperand(h, "ptr")
       << ")\n";
  return castPayloadFromI64(tmp, a->type);
}

std::string Irgen::genHirLambda(const HirLambda* l) {
  // v0.95: `(params) => corpo` — enfileira a função sintética e materializa
  // o handle {code, env} (16 bytes). Capturas por valor no env.
  queueLambda(std::move(const_cast<HirLambda*>(l)->body), l->params,
              l->retType, l->captures);
  const std::string& label = pendingLambdas_.back().fnLabel;
  std::string env = "null";
  if (!l->captures.empty()) {
    env = newTemp();
    out_ << "  " << env << " = call noalias ptr @malloc(i64 "
         << (l->captures.size() * 8) << ")\n";
    for (size_t i = 0; i < l->captures.size(); i++) {
      auto capVar = hirLocal(l->captures[i].first, l->captures[i].second);
      std::string val = genHirExpr(capVar.get());
      std::string slot = newTemp();
      out_ << "  " << slot << " = getelementptr inbounds i8, ptr " << env
           << ", i64 " << (i * 8) << "\n";
      std::string slotPtr = newTemp();
      out_ << "  " << slotPtr << " = bitcast ptr " << slot << " to ptr\n";
      if (isPointerType(l->captures[i].second)) {
        out_ << "  store ptr " << llvmInstrOperand(val, llvmType(l->captures[i].second))
             << ", ptr " << slotPtr << ", align 8\n";
      } else {
        std::string v64 = extendToI64(val, l->captures[i].second);
        std::string s64 = newTemp();
        out_ << "  " << s64 << " = bitcast ptr " << slotPtr << " to i64*\n";
        out_ << "  store i64 " << v64 << ", i64* " << s64 << ", align 8\n";
      }
    }
  }
  // closure box {code, env}
  std::string box = newTemp();
  out_ << "  " << box << " = call noalias ptr @malloc(i64 16)\n";
  std::string codePtr = newTemp();
  out_ << "  " << codePtr << " = bitcast ptr " << box << " to ptr\n";
  out_ << "  store ptr " << label << ", ptr " << codePtr << ", align 8\n";
  std::string envPtr = newTemp();
  out_ << "  " << envPtr << " = getelementptr inbounds i8, ptr " << box << ", i64 8\n";
  std::string envPtr2 = newTemp();
  out_ << "  " << envPtr2 << " = bitcast ptr " << envPtr << " to ptr\n";
  out_ << "  store ptr " << env << ", ptr " << envPtr2 << ", align 8\n";
  // v0.95: auto-referência — grava o próprio box no slot do env (ponto fixo)
  if (!l->selfName.empty() && !l->captures.empty() && env != "null") {
    for (size_t i = 0; i < l->captures.size(); i++) {
      if (l->captures[i].first == l->selfName) {
        std::string selfSlot = newTemp();
        out_ << "  " << selfSlot << " = getelementptr inbounds i8, ptr " << env
             << ", i64 " << (i * 8) << "\n";
        std::string selfSlot2 = newTemp();
        out_ << "  " << selfSlot2 << " = bitcast ptr " << selfSlot << " to ptr\n";
        out_ << "  store ptr " << box << ", ptr " << selfSlot2 << ", align 8\n";
        break;
      }
    }
  }
  return box;
}

std::string Irgen::genHirIndirectCall(const HirCall* c) {
  // v0.95: `f(args)` com f valor `func` — code+env do handle; env é o
  // primeiro argumento (como `this`), args tipados pela assinatura.
  const Type& ft = c->funcValue->type;
  std::string box = genHirExpr(c->funcValue.get());
  std::string codePtr = newTemp();
  out_ << "  " << codePtr << " = bitcast ptr " << box << " to ptr\n";
  std::string code = newTemp();
  out_ << "  " << code << " = load ptr, ptr " << codePtr << ", align 8\n";
  std::string envPtr = newTemp();
  out_ << "  " << envPtr << " = getelementptr inbounds i8, ptr " << box << ", i64 8\n";
  std::string envPtr2 = newTemp();
  out_ << "  " << envPtr2 << " = bitcast ptr " << envPtr << " to ptr\n";
  std::string env = newTemp();
  out_ << "  " << env << " = load ptr, ptr " << envPtr2 << ", align 8\n";
  std::string args = "ptr " + env;
  for (size_t i = 0; i < c->args.size(); i++) {
    std::string v = genHirExpr(c->args[i].get());
    Type pt = (i < ft.genericArgs.size()) ? ft.genericArgs[i] : c->args[i]->type;
    // converte p/ o tipo do parametro (ex.: double -> float); no-op se iguais
    v = convertForStore(v, c->args[i]->type, llvmType(pt));
    args += ", " + llvmCallOperand(v, llvmType(pt));
  }
  std::string retTy = "void";
  if (c->type.kind != Type::Kind::Void) retTy = llvmReturnType(c->type);
  if (retTy == "void") {
    out_ << "  call void " << code << "(" << args << ")\n";
    return "";
  }
  std::string tmp = newTemp();
  out_ << "  " << tmp << " = call " << retTy << " " << code << "(" << args << ")\n";
  return tmp;
}

// ---------------------------------------------------------------------------
// Tarefas sintéticas (`spawn` / `parallel`): corpo adiado (genPendingTasks)
// ---------------------------------------------------------------------------
std::string Irgen::makeTaskLabel() {
  return "@__task." + std::to_string(taskCounter_++);
}

void Irgen::queueTask(std::unique_ptr<HirBlock> body,
                      const std::vector<std::pair<std::string, Type>>& captures) {
  PendingTask t;
  t.fnLabel = makeTaskLabel();
  t.captures = captures;
  t.body = std::move(body);
  pendingTasks_.push_back(std::move(t));
}

// ---------------------------------------------------------------------------
// Lambdas sintéticas (v0.95): `__lambda.N(env, params...)` + handle {code, env}
// ---------------------------------------------------------------------------
std::string Irgen::makeLambdaLabel() {
  return "@__lambda." + std::to_string(lambdaCounter_++);
}

void Irgen::queueLambda(std::unique_ptr<HirBlock> body,
                        const std::vector<std::pair<std::string, Type>>& params,
                        const Type& ret,
                        const std::vector<std::pair<std::string, Type>>& captures) {
  PendingLambda l;
  l.fnLabel = makeLambdaLabel();
  l.params = params;
  l.retType = ret;
  l.captures = captures;
  l.body = std::move(body);
  pendingLambdas_.push_back(std::move(l));
}

// converte o payload i64 (HphlTask::result / hphl_wait_task) para o tipo
// LLVM do valor final (float: bits → double; escalar: trunc; ponteiro: inttoptr)
std::string Irgen::castPayloadFromI64(const std::string& v, const Type& t) {
  std::string llvmTy = llvmType(t);
  std::string tmp = newTemp();
  if (t.kind == Type::Kind::Float) {
    out_ << "  " << tmp << " = bitcast i64 " << v << " to double\n";
    return tmp;
  }
  if (t.kind == Type::Kind::Int || t.kind == Type::Kind::UInt) {
    if (llvmTy != "i64" && llvmTy != "ptr") {
      out_ << "  " << tmp << " = trunc i64 " << v << " to " << llvmTy << "\n";
      return tmp;
    }
    return v;
  }
  if (t.kind == Type::Kind::Bool || t.kind == Type::Kind::Char) {
    out_ << "  " << tmp << " = trunc i64 " << v << " to i8\n";
    return tmp;
  }
  // ponteiro (string, classe, list, task, channel, ...)
  if (isPointerType(t)) {
    out_ << "  " << tmp << " = inttoptr i64 " << v << " to " << llvmTy << "\n";
    return tmp;
  }
  return v;
}

// estende um valor escalar para i64 (slot de env/payload de 8 bytes)
std::string Irgen::extendToI64(const std::string& v, const Type& t) {
  std::string llvmTy = llvmType(t);
  if (t.isPointer()) {
    std::string tmp = newTemp();
    out_ << "  " << tmp << " = ptrtoint " << llvmTy << " " << v << " to i64\n";
    return tmp;
  }
  if (llvmTy == "i64") return v;
  std::string tmp = newTemp();
  // narrowing (int<128>+) exige trunc; widening usa sext/zext
  int bw = 0;
  if (llvmTy.size() > 1 && llvmTy[0] == 'i') {
    for (size_t k = 1; k < llvmTy.size(); k++)
      if (llvmTy[k] >= '0' && llvmTy[k] <= '9') bw = bw * 10 + (llvmTy[k] - '0');
      else { bw = 0; break; }
  }
  if (bw == 0) bw = t.bits;
  if (bw > 64)
    out_ << "  " << tmp << " = trunc " << llvmTy << " " << v << " to i64\n";
  else if (t.kind == Type::Kind::UInt)
    out_ << "  " << tmp << " = zext " << llvmTy << " " << v << " to i64\n";
  else if (t.kind == Type::Kind::Int || t.kind == Type::Kind::Bool ||
           t.kind == Type::Kind::Char)
    out_ << "  " << tmp << " = sext " << llvmTy << " " << v << " to i64\n";
  else
    out_ << "  " << tmp << " = bitcast " << llvmTy << " " << v << " to i64\n";
  return tmp;
}

void Irgen::collectArrayBytes(Expr* el, const Type& arrayType, std::vector<uint8_t>& bytes) {
  if (!el) return;
  if (el->kind == ExprKind::ArrayLit) {
    Type elemType = arrayType.elem ? *arrayType.elem : Type::makeInt(32);
    for (auto& c : static_cast<ArrayLitExpr*>(el)->elements)
      collectArrayBytes(c.get(), elemType, bytes);
    return;
  }
  // always 8-byte stride per element (matches x64 .quad layout)
  if (el->kind == ExprKind::IntLit) {
    long long val = static_cast<IntLitExpr*>(el)->value;
    for (int i = 0; i < 8; i++) {
      bytes.push_back(static_cast<uint8_t>(val & 0xFF));
      val >>= 8;
    }
  } else if (el->kind == ExprKind::FloatLit) {
    double d = static_cast<FloatLitExpr*>(el)->value;
    uint64_t raw;
    memcpy(&raw, &d, sizeof(raw));
    for (int i = 0; i < 8; i++) { bytes.push_back(static_cast<uint8_t>(raw & 0xFF)); raw >>= 8; }
  } else if (el->kind == ExprKind::BoolLit) {
    uint8_t v = static_cast<BoolLitExpr*>(el)->value ? 1 : 0;
    bytes.push_back(v);
    for (int i = 1; i < 8; i++) bytes.push_back(0);
  } else if (el->kind == ExprKind::CharLit) {
    uint8_t v = static_cast<uint8_t>(static_cast<CharLitExpr*>(el)->value);
    bytes.push_back(v);
    for (int i = 1; i < 8; i++) bytes.push_back(0);
  } else {
    for (int i = 0; i < 8; i++) bytes.push_back(0);
  }
}

// site de spawn: monta o env das capturas (malloc + cópias dos valores atuais
// dos locais), e chama hphl_spawn_task_ex(fn, env). Devolve o handle
// (HphlTask*, ptr) como SSA — mesmo desenho do emitSpawnSite x64.
std::string Irgen::emitSpawnSite(
    const std::string& taskLabel,
    const std::vector<std::pair<std::string, Type>>& captures) {
  spawnedAny_ = true;
  std::string env;
  if (!captures.empty()) {
    env = newTemp();
    out_ << "  " << env << " = call noalias ptr @malloc(i64 "
         << (captures.size() * 8) << ")\n";
    for (size_t i = 0; i < captures.size(); i++) {
      auto capVar = hirLocal(captures[i].first, captures[i].second);
      std::string val = genHirExpr(capVar.get());
      std::string slot = newTemp();
      out_ << "  " << slot << " = getelementptr inbounds i8, ptr " << env
           << ", i64 " << (i * 8) << "\n";
      std::string slotPtr = newTemp();
      out_ << "  " << slotPtr << " = bitcast ptr " << slot << " to ptr\n";
      if (isPointerType(captures[i].second)) {
        out_ << "  store ptr " << llvmInstrOperand(val, llvmType(captures[i].second))
             << ", ptr " << slotPtr << ", align 8\n";
      } else {
        std::string v64 = extendToI64(val, captures[i].second);
        std::string slot64 = newTemp();
        out_ << "  " << slot64 << " = bitcast ptr " << slot << " to i64*\n";
        out_ << "  store i64 " << v64 << ", i64* " << slot64 << ", align 8\n";
      }
    }
  } else {
    env = "null";
  }
  std::string h = newTemp();
  out_ << "  " << h << " = call ptr @hphl_spawn_task_ex(void (ptr, ptr)* "
       << taskLabel << ", ptr " << env << ")\n";
  return h;
}

// constrói um HirVar sintético para ler o local capturado (o corpo da task
// referencia os nomes das capturas; emitSpawnSite roda no frame do caller)
std::unique_ptr<HirExpr> Irgen::hirLocal(const std::string& name, const Type& t) {
  auto v = std::make_unique<HirVar>();
  v->name = name;
  v->type = t;
  return v;
}

// emite os corpos das tarefas adiadas (após as funções regulares; uma tarefa
// v0.95 (lambdas): emite `define ret @__lambda.N(ptr %.env, params...)`.
// Capturas recarregadas do env em allocas; params com spill como em genFunction.
void Irgen::genPendingLambdas() {
  for (size_t i = 0; i < pendingLambdas_.size(); i++) {
    PendingLambda& l = pendingLambdas_[i];
    // contexto de função: return usa curFnRetType_; lambda nunca é task
    Type prevRet = curFnRetType_;
    FunctionDecl* prevDecl = curFnDecl_;
    bool prevTask = inTask_;
    curFnRetType_ = l.retType;
    curFnDecl_ = nullptr;
    inTask_ = false;
    std::string retTy = "void";
    if (l.retType.kind != Type::Kind::Void) retTy = llvmReturnType(l.retType);
    std::string params = "ptr %.env";
    for (size_t k = 0; k < l.params.size(); k++)
      params += ", " + llvmType(l.params[k].second) + " %." + l.params[k].first;
    out_ << "\n; Lambda: " << l.fnLabel << "\n";
    out_ << "define " << retTy << " " << l.fnLabel << "(" << params << ") {\n";
    std::ostringstream bodyBuf;
    out_.swap(bodyBuf);
    paramNames_.clear();
    prologue_.str("");
    prologue_.clear();
    // capturas do env → allocas locais
    for (size_t k = 0; k < l.captures.size(); k++) {
      const Type& ct = l.captures[k].second;
      std::string capSlot = emitAllocaSlot(l.captures[k].first, ct);
      std::string slotPtr = newTemp();
      out_ << "  " << slotPtr << " = getelementptr inbounds i8, ptr %.env, i64 "
           << (k * 8) << "\n";
      if (isPointerType(ct)) {
        std::string sp = newTemp();
        out_ << "  " << sp << " = bitcast ptr " << slotPtr << " to ptr\n";
        std::string v = newTemp();
        out_ << "  " << v << " = load ptr, ptr " << sp << ", align 8\n";
        out_ << "  store ptr " << v << ", ptr " << capSlot << ", align 8\n";
      } else {
        std::string s64 = newTemp();
        out_ << "  " << s64 << " = bitcast ptr " << slotPtr << " to i64*\n";
        std::string v = newTemp();
        out_ << "  " << v << " = load i64, i64* " << s64 << ", align 8\n";
        std::string back = castPayloadFromI64(v, ct);
        out_ << "  store " << llvmType(ct) << " " << back << ", "
             << ptrTo(llvmType(ct)) << " " << capSlot << ", align 8\n";
      }
    }
    // params: spill em allocas (leituras passam pelos slots)
    for (size_t k = 0; k < l.params.size(); k++) {
      const std::string& pName = l.params[k].first;
      Type pT = l.params[k].second;
      std::string slot = emitAllocaSlot(pName, pT);
      std::string llvmTy = llvmType(pT);
      out_ << "  store " << llvmTy << " %." << pName << ", "
           << ptrTo(llvmTy) << " " << slot << ", align 8\n";
    }
    returned_ = false;
    wasmCatchStack_.clear();
    wasmPropLabel_.clear();
    genBlock(l.body.get());
    if (!wasmPropLabel_.empty()) {
      if (!curBlockTerminated())
        out_ << "  br label %" << wasmPropLabel_ << "\n";
      out_ << wasmPropLabel_ << ":\n";
      if (retTy == "void") out_ << "  ret void\n";
      else if (retTy == "ptr") out_ << "  ret ptr null\n";
      else out_ << "  ret " << retTy << " 0\n";
    }
    if (!returned_) {
      if (retTy == "void") out_ << "  ret void\n";
      else if (retTy == "ptr") out_ << "  ret ptr null\n";
      else out_ << "  ret " << retTy << " 0\n";
    }
    out_ << "}\n";
    std::string finalIr = bodyBuf.str() + prologue_.str() + out_.str();
    out_.str("");
    out_ << finalIr;
    curFnRetType_ = prevRet;
    curFnDecl_ = prevDecl;
    inTask_ = prevTask;
  }
  pendingLambdas_.clear();
}

// pode criar outras — paralelismo aninhado — então itera por índice)
void Irgen::genPendingTasks() {
  for (size_t i = 0; i < pendingTasks_.size(); i++) {
    PendingTask& t = pendingTasks_[i];
    bool prevTask = inTask_;
    inTask_ = true;
    std::string label = t.fnLabel;
    out_ << "\n; Task: " << label << "\n";
    out_ << "define void " << label << "(ptr %.env, ptr %.res) {\n";
    std::ostringstream bodyBuf;
    out_.swap(bodyBuf);
    // prólogo: materializa as capturas do env como locais próprios da tarefa
    paramNames_.clear();
    prologue_.str("");
    prologue_.clear();
    for (size_t k = 0; k < t.captures.size(); k++) {
      const Type& ct = t.captures[k].second;
      std::string capSlot = emitAllocaSlot(t.captures[k].first, ct);
      std::string slotPtr = newTemp();
      out_ << "  " << slotPtr << " = getelementptr inbounds i8, ptr %.env, i64 "
           << (k * 8) << "\n";
      if (isPointerType(ct)) {
        std::string sp = newTemp();
        out_ << "  " << sp << " = bitcast ptr " << slotPtr << " to ptr\n";
        std::string v = newTemp();
        out_ << "  " << v << " = load ptr, ptr " << sp << ", align 8\n";
        out_ << "  store ptr " << v << ", ptr " << capSlot << ", align 8\n";
      } else {
        std::string s64 = newTemp();
        out_ << "  " << s64 << " = bitcast ptr " << slotPtr << " to i64*\n";
        std::string v = newTemp();
        out_ << "  " << v << " = load i64, i64* " << s64 << ", align 8\n";
        // converte de volta ao tipo original
        std::string back = castPayloadFromI64(v, ct);
        out_ << "  store " << llvmType(ct) << " " << back << ", "
             << ptrTo(llvmType(ct)) << " " << capSlot << ", align 8\n";
      }
    }
    returned_ = false;
    wasmCatchStack_.clear();
    wasmPropLabel_.clear();
    genBlock(t.body.get());
    // M14.3: bloco de propagação do EH por flag dentro da task (ret void)
    if (!wasmPropLabel_.empty()) {
      if (!curBlockTerminated())
        out_ << "  br label %" << wasmPropLabel_ << "\n";
      out_ << wasmPropLabel_ << ":\n";
      out_ << "  ret void\n";
    }
    if (!returned_) out_ << "  ret void\n";
    out_ << "}\n";
    // monta: [define][ALLOCAS das capturas][corpo] (mesmo modelo do genFunction)
    std::string finalIr = bodyBuf.str() + prologue_.str() + out_.str();
    out_.str("");
    out_ << finalIr;
    inTask_ = prevTask;
  }
  pendingTasks_.clear();

  // wrappers de chamadas async (gerados após as tarefas regulares)
  for (auto& w : asyncWrappers_) {
    FunctionDecl* f = w.fn;
    std::string fname = fnLabel(f);
    wasmCatchStack_.clear();
    wasmPropLabel_.clear();
    out_ << "\n; Async wrapper: " << fname << "\n";
    out_ << "define void " << w.fnLabel << "(ptr %.env, ptr %.res) {\n";
    // lê os parâmetros do env (por valor, mesmo desenho do prólogo async x64)
    std::string innerArgs;
    size_t envOff = 0;
    if (f->isMethod && !f->isStatic) {
      // A3: método async — `this` é o arg 0 do env; a função interna espera
      // %.this primeiro (sem isso o receiver era descartado → lixo)
      std::string tslot = newTemp();
      out_ << "  " << tslot << " = getelementptr inbounds i8, ptr %.env, i64 0\n";
      std::string tsp = newTemp();
      out_ << "  " << tsp << " = bitcast ptr " << tslot << " to ptr\n";
      std::string tv = newTemp();
      out_ << "  " << tv << " = load ptr, ptr " << tsp << ", align 8\n";
      innerArgs = llvmCallOperand(tv, "ptr");
      envOff = 1;
    }
    for (size_t i = 0; i < f->params.size(); i++) {
      const Type& pt = f->params[i]->type;
      std::string slot = newTemp();
      out_ << "  " << slot << " = getelementptr inbounds i8, ptr %.env, i64 "
           << ((i + envOff) * 8) << "\n";
      std::string v;
      if (isPointerType(pt)) {
        std::string sp = newTemp();
        out_ << "  " << sp << " = bitcast ptr " << slot << " to ptr\n";
        v = newTemp();
        out_ << "  " << v << " = load ptr, ptr " << sp << ", align 8\n";
        std::string b = newTemp();
        out_ << "  " << b << " = bitcast ptr " << v << " to " << llvmType(pt)
             << "\n";
        v = b;
      } else {
        std::string s64 = newTemp();
        out_ << "  " << s64 << " = bitcast ptr " << slot << " to i64*\n";
        std::string l = newTemp();
        out_ << "  " << l << " = load i64, i64* " << s64 << ", align 8\n";
        v = castPayloadFromI64(l, pt);
      }
      if (!innerArgs.empty()) innerArgs += ", ";
      innerArgs += llvmCallOperand(v, llvmType(pt));
    }
    if (f->returnType.kind == Type::Kind::Void) {
      out_ << "  call void " << fname << "(" << innerArgs << ")\n";
      out_ << "  ret void\n";
    } else {
      std::string r = newTemp();
      out_ << "  " << r << " = call " << llvmReturnType(f->returnType) << " "
           << fname << "(" << innerArgs << ")\n";
      // grava o payload em res (i64 bits — o await converte de volta)
      if (isPointerType(f->returnType)) {
        std::string p = newTemp();
        out_ << "  " << p << " = ptrtoint " << llvmType(f->returnType) << " "
             << r << " to i64\n";
        std::string s = newTemp();
        out_ << "  " << s << " = bitcast ptr %.res to i64*\n";
        out_ << "  store i64 " << p << ", i64* " << s << ", align 8\n";
      } else {
        std::string v64 = extendToI64(r, f->returnType);
        std::string s = newTemp();
        out_ << "  " << s << " = bitcast ptr %.res to i64*\n";
        out_ << "  store i64 " << v64 << ", i64* " << s << ", align 8\n";
      }
      out_ << "  ret void\n";
    }
    out_ << "}\n";
  }
  asyncWrappers_.clear();
}

// struct: classe com semântica de valor — cópia nas fronteiras de passagem
bool Irgen::isStructType(const Type& t) const {
  if (t.kind != Type::Kind::Class) return false;
  auto it = sem_.classes().find(t.name);
  return it != sem_.classes().end() && it->second.decl->isStruct;
}

// A2 (interface como tipo): Class cujo decl é interface
bool Irgen::isInterfaceType(const Type& t) const {
  if (t.kind != Type::Kind::Class) return false;
  auto it = sem_.classes().find(t.name);
  return it != sem_.classes().end() && it->second.decl &&
         it->second.decl->isInterface;
}

// envolve o valor (ponteiro do bloco) em hphl_struct_copy → bloco independente
std::string Irgen::genStructCopy(const Type& t, const std::string& val) {
  auto& ci = sem_.classes().at(t.name);
  std::string tmp = newTemp();
  out_ << "  " << tmp << " = call ptr @hphl_struct_copy(ptr " << val
       << ", i64 " << ci.size << ")\n";
  return tmp;
}

// expr que já produz bloco fresco (New / chamada de ctor) — não aliam
bool Irgen::isFreshAlloc(const HirExpr* e) const {
  if (!e) return false;
  if (e->kind == HirExprKind::New) return true;
  if (e->kind == HirExprKind::Call) {
    auto* c = static_cast<const HirCall*>(e);
    return c->resolved && c->resolved->isConstructor;
  }
  return false;
}

} // namespace hphl


