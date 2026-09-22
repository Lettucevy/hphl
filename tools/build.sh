#!/usr/bin/bash
# tools/build.sh - Cross-platform build script for HP-HL (Linux/macOS).
# For Windows, use tools/build.ps1 instead.
#
# Usage: ./tools/build.sh [x64|llvm|ir|aarch64|wasm] [--debug] [--clean]
#
# Default backend: x64 (compiles via gcc/g++ native).
# Other backends require LLVM-22 installed (apt install llvm-22-dev or brew install llvm@22).

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
COMPILER_DIR="$ROOT_DIR/compiler"
BUILD_DIR="$COMPILER_DIR/build"
BIN_DIR="$COMPILER_DIR/bin"

BACKEND="${1:-x64}"
DEBUG_FLAG=""
CLEAN=0
JOBS=$(nproc 2>/dev/null || echo 4)

# Parse args
for arg in "$@"; do
    case $arg in
        --debug) DEBUG_FLAG="-g" ;;
        --clean) CLEAN=1 ;;
        x64|llvm|ir|aarch64|wasm) BACKEND="$arg" ;;
        --help|-h)
            cat <<EOF
HP-HL Cross-Platform Build (Linux/macOS)
Usage: $0 [x64|llvm|ir|aarch64|wasm] [--debug] [--clean] [--jobs N]

Backends:
  x64       native (default) - gcc/g++
  llvm      via LLVM C API + libLLVM-22
  ir        textual LLVM IR (for cross-compile to aarch64/wasm)
  aarch64   cross-compile via LLVM to aarch64-linux-gnu
  wasm      cross-compile via LLVM to wasm32-unknown-unknown
EOF
            exit 0
            ;;
        --jobs|-j) shift; JOBS="$1" ;;
    esac
done

echo "[build] HP-HL backend=$BACKEND jobs=$JOBS"
echo "[build] ROOT=$ROOT_DIR"

if [ "$CLEAN" = "1" ]; then
    echo "[build] Cleaning..."
    rm -rf "$BUILD_DIR" "$BIN_DIR"
fi

mkdir -p "$BUILD_DIR" "$BIN_DIR"

# Detecta clang/gcc
CC=${CC:-gcc}
CXX=${CXX:-g++}
CFLAGS="-O2 -w $DEBUG_FLAG -I$COMPILER_DIR/src -I$COMPILER_DIR/src/runtime"
CXXFLAGS="-std=c++17 -O2 -w $DEBUG_FLAG -I$COMPILER_DIR/src -I$COMPILER_DIR/src/runtime"

# Compila runtime C + standalone C sources
echo "[build] Compiling runtime..."
for f in $COMPILER_DIR/src/runtime/main.c $COMPILER_DIR/src/sha512.c; do
    base=$(basename "$f" .c)
    if [ ! -f "$BUILD_DIR/${base}.o" ] || [ "$f" -nt "$BUILD_DIR/${base}.o" ]; then
        $CC $CFLAGS -c "$f" -o "$BUILD_DIR/${base}.o"
    fi
done

# Compila C++ files
echo "[build] Compiling C++ sources..."
CXX_SRCS=$(find $COMPILER_DIR/src -name "*.cpp" -not -path "*/runtime/*" | sort)
for f in $CXX_SRCS; do
    rel=${f#$COMPILER_DIR/src/}
    base=$(echo "$rel" | tr '/' '_' | sed 's/\.cpp$//')
    if [ ! -f "$BUILD_DIR/${base}.o" ] || [ "$f" -nt "$BUILD_DIR/${base}.o" ]; then
        $CXX $CXXFLAGS -c "$f" -o "$BUILD_DIR/${base}.o" &
    fi
    # paraleliza quando passar de N files
    if [ $(jobs -r | wc -l) -ge "$JOBS" ]; then
        wait
    fi
done
wait

# Link
echo "[build] Linking hphlc..."
LDFLAGS=""
if [ "$BACKEND" = "x64" ]; then
    # tenta linkar com LLVM se disponivel
    if ldconfig -p 2>/dev/null | grep -q libLLVM-22; then
        LDFLAGS="-lLLVM-22"
    elif [ -f /usr/lib/llvm-22/lib/libLLVM.so ]; then
        LDFLAGS="-L/usr/lib/llvm-22/lib -lLLVM-22"
    fi
    $CXX -o "$BIN_DIR/hphlc" $BUILD_DIR/*.o $LDFLAGS -lpthread -ldl
elif [ "$BACKEND" = "llvm" ]; then
    $CXX -o "$BIN_DIR/hphlc" $BUILD_DIR/*.o -lLLVM-22 -lpthread -ldl
else
    $CXX -o "$BIN_DIR/hphlc" $BUILD_DIR/*.o -lpthread -ldl
fi

chmod +x "$BIN_DIR/hphlc"
echo "[build] Built: $BIN_DIR/hphlc"
ls -la "$BIN_DIR/hphlc"
