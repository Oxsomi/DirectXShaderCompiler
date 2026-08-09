# DXC → WebAssembly (emscripten)

Builds this static-linking DXC fork as **wasm64** static libraries (wasm32
optional) — the same library set `conanfile.py`'s `package_info()` exposes on
other platforms — for OxC3's shader compiler. Verified 2026-08-06: HLSL→DXIL
and HLSL→SPIR-V fully in-memory under node and in-browser.

## Build

```bash
# standalone (needs emsdk at ~/emsdk or $EMSDK):
wasm/build_wasm.sh            # → build-wasm/wasm/lib/*.a

# conan:
conan create . -pr:h ./wasm/profiles/emscripten-wasm64.jinja -pr:b default
```

Env knobs for `build_wasm.sh`: `DXC_WASM_MEMORY64` (default 1), `DXC_WASM_EH`
(`wasm`|`js`), `DXC_WASM_BUILD_TYPE` (Release), `DXC_WASM_ASSERTIONS` (ON,
conan parity), `DXC_WASM_TBLGEN_DIR`, `EMSDK`, `DXC_WASM_JOBS`.

Cross builds bootstrap native `llvm-tblgen`/`clang-tblgen` from this same tree
automatically (POSIX build machines; ~3 min, cached). On a **Windows build
machine**, prebuild them with MSVC and set `DXC_NATIVE_TBLGEN_DIR`.

## How it works

Two-phase build — the standard recipe for running LLVM-family code *inside*
wasm:

1. **Native phase** — host `llvm-tblgen`/`clang-tblgen` (needs
   `LLVM_ENABLE_EH/RTTI=ON`: this fork's `LLVMSupport` throws).
2. **Cross phase** — `emcmake` with the usual conanfile options, plus:

   | Flag | Why |
   |---|---|
   | `-DLLVM_TABLEGEN/-DCLANG_TABLEGEN=<native>` | bypasses the in-tree host-tools machinery |
   | `-DLLVM_USE_HOST_TOOLS=OFF` | stops the auto-configured NATIVE sub-build, which uses default options (`LLVM_ENABLE_EH=OFF`) and cannot compile this fork. Same trap broke Android; the CMake patches fix both |
   | `-DLLVM_INFERRED_HOST_TRIPLE=wasm64-unknown-emscripten` | skips `config.guess` |
   | `-DLLVM_ENABLE_THREADS=OFF` | must be forced (emscripten ships stub pthreads that fool config-ix); avoids SharedArrayBuffer/COOP/COEP |
   | `-DLLVM_ENABLE_ZLIB=OFF`, `-DLLVM_ENABLE_PIC=OFF` | no real zlib in sysroot; PIC meaningless for static wasm |
   | `-fwasm-exceptions` (compile **and** link) | DXC requires exception *catching* at runtime (error paths throw) |
   | `-m64` (compile **and** link) | wasm64; pointer size is ABI |

   `ENABLE_DXC_STATIC_LINKING=ON` builds no executables — only libraries.
   Conan cross builds compile the explicit `package_info()` target list, not
   `all` (target names differ twice: `clang`→`libclang`,
   `SPIRV-Tools`→`SPIRV-Tools-static`).

## Rules for the consumer link (OxC3)

Link-time requirements on the final `emcc` link; archives cannot carry them:

- **Same EH flag as the lib build** (`-fwasm-exceptions`). EH mode is ABI;
  forgetting it entirely means every `throw` aborts
  (`-sDISABLE_EXCEPTION_CATCHING=1` is emscripten's default).
- **Same `-m64`** — also a compile flag for any TU including DXC headers.
- **`-sSTACK_SIZE=8MB`** or more (default is 64KB; LLVM recursion overflows
  silently — the wasm stack cannot grow).
- **`-sALLOW_MEMORY_GROWTH=1`** + sane `-sMAXIMUM_MEMORY` (default cap 2GB).
- Order archives as in `package_info()` (most→least dependent).
- If the wrapper never references `DxcCreateInstance` directly from C++, wrap
  `libdxcompiler.a` in `-Wl,--whole-archive` so factories aren't stripped.

Runtime: single-threaded only; statically linked `DxcCreateInstance` +
`DxcInitialize()`/`DxcShutdown()` (never `dxcapi.use.h`'s dlopen path);
sources/includes as in-memory blobs via `IDxcIncludeHandler`.

## Notes for the OxC3 wrapper

- **wasm64 + embind**: `size_t`/pointer-sized values cross into JS as
  **BigInt** (e.g. `IDxcBlob::GetBufferSize()`); cast to `uint32_t` C++-side
  before constructing JS values. Copy blob bytes out via `typed_memory_view`
  + `.set()` — heap views dangle once the blob is released.
- **Wide-char args**: `IDxcCompiler3::Compile` takes `LPCWSTR*`; emscripten
  `wchar_t` is 4-byte UTF-32 — convert UTF-8 properly.
- **Exceptions must not cross into JS** (they surface as opaque
  `WebAssembly.Exception`): wrap exports in `try/catch(...)`, return
  diagnostics as values.
- SPIR-V disassembly comes free from the linked SPIRV-Tools
  (`spvBinaryToText`); DXIL via `IDxcCompiler3::Disassemble`.

## Caveats

- **Engine floor (wasm64 + wasm-EH)**: Chrome 133+, Firefox 134+, Safari 26+,
  Node 20+. For older engines build wasm32 (`DXC_WASM_MEMORY64=0`) and/or JS
  exceptions (`DXC_WASM_EH=js`) — EH/pointer-size choices must match in every
  consumer.
- **emcc has no ABI stability between versions** — pin the emsdk version; on
  conan ≥ 2.18 model it properly (`arch=wasm64`, `compiler=emcc`,
  `compiler.version=<exact emsdk version>`) instead of this profile's
  conan-2.11 shim.
