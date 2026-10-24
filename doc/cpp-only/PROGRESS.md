# 进度日志

> 每次阶段性工作后追加一条，保留时间、执行人、命令、结果、遗留问题。
> 时间统一使用北京时间（Asia/Shanghai）。

## 2026-10-25

### 00:04 初始化

- 在 `external/ops`（独立 git 仓库）创建分支 `slim/cpp-only-vs`（基于 master `d00f837`）。
- 确认工作区干净，远端为 `https://github.com/Eunsolfs/ops`。
- 用户确认 4 项决策：彻底删 API + 同步 Twinkle、保留 COM、完全原生 VS 工程、OpenCV 必需静态链接。

### 00:08 现状勘察

- 读通 `build.py`、根 `CMakeLists.txt`、`libop/CMakeLists.txt`。
- 确认 Twinkle 只依赖 `op_c_api_x64`（C API），`op_c_api_x64.dll` 不依赖 `op_x64.dll`；
  `op_x64.dll` 仅 COM 与 Hook 注入载荷需要。
- 确认本机 VS2022 17.11.5、ATL、Windows SDK 10.0.22621、midl.exe、vcpkg、CMake 4.4.3、
  Python 3.10.10 均可用。
- 确认 vcpkg 已安装 minhook / directx-headers / gtest / opencv4 / tesseract。

### 00:10 基线构建（复现用户报错）

- 命令：`python build.py -g vs2022 -t Release -a x64 --vcpkg-root Y:\Tools\vcpkg`
- 日志：`build-baseline.log`（00:13:46 结束）
- 结果：vcpkg → BlackBone 构建成功 → OpenCV 5.0.0 编译在 `features` 模块失败。

### 00:14 复现的报错

```text
modules/features/src/feature2d_aliked.cpp(23,19): error C2039:
  "ENGINE_NEW": 不是 "cv::dnn" 的成员
modules/features/src/feature2d_aliked.cpp(41,64): error C2039:
  "EngineType": 不是 "cv::dnn" 的成员
```

初步分析（后来被修正）：`BUILD_LIST` 不含 `dnn`，但 `features` 的 OPTIONAL dnn 分支
仍被编译。先尝试把 `dnn` 加入 OpenCV 构建列表。

### 00:17 根因修正（重要）

加 `dnn` 后，失败点转移到 OpenCV 自带 **protobuf**：

```text
3rdparty/protobuf/src/google/protobuf/descriptor.cc: error C2039:
  "lazy_type_name": 不是 ... 的成员
```

编译命令行里出现了 `/I"Y:\Tools\vcpkg\installed\x64-windows\include"`。进一步确认：

- 本机执行过全局 `vcpkg integrate install`
  （`%LOCALAPPDATA%\vcpkg\vcpkg.user.targets` 存在）。
- 它把 vcpkg `x64-windows`（动态 CRT，且含 OpenCV4/protobuf）的 include/lib
  注入**所有** MSBuild 工程。
- 因此 OpenCV 5 构建优先命中了 vcpkg 里的 **OpenCV 4** `opencv_modules.hpp` / `dnn.hpp`
  与旧 protobuf 头，而不是源码树里的 OpenCV 5 头/自带 protobuf。

真正的根因：**全局 vcpkg 集成污染了 OpenCV 5 的编译头文件搜索路径**。
最初的 `ENGINE_NEW` 错误不是因为缺 dnn，而是因为 `HAVE_OPENCV_DNN` 来自 vcpkg
OpenCV4 的 `opencv_modules.hpp`，而 `dnn.hpp` 也是旧版。

修复：

1. 构建 OpenCV 时移除 `VCPKG_ROOT` 环境变量；
2. `cmake --build ... -- /p:VcpkgEnabled=false /p:VcpkgApplocalDeps=false /p:VcpkgManifestInstall=false`；
3. 回到原始 `BUILD_LIST`（不含 dnn）；
4. OP 工程显式关闭 vcpkg 集成，手动引用 `x64-windows-static` 的 minhook/directx-headers。

### 00:18 建立改造记录与任务分工

- 新增 `doc/cpp-only/README.md`（目标/决策/方案/验证）。
- 新增 `doc/cpp-only/API_CHANGES.md`（精确删除/保留清单）。
- 新增本文件。
- task-1：移除点阵字库 OCR（ops 核心 + API + COM + 测试）。
- task-2：移除 Python/Go/SWIG 与 wheel 打包链。
- task-3：原生 VS 解决方案 + 依赖接线。
- task-4：同步修改 Twinkle 的 OPController。

### 00:21 OpenCV 5.0.0 静态库构建成功

- 禁用 vcpkg 集成、恢复原始 BUILD_LIST 后重新构建，`BUILD_EXIT=0`。
- 产物：`build/_deps/opencv/install/vs2022-x64/`
  - include: `include/opencv2/...`
  - lib: `x64/vc17/staticlib/`（opencv_*500.lib + libclapack/libjpeg-turbo/libpng/zlib）
- features/ALIKED 正常编译。
- 通知 vs-builder 开始最终链接。

### 00:26 task-2 完成

- binding-sweeper 完成 Python/Go/SWIG/wheel 清理：删除 `bindings/`、`swig/`、`python/`、
  `pyproject.toml`、`scripts/`、`.github/workflows/wheels.yml`；README/README_EN/CLAUDE.md 同步。
- 注意：根 CMakeLists 当时仍引用 swig；`.github/workflows/ci.yml` 仍依赖 build.py/CMake。
  两者在 VS 构建通过后随构建链一起删除。
- `tools/op_yolo_engine.py` 保留（YOLO HTTP 示例，不是绑定）。

### 00:28 task-4 完成（Twinkle 侧）

- twinkle-sync 删除 `OPController` 的 27 个被删 API 封装（声明 + 实现），
  保留 8 个 HTTP OCR/FindLine 封装。
- `Twinkle.Background.vcxproj` Debug|x64 编译通过（0 警告 0 错误，23.8s）。
- 该次编译仍用旧 `op_c_api.h`/旧 lib；新 lib 链接在 00:45 后复验。
- `Twinkle.Ocr` 模块是 HTTP OCR IPC 客户端，不依赖被删字库能力。

### 00:30 task-3 首次构建成功

- vs-builder 完成原生 VS 工程：
  `op.sln` + `libop.vcxproj` + `op_x64.vcxproj` + `op_c_api_x64.vcxproj` +
  `tools.vcxproj` + `vs/op.common.props` + `vs/op.deps.props` + `vs/op.sources.props`。
- 首次 Release|x64 构建通过，产出 `op_x64.dll` / `op_c_api_x64.dll` / `tools.dll`。

### 00:34 task-1 完成（字库 OCR 移除）

- ocr-surgeon 完成：
  - 删除 `libop/ocr/Dictionary.h`、`TesseractOcr.*`；
  - `ImageSearchAlgorithms` / `ImageSearchService` 删除字库槽、二值预处理状态、
    取字/预览/字库工具；`Ocr/OcrEx/FindStr/FindStrEx` 统一走 HTTP；
    `FindLine` 保留独立二值化路径；
  - C++ 27 个、C API 27 个、COM 27 个 dispid 同步删除；
  - tests 保留 15 个 HTTP OCR 用例、20 个颜色/找图用例。
- Lead 发现并让 ocr-surgeon 补回 `str2binaryfbk(color, sim)` 定义（否则 FindLine/
  FindColorBlock 会 LNK2019），已修复。

### 00:37 最终干净重建

- 命令：`MSBuild op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m`
- 结果：**0 警告 / 0 错误**，约 57 秒。
- 产物：
  - `bin/x64/op_x64.dll`（10,190,848）
  - `bin/x64/op_c_api_x64.dll`（10,129,920）
  - `bin/x64/tools.dll`
  - `lib/x64/libop.lib`（23,861,072）
  - `lib/x64/op_c_api_x64.lib`
- C API 导出 200 个；27 个字库类符号全部不存在；COM/Hook 导出完整。
- `op_c_api_x64.dll` 导入表只有系统 DLL（OpenCV/BlackBone/MinHook 均静态链接）。

### 00:39 纯 C++ 示例 + 冒烟

- 新增 `examples/cpp_call/`（`main.cpp` + `cpp_call.vcxproj` + README），并加入 `op.sln`。
- 直接链接 `libop.lib`、使用 `op::Op`，构建 0 警告 0 错误。
- 运行输出：

```text
Ver      = 0.4.8.3
BasePath =
ID       = 0
OpenCV   = 5.0.0
```

### 00:41 文档与依赖脚本

- 新增 `doc/cpp-only/BUILD_VS.md`（原生 VS 构建、根因、排错、验证记录）。
- 新增 `scripts/bootstrap-deps.ps1`，并验证已存在依赖时的跳过路径。
- README / README_EN / CLAUDE.md 改为原生 VS 构建口径。
- 待办：清理临时文件（`build-*.log`、`smoke.cpp`、`build-opencv-dnn.ps1`），
  同步新 `op_c_api.h` / lib / dll 到 Twinkle 并复验链接，最后提交 ops 分支。

### 00:44 Twinkle 换库 + 链接验证

- 新产物同步到 Twinkle（均为 `.gitignore` 路径）：
  - `external/OP/op-include/op_c_api.h`
  - `external/OP/op-x64/{op_c_api_x64.dll,op_x64.dll,lib/op_c_api_x64.lib}`
  - `modules/Twinkle.Background/lib/{op_c_api_x64.dll,op_c_api_x64.lib,op_x64.dll}`
- `Twinkle.Background.vcxproj` Release|x64：**0 警告 / 0 错误**。
- `ColorSelfTest.vcxproj` Release|x64：**0 警告 / 0 错误**（链接新
  `op_c_api_x64.lib`，PostBuild 复制新 DLL）。
- 输出目录 `output/bin/x64/Release/op_c_api_x64.dll` 与新 DLL 哈希一致；
  导出表中不存在已删字库符号。
- Twinkle 侧 `docs/23_OP_C_API更新.md`、`docs/_research/p1_colorfix_report.md`
  已同步历史说明。

### 00:45 最终验证汇总

- `MSBuild op.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /m`
  → **0 警告 / 0 错误**，五个工程（libop / op_x64 / op_c_api_x64 / tools / cpp_call）。
- `cpp_call.exe`：
  ```text
  Ver      = 0.4.8.3
  BasePath =
  ID       = 0
  OpenCV   = 5.0.0
  ```
- `op_c_api_x64.dll`：200 个导出；27 个字库类符号 0 个存在。
- `op_x64.dll`：COM + Hook 导出完整。
- `bootstrap-deps.ps1` 在依赖已存在时正确跳过。
- 临时日志/脚本/冒烟源码已清理。

### 00:47 提交

- 提交（`slim/cpp-only-vs` HEAD）：`refactor: 精简为纯 C++ + 原生 VS 工程`
- 79 个文件变更（+2,376 / -32,395），工作区干净。

### 00:52 DPI 感知开关

- 问题：`OpContext` 构造函数无条件 `::SetProcessDPIAware()`，使宿主进程转为 DPI 感知；
  150% 下 OP 帧 1704x1014，而 MuMu/ADB native 是 1136x640，混用 provider 坐标差 1.5x。
- 修复：
  - 默认不再调用 `SetProcessDPIAware()`；
  - 新增 `op::Op::SetProcessDpiAware(bool)` / `IsProcessDpiAware()`；
  - 新增 C API `OpSetProcessDpiAware(int)` / `OpGetProcessDpiAware()`；
  - 必须在第一次 `OpCreate` 之前调用；之后返回 0，不可撤销；
  - `OpContext` 用 mutex 保证“配置 → 锁定 → 调用”原子化。
- 探针验证：
  - 默认 / 显式 0：`actual=0 (unaware)`；
  - 显式 1 + OpCreate：`actual=1 (system-aware)`；
  - OpCreate 后再设置：返回 0，状态不变。
- 构建：`op.sln` Release|x64，0 warning / 0 error。
- 文档：[DPI.md](DPI.md)；Twinkle 建议 + 坐标归一说明见 [TWINKLE_SYNC.md](TWINKLE_SYNC.md)。

### 00:54 Twinkle 同步 + 编译（DPI 版）

- 新 `op_c_api.h` / `op_c_api_x64.lib` / `op_c_api_x64.dll` / `op_x64.dll` 已再次同步到
  `external/OP` 与 `modules/Twinkle.Background/lib`。
- `Twinkle.Background.vcxproj` Release|x64：0 警告 / 0 错误。
- `ColorSelfTest.vcxproj` Release|x64：0 警告 / 0 错误。
- 新版 DLL 默认不改宿主 DPI，Twinkle 无需改代码。

### 01:10 x86 依赖

- x86-deps 完成 `scripts/bootstrap-deps.ps1 -Arch x64|x86|both`，实际构建 x86：
  - vcpkg x86-windows-static：minhook.x32.lib / directx-headers
  - BlackBone Win32：`build/_deps/BlackBone/build/vs2022-Win32/BlackBone/Release/BlackBone.lib`
  - OpenCV 5.0.0 Win32：`build/_deps/opencv/install/vs2022-Win32/x86/vc17/staticlib`
- 耗时约 11.5 分钟；磁盘 +1.3 GB。
- 关键坑：x86 MinHook 库名是 `minhook.x32.lib`，不是 `minhook.x86.lib`。

### 01:15 x86 工程配置与全量构建

- x86-config 给 5 个 vcxproj 加 Win32 配置；`vs/op.deps.props` 按 `OpArchDir` 输出
  `bin/{x64,x86}`、`lib/{x64,x86}`；MIDL Win32 用 `HostX86\x86`。
- `op.sln` Release|Win32 `/t:Rebuild`：**0 警告 / 0 错误**（含 op_test）。
- 产物 `bin/x86/op_x86.dll`、`op_c_api_x86.dll`、`tools.dll`、`cpp_call.exe`、`op_test.exe`。
- PostBuild 把 x64/x86 的 DLL/LIB 同步到 `modules/Twinkle.Background/lib`。

### 01:21 tests 工程

- 新增 `tests/op_test.vcxproj`（Debug/Release × x64/Win32），GUID 1006，加入 `op.sln`。
- 13 suites / 132 cases；x64 核心集 10/10 PASS，扩展集 16/16 PASS；
  x86 核心集 9 PASS + 1 SKIP，扩展集 15 PASS + 1 SKIP。
- OCR 用例需要外部服务，未纳入；WGC/窗口/DX 注入类需真机环境。
- `tests/opencv_test.cpp` 修 9 处 C4244（double → RecordProperty）。
- 2026-10-25 修复：Debug 全仓库改为 `/MT`（与 Release 依赖 CRT 一致），x64/Win32
  Debug 均 0 警告 0 错误；代价是无 `_DEBUG`/迭代器调试。

### 01:22 Twinkle.sln 集成

- `Twinkle.sln` 新增 OP 解决方案文件夹，加入 libop / op_x64 / op_c_api_x64 / tools /
  cpp_call；配置映射齐全，Win32→x64 桥接。
- 7 个 Twinkle 工程加 solution 级依赖 → op_c_api_x64。
- 验证：`/t:"OP\op_c_api_x64"` 与 `/t:Twinkle_CLI` Release|x64 均 PASS；
  `modules/Twinkle.Background/lib` 与 external/ops 产物 hash 一致。
- 修复 HEAD 中 EmulatorProbe/DecoupleTest 各缺一个 `EndProject` 的旧结构问题。

### 02:30 削减后功能审计与 P0/P1 修复

- 三路只读审计完成，报告：
  - [AUDIT_RUNTIME.md](AUDIT_RUNTIME.md)：运行时/线程/资源/性能（16 条）
  - [AUDIT_OCR_API.md](AUDIT_OCR_API.md)：OCR/API/COM/测试一致性（15 条）
  - [AUDIT_BUILD.md](AUDIT_BUILD.md)：VS/x86/依赖/op_test 构建链（18 条）
  - 汇总与当前状态：[AUDIT_SUMMARY.md](AUDIT_SUMMARY.md)
- 已修 P0：
  - `BindWindowEx` 参数校验失败先清空输入后端 → 后续鼠标/键盘空指针。
  - GDI printwindow 窗口 resize 后共享帧容量不足 → 越界写。
- 已修 P1：
  - `point_t::operator<` 非严格弱序 + OCR 用 `std::map` 丢框/顺序不一致；改为行带键 + `vector/stable_sort`。
  - `OCR` 恒返回 1（后端不可用伪装成空结果）；改为成功 1 / HTTP 失败 0。
  - OCR 文本 UTF-8 → CP936 → CP_ACP 双重有损；改为直接 UTF-8 → `wstring`。
  - Debug 用 `/MTd` 与 Release 依赖 CRT 冲突；改 `/MT`，Debug x64/Win32 恢复可构建。
  - `bootstrap-deps.ps1` 未安装 gtest；补齐并校验。
  - PostBuild 任何配置都覆盖 Twinkle；改为仅 Release + `OpSyncTwinkle`，并同步 `op_c_api.h`。
- 已修 P2：`FindLine` 空图伪 `0,0`、`Find*Ex` 上限 off-by-one、`OcrFromFile*` 不检查读图、
  COM `FindStr` 空指针、OCR 测试默认端点 8080/8082 不一致、文档计数 27/200/215。
- 验证：
  - Release/Debug × x64/Win32 四配置 `/t:Rebuild` 均 **0 警告 / 0 错误**。
  - `op_test` 132 cases；x64 核心集 9/9 PASS，x86 7/7 PASS（另 1 条 32 位专用 SKIP）。
  - `cpp_call.exe`：`Ver 0.4.8.3` / `OpenCV 5.0.0`。
  - Release PostBuild 已实测同步 DLL/LIB/头文件，`op_c_api.h` SHA256 一致。
- 提交：`188cfae fix(audit): 修复削减分支的 P0/P1 隐藏问题并补充审计`。
- 遗留（需决策/后续）：
  - COM 早期绑定：删除中段方法但 IID/typelib 未升，旧 early-binding 客户端可能跳错 vtable；
    需决定升 IID 还是新增 `IOpAutomation2`。
  - x86 `GetProcAddress` 需用 `_OpXxx@N` 修饰名（隐式链接用 `.lib` 不受影响）。
  - OCR HTTP 全局串行、积分图 int32 溢出、COM 无 try/catch、`check_bind` 隐式绑定桌面、
    HTTP 正则/响应上限、FindPic 多份拷贝等性能/健壮性项。
  - 测试缺口：FindLine、COM、GDI resize、绑定失败后输入、DPI、5K 积分图。

### 07:00 COM/OCR/积分图/异常边界优化

- COM 兼容：`IOpAutomation` IID 升为 `59d588f8-b39e-4427-9826-c1fba566318d`，typelib 升到 `1.1`，
  `IDispatchImpl` 同步 1,1。旧 early-binding 客户端 QI 旧 IID 会干净失败；late binding 按
  CLSID+dispid 仍可用。
- x86 C API：新增 `libop/c_api/op_c_api_x86.def`，保留 `_OpXxx@N` 并加 `OpXxx` 别名；
  x86 导出 400 个（200 修饰 + 200 别名），`GetProcAddress` 两种写法都可用。
- OCR：配置锁只保护 endpoint/timeout 快照，HTTP 在锁外；最多 4 个在途请求，避免单次后端
  卡死阻塞全进程 OCR/SetOcrEngine。
- 积分图：`_sum` 从 int32 `Image` 改为 int64 `ImageSum`；`region_sum`/`gray_norm`/`real_match`/
  `FindLine` 全 64 位，修 5K/双 4K 溢出。
- COM 异常：`OpAutomation::Invoke` 统一 `try/catch(...) -> E_FAIL`，覆盖 late-binding。
- 验证：四配置 `/t:Rebuild` 均 0 警告/0 错误；x64 核心 10/10 PASS，x86 核心 8/8 PASS
  （含 `UtilsTest.X86CapiUndecoratedAliases` 与 `UtilsTest.ComIidBumpedForEarlyBinding`）。
- 输出隔离：Debug 产物改到 `bin/{x64|x86}/Debug/`、`lib/{x64|x86}/Debug/`，避免与 Release
  同名 DLL/LIB 互相覆盖；Twinkle 仍只使用 Release。
- 提交：`1ccc557 feat(com-ocr): COM IID 1.1、x86 C API 别名、OCR 并发与 int64 积分图`。
- 最终验证：四配置 0 警告/0 错误；Release x64 10/10、Release x86 8/8、Debug x64 10/10、
  Debug x86 8/8（过滤核心集）；Release PostBuild 同步 `op_c_api.h` SHA256 与 ops 源一致。
- 遗留：`check_bind` 隐式桌面绑定、HTTP JSON/响应上限、FindPic/frameId 性能、`Delay` 忙等、
  DPI 实际状态、C API 线程安全/返回值生命周期文档、大图积分图测试、COM 注册用例。

### 待办清单

- [x] 清理临时文件与构建日志
- [x] 将新 `include/op_c_api.h`、`lib/x64/op_c_api_x64.lib`、
      `bin/x64/op_c_api_x64.dll`、`bin/x64/op_x64.dll` 同步到 Twinkle
- [x] 用新头文件/新 lib 重新编译 Twinkle.Background / ColorSelfTest，确认链接
- [x] 更新 Twinkle `docs/` 中 2 处引用已删 API 的历史文档
- [x] 提交 ops 分支 `slim/cpp-only-vs`（见 `git log`）
- [x] Twinkle 临时分支 `slim/ops-cpp-only` 已合并到 master 并删除
- [x] DPI 开关 + 探针验证
- [x] Twinkle 无需改代码：新版 DLL 默认不改宿主 DPI
- [x] 提交 DPI 变更到 ops 分支，并同步新 DLL/LIB/头文件到 Twinkle
- [x] x86 依赖 + Win32 工程配置 + 全量构建
- [x] tests/op_test.vcxproj + x64/x86 用例
- [x] Twinkle.sln 加入 OP 工程与构建依赖
- [x] 提交 tests/x86/Twinkle.sln 变更（Lead）
- [x] 削减后功能审计 + P0/P1 修复（提交 `188cfae`）
- [x] COM 早期绑定兼容（IID/typelib 1.1）
- [x] x86 C API `GetProcAddress` 别名
- [x] OCR HTTP 串行化改造（4 并发槽）
- [x] 积分图 int32 → int64
- [x] COM `Invoke` 统一 try/catch
- [ ] `check_bind` 隐式桌面绑定、HTTP JSON/响应上限、FindPic/frameId 性能
