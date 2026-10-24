# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

OP is a Windows desktop automation COM plugin written in C++17. It provides screen capture (GDI/DirectX/OpenGL), input simulation (SendInput/SendMessage/hooks), image search with fuzzy matching, and OCR through an external HTTP service. The plugin exposes a C API (`op_c_api_*.dll`) plus a COM `IDispatch`-based interface consumable from C++, C#, and Lua. The project builds with a native VS2022 solution (`op.sln`); the Python/Go/SWIG bindings and bitmap-font OCR have been removed.

## Build commands

```powershell
# One-time third-party dependencies (BlackBone / OpenCV / MinHook)
pwsh -File scripts\bootstrap-deps.ps1                 # x64
pwsh -File scripts\bootstrap-deps.ps1 -Arch x86       # x86

# Build the whole solution (Release)
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
    op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m

& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
    op.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m
```

Artifacts: `bin/{x64|x86}/op_{x64|x86}.dll`, `op_c_api_{x64|x86}.dll`, `tools.dll`,
`cpp_call.exe`, `op_test.exe`, `lib/{x64|x86}/libop.lib`, `op_c_api_{x64|x86}.lib`.

Dependencies live in `build/_deps/{BlackBone,opencv}` and the vcpkg root. The projects set
`VcpkgEnabled=false` because a global `vcpkg integrate install` would inject the dynamic
`x64-windows` triplet (wrong CRT and OpenCV4 headers); see
`doc/cpp-only/BUILD_VS.md` for the details.

## Test commands

`tests/op_test.vcxproj` builds the GoogleTest suite (x64 and Win32):

```powershell
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
    tests\op_test.vcxproj /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m

.\bin\x64\op_test.exe --gtest_list_tests
.\bin\x64\op_test.exe --gtest_filter="UtilsTest.*:HandleCompatTest.*:AlgorithmTest.*:OpTest.*"
```

OCR test cases need an external OCR service (`OP_OCR_URL`); they SKIP/FAIL without it.
Only Release is validated; Debug requires a Debug build of the OpenCV static libraries.

The pure C++ smoke test is `examples/cpp_call`:

```powershell
.\bin\x64\cpp_call.exe
# Ver = 0.4.8.3, OpenCV = 5.0.0
```

`cpp_call` also exercises the pure C++ path: `libop.lib` + `include/libop.h` +
`U_STATIC_IMPLEMENTATION`.

## Architecture

```
include/libop.h          — Public API declaration (class op::Op, exported via OP_API)
libop/libop.cpp          — Main implementation: coordinates WindowService, BindingSession, capture/input backends, and ImageSearchService
libop/com/op.idl         — MIDL COM interface definition (IOpAutomation, ~300 dispids)
libop/com/OpAutomation.*  — COM IDispatch implementation, calls through to libop
libop/base/Types.h       — Core types: point_t, rect_t, ocr_rec_t, bytearray aliases
libop/base/AutomationModes.h   — Render type enums, input types, shared constants, version string
libop/base/Utils.*       — Utility functions (string conversion, hex dump, error helpers)
libop/binding/           — Window binding and background-mode orchestration
libop/capture/           — Capture sources and GDI/DXGI/WGC/Hook backends
libop/input/             — Mouse, keyboard, and DX input backends
libop/hook/              — Display/input hooks, injection protocol, and hook exports
libop/ipc/               — Shared memory and process mutex primitives
libop/image/             — Image search (ImageSearchAlgorithms), image processing (ImageSearchService),
                           OCR via HTTP service (HttpOcrService, singleton with mutex)
libop/window/            — Window, process, clipboard, command, injection, and layout services
libop/algorithm/         — A* pathfinding (AStar.hpp)
tests/                   — GoogleTest suite (tests/op_test.vcxproj; x64 + Win32)
tools/                   — EasyCom DLL (COM helper, separate from the main plugin)
examples/cpp_call/       — Pure C++ static-link example / smoke test
3rd_party/               — Kiero source + pre-built libs for some architectures
op.sln                   — Native VS2022 solution (libop / op_x64 / op_c_api_x64 / tools / cpp_call / op_test)
vs/                      — op.common.props (compile/link/MIDL), op.deps.props (paths), op.sources.props
scripts/                 — bootstrap-deps.ps1 (BlackBone / OpenCV / MinHook)
doc/cpp-only/            — Refactor notes, API changes, progress, native VS build guide
```

### Key architectural patterns

- **Pimpl + COM**: `class op::Op` (in `include/libop.h`) is the public DLL-exported facade. It holds a `unique_ptr<op_context>` which bundles Windows API helpers, window binding/capture state, and `ImageSearchService`. The COM layer (`IOpAutomation` / `OpAutomation`) converts `BSTR`/`VARIANT` arguments and delegates to `libop`.
- **COINIT_APARTMENTTHREADED**: The COM object uses apartment threading. All calls from a single thread share the same `op_context`.
- **Screen capture modes**: Controlled by `SetDisplayInput()` — supports GDI (`normal`), DXGI, WGC, and `mem:<ptr>` for external pixel buffers.
- **OCR is HTTP service-backed**: `Ocr`/`OcrEx`/`OcrAuto`/`OcrFromFile`/`OcrAutoFromFile` all go through `HttpOcrService` (singleton with mutex) to an external service (`ocr_server.exe` or PaddleOCR); `FindStr`/`FindStrEx` post-process the returned `ocr_rec_t` list, and `FindLine` is a standalone binarized-image line detector that needs no OCR service.
- **Input modes**: `mouse`/`keypad` accept `normal`, `windows`, or `dx` backends. `dx` mode uses MinHook-injected DLLs to intercept/replay input in the target process.
- **Pre-commit**: clang-format (v16) runs on `.c/.h/.cpp/.hpp/.cc/.cxx` files.

## Platform constraints

- **Windows only**. Compiles with MSVC v143 (VS2022), `/MT` static CRT, `/EHa` exception handling.
- The native solution builds both x64 and Win32/x86. Use
  `scripts/bootstrap-deps.ps1 -Arch x86` for the x86 dependencies.
- Output directories are centralized in `vs/op.deps.props`
  (`bin/{x64|x86}`, `lib/{x64|x86}`, `build/vs/{x64|Win32}/<Config>`).
- Only Release is validated. Debug requires Debug builds of the OpenCV static libraries
  (`cv::debug_build_guard` otherwise causes LNK2019).
