# OP - Windows Automation Plugin

[![GitHub Release](https://img.shields.io/github/v/release/WallBreaker2/op?style=flat-square)](https://github.com/WallBreaker2/op/releases)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-blue.svg)](https://microsoft.com/windows)

[中文](README.md)

OP (Operator & Open) is a Windows automation plugin. It brings window discovery, background binding, screen capture, mouse and keyboard input, color and image search, OCR, OpenCV, YOLO HTTP detection, and process memory access into one set of APIs for desktop automation programs.

The core is written in C++. It exposes a C API and a COM interface; the native VS solution builds both x64 and x86. Capture backends cover normal windows, GDI, DXGI, WGC, DirectX hooks, OpenGL, and OpenGL ES. Image templates can be loaded from files or from memory, which makes it easier to ship resources inside your own program.

> This fork removes bitmap-font OCR (local/memory dictionaries) and the Python, Go, and SWIG bindings. OCR is HTTP service only, and the project builds with a native Visual Studio solution.

OCR is delegated to a standalone HTTP service: OP captures or loads the image and wraps the request, while recognition runs in [op_ocr_engine](https://github.com/WallBreaker2/op_ocr_engine), backed by engines such as Tesseract or PaddleOCR.

YOLO detection follows the same external-service model. OP captures or loads the image and wraps the HTTP request; model inference runs in a separate service. See [YOLO HTTP detection](doc/yolo.md) for the API format. A minimal sample service is included at `tools/op_yolo_engine.py`.

## Documentation

- [GitHub Wiki](https://github.com/WallBreaker2/op/wiki): installation, API reference, OpenCV, OCR, and registration-free usage
- [Releases](https://github.com/WallBreaker2/op/releases)
- [Local OCR notes](doc/ocr.md)
- [YOLO HTTP detection](doc/yolo.md)
- [OPTestTool](https://github.com/flaot/OPTestTool)

## Features

- Window enumeration, process lookup, window state control, and batch window layout
- Normal binding, background binding, and separated display/input window handles
- GDI, DXGI, WGC, DX hook, OpenGL, and OpenGL ES capture modes
- Foreground/background mouse and keyboard input, smooth movement, path movement, and DX input locking
- Color search, image search, transparent templates, OpenCV template matching, and feature matching
- OCR and YOLO HTTP service integration
- C API and COM access
- Process memory read/write, assembly calls, and utility algorithms

## Repository Layout

```text
op/
├─ libop/            Core C++ source
│  ├─ op/            Split implementation of the public op::Op API
│  ├─ c_api/         C API wrapper
│  ├─ com/           COM component, IDL, and automation interface
│  ├─ binding/       Window binding and background-mode dispatch
│  ├─ capture/       GDI, DXGI, WGC, Hook, and related capture backends
│  ├─ hook/          Remote injection, display/input hooks, and shared-frame writing
│  ├─ input/         Mouse and keyboard input backends
│  ├─ image/         Image loading, color search, image search, and image services
│  ├─ ocr/           OCR service integration
│  ├─ opencv/        OpenCV bridge, template matching, and image processing
│  ├─ window/        Window, process, and DLL injection helpers
│  ├─ base/          Basic types, runtime environment, and utility functions
│  ├─ ipc/           Shared memory, mutexes, pipes, and other IPC helpers
│  ├─ memory/        Target-process memory access
│  ├─ network/       HTTP client and network helpers
│  ├─ algorithm/     Internal algorithms and shared calculation logic
│  └─ yolo/          YOLO detector wrapper
├─ include/          Public headers
├─ examples/         Pure C++ usage example
├─ tools/            Registration-free loader tools and YOLO sample service
├─ tests/            C++ test sources (not in the solution yet)
├─ doc/              In-repository notes and diagrams
├─ 3rd_party/        Third-party source or local dependencies
├─ op.sln            Native VS2022 solution
├─ vs/               Compile/dependency/source-list property sheets
└─ scripts/          Third-party dependency bootstrap script
```

## Quick Start

Download a release package and use the DLL that matches the bitness of your host process. COM usage requires registration:

```powershell
# 32-bit host process
regsvr32 .\op_x86.dll

# 64-bit host process
regsvr32 .\op_x64.dll
```

Once registered, the COM interface can be called from any COM-capable language (C++, C#, Lua, and others). For the C API, load `op_c_api_*.dll` directly; declarations are in `include/op_c_api.h`.

For registration-free usage, see the Wiki:

- [Installation and registration-free usage](https://github.com/WallBreaker2/op/wiki/docs/install)
- [COM usage demos (C#, Lua, and others)](https://github.com/WallBreaker2/op/wiki/Home)

## Build

Requirements:

- Windows 10 or newer
- Visual Studio 2022 (v143 with ATL and the Windows SDK)
- vcpkg (for the MinHook / DirectX-Headers static libraries)
- CMake (only to build the BlackBone / OpenCV third-party dependencies once)

### 1. Prepare third-party dependencies (one time)

```powershell
pwsh -File scripts\bootstrap-deps.ps1
```

The script installs `minhook` and `directx-headers` for `x64-windows-static`, clones and
builds BlackBone (`/MT`), and downloads/builds OpenCV 5.0.0 static libraries (`/MT`,
minimal module set). Artifacts go to `build\_deps`; existing builds are reused unless
`-Force` is passed. Use `-Arch x86` (or `-Arch both`) to prepare the x86 dependencies.

> If this machine has run `vcpkg integrate install`, the script disables vcpkg integration
> while building OpenCV. Otherwise vcpkg's OpenCV4/protobuf headers pollute the OpenCV 5
> build and produce errors such as `"ENGINE_NEW" is not a member of "cv::dnn"`. See
> [Native VS build notes](doc/cpp-only/BUILD_VS.md).

### 2. Build

Open `op.sln` in VS2022 and build `Release|x64` or `Release|Win32`, or use the command line:

```powershell
# x64
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
    op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m

# x86
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
    op.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m
```

Artifacts:

```text
bin/x64/op_x64.dll              bin/x86/op_x86.dll
bin/x64/op_c_api_x64.dll        bin/x86/op_c_api_x86.dll
bin/x64/tools.dll               bin/x86/tools.dll
bin/x64/cpp_call.exe            bin/x86/cpp_call.exe
bin/x64/op_test.exe             bin/x86/op_test.exe
lib/x64/libop.lib               lib/x86/libop.lib
lib/x64/op_c_api_x64.lib        lib/x86/op_c_api_x86.lib
```

x86 dependencies: `pwsh -File scripts\bootstrap-deps.ps1 -Arch x86`. Only Release is
currently validated; Debug requires a Debug build of the OpenCV static libraries.

### 3. Pure C++ usage

- Static library: include `include/libop.h`, define `U_STATIC_IMPLEMENTATION`, and link
  `lib/x64/libop.lib` (or `lib/x86/libop.lib`) plus the OpenCV / BlackBone / MinHook static
  libraries. See [`examples/cpp_call`](examples/cpp_call).
- C API: include `include/op_c_api.h`, link `lib/x64/op_c_api_x64.lib` (or
  `lib/x86/op_c_api_x86.lib`), and load the matching DLL at runtime.

```cpp
#include <libop.h>

op::Op op;
std::wcout << op.Ver();                 // 0.4.8.3
std::wstring cv;
op.CvGetOpenCvVersion(cv);              // 5.0.0
```

## Community

- [GitHub Issues](https://github.com/WallBreaker2/op/issues)
- [GitHub Discussions](https://github.com/WallBreaker2/op/discussions)
- QQ groups: `743710486`, `27872381`

## License

[MIT License](LICENSE)

## References

- [TSPLUG](https://github.com/tcplugins/tsplug)
- [Kiero](https://github.com/Rebzzel/kiero)
