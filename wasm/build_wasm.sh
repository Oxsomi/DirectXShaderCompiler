#!/usr/bin/env bash
# =============================================================================
# Build DXC (Oxsomi static-linking fork) for WebAssembly via emscripten.
#
# Produces the same set of static libraries that conanfile.py's package_info()
# exposes on other platforms, as wasm32 archives consumable by a downstream
# emscripten link (e.g. OxC3's shader compiler).
#
# Usage:
#   wasm/build_wasm.sh [configure|build|all]   (default: all)
#
# Environment overrides:
#   EMSDK                - path to emsdk root        (default: ~/emsdk)
#   DXC_WASM_BUILD_TYPE  - CMake build type          (default: Release)
#   DXC_WASM_MEMORY64    - 1 = wasm64, 0 = wasm32    (default: 1)
#   DXC_WASM_EH          - "wasm" or "js" exceptions (default: wasm)
#   DXC_WASM_ASSERTIONS  - ON/OFF LLVM assertions    (default: ON, conan parity)
#   DXC_WASM_TBLGEN_DIR  - dir with prebuilt native llvm-tblgen/clang-tblgen
#                          (default: auto-build in build-wasm/native/bin)
#   DXC_WASM_JOBS        - parallel jobs             (default: nproc)
#
# Notes:
#  * Exceptions: dxcompiler REQUIRES C++ exception catching at runtime.
#    The final application link must use the SAME EH flag as this build
#    (-fwasm-exceptions for "wasm", -fexceptions for "js").
#  * Threads: built single-threaded (LLVM_ENABLE_THREADS=OFF, no -pthread).
# =============================================================================
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="${SRC_DIR}/build-wasm"
NATIVE_DIR="${OUT_DIR}/native"
WASM_DIR="${OUT_DIR}/wasm"

EMSDK="${EMSDK:-$HOME/emsdk}"
DXC_WASM_BUILD_TYPE="${DXC_WASM_BUILD_TYPE:-Release}"
DXC_WASM_EH="${DXC_WASM_EH:-wasm}"
DXC_WASM_ASSERTIONS="${DXC_WASM_ASSERTIONS:-ON}"
# nproc is Linux; fall back for macOS (sysctl) and elsewhere.
DXC_WASM_JOBS="${DXC_WASM_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

STAGE="${1:-all}"

# --- emsdk ------------------------------------------------------------------
if [[ ! -f "${EMSDK}/emsdk_env.sh" ]]; then
    echo "error: emsdk not found at ${EMSDK} (set EMSDK=...)" >&2
    exit 1
fi
# shellcheck disable=SC1091
source "${EMSDK}/emsdk_env.sh" >/dev/null 2>&1
echo "-- emcc: $(emcc --version | head -1)"

case "${DXC_WASM_EH}" in
    wasm) EH_FLAG="-fwasm-exceptions" ;;
    js)   EH_FLAG="-fexceptions" ;;
    *) echo "error: DXC_WASM_EH must be 'wasm' or 'js'" >&2; exit 1 ;;
esac

DXC_WASM_MEMORY64="${DXC_WASM_MEMORY64:-1}"
if [[ "${DXC_WASM_MEMORY64}" == "1" ]]; then
    # wasm64: needed at compile AND link time (pointer size is ABI).
    MEM_FLAG="-m64"
    WASM_TRIPLE="wasm64-unknown-emscripten"
else
    MEM_FLAG=""
    WASM_TRIPLE="wasm32-unknown-emscripten"
fi

# --- 1. native tablegen tools -----------------------------------------------
# Cross builds need host llvm-tblgen/clang-tblgen. Build them from this same
# tree with the host compiler (once), unless DXC_WASM_TBLGEN_DIR points at
# prebuilt ones (e.g. an existing native build's bin/ dir).
if [[ -n "${DXC_WASM_TBLGEN_DIR:-}" ]]; then
    TBLGEN_BIN="${DXC_WASM_TBLGEN_DIR}"
else
    TBLGEN_BIN="${NATIVE_DIR}/bin"
    if [[ ! -x "${TBLGEN_BIN}/llvm-tblgen" || ! -x "${TBLGEN_BIN}/clang-tblgen" ]]; then
        echo "-- Building native tablegen tools into ${NATIVE_DIR}"
        # Plain host configure: deliberately NOT under emsdk's toolchain.
        env -u CC -u CXX cmake -S "${SRC_DIR}" -B "${NATIVE_DIR}" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DLLVM_ENABLE_EH=ON -DLLVM_ENABLE_RTTI=ON \
            -DLLVM_TARGETS_TO_BUILD=None \
            -DLLVM_DEFAULT_TARGET_TRIPLE=dxil-ms-dx \
            -DLLVM_INCLUDE_TESTS=OFF -DHLSL_INCLUDE_TESTS=OFF \
            -DCLANG_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF \
            -DLLVM_INCLUDE_DOCS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
            -DCLANG_BUILD_EXAMPLES=OFF \
            -DHLSL_BUILD_DXILCONV=OFF \
            -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_ARCMT=OFF \
            -DLLVM_ENABLE_TERMINFO=OFF \
            -DHLSL_OPTIONAL_PROJS_IN_DEFAULT=OFF
        cmake --build "${NATIVE_DIR}" --target llvm-tblgen clang-tblgen -j "${DXC_WASM_JOBS}"
    fi
fi
for t in llvm-tblgen clang-tblgen; do
    [[ -x "${TBLGEN_BIN}/${t}" ]] || { echo "error: missing ${TBLGEN_BIN}/${t}" >&2; exit 1; }
done
echo "-- tablegen: ${TBLGEN_BIN}"

# --- 2. configure the emscripten cross build --------------------------------
if [[ "${STAGE}" == "configure" || "${STAGE}" == "all" || ! -f "${WASM_DIR}/build.ninja" ]]; then
    emcmake cmake -S "${SRC_DIR}" -B "${WASM_DIR}" -G Ninja \
        -DCMAKE_BUILD_TYPE="${DXC_WASM_BUILD_TYPE}" \
        -DCMAKE_CXX_STANDARD=20 -DCMAKE_CXX_STANDARD_REQUIRED=ON \
        -DCMAKE_C_FLAGS="${EH_FLAG} ${MEM_FLAG}" \
        -DCMAKE_CXX_FLAGS="${EH_FLAG} ${MEM_FLAG}" \
        -DCMAKE_EXE_LINKER_FLAGS="${EH_FLAG} ${MEM_FLAG}" \
        \
        -DLLVM_TABLEGEN="${TBLGEN_BIN}/llvm-tblgen" \
        -DCLANG_TABLEGEN="${TBLGEN_BIN}/clang-tblgen" \
        -DLLVM_USE_HOST_TOOLS=OFF \
        \
        -DENABLE_DXC_STATIC_LINKING=ON \
        -DLIBCLANG_BUILD_STATIC=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DENABLE_SPIRV_CODEGEN=ON \
        -DLLVM_ENABLE_RTTI=ON \
        -DLLVM_ENABLE_EH=ON \
        -DLLVM_APPEND_VC_REV=ON \
        -DLLVM_ENABLE_ASSERTIONS="${DXC_WASM_ASSERTIONS}" \
        -DLLVM_TARGETS_TO_BUILD=None \
        -DLLVM_DEFAULT_TARGET_TRIPLE=dxil-ms-dx \
        -DLLVM_INFERRED_HOST_TRIPLE="${WASM_TRIPLE}" \
        \
        -DLLVM_ENABLE_THREADS=OFF \
        -DLLVM_ENABLE_PIC=OFF \
        -DLLVM_ENABLE_ZLIB=OFF \
        -DLLVM_ENABLE_TERMINFO=OFF \
        \
        -DCLANG_BUILD_EXAMPLES=OFF -DCLANG_CL=OFF \
        -DCLANG_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_TESTS=OFF \
        -DHLSL_INCLUDE_TESTS=OFF -DSPIRV_BUILD_TESTS=OFF \
        -DLLVM_INCLUDE_DOCS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF \
        -DLLVM_OPTIMIZED_TABLEGEN=OFF \
        -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_ENABLE_ARCMT=OFF \
        -DHLSL_BUILD_DXILCONV=OFF -DHLSL_ENABLE_FIXED_VER=OFF \
        -DHLSL_OFFICIAL_BUILD=OFF -DHLSL_OPTIONAL_PROJS_IN_DEFAULT=OFF \
        -DDXC_USE_LIT=OFF \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

[[ "${STAGE}" == "configure" ]] && exit 0

# --- 3. build the library set core3 links (conanfile package_info) ----------
TARGETS=(
    dxcompiler dxcvalidator dxcreflection dxcreflectioncontainer libclang
    LLVMDxilHash LLVMDxilValidation LLVMDxrFallback
    clangFrontendTool clangCodeGen LLVMTarget
    LLVMScalarOpts LLVMPassPrinters LLVMProfileData LLVMDxilCompression
    LLVMOption LLVMDxilDia LLVMDxilPdbInfo LLVMPasses LLVMDxilRootSignature
    LLVMInstCombine LLVMDxilPIXPasses LLVMVectorize
    clangRewriteFrontend clangTooling clangSPIRV
    SPIRV-Tools-opt SPIRV-Tools-static
    clangFrontend clangDriver clangParse clangASTMatchers clangIndex
    clangFormat clangToolingCore clangRewrite
    clangSema clangAST clangEdit clangLex clangBasic
    LLVMLinker LLVMDxilContainer LLVMTransformUtils LLVMipa LLVMAnalysis
    LLVMDxcBindingTable LLVMDXIL LLVMIRReader LLVMBitWriter LLVMBitReader
    LLVMAsmParser LLVMTableGen LLVMDxcSupport LLVMCore LLVMSupport LLVMMSSupport
)
ninja -C "${WASM_DIR}" -j "${DXC_WASM_JOBS}" "${TARGETS[@]}"

echo "-- Done. Static wasm libs in: ${WASM_DIR}/lib"
ls -la "${WASM_DIR}/lib" | head -70
