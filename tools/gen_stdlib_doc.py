#!/usr/bin/env python3
"""Generate docs/stdlib/index.md from compiler/src/stdlib/stdbuiltins.cpp.

Usage:
    python3 tools/gen_stdlib_doc.py

Output:
    docs/stdlib/index.md  -- Markdown table of every StdBuiltin entry
"""
import re
import os
import sys
from collections import OrderedDict

# Paths relative to the repo root
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, "compiler/src/stdlib/stdbuiltins.cpp")
OUT = os.path.join(ROOT, "docs/stdlib/index.md")

# Map SBType::Variant → human-readable name
SB_TYPE_NAMES = {
    "Int":    "int",
    "UInt":   "uint",
    "Float":  "float",
    "Bool":   "bool",
    "String": "string",
    "Void":   "void",
    "Ptr":    "ptr",
    "List":   "list",
    "Map":    "map",
}

CATEGORY_PATTERNS = [
    ("Math",      re.compile(r"//.*math", re.I),           "math"),
    ("String",    re.compile(r"//.*string", re.I),         "string"),
    ("File IO",   re.compile(r"//.*file|//.*io", re.I),   "fileio"),
    ("JSON",      re.compile(r"//.*json", re.I),           "json"),
    ("Time",      re.compile(r"//.*time|//.*date", re.I),  "time"),
    ("Socket",    re.compile(r"//.*socket|//.*net", re.I),"socket"),
    ("System",    re.compile(r"//.*system|//.*env|//.*process", re.I), "system"),
    ("Crypto",    re.compile(r"//.*crypto|//.*hash|//.*random", re.I), "crypto"),
]


def categorize(current_comment):
    """Return category key based on the most recent leading comment."""
    for label, pat, _key in CATEGORY_PATTERNS:
        if pat.search(current_comment):
            return label
    return "Other"


def parse_builtins(text):
    """Walk the file and return a list of (name, params, return, link, category)."""
    rows = []
    current_comment = ""
    # Match:  { "name", {SBType::X, SBType::Y, ...}, SBType::Z, "link"},
    pat = re.compile(
        r'\{\s*"([^"]+)"\s*,\s*\{([^}]*)\}\s*,\s*SBType::(\w+)\s*,\s*"([^"]+)"\s*\}'
    )
    sbpat = re.compile(r"SBType::(\w+)")
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("//"):
            current_comment = current_comment + " " + stripped
            continue
        m = pat.search(line)
        if m:
            name, params_raw, ret, link = m.group(1), m.group(2), m.group(3), m.group(4)
            params = [SB_TYPE_NAMES.get(x, x) for x in sbpat.findall(params_raw)]
            ret_name = SB_TYPE_NAMES.get(ret, ret)
            rows.append({
                "name": name,
                "params": params,
                "ret": ret_name,
                "link": link,
                "category": categorize(current_comment),
            })
            current_comment = ""
    return rows


def render_table(rows, category):
    """Render one category section as Markdown."""
    lines = []
    lines.append(f"### {category}")
    lines.append("")
    lines.append("| Name | Signature | Returns | Runtime |")
    lines.append("|------|-----------|---------|---------|")
    for r in rows:
        params = ", ".join(r["params"]) if r["params"] else ""
        sig = f"`{r['name']}({params})`"
        lines.append(f"| `{r['name']}` | {sig} | `{r['ret']}` | [`{r['link']}`](runtime.md#{r['link'].lower()}) |")
    lines.append("")
    return "\n".join(lines)


def main():
    if not os.path.exists(SRC):
        sys.exit(f"ERROR: cannot find {SRC}")
    with open(SRC, "r", encoding="utf-8") as f:
        text = f.read()
    rows = parse_builtins(text)
    if not rows:
        sys.exit("ERROR: no StdBuiltin entries parsed (format changed?)")

    # Group by category, preserve insertion order
    by_cat = OrderedDict()
    for r in rows:
        by_cat.setdefault(r["category"], []).append(r)

    # Deduplicate (same name+signature) while preserving order
    seen = set()
    deduped = []
    for r in rows:
        key = (r["name"], tuple(r["params"]), r["ret"])
        if key in seen:
            continue
        seen.add(key)
        deduped.append(r)

    # Re-group
    by_cat = OrderedDict()
    for r in deduped:
        by_cat.setdefault(r["category"], []).append(r)

    out = []
    out.append("# HP-HL Standard Library Reference")
    out.append("")
    out.append("> **Auto-generated** from `compiler/src/stdlib/stdbuiltins.cpp` by `tools/gen_stdlib_doc.py`.")
    out.append("> Do not edit by hand; re-run the script after changing the table.")
    out.append("")
    out.append(f"Total: **{len(deduped)}** builtins across **{len(by_cat)}** categories.")
    out.append("")
    out.append("## Table of Contents")
    out.append("")
    for cat in by_cat:
        anchor = cat.lower().replace(" ", "-")
        out.append(f"- [{cat}](#{anchor})")
    out.append("")
    for cat, items in by_cat.items():
        out.append(render_table(items, cat))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print(f"Wrote {OUT} ({len(deduped)} builtins across {len(by_cat)} categories)")


if __name__ == "__main__":
    main()
