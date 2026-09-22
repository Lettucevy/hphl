# Gera binários binary-trees para depths 8, 10, 12, 14 em C++ e HPHL
$ErrorActionPreference = "Stop"
$benchDir = "C:\Projetos\HPHL\examples\benchmarks"
$compilerExe = "C:\Projetos\HPHL\compiler\bin\hphlc.exe"
$gcc = "C:\msys64\ucrt64\bin\gcc.exe"
$gpp = "C:\msys64\ucrt64\bin\g++.exe"
$runtime = "C:\Projetos\HPHL\compiler\runtime.o"

# Source files in $benchDir
foreach ($d in @(8, 10, 12, 14)) {
    Write-Host "[build] depth=$d" -ForegroundColor Cyan

    # HPHL
    $hphlSrc = Join-Path $benchDir "binary-trees-${d}.hphl"
    $hphlS   = "binary-trees-${d}.s"
    $hphlExe = Join-Path $benchDir "binary-trees-${d}-hphl.exe"
    if (Test-Path $hphlSrc) {
        cmd /c "$compilerExe --keep-asm --no-link $hphlSrc" 2>&1 | Out-Null
        if (Test-Path $hphlS) {
            cmd /c "$gcc -O2 -Wl,--stack,268435456 -o $hphlExe $hphlS $runtime" 2>&1 | Out-Null
        }
    }

    # HPHL pressure
    $hphlPSrc = Join-Path $benchDir "binary-trees-${d}-pressure.hphl"
    $hphlPS   = "binary-trees-${d}-pressure.s"
    $hphlPExe = Join-Path $benchDir "binary-trees-${d}-pressure-hphl.exe"
    if (Test-Path $hphlPSrc) {
        cmd /c "$compilerExe --keep-asm --no-link $hphlPSrc" 2>&1 | Out-Null
        if (Test-Path $hphlPS) {
            cmd /c "$gcc -O2 -Wl,--stack,268435456 -o $hphlPExe $hphlPS $runtime" 2>&1 | Out-Null
        }
    }

    # C++ (assuming binary-trees-8.cpp has 14 as maxDepth after our edit; for others we'll
    # generate fresh sources on the fly by templating the maxDepth)
    $cppSrc = Join-Path $benchDir "binary-trees-${d}.cpp"
    $cppExe = Join-Path $benchDir "binary-trees-${d}-cpp.exe"
    $cppSrcText = @"
#include <cstdio>
#include <cstdlib>
class Node { public: int item; Node* left; Node* right; Node(int i): item(i), left(nullptr), right(nullptr) {} Node(int i, Node* l, Node* r): item(i), left(l), right(r) {} };
int itemCheck(Node* n) { if (n->left == nullptr) return n->item; return n->item + itemCheck(n->left) - itemCheck(n->right); }
Node* bottomUpTree(int i, int d) { if (d > 0) return new Node(i, bottomUpTree(2*i-1, d-1), bottomUpTree(2*i, d-1)); return new Node(i, nullptr, nullptr); }
void freeTree(Node* n) { if (!n) return; freeTree(n->left); freeTree(n->right); delete n; }
int main() {
    int minD=4, maxD=${d}, sD=maxD+1;
    std::printf("Stretching tree of depth %d\n", sD);
    Node* st = bottomUpTree(0, sD);
    int ch = itemCheck(st); freeTree(st);
    std::printf("Item check: %d\n", ch);
    for (int depth=minD; depth<=maxD; depth+=2) {
        int it = 1 << (maxD-depth+minD);
        int sum = 0;
        for (int i=1; i<=it; i++) { Node* t1=bottomUpTree(i, depth); Node* t2=bottomUpTree(-i, depth); sum += itemCheck(t1) - itemCheck(t2); freeTree(t1); freeTree(t2); }
        std::printf("%d trees of depth %d item check: %d\n", it*2, depth, sum);
    }
    Node* ll = bottomUpTree(0, maxD);
    std::printf("Long lived tree of depth %d item check: %d\n", maxD, itemCheck(ll));
    freeTree(ll);
    return 0;
}
"@
    Set-Content -Path $cppSrc -Value $cppSrcText
    cmd /c "$gpp -O2 -Wl,--stack,268435456 -o $cppExe $cppSrc" 2>&1 | Out-Null

    # C++ no-free
    $nfSrc = Join-Path $benchDir "binary-trees-${d}-nofree.cpp"
    $nfExe = Join-Path $benchDir "binary-trees-${d}-nofree.exe"
    $nfSrcText = @"
#include <cstdio>
#include <cstdlib>
class Node { public: int item; Node* left; Node* right; Node(int i): item(i), left(nullptr), right(nullptr) {} Node(int i, Node* l, Node* r): item(i), left(l), right(r) {} };
int itemCheck(Node* n) { if (n->left == nullptr) return n->item; return n->item + itemCheck(n->left) - itemCheck(n->right); }
Node* bottomUpTree(int i, int d) { if (d > 0) return new Node(i, bottomUpTree(2*i-1, d-1), bottomUpTree(2*i, d-1)); return new Node(i, nullptr, nullptr); }
int main() {
    int minD=4, maxD=${d}, sD=maxD+1;
    std::printf("Stretching tree of depth %d\n", sD);
    int ch = itemCheck(bottomUpTree(0, sD));
    std::printf("Item check: %d\n", ch);
    for (int depth=minD; depth<=maxD; depth+=2) {
        int it = 1 << (maxD-depth+minD);
        int sum = 0;
        for (int i=1; i<=it; i++) { sum += itemCheck(bottomUpTree(i, depth)) - itemCheck(bottomUpTree(-i, depth)); }
        std::printf("%d trees of depth %d item check: %d\n", it*2, depth, sum);
    }
    std::printf("Long lived tree of depth %d item check: %d\n", maxD, itemCheck(bottomUpTree(0, maxD)));
    return 0;
}
"@
    Set-Content -Path $nfSrc -Value $nfSrcText
    cmd /c "$gpp -O2 -Wl,--stack,268435456 -o $nfExe $nfSrc" 2>&1 | Out-Null

    # C++ with gc-style env (using mimalloc if available, else default)
    Write-Host "  done" -ForegroundColor Green
}
Write-Host "[done] All variants built" -ForegroundColor Green