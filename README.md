# OP - Windows 自动化插件

[![GitHub Release](https://img.shields.io/github/v/release/WallBreaker2/op?style=flat-square)](https://github.com/WallBreaker2/op/releases)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-blue.svg)](https://microsoft.com/windows)

[English](README_EN.md)

OP（Operator & Open）是一个面向 Windows 的自动化插件。它把窗口查找、后台绑定、截图、键鼠输入、找色找图、OCR、OpenCV、YOLO HTTP 检测和进程内存读写放在同一套接口里，方便桌面自动化程序直接调用。

核心代码使用 C++ 实现，对外提供 C API 和 COM 两种调用方式；原生 VS 工程同时提供 x64 与 x86 构建。截图后端覆盖普通窗口、GDI、DXGI、WGC、DirectX Hook、OpenGL 和 OpenGL ES；图片模板可以从本地文件加载，也可以从内存加载，适合把资源随程序一起打包。

OCR 走外部 HTTP 服务：OP 负责截图、读图和请求封装，识别由独立服务 [op_ocr_engine](https://github.com/WallBreaker2/op_ocr_engine) 完成，可接入 Tesseract、PaddleOCR 等后端。

> 本 fork 已移除点阵字库 OCR（本地字库 / 内存字库）以及 Python、Go、SWIG 绑定；OCR 只保留 HTTP 服务接入，构建改为原生 Visual Studio 工程。

YOLO 检测同样走外部 HTTP 服务模式。OP 负责截图、读图和请求封装，模型推理由独立服务完成。接口格式见 [YOLO HTTP 检测](doc/yolo.md)，仓库里也放了一个最小服务样例：`tools/op_yolo_engine.py`。

## 文档

- [GitHub Wiki](https://github.com/WallBreaker2/op/wiki)：安装、接口说明、OpenCV、OCR、免注册
- [插件下载](https://github.com/WallBreaker2/op/releases)
- [本地 OCR 说明](doc/ocr.md)
- [YOLO HTTP 检测](doc/yolo.md)
- [测试工具 OPTestTool](https://github.com/flaot/OPTestTool)

## 主要能力

- 窗口枚举、进程查询、窗口状态控制、批量窗口布局
- 普通绑定、后台绑定、显示句柄和输入句柄分离绑定
- GDI、DXGI、WGC、DX Hook、OpenGL、OpenGL ES 等截图方式
- 前台/后台鼠标键盘模拟，支持平滑移动、轨迹移动和 DX 输入锁定
- 找色、找图、透明图片模板、OpenCV 模板匹配和特征匹配
- OCR / YOLO HTTP 服务接入
- C API、COM 调用方式
- 进程内存读写、汇编调用和常用算法工具

## 目录概览

```text
op/
├─ libop/            核心 C++ 源码
│  ├─ op/            op::Op 对外接口的分文件实现
│  ├─ c_api/         C API 封装
│  ├─ com/           COM 组件、IDL 和自动化接口实现
│  ├─ binding/       窗口绑定和后台模式调度
│  ├─ capture/       GDI、DXGI、WGC、Hook 等截图后端
│  ├─ hook/          远端注入、显示 hook、输入 hook 和共享帧写入
│  ├─ input/         鼠标、键盘输入后端
│  ├─ image/         图片加载、找色、找图和图像搜索服务
│  ├─ ocr/           OCR 服务接入
│  ├─ opencv/        OpenCV 桥接、模板匹配和图像处理
│  ├─ window/        窗口、进程和 DLL 注入相关能力
│  ├─ base/          基础类型、运行环境和工具函数
│  ├─ ipc/           共享内存、互斥量、管道等进程间通信封装
│  ├─ memory/        目标进程内存读写
│  ├─ network/       HTTP 客户端等网络辅助能力
│  ├─ algorithm/     内部算法与通用计算逻辑
│  └─ yolo/          YOLO 检测器封装
├─ include/          对外头文件
├─ examples/         纯 C++ 调用示例
├─ tools/            免注册加载工具和 YOLO 服务样例
├─ tests/            C++ 测试源码（当前未纳入解决方案）
├─ doc/              项目内补充文档和流程图
├─ 3rd_party/        第三方源码或本地依赖
├─ op.sln            原生 VS2022 解决方案
├─ vs/               编译/依赖/源码清单属性表
└─ scripts/          第三方依赖准备脚本
```

## 快速开始

下载 Release 后，根据宿主程序位数使用对应 DLL。COM 方式需要注册：

```powershell
# 32 位宿主程序
regsvr32 .\op_x86.dll

# 64 位宿主程序
regsvr32 .\op_x64.dll
```

COM 注册后可由任意支持 COM 的语言调用（C++、C#、Lua 等）；C API 场景直接加载 `op_c_api_*.dll`，声明见 `include/op_c_api.h`。

免注册调用请看 Wiki：

- [安装与免注册](https://github.com/WallBreaker2/op/wiki/docs/install)
- [COM 调用示例（C# / Lua 等）](https://github.com/WallBreaker2/op/wiki/Home)

## 源码编译

环境要求：

- Windows 10 或更新版本
- Visual Studio 2022（v143，安装 ATL 与 Windows SDK）
- vcpkg（用于 minhook / directx-headers 静态库）
- CMake（只用于一次性构建 BlackBone / OpenCV 第三方依赖）

### 1. 准备第三方依赖（一次性）

```powershell
pwsh -File scripts\bootstrap-deps.ps1
```

脚本会：

- 通过 vcpkg 安装 `x64-windows-static` 的 `minhook`、`directx-headers`
- 克隆并构建 BlackBone（`/MT`）
- 下载并构建 OpenCV 5.0.0 静态库（`/MT`，最小模块集）

产物位于 `build\_deps`。已存在时自动跳过，`-Force` 可强制重建。

> 如果本机执行过 `vcpkg integrate install`，脚本会在构建 OpenCV 时禁用集成，避免
> vcpkg 里的 OpenCV4 / protobuf 头文件污染。原因与排查见
> [原生 VS 构建说明](doc/cpp-only/BUILD_VS.md)。

### 2. 构建

用 VS2022 打开仓库根目录的 `op.sln`，选择 `Release|x64` 或 `Release|Win32` 构建；或使用命令行：

```powershell
# x64
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
    op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m

# x86
& 'Y:\Microsoft Visual Studio\2022\MSBuild\Current\Bin\MSBuild.exe' `
    op.sln /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /m
```

产物：

```text
bin/x64/op_x64.dll              bin/x86/op_x86.dll
bin/x64/op_c_api_x64.dll        bin/x86/op_c_api_x86.dll
bin/x64/tools.dll               bin/x86/tools.dll
bin/x64/cpp_call.exe            bin/x86/cpp_call.exe
bin/x64/op_test.exe             bin/x86/op_test.exe
lib/x64/libop.lib               lib/x86/libop.lib
lib/x64/op_c_api_x64.lib        lib/x86/op_c_api_x86.lib
```

x86 依赖用 `pwsh -File scripts\bootstrap-deps.ps1 -Arch x86` 准备。当前只验证
Release；Debug 需要额外构建 Debug 版 OpenCV 静态库。

### 3. 纯 C++ 调用

- 直接链接静态库：包含 `include/libop.h`，定义 `U_STATIC_IMPLEMENTATION`，链接
  `lib/x64/libop.lib`（或 `lib/x86/libop.lib`）及 OpenCV / BlackBone / MinHook 静态库。
  示例见 [`examples/cpp_call`](examples/cpp_call)。
- 或使用 C API：包含 `include/op_c_api.h`，链接 `lib/x64/op_c_api_x64.lib`（或
  `lib/x86/op_c_api_x86.lib`），运行期加载对应 DLL。

```cpp
#include <libop.h>

op::Op op;
std::wcout << op.Ver();                 // 0.4.8.3
std::wstring cv;
op.CvGetOpenCvVersion(cv);              // 5.0.0
```

## 社区

- [GitHub Issues](https://github.com/WallBreaker2/op/issues)
- [GitHub Discussions](https://github.com/WallBreaker2/op/discussions)
- QQ 群：`743710486`、`27872381`

## 许可证

[MIT License](LICENSE)

## 参考项目

- [TSPLUG](https://github.com/tcplugins/tsplug)
- [Kiero](https://github.com/Rebzzel/kiero)
