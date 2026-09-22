// Auto-generated from codegen.cpp
#include "codegen.h"
#include <cstdio>
#include <functional>
#include "stdlib/stdbuiltins.h"
#include <cstdint>
#include <cstring>

/* M27: hphl_set_class_desc â€” called at startup to register class bitmaps. */
extern "C" int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);


namespace hphl {
bool Codegen::isGcPointer(const Type& t) {
  switch (t.kind) {
    case Type::Kind::String:
    case Type::Kind::Class:
    case Type::Kind::Array:
    case Type::Kind::List:
    case Type::Kind::Map:
    case Type::Kind::Tuple:
    case Type::Kind::Channel:
    case Type::Kind::Task:
    case Type::Kind::Option:
    case Type::Kind::Result:
    case Type::Kind::Enum:
    case Type::Kind::Func: // v0.95: closure box {code, env} é GC pointer
      return true;
    default: return false;
  }
}

void Codegen::emitClassDesc(const std::string& canon, const ClassInfo& ci) {
  int sz = classSize(canon);
  int slots = (sz + 7) / 8;
  int nbytes = (slots + 7) / 8;
  std::vector<unsigned char> bm(nbytes, 0);
  // coleta campos incluindo base
  std::function<void(const std::string&)> walk = [&](const std::string& c) {
    auto it = sem_.classes().find(c);
    if (it == sem_.classes().end()) return;
    const ClassInfo& cci = it->second;
    if (!cci.base.empty()) walk(cci.base);
    for (auto& kv : cci.fields) {
      const Type& ft = kv.second.first;
      int off = kv.second.second;
      if (!isGcPointer(ft)) continue;
      // offset em bytes; vptr em 0 pode deslocar, mas field offset já inclui vptr
      int slot = off / 8;
      // corrige offset negativo da moldura? fields são positivos dentro do objeto (0..size)
      // No layout, offset para campos é negativo? Ajustar: classSize calcula com offset crescente desde 8 (vptr)
      // Se off negativo, mapear com sz+off
      if (off < 0) slot = (sz + off) / 8;
      if (slot < 0 || slot >= slots) continue;
      // M32: arrays inline de ponteiros (ex: string[120000] no CG): CADA
      // elemento é um slot GC e precisa de bit próprio. Antes só o elemento 0
      // era marcado — o sweep liberava strings/objetos vivos em arr[1..]
      // (crash do bootstrap no amálgama + repro gcarr3). Elementos struct
      // (multi-slot) mantêm só o bit base; o resto cai no fallback
      // conservador do gc_scan_obj.
      int totalSlots = 1;
      if (ft.kind == Type::Kind::Array) {
        const Type* pe = &ft;
        long long nelems = 1;
        while (pe->kind == Type::Kind::Array && pe->elem) {
          if (pe->arraySize <= 0) { nelems = 1; break; }  // dimensão simbólica: só base
          nelems *= pe->arraySize;
          pe = pe->elem.get();
          if (nelems > slots) break;  // saturação, o bound-check abaixo limita
        }
        if (pe != &ft && nelems > 1 && isGcPointer(*pe) && typeSize(*pe) == 8) {
          totalSlots = (nelems > slots) ? slots : (int)nelems;
        }
      }
      for (int k = 0; k < totalSlots; k++) {
        int s2 = slot + k;
        if (s2 < 0 || s2 >= slots) break;
        bm[s2 / 8] |= (1u << (s2 % 8));
      }
    }
  };
  walk(canon);

  /* M27: assign classId and emit hphl_set_class_desc call */
  int cid = nextClassId_++;
  classIdMap_[canon] = cid;

  std::string label = "hphl_class_desc_" + canon;
  rodata_ << label << ":\n";
  rodata_ << "  .quad " << sz << "  # size\n";
  rodata_ << "  .quad " << nbytes << "  # bitmap bytes\n";
  if (nbytes > 0) {
    rodata_ << "  .byte ";
    for (int i = 0; i < nbytes; i++) {
      if (i) rodata_ << ", ";
      rodata_ << (int)bm[i];
    }
    rodata_ << "\n";
  }
  rodata_ << "  .align 8\n";

  /* M27: emit call to hphl_set_class_desc(size, nbytes, &bitmap)
   * Windows x64 ABI: arg1=rcx, arg2=rdx, arg3=r8 */
  classInitText_ << "  # === class init " + canon + " id=" + std::to_string(cid) + " ===\n";
  classInitText_ << "  movq $" + std::to_string(sz) + ", %rcx\n";
  classInitText_ << "  movq $" + std::to_string(nbytes) + ", %rdx\n";
  classInitText_ << "  leaq " + label + "+16(%rip), %r8\n";
  classInitText_ << "  subq $32, %rsp\n";
  classInitText_ << "  call hphl_set_class_desc\n";
  classInitText_ << "  addq $32, %rsp\n";
}

int Codegen::dbgTypeCode(const Type& t) {
  switch (t.kind) {
    case Type::Kind::Int: return 1;
    case Type::Kind::UInt: return 2;
    case Type::Kind::Float: return 3;
    case Type::Kind::Bool: return 4;
    case Type::Kind::Char: return 5;
    case Type::Kind::String: return 6;
    case Type::Kind::Array: return 7;
    case Type::Kind::List: return 8;
    case Type::Kind::Class: return 9;
    case Type::Kind::Enum: return 10;
    default: return 11; // ponteiro opaco
  }
}

int Codegen::dbgElemCode(const Type& t) {
  return t.elem ? dbgTypeCode(*t.elem) : 0;
}

// nome canÃ´nico do tipo para a meta de debug (F1.2): a cadeia completa
// "base[dim][dim]..." permite ao adapter expandir arrays multidimensionais
// e classes sem tabela de tipos separada
std::string Codegen::dbgTypeName(const Type& t) {
  switch (t.kind) {
    case Type::Kind::Int: return "int";
    case Type::Kind::UInt: return "uint";
    case Type::Kind::Float: return "float";
    case Type::Kind::Bool: return "bool";
    case Type::Kind::Char: return "char";
    case Type::Kind::String: return "string";
    case Type::Kind::List:
      // list<T>: "list:T" (T pode ser array/classe/outro list â€” F2.1)
      return t.elem ? "list:" + dbgTypeName(*t.elem) : std::string("list");
    case Type::Kind::Map:
      return t.elem && t.elem2 ? "map:" + dbgTypeName(*t.elem) + ":" + dbgTypeName(*t.elem2) : std::string("map");
    case Type::Kind::Class: return "class:" + t.name;
    case Type::Kind::Enum: return "enum:" + t.name;
    case Type::Kind::Array: {
      // nÃ­vel mais externo primeiro, como a sintaxe: int[2][3] = 2 de (3 de int)
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

void Codegen::dbgAddLocal(const std::string& name, const Type& t, int slot,
                          bool byRef, bool isArg, bool stackArray) {
  if (dbgFns_.empty()) return;
  DbgVarMeta m;
  m.name = name;
  m.type = t;
  m.slot = slot;
  m.byRef = byRef;
  m.isArg = isArg;
  m.stackArray = stackArray;
  dbgFns_.back().locals.push_back(std::move(m));
}

void Codegen::dbgAddGlobal(const std::string& name, const Type& t, const std::string& label) {
  DbgGlobalMeta m;
  m.name = name;
  m.type = t;
  m.label = label;
  dbgGlobals_.push_back(std::move(m));
}

// Registra a funÃ§Ã£o na tabela e prÃ©-computa os slots dos locais do prÃ³logo
// (this, env/res, params, capturas â€” exatamente na ordem que o prÃ³logo aloca,
// para os dois caminhos: genFunction e genFunctionHir).
void Codegen::dbgStartFunction(FunctionDecl* fn) {
  DbgFnMeta m;
  m.fn = fn;
  m.file = fn->filePath.empty() ? filename_ : fn->filePath;
  dbgFns_.push_back(std::move(m));
  dbgFnId_ = (int)dbgFns_.size() - 1;
  lastTrapLine_ = -1;
  int slot = 0;
  if (fn->isMethod && !fn->isStatic) dbgAddLocal("this", Type::makeClass(fn->ownerClass), slot++, false, true);
  if (fn->isTask || fn->isAsync) slot += 2; // env/res
  for (auto& p : fn->params)
    dbgAddLocal(p->name, p->type, slot++, p->isByRef(), true,
                p->type.kind == Type::Kind::Array && !p->isByRef());
  for (auto& cap : fn->taskCaptures) dbgAddLocal(cap.first, cap.second, slot++, false, false);
}

void Codegen::dbgEndFunction() {
  dbgFnId_ = -1;
  lastTrapLine_ = -1;
}

void Codegen::emitDebugTrap(int line) {
  emitText("movq $" + std::to_string(line) + ", %rcx");
  emitRuntimeCall("hphl_dbg_trap");
  if (!dbgFns_.empty()) dbgFns_.back().trapLines.push_back(line);
}

void Codegen::emitDebugPrologue() {
  // fnId via %r10 (volÃ¡til, sem valor vivo): o enter Ã© chamado ANTES dos
  // stores de args do corpo, e %rcx ainda carrega o 1Âº parÃ¢metro da funÃ§Ã£o
  emitText("movq $" + std::to_string(dbgFnId_) + ", %r10");
  emitRuntimeCall("hphl_dbg_enter");
}

void Codegen::emitDebugEpilogue() {
  emitRuntimeCall("hphl_dbg_leave");
}

std::string Codegen::buildDbgMeta() {
  std::ostringstream out;
  int fnCount = (int)dbgFns_.size();
  int gblCount = (int)dbgGlobals_.size(); // M20-A 4.4: usa os globais registrados
  // classes (F1.2): nome canï¿½nico ? tabela de campos (nome, tipoStr, offset)
  const auto& allClasses = sem_.classes();
  int clsCount = (int)allClasses.size();
  out << ".globl hphl_dbg_meta\n"
      << "hphl_dbg_meta: .quad " << fnCount << ", .Ldbg_fns\n"
      << "  .quad " << gblCount << ", .Ldbg_globals\n"
      << "  .quad " << clsCount << ", .Ldbg_classes\n";
  if (fnCount == 0 && gblCount == 0) return out.str();
  std::string fnsLine = ".Ldbg_fns: .quad";
  for (int i = 0; i < fnCount; i++) fnsLine += " .Ldbg_fn" + std::to_string(i) + ",";
  fnsLine.pop_back();
  out << fnsLine << "\n";
  for (int i = 0; i < fnCount; i++) {
    const auto& m = dbgFns_[i];
    std::string disp =
        m.fn->isMethod ? m.fn->ownerClass + "." + m.fn->name : m.fn->name;
    std::string nameL = ".Ldbg_fn" + std::to_string(i) + "_name";
    std::string fileL = ".Ldbg_fn" + std::to_string(i) + "_file";
    std::string locsL = ".Ldbg_fn" + std::to_string(i) + "_locs";
    std::string trapsL = ".Ldbg_fn" + std::to_string(i) + "_traps";
    out << nameL << ": .asciz \"" << disp << "\"\n";
    out << fileL << ": .asciz \"" << m.file << "\"\n";
    std::string locsLine = locsL + ": .quad";
    for (size_t k = 0; k < m.locals.size(); k++)
      locsLine += " .Ldbg_fn" + std::to_string(i) + "_loc" + std::to_string(k) + ",";
    if (!m.locals.empty()) locsLine.pop_back();
    out << locsLine << "\n";
    for (size_t k = 0; k < m.locals.size(); k++) {
      const auto& l = m.locals[k];
      std::string ln = ".Ldbg_fn" + std::to_string(i) + "_loc" + std::to_string(k);
      out << ln << ": .quad " << ln << "_n, " << ln << "_t, "
          << (8 * (l.slot + 1)) << ", "
          << ((l.byRef ? 1 : 0) | (l.isArg ? 2 : 0) | (l.stackArray ? 4 : 0))
          << "\n";
      out << ln << "_n: .asciz \"" << l.name << "\"\n";
      out << ln << "_t: .asciz \"" << dbgTypeName(l.type) << "\"\n";
    }
    std::string trapsLine = trapsL + ": .quad";
    for (size_t k = 0; k < m.trapLines.size(); k++)
      trapsLine += " " + std::to_string(m.trapLines[k]) + ",";
    if (!m.trapLines.empty()) trapsLine.pop_back();
    out << trapsLine << "\n";
    out << ".Ldbg_fn" << i << ": .quad " << nameL << ", " << fileL << ", 0, "
        << m.locals.size() << ", " << locsL << ", " << m.trapLines.size()
        << ", " << trapsL << "\n";
  }
  std::string gblLine = ".Ldbg_globals: .quad";
  for (int i = 0; i < gblCount; i++) gblLine += " .Ldbg_gbl" + std::to_string(i) + ",";
  if (gblCount > 0) gblLine.pop_back();
  out << gblLine << "\n";
  for (int i = 0; i < gblCount; i++) {
    // M20-A 4.4: usa dbgGlobals_ registrado em genGlobal
    const auto& gm = dbgGlobals_[i];
    out << ".Ldbg_gbl" << i << "_n: .asciz \"" << gm.name << "\"\n";
    out << ".Ldbg_gbl" << i << "_t: .asciz \"" << dbgTypeName(gm.type) << "\"\n";
    out << ".Ldbg_gbl" << i << ": .quad .Ldbg_gbl" << i << "_n, "
        << ".Ldbg_gbl" << i << "_t, -1, " << gm.label << ", "
        << (gm.type.kind == Type::Kind::Array ? 1 : 0) << "\n";
  }
  // classes: tabela de campos (ordem do map = ordem canÃ´nica, estÃ¡vel)
  std::string clsLine = ".Ldbg_classes: .quad";
  for (int i = 0; i < clsCount; i++)
    clsLine += " .Ldbg_cls" + std::to_string(i) + ",";
  if (clsCount > 0) clsLine.pop_back();
  else clsLine += " 0";
  out << clsLine << "\n";
  int ci = 0;
  for (const auto& [cname, cmeta] : allClasses) {
    std::string cn = ".Ldbg_cls" + std::to_string(ci);
    out << cn << "_n: .asciz \"" << cname << "\"\n";
    if (cmeta.fields.empty()) {
      out << cn << "_f: .quad 0\n";
    } else {
      out << cn << "_f: .quad";
      for (size_t k = 0; k < cmeta.fields.size(); k++)
        out << " .Ldbg_cls" << ci << "_fld" << k << ",";
      out.seekp(-1, std::ios_base::cur);
      out << "\n";
    }
    int fi = 0;
    for (const auto& [fname, fmeta] : cmeta.fields) {
      std::string fn = cn + "_fld" + std::to_string(fi);
      out << fn << "_n: .asciz \"" << fname << "\"\n";
      out << fn << "_t: .asciz \"" << dbgTypeName(fmeta.first) << "\"\n";
      out << fn << ": .quad " << fn << "_n, " << fn << "_t, " << fmeta.second
          << "\n";
      fi++;
    }
    out << cn << ": .quad " << cn << "_n, " << cmeta.fields.size() << ", "
        << cn << "_f\n";
    ci++;
  }
  return out.str();
}

std::string Codegen::generate() {
  // offsets das globais threadlocal (ordem de declaraÃ§Ã£o, 8 bytes por slot)
  {
    int tlsNext = 0;
    for (auto* g : sem_.globals())
      if (g->storage == StoragePolicy::ThreadLocal) {
        tlsOffsets_[g->name] = tlsNext;
        tlsNext += 8;
      }
    tlsSizeBytes_ = tlsNext;
  }
  for (auto* g : sem_.globals()) genGlobal(g);
  // M10 (v0.44): vtables â€” por classe nÃ£o-struct, slot i = implementaÃ§Ã£o
  // mais derivada visÃ­vel a partir dela
  for (auto& [canon, ci] : sem_.classes()) {
    if (!ci.hasVptr || ci.isTemplate || ci.decl->isInterface) continue;
    data_ << ".Lvt_" << canon << ":\n";
    for (int s = 0; s < (int)sem_.vslots().size(); s++) {
      FunctionDecl* impl = sem_.findOverrideIn(canon, s);
      data_ << "  .quad " << (impl ? fnLabel(impl) : std::string("0")) << "\n";
    }
  }
  // M21.1 1.2: descritores de classe para GC scan preciso (bitmap)
  for (auto& [canon, ci] : sem_.classes()) {
    if (ci.isTemplate || ci.decl->isInterface) continue;
    if (ci.fields.empty() && ci.base.empty()) continue;
    emitClassDesc(canon, ci);
  }
  generateFunctions();
  if (needExc_) data_ << "hphl_exc_hdl: .quad 0\n"; // handler corrente de try/catch
  // metadados do debugger (M7): zeros quando sem --debug (o runtime.o
  // referencia o sÃ­mbolo em todos os links); buildDbgMeta define com --debug
  if (!debug_)
    data_ << ".globl hphl_dbg_meta\n"
          << "hphl_dbg_meta: .quad 0, 0\n"
          << "  .quad 0, 0\n"
          << "  .quad 0, 0\n";
  else
    data_ << buildDbgMeta();
  // limites de doubles p/ a polÃ­tica de overflow float (relativos a %rip,
  // sempre emitidos â€” 8 bytes por constante)
  data_ << ".align 8\n"
        << ".Lflt_max: .quad 0x47EFFFFFE0000000\n" // FLT_MAX como double
        << ".Lflt_min: .quad 0xC7EFFFFFE0000000\n" // -FLT_MAX como double
        << ".Ldbl_max: .quad 0x7FEFFFFFFFFFFFFF\n"
        << ".Ldbl_min: .quad 0xFFEFFFFFFFFFFFFF\n";

  std::string out = ".section .text\n";
  out += foldSetccJump(optimizeAsm(text_.str()));
  if (data_.str().size()) {
    out += "\n.section .data\n";
    out += data_.str();
  }
  if (rodata_.str().size()) {
    out += "\n.section .rdata\n";
    out += rodata_.str();
  }
  // COMPAT-2 (Sprint 4): string de erro de mismatch de ABI
  out += "\n.section .rdata\n";
  out += ".Lstr_abi_mismatch: .asciz \"HP-HL runtime ABI mismatch (compilado com 100, esperado pelo runtime)\\n\"\n";
  return out;
}




} // namespace hphl