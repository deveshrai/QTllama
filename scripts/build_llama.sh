#!/usr/bin/env sh
# Builds the pinned llama.cpp submodule as static libraries and installs them to
# third_party/llama-install, which llama.pri (qmake) links against.
#
# Usage:  scripts/build_llama.sh [extra cmake args...]
#   e.g.  scripts/build_llama.sh -DGGML_VULKAN=ON
#
# Environment:
#   JOBS        parallel build jobs (default: CPU count)
#   BUILD_TYPE  CMake build type (default: Release)
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/third_party/llama.cpp"
BUILD="$ROOT/third_party/llama-build"
PREFIX="$ROOT/third_party/llama-install"
BUILD_TYPE="${BUILD_TYPE:-Release}"

if [ ! -f "$SRC/CMakeLists.txt" ]; then
    echo "llama.cpp submodule missing; fetching..."
    git -C "$ROOT" submodule update --init --depth 1 third_party/llama.cpp
fi

if [ -z "${JOBS:-}" ]; then
    JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
fi

OS="$(uname -s)"
case "$(uname -o 2>/dev/null || true)" in
    Android) OS=Android ;;
esac

# Common options: only the core libraries, no tools/servers/network deps.
# OpenMP is disabled so we don't need to match libgomp/libomp per platform;
# ggml falls back to its own thread pool.
set -- \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DBUILD_SHARED_LIBS=OFF \
    -DLLAMA_BUILD_COMMON=OFF \
    -DLLAMA_BUILD_TESTS=OFF \
    -DLLAMA_BUILD_EXAMPLES=OFF \
    -DLLAMA_BUILD_TOOLS=OFF \
    -DLLAMA_BUILD_SERVER=OFF \
    -DLLAMA_BUILD_APP=OFF \
    -DLLAMA_OPENSSL=OFF \
    -DGGML_OPENMP=OFF \
    "$@"

case "$OS" in
    Darwin)
        # Metal + Accelerate are on by default; embed the Metal shaders so the
        # binary doesn't need a .metallib next to it.
        # Match Qt's minimum macOS so the linker doesn't warn about every object.
        set -- -DGGML_METAL=ON -DGGML_METAL_EMBED_LIBRARY=ON \
            -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-13.0}" "$@" ;;
    Android)
        # Termux: build natively for the device's CPU (ARMv8.x dotprod/i8mm).
        set -- -DGGML_NATIVE=ON "$@" ;;
    *)
        set -- -DGGML_NATIVE=ON "$@" ;;
esac

echo "==> Configuring llama.cpp ($OS, $BUILD_TYPE)"
cmake -S "$SRC" -B "$BUILD" "$@"
echo "==> Building with $JOBS jobs"
cmake --build "$BUILD" --config "$BUILD_TYPE" -j "$JOBS"
echo "==> Installing to $PREFIX"
cmake --install "$BUILD" --config "$BUILD_TYPE"

echo "==> Done. Libraries:"
ls "$PREFIX/lib" | grep -E '\.(a|lib)$' || true
