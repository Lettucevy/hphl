#!/usr/bin/env python3
"""Generate HP-HL Runtime C API reference in both English and Portuguese.

Usage:
    python3 tools/gen_runtime_doc.py

Output:
    docs/stdlib/runtime.md     -- English C API reference
    docs/pt/stdlib_runtime.md  -- Portuguese C API reference
    (Also syncs to Web documentation portal if present)
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
RTDIR = os.path.join(ROOT, "compiler", "src", "runtime")
OUT_EN = os.path.join(ROOT, "docs", "stdlib", "runtime.md")
OUT_PT = os.path.join(ROOT, "docs", "pt", "stdlib_runtime.md")

WEB_MD_EN = os.path.normpath(r"C:\Projetos\Web\VelafacePage\hphl\docs\md\stdlib_runtime.md")
WEB_MD_PT = os.path.normpath(r"C:\Projetos\Web\VelafacePage\hphl\docs\md_pt\stdlib_runtime.md")
WEB_BUILD_JS = os.path.normpath(r"C:\Projetos\Web\VelafacePage\hphl\build-docs.js")

sys.path.insert(0, HERE)
from runtime_descriptions import DESCRIPTIONS

# rettype hphl_name(params) {   (line-start anchored; skips static helpers)
DEF_PAT = re.compile(
    r"^(?P<ret>[A-Za-z_][\w\s\*]*?)\s+(?P<name>hphl_\w+)\s*"
    r"\((?P<params>[^;{}]*)\)\s*\{",
    re.M,
)


def get_desc(name: str, lang: str = "en") -> str:
    if name in DESCRIPTIONS:
        return DESCRIPTIONS[name].get(lang, DESCRIPTIONS[name]["en"])
    if lang == "pt":
        return f"Função de execução do runtime HP-HL: `{name}`."
    return f"HP-HL runtime intrinsic: `{name}`."


def generate_doc(groups: dict, lang: str = "en") -> str:
    total = sum(len(v) for v in groups.values())
    lines = []
    
    if lang == "pt":
        lines.append("# Referência da API C do Runtime HP-HL\n")
        lines.append(
            "> **Gerado automaticamente** a partir de `compiler/src/runtime/*/*.c` pelo\n"
            "> `tools/gen_runtime_doc.py`. Não edite manualmente; execute novamente o\n"
            "> script após modificar o runtime.\n"
        )
        lines.append(f"Total: **{total}** funções `hphl_*`.\n")
        lines.append("Consulte também: [Biblioteca Padrão](index.md) (funções integradas do HP-HL).\n")
        lines.append("## Subsistema `libc` (libm C99, chamada direta)\n")
        
        for nm, sig in (
            ("fmin", "double fmin(double x, double y);"),
            ("fmax", "double fmax(double x, double y);"),
        ):
            desc = get_desc(nm, "pt")
            lines.append(f"### {nm}\n\n```c\n{sig}\n```\n\n{desc}\n\n_Origem: `libm`_\n")
            
        for group in sorted(groups):
            lines.append(f"## Subsistema `{group}`\n")
            for name, ret, params, where in sorted(groups[group]):
                desc = get_desc(name, "pt")
                lines.append(f"### {name}\n\n```c\n{ret} {name}({params});\n```\n\n{desc}\n\n_Origem: `{where}`_\n")
    else:
        lines.append("# HP-HL Runtime C API Reference\n")
        lines.append(
            "> **Auto-generated** from `compiler/src/runtime/*/*.c` by\n"
            "> `tools/gen_runtime_doc.py`. Do not edit by hand; re-run the\n"
            "> script after changing the runtime.\n"
        )
        lines.append(f"Total: **{total}** `hphl_*` functions.\n")
        lines.append("Companion: [Standard Library](index.md) (HP-HL builtins).\n")
        lines.append("## Subsystem `libc` (C99 libm, no wrapper)\n")
        
        for nm, sig in (
            ("fmin", "double fmin(double x, double y);"),
            ("fmax", "double fmax(double x, double y);"),
        ):
            desc = get_desc(nm, "en")
            lines.append(f"### {nm}\n\n```c\n{sig}\n```\n\n{desc}\n\n_Source: `libm`_\n")
            
        for group in sorted(groups):
            lines.append(f"## Subsystem `{group}`\n")
            for name, ret, params, where in sorted(groups[group]):
                desc = get_desc(name, "en")
                lines.append(f"### {name}\n\n```c\n{ret} {name}({params});\n```\n\n{desc}\n\n_Source: `{where}`_\n")
                
    return "\n".join(lines)


def main():
    groups = {}
    for dirpath, _, filenames in os.walk(RTDIR):
        for fn in sorted(filenames):
            if not fn.endswith(".c"):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, ROOT).replace(os.sep, "/")
            group = os.path.basename(dirpath)
            with open(full, encoding="utf-8", errors="replace") as f:
                text = f.read()
            for m in DEF_PAT.finditer(text):
                if m.group(0).lstrip().startswith("static"):
                    continue
                name = m.group("name")
                ret = " ".join(m.group("ret").split())
                params = " ".join(m.group("params").split())
                lineno = text.count("\n", 0, m.start())
                groups.setdefault(group, []).append(
                    (name, ret, params, f"{rel}:{lineno + 1}")
                )

    total = sum(len(v) for v in groups.values())

    # Generate English
    os.makedirs(os.path.dirname(OUT_EN), exist_ok=True)
    doc_en = generate_doc(groups, "en")
    with open(OUT_EN, "w", encoding="utf-8", newline="\n") as f:
        f.write(doc_en)
    print(f"Wrote {OUT_EN} ({total} functions)")

    # Generate Portuguese
    os.makedirs(os.path.dirname(OUT_PT), exist_ok=True)
    doc_pt = generate_doc(groups, "pt")
    with open(OUT_PT, "w", encoding="utf-8", newline="\n") as f:
        f.write(doc_pt)
    print(f"Wrote {OUT_PT} ({total} functions)")

    # Sync to Web portal if directories exist
    if os.path.exists(os.path.dirname(WEB_MD_EN)):
        shutil.copyfile(OUT_EN, WEB_MD_EN)
        print(f"Synced -> {WEB_MD_EN}")
    if os.path.exists(os.path.dirname(WEB_MD_PT)):
        shutil.copyfile(OUT_PT, WEB_MD_PT)
        print(f"Synced -> {WEB_MD_PT}")

    # Rebuild docs-data.js if build script exists
    if os.path.exists(WEB_BUILD_JS):
        try:
            res = subprocess.run(
                ["node", WEB_BUILD_JS],
                cwd=os.path.dirname(WEB_BUILD_JS),
                capture_output=True,
                text=True,
                check=True
            )
            print(f"Rebuilt Web docs-data.js:\n{res.stdout.strip()}")
        except Exception as e:
            print(f"Failed to rebuild docs-data.js: {e}")


if __name__ == "__main__":
    main()
