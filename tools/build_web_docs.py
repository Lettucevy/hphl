import os
import json
import shutil

ROOT_DIR = r"c:\Projetos\HPHL"
DOCS_DIR = os.path.join(ROOT_DIR, "docs")
WEB_DIR = r"C:\Projetos\Web\VelafacePage\hphl"
TARGET_MD_DIR = os.path.join(WEB_DIR, "docs", "md")
TARGET_JS = os.path.join(WEB_DIR, "docs-data.js")

os.makedirs(TARGET_MD_DIR, exist_ok=True)

DOC_FILES = [
    {
        "id": "index.md",
        "title": "Language Overview",
        "category": "Getting Started",
        "icon": "ri-book-open-line",
        "path": "index.md"
    },
    {
        "id": "installation.md",
        "title": "Installation Guide",
        "category": "Getting Started",
        "icon": "ri-download-cloud-line",
        "path": "installation.md"
    },
    {
        "id": "tutorial_30min.md",
        "title": "30-Minute Tutorial",
        "category": "Getting Started",
        "icon": "ri-rocket-line",
        "path": "tutorial/30min.md"
    },
    {
        "id": "reference_language.md",
        "title": "Language Reference",
        "category": "Language Guide",
        "icon": "ri-code-s-slash-line",
        "path": "reference/language.md"
    },
    {
        "id": "reference_memory.md",
        "title": "Memory Architecture & GC",
        "category": "Language Guide",
        "icon": "ri-cpu-line",
        "path": "reference/memory.md"
    },
    {
        "id": "reference_ffi.md",
        "title": "Native FFI & Win32",
        "category": "Language Guide",
        "icon": "ri-plug-line",
        "path": "reference/ffi.md"
    },
    {
        "id": "reference_diagnostics.md",
        "title": "Diagnostics & Errors",
        "category": "Language Guide",
        "icon": "ri-alert-line",
        "path": "reference/diagnostics.md"
    },
    {
        "id": "performance_guide.md",
        "title": "Performance Tuning (1.87x)",
        "category": "Deep Dive",
        "icon": "ri-speed-up-line",
        "path": "performance_guide.md"
    },
    {
        "id": "vulkan_game_engines.md",
        "title": "Game Engines & Vulkan",
        "category": "Deep Dive",
        "icon": "ri-gamepad-line",
        "path": "vulkan_game_engines.md"
    },
    {
        "id": "webassembly.md",
        "title": "WebAssembly Backend",
        "category": "Deep Dive",
        "icon": "ri-global-line",
        "path": "webassembly.md"
    },
    {
        "id": "migration.md",
        "title": "Migrating from C++ & Rust",
        "category": "Deep Dive",
        "icon": "ri-shuffle-line",
        "path": "migration.md"
    },
    {
        "id": "stdlib_index.md",
        "title": "Standard Library Catalog",
        "category": "Standard Library",
        "icon": "ri-archive-stack-line",
        "path": "stdlib/index.md"
    },
    {
        "id": "stdlib_runtime.md",
        "title": "Runtime C API Reference",
        "category": "Standard Library",
        "icon": "ri-file-code-line",
        "path": "stdlib/runtime.md"
    },
    {
        "id": "cookbook_index.md",
        "title": "Cookbook & Recipes",
        "category": "Standard Library",
        "icon": "ri-restaurant-line",
        "path": "cookbook/index.md"
    },
    {
        "id": "compat.md",
        "title": "ABI & Compatibility",
        "category": "Standard Library",
        "icon": "ri-shield-check-line",
        "path": "COMPAT.md"
    }
]

docs_data = {}

for item in DOC_FILES:
    src_path = os.path.join(DOCS_DIR, item["path"].replace("/", os.sep))
    if not os.path.exists(src_path):
        print(f"Warning: {src_path} not found")
        continue

    with open(src_path, "r", encoding="utf-8", errors="replace") as f:
        content = f.read()

    # Copy raw md to web md folder
    flat_name = item["id"]
    dst_md_path = os.path.join(TARGET_MD_DIR, flat_name)
    with open(dst_md_path, "w", encoding="utf-8") as f:
        f.write(content)

    docs_data[item["id"]] = {
        "id": item["id"],
        "title": item["title"],
        "category": item["category"],
        "icon": item["icon"],
        "rawUrl": f"docs/md/{flat_name}",
        "markdown": content
    }

js_content = "window.HPHL_DOCS = " + json.dumps(docs_data, ensure_ascii=False, indent=2) + ";\n"

with open(TARGET_JS, "w", encoding="utf-8") as f:
    f.write(js_content)

print(f"Built web docs database: {len(docs_data)} markdown documents written to {TARGET_JS}")
