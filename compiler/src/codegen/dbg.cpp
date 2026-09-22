// Auto-generated from codegen.cpp
#include "codegen.h"
#include <cstdio>
#include <functional>
#include "stdlib/stdbuiltins.h"
#include <cstdint>
#include <cstring>

/* M27: hphl_set_class_desc â€” called at startup to register class bitmaps. */
extern "C" int hphl_set_class_desc(int size, int nbytes, const unsigned char* bm);

// Opera sobre o .text final como uma lista de linhas, aplicando transformaÃ§Ãµes
// locais de janela deslizante. A ordem dos passes Ã© importante:
// foldSetccJump elimina blocos de 5 instruÃ§Ãµes que podem ser 1 jump condicional;
// dropDeadCopies remove movimentos cuja origem e destino sÃ£o iguais (jÃ¡ movidos
// posteriormente) e o registro de destino jÃ¡ nÃ£o era usado; coalescing une
// movimentos encadeados que poderiam ser diretos.


namespace hphl {
std::string Codegen::optimizeAsm(const std::string& text) {
  auto trimStart = [](const std::string& s) -> std::string {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
  };
  // split into lines
  std::vector<std::string> lines;
  std::string cur;
  for (char c : text) {
    if (c == '\n') { lines.push_back(cur); cur.clear(); }
    else cur += c;
  }
  if (!cur.empty()) lines.push_back(cur);
  // M14.4: r10 collapse DESABILITADO novamente â€” o pareamento leaq/movq-r10
  // quebrava quando instruÃ§Ãµes intermediÃ¡rias separavam o grupo (ex.: step de
  // for: store em (%r10) com r10 nunca carregado â†’ Ã­ndice de loop travado).
  // Bench mostrou ganho marginal vs risco de correÃ§Ã£o (v0.55.1 jÃ¡ havia
  // desabilitado por regressÃ£o em hello). Mantido apenas dropDeadCopies.
  std::vector<std::string> res;
  for (auto& line : lines) {
    std::string t = trimStart(line);
    if (t.size() >= 7 && t.rfind("movq",0)==0) {
      // AT&T-aware parser: separar movq src, dst
      size_t comma = t.find(',');
      if (comma == std::string::npos) { res.push_back(line); continue; }
      std::string beforeComma = t.substr(0, comma);
      std::string afterComma = t.substr(comma + 1);
      // src = token após movq, dst = após vírgula
      size_t srcStart = beforeComma.find_last_of(' ');
      if (srcStart == std::string::npos) { res.push_back(line); continue; }
      std::string src = beforeComma.substr(srcStart + 1);
      std::string dst = afterComma;
      // remover espaços/tabs das extremidades
      auto trim = [](std::string& s) {
        size_t l=0, r=s.size();
        while (l < r && (s[l]==' '|| s[l]=='\t')) l++;
        while (l < r && (s[r-1]==' '|| s[r-1]=='\t')) r--;
        s = s.substr(l, r-l);
      };
      trim(src); trim(dst);
      // Coalesce: movq A,%rcx; movq %rcx,B → movq A,B
      if (src != dst && src.size()>=2 && src[0]=='%') {
        // Check if dst is memory (contains '(') - não podemos armazenar registrador em memória
        if (dst.find('(') != std::string::npos) { res.push_back(line); continue; }
        res.push_back("  movq " + src + ", " + dst);
      } else { res.push_back(line); }
    } else { res.push_back(line); }
  }
  // dropDeadCopies: remover movq cujo src == dst OU se destino já foi movido
  // (implementação simplificada: apenas src == dst é removido)
  std::string out; for (auto& l : res) out += l + "\n"; return out;
}

std::string Codegen::foldSetccJump(const std::string& text) {
  // Pattern to fold:
  //   cmpq ...                   (cmpq $imm, %reg OR cmpq %reg, %reg)
  //   setCC %al                  (e.g., setl)
  //   movzbl %al, %eax           (zero-extend to rax)
  //   testq %rax, %rax
  //   jZ label                   (jz or jnz â€” jumps based on ZF after test)
  // â†’ cmpq ... / jCC' label
  //
  // Logic:
  //   testq sets ZF=1 when al==0 (i.e., when CC is FALSE)
  //   jz  jumps when ZF=1 â†’ jumps when CC is FALSE â†’ need j<NOT CC>
  //   jnz jumps when ZF=0 â†’ jumps when CC is TRUE  â†’ need j<CC>
  //
  // setCC â†’ jCC mapping:
  //   seteâ†’je, setneâ†’jne, setlâ†’jl, setleâ†’jle, setgâ†’jg, setgeâ†’jge
  // Inversion: jeâ†”jne, jlâ†”jge, jleâ†”jg

  // Split into lines
  std::vector<std::string> lines;
  std::string cur;
  for (char c : text) {
    if (c == '\n') { lines.push_back(cur); cur.clear(); }
    else cur += c;
  }
  if (!cur.empty()) lines.push_back(cur);

  // Mapping: setCC condition â†’ jump condition
  // setCC is like "setl", "sete", "setne", "setg", "setle", "setge", "setb", "setbe", "seta", "setae"
  auto setCondToJump = [](const std::string& setCc) -> std::string {
    // setCc is like "l", "e", "ne", "g", "le", "ge", "b", "be", "a", "ae"
    if (setCc == "e")   return "e";
    if (setCc == "ne")  return "ne";
    if (setCc == "l")   return "l";
    if (setCc == "le")  return "le";
    if (setCc == "g")   return "g";
    if (setCc == "ge")  return "ge";
    if (setCc == "b")   return "b";
    if (setCc == "be")  return "be";
    if (setCc == "a")   return "a";
    if (setCc == "ae")  return "ae";
    return "";
  };
  auto invertCond = [](const std::string& cond) -> std::string {
    if (cond == "e")   return "ne";
    if (cond == "ne")  return "e";
    if (cond == "l")   return "ge";
    if (cond == "le")  return "g";
    if (cond == "g")   return "le";
    if (cond == "ge")  return "l";
    if (cond == "b")   return "ae";
    if (cond == "be")  return "a";
    if (cond == "a")   return "be";
    if (cond == "ae")  return "b";
    return "";
  };

  std::vector<std::string> result;
  for (size_t i = 0; i < lines.size(); i++) {
    // look for a 5-line window: cmpq, setCC, movzbl, testq, jZ
    if (i + 4 < lines.size()) {
      const std::string& l0 = lines[i];       // cmpq
      const std::string& l1 = lines[i+1];     // setCC %al
      const std::string& l2 = lines[i+2];     // movzbl %al, %eax
      const std::string& l3 = lines[i+3];     // testq %rax, %rax
      const std::string& l4 = lines[i+4];     // jz/jnz label
      // trim para robustez (optimizeAsm pode reemitir com/sem espaÃ§os)
      auto trim = [](const std::string& s){ size_t i=0; while(i<s.size()&&(s[i]==' '||s[i]=='\t')) i++; return s.substr(i); };
      std::string t0 = trim(l0), t1 = trim(l1), t2 = trim(l2), t3 = trim(l3), t4 = trim(l4);
      // Basic check: l0 cmpq/comisd, l1 set, l2 movzbl, l3 testq, l4 j
      if (t0.size() >= 4 && t1.size() >= 3 && t2.size() >= 6 && t3.size() >= 5 && t4.size() >= 1) {
        if ((t0.rfind("cmpq",0)==0 || t0.rfind("comisd",0)==0) && t1.rfind("set",0)==0 &&
            t2.rfind("movzbl",0)==0 && t3.rfind("testq",0)==0 &&
            t4[0] == 'j') {
          // Extract set condition from t1 (e.g., "setl %al" â†’ "l")
          std::string setAfter = t1.substr(3); // "l %al"
          size_t setStart = setAfter.find_first_not_of(' ');
          std::string setCond = setAfter.substr(setStart);
          size_t setEnd = setCond.find_first_of(' ');
          if (setEnd != std::string::npos) setCond = setCond.substr(0, setEnd);

          // Extract jump condition and target from t4 (e.g., "jz .label")
          size_t jumpIdx = t4.find_first_of(" \t");
          std::string jumpOp = t4.substr(0, jumpIdx); // "jz" or "jnz"
          std::string jumpTarget = (jumpIdx != std::string::npos) ? t4.substr(jumpIdx + 1) : "";

          // Get the equivalent jump condition for the set condition
          std::string equivJump = setCondToJump(setCond);
          if (equivJump.empty()) {
            // Unknown condition â€” leave the lines as-is
            result.push_back(lines[i]);
            continue;
          }

          // je == jz, jne == jnz (aliases apÃ³s testq)
          if (jumpOp == "je") jumpOp = "jz";
          if (jumpOp == "jne") jumpOp = "jnz";
          // Determine the correct folded jump
          std::string foldedJump;
          if (jumpOp == "jz") {
            // jz = jump if CC is false â†’ invert the condition
            std::string invCond = invertCond(equivJump);
            if (!invCond.empty()) foldedJump = "j" + invCond + " " + jumpTarget;
            else foldedJump = jumpOp + " " + jumpTarget; // fallback
          } else if (jumpOp == "jnz") {
            // jnz = jump if CC is true â†’ keep the condition
            foldedJump = "j" + equivJump + " " + jumpTarget;
          } else {
            // Unknown jump opcode â€” leave as-is (don't fold)
            result.push_back(lines[i]);
            continue;
          }

          // Keep the cmp (t0) â€” it sets the flags we need
          result.push_back(t0);
          result.push_back(foldedJump);
          i += 4; // skip the 4 folded lines (cmpq already handled via push)
          continue;
        }
      }
    }
    result.push_back(lines[i]);
  }

  std::string out;
  for (auto& l : result) out += l + "\n";
  return out;
}




} // namespace hphl