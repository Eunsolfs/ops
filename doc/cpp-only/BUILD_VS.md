# 原生 VS2022 构建说明（CPP-only 分支）

> 适用分支：`slim/cpp-only-vs`
> 目标：不依赖 CMake / build.py / Python，直接用 Visual Studio 2022 构建 OP 的纯 C++ 版本。

## 1. 工程结构

```text
op.sln
├─ libop.vcxproj         静态库：libop 全部核心源码（含 OpenCV / Kiero / WGC）
├─ op_x64.vcxproj        COM DLL：libop 源码 + COM 源码 + MIDL 生成的 op_i.c + op.rc + op.def
├─ op_c_api_x64.vcxproj  C API DLL：c_api + HookExport，链接 libop.lib
├─ tools.vcxproj         EasyCom（只链 MinHook）
└─ examples/cpp_call/    纯 C++ 静态链接示例（也是冒烟测试）
```

公共属性：

| 文件 | 作用 |
| --- | --- |
| `vs/op.common.props` | 编译/链接/MIDL 选项、include 顺序、输出目录 |
| `vs/op.deps.props` | BlackBone / vcpkg / OpenCV 路径，全部相对 `$(SolutionDir)` |
| `vs/op.sources.props` | libop 源码清单（与旧 CMake 逐条对应） |

构建配置只有 `Debug|x64` 与 `Release|x64`。

## 2. 环境要求

- Windows 10/11
- Visual Studio 2022（v143；需要 **ATL** 与 **Windows SDK 10.0.22621**）
- vcpkg（`minhook`、`directx-headers` 的 `x64-windows-static` 包）
- CMake（只用于一次性构建 BlackBone / OpenCV，第三方工程本来就用 CMake）

本机已验证路径：

- VS：`Y:\Microsoft Visual Studio\2022`
- Windows SDK：`Y:\Windows Kits\10`（`midl.exe` 在 `bin\10.0.22621.0\x64`）
- vcpkg：`Y:\Tools\vcpkg`

## 3. 一次性准备第三方依赖

```powershell
pwsh -File scripts\bootstrap-deps.ps1
```

脚本执行：

1. `vcpkg install minhook:x64-windows-static directx-headers:x64-windows-static`
2. `git clone https://github.com/DarthTon/Blackbone` 到 `build\_deps\BlackBone`
3. 构建 BlackBone（`/MT`，Release）
4. 下载 OpenCV 5.0.0 源码到 `build\_deps\opencv`，构建静态库并 install 到
   `build\_deps\opencv\install\vs2022-x64`

已存在时自动跳过；`-Force` 强制重建。可选参数：`-VcpkgRoot`、`-SkipOpenCv`。

### 3.1 为什么必须处理 vcpkg 全局集成

本机执行过：

```powershell
vcpkg integrate install
```

它会在 `%LOCALAPPDATA%\vcpkg\vcpkg.user.targets` 里注册全局 MSBuild 集成，把
`Y:\Tools\vcpkg\installed\x64-windows\include`（动态 CRT、且含 OpenCV4 / protobuf）
注入**所有** VS 工程。后果：

```text
modules/features/src/feature2d_aliked.cpp(23,19): error C2039:
  "ENGINE_NEW": 不是 "cv::dnn" 的成员
```

因为 OpenCV 5 的 `features` 在本构建配置里没有 `dnn`，但编译器先命中了 vcpkg 里
OpenCV4 的 `opencv_modules.hpp` / `dnn.hpp`，于是 `HAVE_OPENCV_DNN` 被旧头文件置位，
而旧版 `cv::dnn` 没有 `ENGINE_NEW`。

修复：

- 构建 OpenCV 时临时移除 `VCPKG_ROOT`，并在 `cmake --build ... --` 后传
  `/p:VcpkgEnabled=false /p:VcpkgApplocalDeps=false /p:VcpkgManifestInstall=false`。
- OP 自己的工程也在 `Globals`/`Configuration` 里设 `VcpkgEnabled=false`，手动引用
  `x64-windows-static` 的 `minhook` 与 `directx-headers`。
- `vs/op.common.props` 里把 OpenCV 5 的 include 放在 vcpkg include **前面**，防止
  vcpkg 自带的 OpenCV4 头再次混入。

## 4. 构建

### 4.1 VS IDE

用 VS2022 打开 `op.sln`，选择 `Release|x64`，生成解决方案。

### 4.2 命令行（不需要 vcvars/dev prompt）

```powershell
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
    op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m

# x86（Win32）
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
    op.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m
```

增量构建去掉 `/t:Rebuild` 即可。MIDL 是自定义 target，`op.idl` 变化或 `op.tlb`
丢失时会自动重新生成。x86 的 MIDL 使用 `HostX86\x86` 的 cl host。

### 4.3 产物

```text
bin/x64/op_x64.dll           COM 插件 + Hook 导出（DX Hook 注入载荷）
bin/x64/op_c_api_x64.dll     C API
bin/x64/tools.dll            EasyCom
bin/x64/cpp_call.exe         纯 C++ 示例
bin/x64/op_test.exe          GoogleTest 测试
lib/x64/libop.lib            静态库（纯 C++ 直接调用）
lib/x64/op_c_api_x64.lib     C API 导入库

bin/x86/op_x86.dll           x86 COM 插件
bin/x86/op_c_api_x86.dll     x86 C API
bin/x86/tools.dll
bin/x86/cpp_call.exe
bin/x86/op_test.exe
lib/x86/libop.lib
lib/x86/op_c_api_x86.lib
```

`op_c_api_x64.dll` 的导入表只有系统 DLL：OpenCV / BlackBone / MinHook 都是静态链接，
不需要额外运行库。

## 5. 纯 C++ 调用

### 5.1 静态库方式

```cpp
#include <libop.h>

op::Op op;
std::wcout << op.Ver();          // 0.4.8.3
std::wstring cv;
op.CvGetOpenCvVersion(cv);       // 5.0.0
```

工程必须：

- 定义 `U_STATIC_IMPLEMENTATION`（否则 `OP_API` 会变成 `dllimport`）
- 链接 `lib/x64/libop.lib`
- 链接 OpenCV / BlackBone / MinHook 静态库（参考 `examples/cpp_call/cpp_call.vcxproj`
  与 `vs/op.common.props`）

完整示例：`examples/cpp_call/`。

### 5.2 C API 方式

```cpp
#include <op_c_api.h>

op_handle h = OpCreate();
std::wcout << OpVer();            // OpVer 不需要 handle
OpDestroy(h);
```

链接 `lib/x64/op_c_api_x64.lib`，运行期把 `op_c_api_x64.dll` 放在 exe 同目录。

## 6. 验证记录（2026-10-25）

- `MSBuild op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m`
  → **0 error / 0 warning**，约 57 秒。
- `cpp_call.exe`：
  ```text
  Ver      = 0.4.8.3
  BasePath =
  ID       = 0
  OpenCV   = 5.0.0
  ```
- `dumpbin /exports op_c_api_x64.dll`：
  - 198 个导出；`OpCreate` / `OpDestroy` / `OpVer` / `OpOcr` / `OpFindStr` /
    `OpFindLine` / `OpSetOcrEngine` / `OpCv*` 存在。
  - 已删的 28 个字库类符号（`OpSetDict`、`OpFetchWord*`、`OpGetWordPreview`、
    `OpGetBinaryPreview` 等）**0 个存在**。
- `op_x64.dll`：213 个导出，`DllGetClassObject` / `DllRegisterServer` /
  `SetDisplayHook` / `SetInputHook` 等 COM + Hook 导出完整。
- 版本资源：`op_x64.dll` / `op_c_api_x64.dll` 的 `FileVersion` / `ProductVersion`
  均为 `0.4.8.3`。

## 7. 常见问题

### 7.1 `ENGINE_NEW` / `cv::dnn` 编译错误

见 3.1。核心是禁用 vcpkg 全局集成后再构建 OpenCV；不要简单地把 `dnn` 加进
`BUILD_LIST` 绕过，否则会引入 protobuf，并且仍然可能命中 vcpkg 的旧头文件。

### 7.2 MIDL1005 / MIDL1001（找不到 cl.exe / oaidl.idl）

`vs/op.common.props` 的 MIDL 自定义 target 已经显式设置 `INCLUDE`（VC + Windows SDK）
和 `PATH`（`VCToolsInstallDir\bin\HostX64\x64`），普通 PowerShell 直接 msbuild 即可。
如果换了 VS 版本/路径，检查这两个变量。

### 7.3 `cv::DistanceTypes` 类型重定义

vcpkg 的 `x64-windows-static` 里也装过 OpenCV4（`opencv_world4`），它同样提供
`opencv2/*.hpp`。保证 `$(OpOpenCvIncludeDir)` 排在 `$(OpVcpkgStaticDir)\include` 之前。

### 7.4 `windowsapp.lib` 找不到

WGC 需要 `windowsapp.lib` + `runtimeobject.lib`，已加在 `AdditionalDependencies`。
若手工改工程，注意不是 `windowapp.lib`。

### 7.5 C4819 / 源码中文编码

工程统一 `/utf-8`；MIDL 用 `_CL_=/utf-8`。不要在 dev prompt 里设置冲突的 `_CL_`。

### 7.6 依赖路径迁移

`vs/op.deps.props` 默认从 `$(SolutionDir)build\_deps` 取 BlackBone/OpenCV，vcpkg 优先
`$env:VCPKG_ROOT`，兜底 `Y:\Tools\vcpkg`。换机器时可传：

```powershell
/p:BlackBoneRoot=... /p:BlackBoneLib=... /p:OpenCV_ROOT=... /p:OpVcpkgRoot=...
```

### 7.7 DPI 感知

旧版 `OpCreate` 会无条件 `SetProcessDPIAware()`，使整个宿主进程转为 DPI 感知，导致
OP 帧是物理像素、其它 provider 是逻辑像素。现在默认不修改宿主 DPI：

```c
OpSetProcessDpiAware(0);   // 默认，可省略
op_handle h = OpCreate();

// 需要物理像素：
OpSetProcessDpiAware(1);   // 必须在第一次 OpCreate 之前
op_handle h = OpCreate();
```

详见 [DPI.md](DPI.md)。

## 8. 已知限制

- **Debug 配置**：OpenCV 目前只构建了 Release/MT 静态库；Debug 会切到
  `cv::debug_build_guard` 命名空间，链接报 `LNK2019`。当前只验证 **Release|x64 /
  Release|Win32**。需要 Debug 时得补一份 Debug 版 OpenCV（`opencv_*d.lib`），再调整
  公共 props 的库名。
- **x86 依赖**：已经可用；用 `pwsh -File scripts/bootstrap-deps.ps1 -Arch x86` 准备。
  x86 的 MinHook 库名是 `minhook.x32.lib`（不是 `minhook.x86.lib`），已在
  `vs/op.deps.props` 处理。
- **tests 工程**：`tests/op_test.vcxproj` 已加入 `op.sln`，x64/x86 均可构建；
  OCR 用例需要外部 OCR 服务（`OP_OCR_URL`），未启动时会 SKIP/FAIL，属环境依赖。
- **CI 已删除**：`.github/workflows/ci.yml`、`wheels.yml` 已移除；如需 CI，按
  `MSBuild op.sln /p:Platform=x64|Win32` 重写。

## 9. 删除的旧构建文件

- `CMakeLists.txt`（根、libop、tools、tests）
- `build.py`
- `ci/triplets/`
- `.github/workflows/ci.yml`、`wheels.yml`
- `swig/`、`python/`、`bindings/`、`pyproject.toml`、`scripts/build_wheel.ps1` 等

旧 CMake 构建链如需回退，可从 `master` 分支取。
